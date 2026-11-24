#include "EnginePCH.h"
#include "BodyInstance.h"
#include "BodySetup.h"
#include "PhysScene.h"
#include "PhysicsCore.h"
#include "PhysXConversions.h"
#include "PhysXIncludes.h"
#include "PhysXFilterShader.h"
#include "PhysicsLock.h"
#include "PhysicalMaterial.h"
#include "Components/PrimitiveComponent.h"

namespace
{
	// 스케일은 형상 크기에 반영한다. 캡슐·구는 균일해야 하므로 가장 큰 축을 쓴다.
	float MaxAbsXY(const FVector& S) { return std::max(std::abs(S.X), std::abs(S.Y)); }
	float MaxAbs3(const FVector& S) { return std::max({ std::abs(S.X), std::abs(S.Y), std::abs(S.Z) }); }

	physx::PxTransform MakeLocalPose(const FVector& Center, const FQuat& Rotation, const FVector& Scale)
	{
		const FVector ScaledCenter(Center.X * Scale.X, Center.Y * Scale.Y, Center.Z * Scale.Z);
		return physx::PxTransform(K2PVector(ScaledCenter), K2PQuat(Rotation));
	}

	void AttachShape(physx::PxRigidActor& Actor, const physx::PxGeometry& Geometry, const physx::PxTransform& LocalPose, FBodyInstance* Owner)
	{
		physx::PxMaterial* Material = FPhysicsCore::Get().GetDefaultMaterial();
		if (Owner->PhysMaterialOverride)
		{
			if (physx::PxMaterial* Override = Owner->PhysMaterialOverride->GetPxMaterial())
				Material = Override;
		}
		physx::PxShape* Shape = physx::PxRigidActorExt::createExclusiveShape(Actor, Geometry, *Material);
		if (!Shape)
		{
			KRS_LOG(Error, "[Physics] Failed to create shape.");
			return;
		}
		Shape->setLocalPose(LocalPose);

		// 시뮬레이션(밀고 밀림)과 쿼리(레이캐스트·스윕) 참여를 따로 켠다.
		const ECollisionEnabled Collision = Owner->CollisionEnabled;
		Shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE,
			Collision == ECollisionEnabled::PhysicsOnly || Collision == ECollisionEnabled::QueryAndPhysics);
		Shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE,
			Collision == ECollisionEnabled::QueryOnly || Collision == ECollisionEnabled::QueryAndPhysics);
		Shape->userData = Owner;

		const physx::PxFilterData FilterData = K2PFilterData(Owner->MakeMaskFilter());
		Shape->setSimulationFilterData(FilterData);   // 바디끼리 충돌 판정 (Filter Shader)
		Shape->setQueryFilterData(FilterData);        // 레이캐스트·스윕 판정 (A7-2)
	}

	// Kinematic에 CCD를 켜면 PhysX가 경고한다. 시뮬레이트 중일 때만 켠다.
	void ApplyDynamicFlags(physx::PxRigidDynamic& Dynamic, bool bSimulate, bool bUseCCD)
	{
		if (!bSimulate)
			Dynamic.setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, false);   // KINEMATIC을 켜기 전에 먼저 끈다
		Dynamic.setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, !bSimulate);
		if (bSimulate)
			Dynamic.setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, bUseCCD);
	}
}

FBodyInstance::~FBodyInstance()
{
	TermBody();
}

FMaskFilter FBodyInstance::MakeMaskFilter() const
{
	FMaskFilter Filter;
	Filter.Category = CategoryBit(ObjectType);
	Filter.CollidesWith = CollidesWith;
	Filter.OwnerId = OwnerId;
	Filter.Flags = bDrivable ? ECollisionFlags::Drivable : 0u;
	return Filter;
}

float FBodyInstance::GetEffectiveDensity() const
{
	return PhysMaterialOverride ? PhysMaterialOverride->Density : Density;
}

