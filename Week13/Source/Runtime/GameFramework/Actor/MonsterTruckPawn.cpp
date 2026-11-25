#include "EnginePCH.h"
#include "GameFramework/Actor/MonsterTruckPawn.h"
#include "Asset/AssetManager.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsLock.h"
#include "Physics/PhysXConversions.h"
#include "Physics/PhysXFilterShader.h"
#include "Serialization/StructuredArchiveSlots.h"
#include <vehicle/PxVehicleNoDrive.h>
#include <vehicle/PxVehicleTireFriction.h>
#include <vehicle/PxVehicleUpdate.h>
#include <vehicle/PxVehicleUtilSetup.h>
#include <cmath>

using namespace physx;

namespace
{
	constexpr PxU32 WheelCount = 4;
	constexpr PxU16 HitsPerWheel = 6;
	constexpr float PhysicsStep = 1.0f / 60.0f;
	constexpr PxU32 MaxPhysicsStepsPerFrame = 4;
	constexpr float WheelRadius = 0.827f;
	constexpr float WheelWidth = 1.052f;
	constexpr float TruckMass = 3280.0f;
	constexpr float DefaultTireGrip = 1.20f;
	const FVector BedLocation(-1.10f, 0.0f, 0.30f);
	const PxVec3 CenterOfMass(-0.16f, 0.0f, -0.15f);
	const PxVec3 WheelCenters[WheelCount] = {
		{ 1.798f, -1.411f, -0.754f }, { 1.798f, 1.414f, -0.754f },
		{ -1.511f, -1.411f, -0.754f }, { -1.511f, 1.414f, -0.754f }
	};

	struct FTruckVisualPose
	{
		FTransform Chassis;
		FTransform Wheels[WheelCount];
	};

	FTransform InterpolateVisualPose(const FTransform& Previous, const FTransform& Current, float Alpha)
	{
		return FTransform(
			FMath::Slerp(Previous.Rotation, Current.Rotation, Alpha),
			FMath::Lerp(Previous.Location, Current.Location, Alpha),
			Current.Scale);
	}

	PxQueryHitType::Enum FilterSuspensionSweep(PxFilterData, PxFilterData Surface, const void*, PxU32, PxHitFlags&)
	{
		return (Surface.word3 & ECollisionFlags::Drivable) ? PxQueryHitType::eTOUCH : PxQueryHitType::eNONE;
	}

	PxConvexMesh* CookWheel()
	{
		constexpr PxU32 Sides = 24;
		PxVec3 Points[Sides * 2];
		for (PxU32 Index = 0; Index < Sides; ++Index)
		{
			const float Angle = PxTwoPi * static_cast<float>(Index) / static_cast<float>(Sides);
			const float X = WheelRadius * std::cos(Angle);
			const float Z = WheelRadius * std::sin(Angle);
			Points[Index] = PxVec3(X, -WheelWidth * 0.5f, Z);
			Points[Index + Sides] = PxVec3(X, WheelWidth * 0.5f, Z);
		}
		PxConvexMeshDesc Desc;
		Desc.points.count = Sides * 2;
		Desc.points.stride = sizeof(PxVec3);
		Desc.points.data = Points;
		Desc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
		auto& Core = FPhysicsCore::Get();
		return Core.GetCooking()->createConvexMesh(Desc, Core.GetPhysics()->getPhysicsInsertionCallback());
	}

