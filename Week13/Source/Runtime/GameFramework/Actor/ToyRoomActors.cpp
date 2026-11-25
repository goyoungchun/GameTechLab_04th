#include "EnginePCH.h"
#include "GameFramework/Actor/ToyRoomActors.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Asset/AssetManager.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/Misc/Paths.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Physics/BodyInstance.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Physics/PhysicsLock.h"
#include "Physics/PhysXConversions.h"
#include "Render/SkeletalMesh.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float DegToRad = PI / 180.0f;

	// -180 ~ 180
	float WrapDegrees(float Degrees)
	{
		Degrees = std::fmod(Degrees + 180.0f, 360.0f);
		if (Degrees < 0.0f) Degrees += 360.0f;
		return Degrees - 180.0f;
	}

	FVector YawToForward(float YawDeg)
	{
		return FVector(std::cos(YawDeg * DegToRad), std::sin(YawDeg * DegToRad), 0.0f);
	}

	float SmoothStep(float X)
	{
		X = std::clamp(X, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}

	// 목표 방향으로 최대 Rate*dt만큼 돈다. 돌고 난 뒤 남은 각도(절댓값)를 돌려준다
	float TurnToward(float& Yaw, const FVector& Direction, float Rate, float DeltaTime)
	{
		if (Direction.X * Direction.X + Direction.Y * Direction.Y < 1.0e-8f)
		{
			return 0.0f;
		}
		const float Desired = std::atan2(Direction.Y, Direction.X) / DegToRad;
		const float Delta = WrapDegrees(Desired - Yaw);
		const float Step = std::clamp(Delta, -Rate * DeltaTime, Rate * DeltaTime);
		Yaw = WrapDegrees(Yaw + Step);
		return std::fabs(Delta - Step);
	}

	FVector Horizontal(const FVector& V)
	{
		return FVector(V.X, V.Y, 0.0f);
	}

	// 스켈레톤의 앞 방향이 액터 +X를 보도록 메시를 돌리고 배율을 맞춘다
	void SetupCharacterMesh(USkeletalMeshComponent* Mesh, const FProceduralWalkRig& Rig, const FProceduralWalkParams& Params,
		float Scale, FProceduralWalkInfo& OutInfo)
	{
		if (!Mesh)
		{
			return;
		}
		float YawOffset = 0.0f;
		if (USkeletalMesh* Asset = Mesh->GetSkeletalMesh())
		{
			FString Error;
			if (ProceduralWalkAnim::AnalyzeRig(Asset->GetRefSkeleton(), Rig, Params, OutInfo, Error))
			{
				YawOffset = -OutInfo.ForwardYawDeg;
			}
			else
			{
				KRS_LOG(Warning, "ToyRoom: rig analysis failed for {} ({})", Asset->GetPath(), Error);
			}
		}
		Mesh->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator(0.0f, YawOffset, 0.0f));
		Mesh->SetRelativeScale3D(FVector(Scale, Scale, Scale));
	}

	void LoadDefaultMesh(USkeletalMeshComponent* Mesh, const char* Path)
	{
		if (!Mesh || Mesh->GetSkeletalMesh() || !FPaths::HasProject())
		{
			return;
		}
		if (USkeletalMesh* Asset = UAssetManager::GetAssetByPath<USkeletalMesh>(Path))
		{
			Mesh->SetSkeletalMesh(Asset);
		}
	}

	FVector BoneWorldLocation(const USkeletalMeshComponent* Mesh, const char* BoneName, const FVector& Fallback)
	{
		if (!Mesh)
		{
			return Fallback;
		}
		const int32 Index = Mesh->FindBoneIndex(BoneName);
		if (Index < 0)
		{
			return Fallback;
		}
		return Mesh->GetBoneWorldMatrix(Index).TransformPosition(FVector::ZeroVector);
	}

	void SetPlayRate(USkeletalMeshComponent* Mesh, float Rate)
	{
		if (UAnimSingleNodeInstance* Instance = Mesh ? Mesh->GetSingleNodeInstance() : nullptr)
		{
			Instance->SetPlayRate(Rate);
		}
	}

	template <typename T>
	T* FindFirstActor(UWorld* World)
	{
		ULevel* Level = World ? World->GetPersistentLevel() : nullptr;
		if (!Level) return nullptr;
		for (AActor* Actor : Level->GetActors())
		{
			if (T* Found = Cast<T>(Actor)) return Found;
		}
		return nullptr;
	}
}