bool FBodyInstance::InitBody(UBodySetup* Setup, const FTransform& WorldTransform, UPrimitiveComponent* PrimComp, FPhysScene* InScene)
{
	TermBody();

	if (!Setup || !InScene || !InScene->IsValid() || !FPhysicsCore::Get().IsInitialized())
		return false;
	if (Setup->AggGeom.GetElementCount() == 0)
	{
		KRS_LOG(Warning, "[Physics] InitBody skipped: BodySetup has no shapes.");
		return false;
	}

	physx::PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
	const physx::PxTransform Pose = K2PTransform(WorldTransform);

	if (Mobility == EBodyMobility::Static)
	{
		Actor = Physics->createRigidStatic(Pose);
	}
	else
	{
		physx::PxRigidDynamic* Dynamic = Physics->createRigidDynamic(Pose);
		if (Dynamic)
		{
			Dynamic->setLinearDamping(LinearDamping);
			Dynamic->setAngularDamping(AngularDamping);
			ApplyDynamicFlags(*Dynamic, bSimulatePhysics, bUseCCD);
		}
		Actor = Dynamic;
	}
	if (!Actor)
	{
		KRS_LOG(Error, "[Physics] Failed to create rigid actor.");
		return false;
	}

	OwnerComponent = PrimComp;
	Scene = InScene;
	Scale3D = WorldTransform.Scale;
	Actor->userData = this;

	// Static이고 삼각형 충돌이 있으면 그것만 쓴다 (UE의 Use Complex As Simple).
	// 계단·지형처럼 오목한 배경을 정확히 막고, 평면 바닥도 두께 없이 그대로 쓸 수 있다.
	if (Mobility == EBodyMobility::Static)
	{
		if (physx::PxTriangleMesh* TriMesh = Setup->GetOrCreateTriMesh())
		{
			const FVector MeshScale(std::abs(Scale3D.X), std::abs(Scale3D.Y), std::abs(Scale3D.Z));
			const physx::PxTriangleMeshGeometry Geometry(TriMesh, physx::PxMeshScale(K2PVector(MeshScale)));
			AttachShape(*Actor, Geometry, physx::PxTransform(physx::PxIdentity), this);

			SCOPED_SCENE_WRITE_LOCK(Scene->GetPxScene());
			Scene->GetPxScene()->addActor(*Actor);
			return true;   // Static은 질량 계산이 없다
		}
	}

	const FKAggregateGeom& Geom = Setup->AggGeom;
	for (const FKSphereElem& Elem : Geom.SphereElems)
	{
		const physx::PxSphereGeometry Sphere(Elem.Radius * MaxAbs3(Scale3D));
		AttachShape(*Actor, Sphere, MakeLocalPose(Elem.Center, Elem.Rotation, Scale3D), this);
	}
	for (const FKBoxElem& Elem : Geom.BoxElems)
	{
		const physx::PxBoxGeometry Box(
			std::abs(Elem.Extent.X * Scale3D.X), std::abs(Elem.Extent.Y * Scale3D.Y), std::abs(Elem.Extent.Z * Scale3D.Z));
		AttachShape(*Actor, Box, MakeLocalPose(Elem.Center, Elem.Rotation, Scale3D), this);
	}
	for (const FKSphylElem& Elem : Geom.SphylElems)
	{
		// PhysX 캡슐은 로컬 X축으로 눕는다. 엔진 캡슐은 Z축으로 서므로 Y축 기준 90도 돌려 세운다.
		const physx::PxCapsuleGeometry Capsule(Elem.Radius * MaxAbsXY(Scale3D), Elem.Length * 0.5f * std::abs(Scale3D.Z));
		physx::PxTransform LocalPose = MakeLocalPose(Elem.Center, Elem.Rotation, Scale3D);
		LocalPose.q = LocalPose.q * physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0, 1, 0));
		AttachShape(*Actor, Capsule, LocalPose, this);
	}

	if (!Geom.ConvexElems.IsEmpty())
	{
		Setup->CreatePhysicsMeshes();
		for (int32 Index = 0; Index < Geom.ConvexElems.Num(); ++Index)
		{
			physx::PxConvexMesh* Mesh = Setup->GetConvexMesh(Index);
			if (!Mesh)
				continue;   // Cooking 실패한 요소는 건너뛴다 (경고는 CreatePhysicsMeshes가 남겼다)

			const FKConvexElem& Elem = Geom.ConvexElems[Index];
			// 구·캡슐과 달리 볼록 메시는 축마다 다른 스케일을 그대로 받는다.
			// 음수(뒤집기) 스케일은 PhysX가 유효하지 않은 지오메트리로 거부하므로 크기만 쓴다.
			const FVector MeshScale(std::abs(Scale3D.X), std::abs(Scale3D.Y), std::abs(Scale3D.Z));
			const physx::PxConvexMeshGeometry Convex(Mesh, physx::PxMeshScale(K2PVector(MeshScale)));
			AttachShape(*Actor, Convex, MakeLocalPose(Elem.Center, Elem.Rotation, Scale3D), this);
		}
	}

	// 여기까지는 씬에 들어가기 전이라 락이 필요 없다. 씬을 바꾸는 addActor만 잠근다.
	UpdateMassProperties();
	{
		SCOPED_SCENE_WRITE_LOCK(Scene->GetPxScene());
		Scene->GetPxScene()->addActor(*Actor);
	}
	return true;
}