	PxVehicleDrivableSurfaceToTireFrictionPairs* CreateTireFrictionPairs(PxPhysics& Physics, PxMaterial& DefaultMaterial)
	{
		constexpr PxU32 MaxSurfaces = PxVehicleDrivableSurfaceToTireFrictionPairs::eMAX_NB_SURFACE_TYPES;
		PxMaterial* RegisteredMaterials[MaxSurfaces];
		const PxU32 RegisteredCount = Physics.getMaterials(RegisteredMaterials, MaxSurfaces);

		// PhysX는 등록되지 않은 표면에 0번 항목을 쓰므로 기본 재질을 먼저 넣는다.
		const PxMaterial* Materials[MaxSurfaces] = { &DefaultMaterial };
		PxVehicleDrivableSurfaceType Types[MaxSurfaces] = {};
		PxU32 Count = 1;
		for (PxU32 Index = 0; Index < RegisteredCount && Count < MaxSurfaces; ++Index)
		{
			if (RegisteredMaterials[Index] == &DefaultMaterial)
				continue;
			Materials[Count] = RegisteredMaterials[Index];
			Types[Count].mType = Count;
			++Count;
		}

		auto* Pairs = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(1, Count);
		if (!Pairs)
			return nullptr;
		Pairs->setup(1, Count, Materials, Types);
		Pairs->setTypePairFriction(0, 0, DefaultTireGrip);
		const float DefaultFriction = DefaultMaterial.getDynamicFriction();
		const float Scale = DefaultFriction > 0.0f ? DefaultTireGrip / DefaultFriction : DefaultTireGrip;
		for (PxU32 Index = 1; Index < Count; ++Index)
			Pairs->setTypePairFriction(Index, 0, Scale * Materials[Index]->getDynamicFriction());
		return Pairs;
	}

	PxShape* AttachCollisionBox(FBodyInstance& Body, const PxVec3& HalfSize, const PxVec3& Offset)
	{
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Body.Actor, PxBoxGeometry(HalfSize),
			*FPhysicsCore::Get().GetDefaultMaterial());
		if (Shape)
		{
			Shape->setLocalPose(PxTransform(Offset));
			const PxFilterData Filter = K2PFilterData(Body.MakeMaskFilter());
			Shape->setSimulationFilterData(Filter);
			Shape->setQueryFilterData(Filter);
			Shape->userData = &Body;
		}
		return Shape;
	}
}

struct FMonsterTruckPhysics
{
	~FMonsterTruckPhysics()
	{
		if (Vehicle) Vehicle->free();
		if (Batch) Batch->release();
		if (Friction) Friction->release();
		for (PxShape* Shape : WheelShapes)
			if (Shape) Truck->detachShape(*Shape);
	}

	FTruckVisualPose ReadVisualPose(const FVector& ChassisScale) const
	{
		FTruckVisualPose Pose;
		Pose.Chassis = P2KTransform(Truck->getGlobalPose(), ChassisScale);
		for (PxU32 Index = 0; Index < WheelCount; ++Index)
			Pose.Wheels[Index] = P2KTransform(WheelShapes[Index]->getLocalPose());
		return Pose;
	}

	PxRigidDynamic* Truck = nullptr;
	PxVehicleNoDrive* Vehicle = nullptr;
	PxBatchQuery* Batch = nullptr;
	PxVehicleDrivableSurfaceToTireFrictionPairs* Friction = nullptr;
	PxSweepQueryResult SweepResults[WheelCount] = {};
	PxSweepHit SweepHits[WheelCount * HitsPerWheel] = {};
	PxWheelQueryResult WheelResults[WheelCount] = {};
	PxShape* WheelShapes[WheelCount] = {};
	FTruckVisualPose PreviousVisualPose;
	FTruckVisualPose CurrentVisualPose;
	PxU32 VisualPoseTimestamp = 0;
	float VisualPoseInterval = PhysicsStep;
	float TimeSincePhysicsStep = 0.0f;
};

UMonsterTruckDriveComponent::UMonsterTruckDriveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::PrePhysics;
}

void UMonsterTruckDriveComponent::TickComponent(float DeltaTime)
{
	if (auto* Truck = Cast<AMonsterTruckPawn>(GetOwner()))
		Truck->UpdateVehicle(DeltaTime);
}