// ============================================================================
// AWoodyToyActor
// ============================================================================

AWoodyToyActor::AWoodyToyActor()
{
	PrimaryActorTick.bCanEverTick = true;
	MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	MeshComponent->SetupAttachment(GetRootComponent());
	LoadDefaultMesh(MeshComponent, DefaultMeshPath);
	ApplyMeshSetup();
}

FProceduralWalkRig AWoodyToyActor::MakeRig()
{
	FProceduralWalkRig Rig;
	Rig.Root = "Body";
	Rig.Pelvis = "Hips";
	Rig.Spine = { "Spine1", "Spine2", "Spine3" };
	Rig.Head = "Head";
	Rig.LegLeft = { "ThighLeft", "CalfLeft", "FootLeft" };
	Rig.LegRight = { "ThighRight", "CalfRight", "FootRight" };
	Rig.ArmLeft = { "BicepLeft", "ForearmLeft", "HandLeft" };
	Rig.ArmRight = { "BicepRight", "ForearmRight", "HandRight" };
	Rig.ToeLeft = "ToesLeft";
	Rig.ToeRight = "ToesRight";
	return Rig;
}

FProceduralWalkParams AWoodyToyActor::MakeWalkParams()
{
	// 헐렁한 봉제 인형이라 팔다리를 크게, 조금 통통 튀게 걷는다
	FProceduralWalkParams Params;
	Params.CycleSeconds = 0.9f;
	Params.ThighSwingDeg = 26.0f;
	Params.KneeBendDeg = 52.0f;
	Params.ArmHangDeg = 12.0f;
	Params.ArmSwingDeg = 28.0f;
	Params.ElbowBaseDeg = 14.0f;
	Params.ElbowSwingDeg = 22.0f;
	Params.PelvisTwistDeg = 8.0f;
	Params.SpineCounterTwistDeg = 10.0f;
	Params.SpineLeanDeg = 5.0f;
	Params.HeadNodDeg = 3.0f;
	Params.BobScale = 1.25f;
	return Params;
}

void AWoodyToyActor::ApplyMeshSetup()
{
	SetupCharacterMesh(MeshComponent, MakeRig(), MakeWalkParams(), MeshScale, WalkInfo);
}

void AWoodyToyActor::BeginPlay()
{
	Super::BeginPlay();
	LoadDefaultMesh(MeshComponent, DefaultMeshPath);
	ApplyMeshSetup();

	USkeletalMesh* Mesh = MeshComponent->GetSkeletalMesh();
	const FString MeshPath = Mesh ? Mesh->GetPath() : FString(DefaultMeshPath);
	UAnimSequence* Walk = ProceduralWalkAnim::LoadOrGenerate(Mesh, MeshPath, WalkAnimPath, MakeRig(), MakeWalkParams(), bRegenerateAnimations);

	// 대기 클립은 장면에서 쓰지 않지만 에디터에서 쓸 수 있도록 같이 만든다
	FProceduralWalkParams IdleParams = MakeWalkParams();
	IdleParams.bWalk = false;
	IdleParams.CycleSeconds = 3.0f;
	ProceduralWalkAnim::LoadOrGenerate(Mesh, MeshPath, IdleAnimPath, MakeRig(), IdleParams, bRegenerateAnimations);

	if (Walk)
	{
		MeshComponent->PlayAnimation(Walk, true);
	}

	const float Cycle = WalkInfo.CycleSeconds > 0.0f ? WalkInfo.CycleSeconds : 1.0f;
	WalkSpeed = WalkInfo.StrideLength * MeshScale / Cycle * SpeedScale;
	Yaw = GetActorRotation().Yaw;

	// 시작 위치를 중심으로 찌그러진 타원 경로. 시작할 때 보는 방향의 왼쪽으로 돈다
	const FVector Center = GetActorLocation();
	constexpr int32 NumPoints = 10;
	Waypoints.Reset();
	for (int32 Index = 0; Index < NumPoints; ++Index)
	{
		const float Angle = Yaw - 90.0f + 360.0f * static_cast<float>(Index + 1) / NumPoints;
		const float Radius = WanderRadius * (0.8f + 0.2f * std::sin(3.0f * Angle * DegToRad));
		const FVector Offset = YawToForward(Angle) * Radius;
		Waypoints.Add(Center + FVector(Offset.X, Offset.Y * 0.75f, 0.0f) - YawToForward(Yaw - 90.0f) * WanderRadius * 0.8f);
	}
	WaypointIndex = 0;
	bLimp = false;
}

