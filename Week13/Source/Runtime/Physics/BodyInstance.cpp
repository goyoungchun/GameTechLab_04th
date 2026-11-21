#include "EnginePCH.h"
#include "BodyInstance.h"
#include "BodySetup.h"
#include "PhysScene.h"
#include "PhysicsCore.h"
#include "PhysXConversions.h"
#include "PhysXIncludes.h"
#include "PhysXFilterShader.h"
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
		physx::PxShape* Shape = physx::PxRigidActorExt::createExclusiveShape(Actor, Geometry, *Material);
		if (!Shape)
		{
			KRS_LOG(Error, "[Physics] Failed to create shape.");
			return;
		}
		Shape->setLocalPose(LocalPose);
		Shape->userData = Owner;

		const physx::PxFilterData FilterData = K2PFilterData(Owner->MakeMaskFilter());
		Shape->setSimulationFilterData(FilterData);   // 바디끼리 충돌 판정 (Filter Shader)
		Shape->setQueryFilterData(FilterData);        // 레이캐스트·스윕 판정 (A7-2)
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
			Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, !bSimulatePhysics);
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

	UpdateMassProperties();
	Scene->GetPxScene()->addActor(*Actor);
	return true;
}

void FBodyInstance::TermBody()
{
	if (!Actor)
		return;

	// 씬에서 먼저 뺀다. exclusive shape는 액터와 함께 해제된다.
	if (physx::PxScene* PxScene = Actor->getScene())
		PxScene->removeActor(*Actor);

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
	return Dynamic && !(Dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC);
}

void FBodyInstance::SetSimulatePhysics(bool bSimulate)
{
	bSimulatePhysics = bSimulate;
	physx::PxRigidDynamic* Dynamic = Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
	if (!Dynamic)
		return;   // Static 바디는 바꿀 수 없다. 아직 InitBody 전이면 설정값만 바뀐다.

	Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, !bSimulate);
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

	// 호출하지 않으면 질량 1, 관성 (1,1,1)로 남아 회전이 이상해진다.
	if (MassInKgOverride > 0.0f)
		physx::PxRigidBodyExt::setMassAndUpdateInertia(*Dynamic, MassInKgOverride);
	else
		physx::PxRigidBodyExt::updateMassAndInertia(*Dynamic, Density);
}

FTransform FBodyInstance::GetWorldTransform() const
{
	if (!Actor)
		return FTransform::Identity;
	return P2KTransform(Actor->getGlobalPose(), Scale3D);
}

void FBodyInstance::SetWorldTransform(const FTransform& NewTransform, bool bTeleport)
{
	if (!Actor)
		return;

	physx::PxRigidDynamic* Dynamic = Actor->is<physx::PxRigidDynamic>();
	const physx::PxTransform Pose = K2PTransform(NewTransform);

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