AMonsterTruckPawn::AMonsterTruckPawn()
{
	CreateDefaultSubobject<UMonsterTruckDriveComponent>("TruckDrive");
	// 물리가 차체 포즈를 동기화한 뒤 스프링 암 틱 전에 바퀴를 갱신한다.
	PrimaryActorTick.TickGroup = ETickingGroup::DuringPhysics;
	Chassis = CreateDefaultSubobject<UBoxComponent>("Chassis");
	SetRootComponent(Chassis);
	Chassis->BoxExtent = FVector(2.12f, 0.53f, 0.20f);
	Chassis->BodyInstance.bSimulatePhysics = true;
	Chassis->BodyInstance.bUseCCD = true;
	Chassis->BodyInstance.MassInKgOverride = TruckMass;
	Chassis->BodyInstance.ObjectType = ECollisionCategory::Vehicle;
	Chassis->bDrawOnlyIfSelected = true;

	Presentation = CreateDefaultSubobject<USceneComponent>("TruckPresentation");
	Presentation->SetupAttachment(Chassis);
	const auto CreateVisual = [&](const char* Name, const char* MeshPath, const FVector& Location) {
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Presentation);
		Part->SetRelativeLocation(Location);
		Part->BodyInstance.CollisionEnabled = ECollisionEnabled::NoCollision;
		Part->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>(MeshPath));
		return Part;
	};
	CreateVisual("CabAndFrame", "Truck/Cab.obj", FVector::ZeroVector);
	const char* WheelNames[] = { "WheelFL", "WheelFR", "WheelRL", "WheelRR" };
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		const char* MeshPath = Index % 2 == 0 ? "Truck/WheelLeft.obj" : "Truck/WheelRight.obj";
		Wheels[Index] = CreateVisual(WheelNames[Index], MeshPath, P2KVector(WheelCenters[Index]));
	}
	Axles[0] = CreateVisual("FrontAxle", "Truck/AxleFront.obj", FVector(WheelCenters[0].x, 0.0f, WheelCenters[0].z));
	Axles[1] = CreateVisual("RearAxle", "Truck/AxleRear.obj", FVector(WheelCenters[2].x, 0.0f, WheelCenters[2].z));

	CreateVisual("DumpBed", "Truck/DumpBed.obj", BedLocation);

	CameraArm = CreateDefaultSubobject<USpringArmComponent>("CameraArm");
	CameraArm->SetupAttachment(Presentation);
	CameraArm->SetRelativeLocation(FVector(0.0f, 0.0f, 1.17f));
	CameraArm->SetRelativeRotation(FRotator(17.0f, -12.0f, 0.0f));
	CameraArm->TargetArmLength = 8.8f;
	CameraArm->bInheritPitch = false;
	CameraArm->bInheritRoll = false;
	CameraArm->bEnableCameraLag = true;
	CameraArm->bEnableCameraRotationLag = true;
	CameraArm->CameraLagSpeed = 7.0f;
	CameraArm->CameraRotationLagSpeed = 4.0f;
	CameraArm->CameraLagMaxDistance = 1.75f;
	CameraArm->bDrawDebug = false;
	auto* Camera = CreateDefaultSubobject<UCameraComponent>("Camera");
	Camera->SetupAttachment(CameraArm);
	Camera->SetExternalInputManaged(true);
	Camera->SetFieldOfView(66.0f);
	Camera->PostProcessSettings.bOverride_bAutoExposure = true;
	Camera->PostProcessSettings.bAutoExposure = false;
}

AMonsterTruckPawn::~AMonsterTruckPawn() = default;

void AMonsterTruckPawn::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	const FArchive& Archive = Record.GetUnderlyingArchive();
	if (!Archive.IsLoading() || Archive.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate) || Physics)
		return;

	if (UActorComponent* LegacyBed = FindComponentByName(FName("BedBody")))
	{
		if (auto* Mesh = Cast<USceneComponent>(FindComponentByName(FName("DumpBed"))))
		{
			Mesh->SetupAttachment(Chassis, EAttachmentRule::KeepRelative);
			Mesh->SetRelativeLocation(BedLocation);
		}
		DestroyComponent(LegacyBed);
	}

	if (!Chassis->IsRegistered()) return;

	// 씬 로더는 저장값을 읽기 전에 기본 컴포넌트를 등록한다. 트럭의 바디와 메시만 갱신한다.
	Chassis->DestroyPhysicsState();
	Chassis->MarkTransformDirty();
	Chassis->CreatePhysicsState();
	for (UActorComponent* Component : GetComponents())
	{
		if (auto* Mesh = Cast<UStaticMeshComponent>(Component))
			Mesh->MarkRenderStateDirty();
	}
}

void AMonsterTruckPawn::BeginPlay()
{
	if (HasBegunPlay()) return;
	Super::BeginPlay();
	SpawnPose = GetActorTransform();
	CameraArm->ResetCameraLag();
	if (!SetupVehicle())
		KRS_LOG(Error, "MonsterTruck: vehicle setup failed.");
}