AToyRoomHumanActor* AWoodyToyActor::FindHumanInRoom() const
{
	ULevel* Level = GetWorld() ? GetWorld()->GetPersistentLevel() : nullptr;
	if (!Level) return nullptr;
	const FVector Location = GetActorLocation();
	for (AActor* Actor : Level->GetActors())
	{
		AToyRoomHumanActor* Human = Cast<AToyRoomHumanActor>(Actor);
		if (Human && Human->IsInRoom() && FVector::Distance(Human->GetActorLocation(), Location) <= NoticeDistance)
		{
			return Human;
		}
	}
	return nullptr;
}

void AWoodyToyActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bLimp || DeltaTime <= 0.0f)
	{
		return;
	}

	if (FindHumanInRoom())
	{
		GoLimp();
		return;
	}

	if (Waypoints.IsEmpty() || WalkSpeed <= 0.0f)
	{
		return;
	}

	FVector Location = GetActorLocation();
	FVector ToTarget = Horizontal(Waypoints[WaypointIndex] - Location);
	if (ToTarget.Size() < 0.25f)
	{
		WaypointIndex = (WaypointIndex + 1) % Waypoints.Num();
		ToTarget = Horizontal(Waypoints[WaypointIndex] - Location);
	}

	// 크게 꺾을 때는 느려지고, 애니메이션도 같은 비율로 느리게 재생해 발이 미끄러지지 않게 한다
	const float Remaining = TurnToward(Yaw, ToTarget, TurnRate, DeltaTime);
	const float SpeedFactor = 0.55f + 0.45f * std::cos(std::min(Remaining, 90.0f) * DegToRad);
	Location += YawToForward(Yaw) * (WalkSpeed * SpeedFactor * DeltaTime);
	GetRootComponent()->SetRelativeLocationAndRotation(Location, FRotator(0.0f, Yaw, 0.0f));
	SetPlayRate(MeshComponent, SpeedFactor);
}

void AWoodyToyActor::GoLimp()
{
	if (bLimp)
	{
		return;
	}
	bLimp = true;

	const FVector Velocity = YawToForward(Yaw) * (WalkSpeed * LimpMomentum);
	MeshComponent->Stop();
	StabilizeRagdollBodies();
	MeshComponent->SetSimulatePhysics(true);
	if (!MeshComponent->IsRagdollSimulating())
	{
		KRS_LOG(Warning, "ToyRoom: Woody has no physics bodies (missing .phys?), cannot ragdoll");
		return;
	}

	// 키네마틱으로 걷던 바디에 걷던 속도를 넘겨, 그대로 앞으로 고꾸라지게 한다
	for (FBodyInstance* Body : MeshComponent->Bodies)
	{
		if (!Body || !Body->Actor || !Body->IsSimulatingPhysics())
		{
			continue;
		}
		physx::PxRigidDynamic* Dynamic = Body->Actor->is<physx::PxRigidDynamic>();
		if (!Dynamic)
		{
			continue;
		}
		SCOPED_SCENE_WRITE_LOCK(Dynamic->getScene());
		Dynamic->setLinearVelocity(K2PVector(Velocity));
		Dynamic->wakeUp();
	}
	KRS_LOG(Info, "ToyRoom: Woody went limp");
}