void FBodyInstance::TermBody()
{
	if (!Actor)
		return;

	// 씬에서 먼저 뺀다. exclusive shape는 액터와 함께 해제된다. 빠진 뒤의 release는 락이 필요 없다.
	if (physx::PxScene* PxScene = Actor->getScene())
	{
		SCOPED_SCENE_WRITE_LOCK(PxScene);
		PxScene->removeActor(*Actor);
	}

	Actor->userData = nullptr;
	Actor->release();
	Actor = nullptr;
	Scene = nullptr;
	OwnerComponent = nullptr;
}

bool FBodyInstance::IsDynamic() const
{
	return Actor && Actor->is<physx::PxRigidDynamic>() != nullptr;
}

bool FBodyInstance::IsSimulatingPhysics() const
{
	const physx::PxRigidDynamic* Dynamic = Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
	if (!Dynamic)
		return false;
	SCOPED_SCENE_READ_LOCK(Dynamic->getScene());
	return !(Dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC);
}

void FBodyInstance::SetSimulatePhysics(bool bSimulate)
{
	bSimulatePhysics = bSimulate;
	physx::PxRigidDynamic* Dynamic = Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
	if (!Dynamic)
		return;   // Static 바디는 바꿀 수 없다. 아직 InitBody 전이면 설정값만 바뀐다.

	SCOPED_SCENE_WRITE_LOCK(Dynamic->getScene());
	ApplyDynamicFlags(*Dynamic, bSimulate, bUseCCD);
	if (bSimulate)
		Dynamic->wakeUp();   // 잠든 채로 전환하면 그 자리에 굳어 있는다
}

void FBodyInstance::SetMassOverride(float MassInKg)
{
	MassInKgOverride = MassInKg;
	UpdateMassProperties();
}

void FBodyInstance::UpdateMassProperties()
{
	physx::PxRigidDynamic* Dynamic = Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
	if (!Dynamic)
		return;

	// InitBody 중(씬에 들어가기 전)에는 getScene()이 null이라 잠그지 않는다.
	SCOPED_SCENE_WRITE_LOCK(Dynamic->getScene());
	// 호출하지 않으면 질량 1, 관성 (1,1,1)로 남아 회전이 이상해진다.
	// 기본은 시뮬레이션 shape만 질량에 넣는다. QueryOnly는 시뮬레이션 shape가 없으므로 쿼리 shape로 계산한다.
	const bool bIncludeNonSimShapes = (CollisionEnabled == ECollisionEnabled::QueryOnly);
	if (MassInKgOverride > 0.0f)
		physx::PxRigidBodyExt::setMassAndUpdateInertia(*Dynamic, MassInKgOverride, nullptr, bIncludeNonSimShapes);
	else
		physx::PxRigidBodyExt::updateMassAndInertia(*Dynamic, GetEffectiveDensity(), nullptr, bIncludeNonSimShapes);
}

FTransform FBodyInstance::GetWorldTransform() const
{
	if (!Actor)
		return FTransform::Identity;
	SCOPED_SCENE_READ_LOCK(Actor->getScene());
	return P2KTransform(Actor->getGlobalPose(), Scale3D);
}

void FBodyInstance::SetWorldTransform(const FTransform& NewTransform, bool bTeleport)
{
	if (!Actor)
		return;

	physx::PxRigidDynamic* Dynamic = Actor->is<physx::PxRigidDynamic>();
	const physx::PxTransform Pose = K2PTransform(NewTransform);

	SCOPED_SCENE_WRITE_LOCK(Actor->getScene());
	// Kinematic은 target으로 옮겨야 다음 simulate에서 부딪힌 물체를 밀어낸다. teleport면 즉시 옮긴다.
	if (Dynamic && !bTeleport && (Dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC))
		Dynamic->setKinematicTarget(Pose);
	else
		Actor->setGlobalPose(Pose);
}

FBodyInstance* FBodyInstance::FromActor(const physx::PxRigidActor* InActor)
{
	return InActor ? static_cast<FBodyInstance*>(InActor->userData) : nullptr;
}

void FBodyInstance::AddImpulseAtLocation(const FVector& Impulse, const FVector& WorldLocation)
{
	physx::PxRigidDynamic* Dynamic = Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
	if (!Dynamic)
		return;

	SCOPED_SCENE_WRITE_LOCK(Dynamic->getScene());
	if (Dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)
		return;   // Kinematic은 힘을 받지 않는다
	// 중심이 아닌 점을 밀면 회전도 생긴다 (모서리를 밀면 돈다).
	physx::PxRigidBodyExt::addForceAtPos(*Dynamic, K2PVector(Impulse), K2PVector(WorldLocation), physx::PxForceMode::eIMPULSE);
}