void AMonsterTruckPawn::EndPlay()
{
	if (Physics)
	{
		SCOPED_SCENE_WRITE_LOCK(Chassis->BodyInstance.Scene->GetPxScene());
		Physics.reset();
	}
	Super::EndPlay();
}

bool AMonsterTruckPawn::SetupVehicle()
{
	auto& ChassisBody = Chassis->BodyInstance;
	if (!ChassisBody.IsSimulatingPhysics()) return false;
	auto& Core = FPhysicsCore::Get();
	// Vehicle SDK의 축은 전역 설정이다. 차량을 준비할 때 엔진의 Z-up, X-forward 기준을 지정한다.
	PxVehicleSetBasisVectors(PxVec3(0, 0, 1), PxVec3(1, 0, 0));

	auto* Scene = ChassisBody.Scene->GetPxScene();
	SCOPED_SCENE_WRITE_LOCK(Scene);
	auto State = MakeUnique<FMonsterTruckPhysics>();
	State->Truck = ChassisBody.Actor->is<PxRigidDynamic>();
	PxConvexMesh* WheelMesh = CookWheel();
	if (!WheelMesh) return false;
	const PxShapeFlags WheelFlags(PxShapeFlag::eVISUALIZATION);
	const PxU32 ExistingShapes = State->Truck->getNbShapes();
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		State->WheelShapes[Index] = PxRigidActorExt::createExclusiveShape(*State->Truck,
			PxConvexMeshGeometry(WheelMesh), *Core.GetDefaultMaterial(), WheelFlags);
		if (State->WheelShapes[Index])
			State->WheelShapes[Index]->setLocalPose(PxTransform(WheelCenters[Index]));
	}
	WheelMesh->release();
	for (auto* Shape : State->WheelShapes)
		if (!Shape) return false;
	if (!AttachCollisionBox(ChassisBody, PxVec3(0.7f, 0.75f, 0.50f), PxVec3(0.88f, 0, 0.70f)) ||
		!AttachCollisionBox(ChassisBody, PxVec3(1.15f, 0.92f, 0.41f), K2PVector(BedLocation) + PxVec3(0, 0, 0.32f)))
		return false;

	State->Truck->setMass(TruckMass);
	State->Truck->setCMassLocalPose(PxTransform(CenterOfMass));
	State->Truck->setMassSpaceInertiaTensor(PxVec3(2700, 5800, 6200));
	State->Truck->setSolverIterationCounts(10, 3);

	PxVehicleWheelsSimData* Sim = PxVehicleWheelsSimData::allocate(WheelCount);
	if (!Sim) return false;
	PxReal SprungMasses[WheelCount];
	PxVehicleComputeSprungMasses(WheelCount, WheelCenters, CenterOfMass, TruckMass, 2, SprungMasses);
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		PxVehicleWheelData Wheel;
		Wheel.mRadius = WheelRadius;
		Wheel.mWidth = WheelWidth;
		Wheel.mMass = 92.0f;
		Wheel.mMOI = 0.5f * Wheel.mMass * WheelRadius * WheelRadius;
		Wheel.mDampingRate = 0.38f;
		Wheel.mMaxBrakeTorque = 5800.0f;
		Wheel.mMaxSteer = 0.55f;
		Sim->setWheelData(Index, Wheel);

		PxVehicleSuspensionData Suspension;
		Suspension.mSprungMass = SprungMasses[Index];
		Suspension.mMaxCompression = 0.34f;
		Suspension.mMaxDroop = 0.40f;
		Suspension.mSpringStrength = 42000.0f;
		Suspension.mSpringDamperRate = 6200.0f;
		Sim->setSuspensionData(Index, Suspension);

		PxVehicleTireData Tire;
		Tire.mType = 0;
		Tire.mFrictionVsSlipGraph[0][1] = 0.94f;
		Tire.mFrictionVsSlipGraph[1][0] = 0.16f;
		Tire.mFrictionVsSlipGraph[1][1] = 1.0f;
		Tire.mFrictionVsSlipGraph[2][0] = 0.75f;
		Tire.mFrictionVsSlipGraph[2][1] = 0.70f;
		Sim->setTireData(Index, Tire);
		const PxVec3 Offset = WheelCenters[Index] - CenterOfMass;
		const PxVec3 ForcePoint(Offset.x, Offset.y, -0.28f);
		Sim->setWheelCentreOffset(Index, Offset);
		Sim->setSuspTravelDirection(Index, PxVec3(0, 0, -1));
		Sim->setSuspForceAppPointOffset(Index, ForcePoint);
		Sim->setTireForceAppPointOffset(Index, ForcePoint);
		Sim->setWheelShapeMapping(Index, static_cast<PxI32>(ExistingShapes + Index));
	}
	Sim->setSubStepCount(7.0f, 3, 2);
	Sim->setMinLongSlipDenominator(2.5f);
	for (PxU32 Axle = 0; Axle < 2; ++Axle)
	{
		PxVehicleAntiRollBarData Bar;
		Bar.mWheel0 = Axle * 2;
		Bar.mWheel1 = Axle * 2 + 1;
		Bar.mStiffness = 5600.0f;
		Sim->addAntiRollBarData(Bar);
	}
	State->Vehicle = PxVehicleNoDrive::create(Core.GetPhysics(), State->Truck, *Sim);
	Sim->free();
	if (!State->Vehicle) return false;
	State->Vehicle->setToRestState();

	PxBatchQueryDesc BatchDesc(0, WheelCount, 0);
	BatchDesc.queryMemory.userSweepResultBuffer = State->SweepResults;
	BatchDesc.queryMemory.userSweepTouchBuffer = State->SweepHits;
	BatchDesc.queryMemory.sweepTouchBufferSize = WheelCount * HitsPerWheel;
	BatchDesc.preFilterShader = FilterSuspensionSweep;
	State->Batch = Scene->createBatchQuery(BatchDesc);
	State->Friction = CreateTireFrictionPairs(*Core.GetPhysics(), *Core.GetDefaultMaterial());
	if (!State->Batch || !State->Friction) return false;

	State->VisualPoseTimestamp = Scene->getTimestamp();
	State->PreviousVisualPose = State->CurrentVisualPose = State->ReadVisualPose(ChassisBody.Scale3D);
	Physics = std::move(State);
	return true;
}