void AWoodyToyActor::StabilizeRagdollBodies()
{
	// 자동 생성된 바디에는 손가락·이·무릎 보조 본처럼 몇 그램짜리가 수백 그램짜리 몸통에 조인트로 붙어 있다.
	// 질량비가 100:1을 넘으면 PhysX 조인트 솔버가 수렴하지 못해, 바닥에 누운 뒤에도 바디가 계속 떨린다.
	// 가장 무거운 바디 대비 최소 질량을 보장하고, 솔버 반복을 늘리고, 회전을 감쇠해 봉제 인형처럼 가라앉게 한다.
	// 우디 에셋은 모자(구 형상)가 4kg로 머리의 5배이고, 3g짜리 무릎 바디가 0.7kg 발목 바디를 매단다.
	// 가장 무거운 쪽에 맞추면 손가락까지 무거워지므로, 중앙값 기준 [Min, Max] 배 범위로 질량을 정규화한다.
	TArray<float> Masses;
	for (FBodyInstance* Body : MeshComponent->Bodies)
	{
		physx::PxRigidDynamic* Dynamic = Body && Body->Actor ? Body->Actor->is<physx::PxRigidDynamic>() : nullptr;
		if (!Dynamic) continue;
		SCOPED_SCENE_READ_LOCK(Dynamic->getScene());
		Masses.Add(Dynamic->getMass());
	}
	if (Masses.IsEmpty())
	{
		return;
	}
	std::sort(Masses.begin(), Masses.end());
	const float Median = Masses[Masses.Num() / 2];
	const float MinMass = Median * RagdollMassRangeMin;
	const float MaxMass = Median * RagdollMassRangeMax;

	// 바디마다 조인트 앵커(바디 공간, 스케일 적용 전)를 모은다. 자식 조인트는 Pos2, 자기 조인트는 Pos1
	const UPhysicsAsset* PhysAsset = MeshComponent->GetPhysicsAsset();
	const TArray<FBoneInfo>& Bones = MeshComponent->GetBones();
	auto AnchorsOf = [&](const FBodyInstance* Body)
	{
		TArray<FVector> Anchors;
		if (!PhysAsset || Body->InstanceBoneIndex < 0 || Body->InstanceBoneIndex >= Bones.Num()) return Anchors;
		const FName BoneName(Bones[Body->InstanceBoneIndex].Name);
		for (const UPhysicsConstraintTemplate* Template : PhysAsset->ConstraintSetup)
		{
			if (!Template) continue;
			const FConstraintInstance& Joint = Template->DefaultInstance;
			if (Joint.ConstraintBone1 == BoneName) Anchors.Add(Joint.Pos1);
			if (Joint.ConstraintBone2 == BoneName) Anchors.Add(Joint.Pos2);
		}
		return Anchors;
	};

	for (FBodyInstance* Body : MeshComponent->Bodies)
	{
		physx::PxRigidDynamic* Dynamic = Body && Body->Actor ? Body->Actor->is<physx::PxRigidDynamic>() : nullptr;
		if (!Dynamic) continue;
		float Mass = 0.0f;
		{
			SCOPED_SCENE_READ_LOCK(Dynamic->getScene());
			Mass = Dynamic->getMass();
		}
		const float Clamped = std::clamp(Mass, MinMass, MaxMass);
		if (Clamped != Mass)
		{
			Body->SetMassOverride(Clamped);   // 관성도 같은 비율로 다시 계산된다
		}

		SCOPED_SCENE_WRITE_LOCK(Dynamic->getScene());

		// 형상보다 훨씬 먼 곳에 조인트가 붙은 바디(예: 2.6cm 무릎 바디 끝 14cm 지점의 발목 조인트)는
		// 회전 관성이 너무 작아 레버 끝의 힘에 휘둘린다. 앵커까지 거리 d로 만든 막대 관성 m·d²/3 이상을 보장한다
		const physx::PxVec3 CenterOfMass = Dynamic->getCMassLocalPose().p;
		float MaxDistSq = 0.0f;
		for (const FVector& Anchor : AnchorsOf(Body))
		{
			const physx::PxVec3 Local = K2PVector(Anchor * Body->Scale3D);
			MaxDistSq = std::max(MaxDistSq, (Local - CenterOfMass).magnitudeSquared());
		}
		const float MinInertia = Dynamic->getMass() * MaxDistSq / 3.0f;
		physx::PxVec3 Inertia = Dynamic->getMassSpaceInertiaTensor();
		Inertia.x = std::max(Inertia.x, MinInertia);
		Inertia.y = std::max(Inertia.y, MinInertia);
		Inertia.z = std::max(Inertia.z, MinInertia);
		Dynamic->setMassSpaceInertiaTensor(Inertia);

		Dynamic->setSolverIterationCounts(static_cast<physx::PxU32>(std::max(4, RagdollSolverIterations)), 4);
		Dynamic->setAngularDamping(RagdollAngularDamping);
		Dynamic->setLinearDamping(0.1f);
		Dynamic->setMaxDepenetrationVelocity(1.0f);
		Dynamic->setMaxAngularVelocity(30.0f);
	}
}