void AMonsterTruckPawn::UpdateVehicle(float DeltaTime)
{
	const FVector DriveInput = ConsumeMovementInputVector();
	if (!Physics || DeltaTime <= 0.0f) return;

	const auto* Controller = GetController();
	const bool bBrake = Controller && Controller->IsKeyDown(static_cast<int32>(EKeyCode::Space));
	const bool bResetDown = Controller && Controller->IsKeyDown(static_cast<int32>(EKeyCode::R));
	const bool bReset = bResetDown && !bResetKeyDown;
	bResetKeyDown = bResetDown;
	if (bReset)
		Chassis->SetWorldLocationAndRotation(SpawnPose.Location, SpawnPose.Rotation.ToFRotator(), false);

	// 현재 씬이 한 프레임에 처리할 수 있는 물리 시간만큼만 차량을 적분한다.
	const float Step = FMath::Min(DeltaTime, PhysicsStep * MaxPhysicsStepsPerFrame);

	auto& State = *Physics;
	auto* Scene = Chassis->BodyInstance.Scene->GetPxScene();
	SCOPED_SCENE_WRITE_LOCK(Scene);
	if (bReset)
	{
		State.Truck->setGlobalPose(K2PTransform(SpawnPose));
		State.Vehicle->setToRestState();
		Steering = 0.0f;
		bResetCameraLag = true;
	}

	const float ForwardSpeed = State.Vehicle->computeForwardSpeed();
	const float Speed = std::abs(ForwardSpeed);
	const float SpeedBlend = FMath::Clamp((Speed - 4.0f) / 15.0f, 0.0f, 1.0f);
	Steering = FMath::Lerp(Steering, FMath::Clamp(DriveInput.Y, -1.0f, 1.0f), 1.0f - std::exp(-7.0f * Step));
	const float FrontSteer = Steering * FMath::Lerp(0.47f, 0.20f, SpeedBlend);
	const float RearSteer = -FrontSteer * FMath::Lerp(0.56f, 0.16f, SpeedBlend);
	const float Throttle = FMath::Clamp(DriveInput.X, -1.0f, 1.0f);
	const bool bDirectionBrake = Speed > 0.7f && Throttle * ForwardSpeed < 0.0f;
	const bool bBraking = bBrake || bDirectionBrake;
	const float SpeedLimit = Throttle >= 0.0f ? 23.0f : 6.0f;
	const float TorqueFactor = FMath::Clamp((SpeedLimit - Speed) / 3.0f, 0.0f, 1.0f);
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		const float Slip = std::abs(State.WheelResults[Index].longitudinalSlip);
		const float GripControl = State.WheelResults[Index].isInAir ? 0.25f :
			1.0f - 0.62f * FMath::Clamp((Slip - 0.18f) / 0.50f, 0.0f, 1.0f);
		State.Vehicle->setDriveTorque(Index, bBraking ? 0.0f : Throttle * 3100.0f * TorqueFactor * GripControl);
		State.Vehicle->setBrakeTorque(Index, bBraking ? 5800.0f : Throttle == 0.0f ? 190.0f : 0.0f);
		State.Vehicle->setSteerAngle(Index, Index < 2 ? FrontSteer : RearSteer);
	}

	PxVehicleWheels* Vehicles[] = { State.Vehicle };
	PxVehicleSuspensionSweeps(State.Batch, 1, Vehicles, WheelCount, State.SweepResults, HitsPerWheel, nullptr, 0.98f, 1.0f);
	PxVehicleWheelQueryResult Results{ State.WheelResults, WheelCount };
	PxVehicleUpdates(Step, Scene->getGravity(), *State.Friction, 1, Vehicles, &Results);
}