FVector AWoodyToyActor::GetBodyLocation() const
{
	return BoneWorldLocation(MeshComponent, "Spine2", GetActorLocation() + FVector(0.0f, 0.0f, 0.3f));
}

// ============================================================================
// AToyRoomHumanActor
// ============================================================================

AToyRoomHumanActor::AToyRoomHumanActor()
{
	PrimaryActorTick.bCanEverTick = true;
	MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	MeshComponent->SetupAttachment(GetRootComponent());
	LoadDefaultMesh(MeshComponent, DefaultMeshPath);
	ApplyMeshSetup();
}

FProceduralWalkRig AToyRoomHumanActor::MakeRig()
{
	FProceduralWalkRig Rig;
	Rig.Root = "COG";
	Rig.Pelvis = "hip";
	Rig.Spine = { "waist", "chest" };
	Rig.Head = "head";
	Rig.LegLeft = { "L_leg", "L_knee", "L_ankle" };
	Rig.LegRight = { "R_leg", "R_knee", "R_ankle" };
	Rig.ArmLeft = { "L_arm", "L_elbow", "L_hand" };
	Rig.ArmRight = { "R_arm", "R_elbow", "R_hand" };
	Rig.ToeLeft = "L_toe";
	Rig.ToeRight = "R_toe";
	return Rig;
}

FProceduralWalkParams AToyRoomHumanActor::MakeWalkParams()
{
	// 덩치 큰 어른: 느긋하고 묵직하게. 어깨가 넓어 팔을 조금 더 벌린다
	FProceduralWalkParams Params;
	Params.CycleSeconds = 1.15f;
	Params.ThighSwingDeg = 22.0f;
	Params.KneeBendDeg = 45.0f;
	Params.ArmHangDeg = 16.0f;
	Params.ArmSwingDeg = 16.0f;
	Params.ElbowBaseDeg = 15.0f;
	Params.ElbowSwingDeg = 14.0f;
	Params.PelvisTwistDeg = 5.0f;
	Params.SpineCounterTwistDeg = 6.0f;
	Params.SpineLeanDeg = 3.0f;
	Params.HeadNodDeg = 1.5f;
	return Params;
}

void AToyRoomHumanActor::ApplyMeshSetup()
{
	SetupCharacterMesh(MeshComponent, MakeRig(), MakeWalkParams(), MeshScale, WalkInfo);
}

void AToyRoomHumanActor::BeginPlay()
{
	Super::BeginPlay();
	LoadDefaultMesh(MeshComponent, DefaultMeshPath);
	ApplyMeshSetup();

	USkeletalMesh* Mesh = MeshComponent->GetSkeletalMesh();
	const FString MeshPath = Mesh ? Mesh->GetPath() : FString(DefaultMeshPath);
	WalkAnim = ProceduralWalkAnim::LoadOrGenerate(Mesh, MeshPath, WalkAnimPath, MakeRig(), MakeWalkParams(), bRegenerateAnimations);
	FProceduralWalkParams IdleParams = MakeWalkParams();
	IdleParams.bWalk = false;
	IdleParams.CycleSeconds = 3.5f;
	IdleAnim = ProceduralWalkAnim::LoadOrGenerate(Mesh, MeshPath, IdleAnimPath, MakeRig(), IdleParams, bRegenerateAnimations);
	if (IdleAnim)
	{
		MeshComponent->PlayAnimation(IdleAnim, true);
	}

	const float Cycle = WalkInfo.CycleSeconds > 0.0f ? WalkInfo.CycleSeconds : 1.0f;
	WalkSpeed = WalkInfo.StrideLength * MeshScale / Cycle * SpeedScale;
	Yaw = GetActorRotation().Yaw;

	Door = nullptr;
	if (ULevel* Level = GetWorld() ? GetWorld()->GetPersistentLevel() : nullptr)
	{
		for (AActor* Actor : Level->GetActors())
		{
			if (Actor && Actor->GetName() == DoorActorName)
			{
				Door = Actor;
				break;
			}
		}
	}
	if (Door)
	{
		DoorClosedRotation = Door->GetActorRotation();
	}
	Woody = FindFirstActor<AWoodyToyActor>(GetWorld());

	Phase = EPhase::Waiting;
	PhaseTime = 0.0f;
	DoorAlpha = 0.0f;
}

void AToyRoomHumanActor::SetPhase(EPhase NewPhase)
{
	Phase = NewPhase;
	PhaseTime = 0.0f;
	UAnimSequence* Wanted = (NewPhase == EPhase::Entering || NewPhase == EPhase::Approaching) ? WalkAnim : IdleAnim;
	if (Wanted)
	{
		MeshComponent->PlayAnimation(Wanted, true);
	}
}

bool AToyRoomHumanActor::IsInRoom() const
{
	return Phase >= EPhase::Entering || (Phase == EPhase::OpeningDoor && DoorAlpha > 0.35f);
}

FVector AToyRoomHumanActor::GetHeadLocation() const
{
	return BoneWorldLocation(MeshComponent, "head", GetActorLocation() + FVector(0.0f, 0.0f, 1.8f));
}

bool AToyRoomHumanActor::WalkToward(const FVector& Target, float DeltaTime, float ArriveDistance)
{
	FVector Location = GetActorLocation();
	const FVector ToTarget = Horizontal(Target - Location);
	const float Distance = ToTarget.Size();
	// 마지막 걸음은 정확히 ArriveDistance까지만 가므로 부동소수 오차만큼 여유를 둔다
	if (Distance <= ArriveDistance + 0.01f)
	{
		SetPlayRate(MeshComponent, 1.0f);
		return true;
	}
	const float Remaining = TurnToward(Yaw, ToTarget, 160.0f, DeltaTime);
	const float SpeedFactor = 0.5f + 0.5f * std::cos(std::min(Remaining, 90.0f) * DegToRad);
	const float Step = std::min(WalkSpeed * SpeedFactor * DeltaTime, Distance - ArriveDistance);
	Location += YawToForward(Yaw) * Step;
	GetRootComponent()->SetRelativeLocationAndRotation(Location, FRotator(0.0f, Yaw, 0.0f));
	SetPlayRate(MeshComponent, SpeedFactor);
	return false;
}