void AMonsterTruckPawn::Tick(float DeltaTime)
{
	if (!Physics) return;

	auto& State = *Physics;
	const FBodyInstance& Body = Chassis->BodyInstance;
	auto* Scene = Body.Scene->GetPxScene();
	PxU32 CompletedSteps = 0;
	{
		SCOPED_SCENE_READ_LOCK(Scene);
		const PxU32 Timestamp = Scene->getTimestamp();
		CompletedSteps = Timestamp - State.VisualPoseTimestamp;
		if (CompletedSteps > 0 || bResetCameraLag)
		{
			State.PreviousVisualPose = State.CurrentVisualPose;
			State.CurrentVisualPose = State.ReadVisualPose(Body.Scale3D);
			State.VisualPoseTimestamp = Timestamp;
		}
	}

	const float CompletedTime = static_cast<float>(CompletedSteps) * PhysicsStep;
	if (CompletedSteps > 0)
		State.VisualPoseInterval = CompletedTime;
	// PhysScene은 최대 스텝 수에 도달하면 해당 프레임의 남은 누적 시간을 비운다.
	State.TimeSincePhysicsStep = CompletedSteps >= MaxPhysicsStepsPerFrame ? 0.0f :
		FMath::Clamp(State.TimeSincePhysicsStep + DeltaTime - CompletedTime, 0.0f, PhysicsStep);
	if (bResetCameraLag)
		State.PreviousVisualPose = State.CurrentVisualPose;

	// 화면을 한 물리 스텝 지연해 같은 시점의 차체와 바퀴를 함께 보간한다.
	const float Alpha = FMath::Clamp(1.0f - (PhysicsStep - State.TimeSincePhysicsStep) / State.VisualPoseInterval,
		0.0f, 1.0f);
	const FTransform ChassisPose = InterpolateVisualPose(State.PreviousVisualPose.Chassis, State.CurrentVisualPose.Chassis, Alpha);
	Presentation->SetTransform(ChassisPose.GetRelativeTransform(GetActorTransform()));
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
		Wheels[Index]->SetTransform(InterpolateVisualPose(
			State.PreviousVisualPose.Wheels[Index], State.CurrentVisualPose.Wheels[Index], Alpha));
	for (PxU32 Axle = 0; Axle < 2; ++Axle)
	{
		const FVector A = Wheels[Axle * 2]->GetRelativeLocation();
		const FVector B = Wheels[Axle * 2 + 1]->GetRelativeLocation();
		const float Roll = std::atan2(B.Z - A.Z, B.Y - A.Y);
		Axles[Axle]->SetTransform(FTransform(P2KQuat(PxQuat(Roll, PxVec3(1, 0, 0))), (A + B) * 0.5f));
	}
	if (bResetCameraLag)
	{
		bResetCameraLag = false;
		CameraArm->ResetCameraLag();
	}
}