void AToyRoomHumanActor::UpdateDoor()
{
	if (!Door || !Door->GetRootComponent())
	{
		return;
	}
	FRotator Rotation = DoorClosedRotation;
	Rotation.Yaw += DoorOpenAngle * DoorAlpha;
	Door->GetRootComponent()->SetRelativeRotation(Rotation);
}

void AToyRoomHumanActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (DeltaTime <= 0.0f)
	{
		return;
	}
	PhaseTime += DeltaTime;

	// 문은 단계와 상관없이 열리기 시작한 뒤로 끝까지 열린다
	if (Phase != EPhase::Waiting && DoorAlpha < 1.0f)
	{
		DoorAlpha = std::min(1.0f, DoorAlpha + DeltaTime / std::max(0.1f, DoorOpenSeconds));
		UpdateDoor();
	}

	switch (Phase)
	{
	case EPhase::Waiting:
		if (PhaseTime >= EnterDelay)
		{
			SetPhase(EPhase::OpeningDoor);
		}
		break;

	case EPhase::OpeningDoor:
		// 문이 반쯤 열리면 걸어 들어온다
		if (SmoothStep(DoorAlpha) >= 0.5f)
		{
			SetPhase(EPhase::Entering);
		}
		break;

	case EPhase::Entering:
		if (WalkToward(EntryPoint, DeltaTime, 0.2f))
		{
			SetPhase(Woody ? EPhase::Approaching : EPhase::Standing);
		}
		break;

	case EPhase::Approaching:
	{
		const FVector WoodyLocation = Woody ? Woody->GetBodyLocation() : GetActorLocation();
		if (WalkToward(WoodyLocation, DeltaTime, StopDistance))
		{
			SetPhase(EPhase::Standing);
		}
		break;
	}

	case EPhase::Standing:
		if (Woody)
		{
			TurnToward(Yaw, Horizontal(Woody->GetBodyLocation() - GetActorLocation()), 60.0f, DeltaTime);
			GetRootComponent()->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
		}
		break;
	}
}

// ============================================================================
// AToyRoomDirector
// ============================================================================

AToyRoomDirector::AToyRoomDirector()
{
	SetActorEnableCollision(false);
	// 캐릭터·랙돌이 이번 프레임 위치를 정한 뒤에 카메라를 옮긴다
	PrimaryActorTick.TickGroup = ETickingGroup::PostUpdateWork;
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	CameraComponent->SetupAttachment(GetRootComponent());
	CameraComponent->SetExternalInputManaged(true);
}

void AToyRoomDirector::FindActors()
{
	if (!Woody) Woody = FindFirstActor<AWoodyToyActor>(GetWorld());
	if (!Human) Human = FindFirstActor<AToyRoomHumanActor>(GetWorld());
}

void AToyRoomDirector::BeginPlay()
{
	Super::BeginPlay();
	FindActors();
	CameraComponent->SetFieldOfView(FieldOfView);
	Time = 0.0f;
	bHasCamera = false;
}

void AToyRoomDirector::ComputeShot(float DeltaTime, FVector& OutLocation, FVector& OutTarget) const
{
	const FVector Up(0.0f, 0.0f, 1.0f);
	const FVector WoodyBody = Woody ? Woody->GetBodyLocation() : GetActorLocation();
	const FVector WoodyGround(WoodyBody.X, WoodyBody.Y, Woody ? Woody->GetActorLocation().Z : 0.0f);

	using EPhase = AToyRoomHumanActor::EPhase;
	const EPhase Phase = Human ? Human->GetPhase() : EPhase::Waiting;

	if (!Human || Phase == EPhase::Waiting)
	{
		// 1) 우디 눈높이로 천천히 돌며 걷는 모습을 보여 준다
		OutLocation = WoodyGround + YawToForward(OrbitAngle) * OrbitRadius + Up * OrbitHeight;
		OutTarget = WoodyBody + Up * 0.05f;
		return;
	}

	const FVector HumanGround = Human->GetActorLocation();
	const FVector HumanHead = Human->GetHeadLocation();
	const bool bEarly = Phase == EPhase::OpeningDoor || (Phase == EPhase::Entering && Human->GetPhaseTime() < 1.6f);
	if (bEarly)
	{
		// 2) 우디 어깨 너머로 문을 본다: 앞에서 쓰러지는 우디와 문간에 선 사람이 한 화면에
		const FVector ToDoor = Horizontal(HumanGround - WoodyGround).GetSafeNormal(SMALL_NUMBER, FVector(1, 0, 0));
		const FVector Side = Up.Cross(ToDoor);
		OutLocation = WoodyGround - ToDoor * 0.9f + Side * 0.35f + Up * 0.28f;
		OutTarget = WoodyGround + (HumanHead - WoodyGround) * 0.55f;
		return;
	}

	if (Phase == EPhase::Entering || Phase == EPhase::Approaching)
	{
		// 3) 옆에서 넓게: 다가오는 사람과 바닥의 우디
		const FVector Mid = (WoodyGround + HumanGround) * 0.5f;
		const FVector Across = Horizontal(HumanGround - WoodyGround).GetSafeNormal(SMALL_NUMBER, FVector(1, 0, 0));
		FVector Side = Up.Cross(Across);
		// 방 가운데 쪽에서 찍는다 (벽을 등지지 않게)
		if (Side.Dot(Horizontal(-Mid)) < 0.0f) Side = -Side;
		OutLocation = Mid + Side * 2.4f + Up * 1.1f;
		OutTarget = Mid + Up * 0.55f;
		return;
	}

	const FVector Look = Horizontal(WoodyGround - HumanGround).GetSafeNormal(SMALL_NUMBER, FVector(1, 0, 0));
	FVector Side = Up.Cross(Look);
	if (Side.Dot(Horizontal(-WoodyGround)) < 0.0f) Side = -Side;   // 방 가운데 쪽
	if (Human->GetPhaseTime() < 4.5f)
	{
		// 4) 사람 옆에서 내려다본 바닥의 우디. 사람 몸에 가리지 않게 어깨 바깥에서 찍는다
		OutLocation = HumanGround + Side * 1.2f - Look * 0.25f + Up * 1.75f;
		OutTarget = WoodyBody;
		return;
	}
	// 5) 바닥 높이에서 쓰러진 우디, 뒤로 우뚝 선 사람
	OutLocation = WoodyGround + Look * 0.85f + Side * 0.35f + Up * 0.16f;
	OutTarget = WoodyBody + (HumanHead - WoodyBody) * 0.3f;
}

void AToyRoomDirector::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	FindActors();
	Time += DeltaTime;
	OrbitAngle = WrapDegrees(OrbitAngle + OrbitSpeed * DeltaTime);

	FVector DesiredLocation, DesiredTarget;
	ComputeShot(DeltaTime, DesiredLocation, DesiredTarget);

	if (!bHasCamera)
	{
		CameraLocation = DesiredLocation;
		CameraTarget = DesiredTarget;
		bHasCamera = true;
	}
	else
	{
		const float Alpha = 1.0f - std::exp(-CameraSmoothing * DeltaTime);
		CameraLocation = CameraLocation + (DesiredLocation - CameraLocation) * Alpha;
		// 시선은 조금 더 빠르게 따라가야 피사체가 화면 밖으로 나가지 않는다
		const float TargetAlpha = 1.0f - std::exp(-CameraSmoothing * 1.8f * DeltaTime);
		CameraTarget = CameraTarget + (DesiredTarget - CameraTarget) * TargetAlpha;
	}

	FRotator Rotation;
	Rotation.LookDirection(CameraTarget - CameraLocation);
	GetRootComponent()->SetRelativeLocationAndRotation(CameraLocation, Rotation);
}
