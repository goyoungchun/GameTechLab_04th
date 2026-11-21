#include "EnginePCH.h"
#include "PhysScene.h"
#include "PhysicsCore.h"
#include "PhysXIncludes.h"
#include "BodyInstance.h"
#include "PhysXConversions.h"
#include "PhysXFilterShader.h"
#include "PhysXSimulationEventCallback.h"
#include "Components/PrimitiveComponent.h"

namespace
{
	// 쿼리 후보 shape마다 불린다. 종류·플래그·제외 컴포넌트를 한곳에서 거른다.
	class FKurisuQueryFilterCallback : public physx::PxQueryFilterCallback
	{
	public:
		explicit FKurisuQueryFilterCallback(const FCollisionQueryParams& InParams) : Params(InParams) {}

		physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData& /*QueryData*/, const physx::PxShape* Shape,
			const physx::PxRigidActor* Actor, physx::PxHitFlags& /*Flags*/) override
		{
			const physx::PxFilterData ShapeData = Shape->getQueryFilterData();
			if ((ShapeData.word0 & Params.ObjectTypesMask) == 0)
				return physx::PxQueryHitType::eNONE;
			if ((ShapeData.word3 & Params.RequiredFlags) != Params.RequiredFlags)
				return physx::PxQueryHitType::eNONE;
			if (Params.IgnoreComponent)
			{
				const FBodyInstance* Body = FBodyInstance::FromActor(Actor);
				if (Body && Body->OwnerComponent == Params.IgnoreComponent)
					return physx::PxQueryHitType::eNONE;
			}
			return physx::PxQueryHitType::eBLOCK;
		}

		physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit&) override
		{
			return physx::PxQueryHitType::eBLOCK;   // ePOSTFILTER를 켜지 않으므로 불리지 않는다
		}

	private:
		const FCollisionQueryParams& Params;
	};

	physx::PxQueryFilterData MakeQueryFilterData()
	{
		// 정적·동적 둘 다 보고, 거르기는 preFilter 콜백이 한다.
		return physx::PxQueryFilterData(physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER);
	}

	// PhysX 캡슐은 X축으로 눕는다. 엔진 캡슐(Z축)과 맞추려고 Y축 기준 90도 돌린다 (BodyInstance와 같은 규약).
	physx::PxQuat CapsuleAxisFix() { return physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0, 1, 0)); }

	// 모양 하나와 그 회전을 함께 만든다. 캡슐만 축 보정이 붙는다.
	physx::PxGeometryHolder MakeGeometry(const FCollisionShape& Shape, physx::PxQuat& InOutRotation)
	{
		switch (Shape.Type)
		{
		case FCollisionShape::EType::Box:
			return physx::PxBoxGeometry(K2PVector(Shape.HalfExtent));
		case FCollisionShape::EType::Capsule:
			InOutRotation = InOutRotation * CapsuleAxisFix();
			return physx::PxCapsuleGeometry(Shape.Radius, std::max(Shape.HalfHeight - Shape.Radius, 0.0f));
		default:
			return physx::PxSphereGeometry(Shape.Radius);
		}
	}

	void FillHitResult(const physx::PxLocationHit& Hit, const FVector& Start, const FVector& End, const FVector& Dir, FHitResult& OutHit)
	{
		const float MaxDistance = (End - Start).Size();
		OutHit = FHitResult();
		OutHit.TraceStart = Start;
		OutHit.TraceEnd = End;
		OutHit.bBlockingHit = true;
		OutHit.bStartPenetrating = Hit.hadInitialOverlap();
		OutHit.Distance = Hit.distance;
		OutHit.Time = MaxDistance > 0.0f ? Hit.distance / MaxDistance : 0.0f;
		OutHit.ImpactPoint = P2KVector(Hit.position);
		OutHit.ImpactNormal = P2KVector(Hit.normal);
		OutHit.Location = Start + Dir * Hit.distance;   // 레이는 ImpactPoint와 같고, 스윕은 모양 중심 위치
		OutHit.Normal = OutHit.ImpactNormal;
		OutHit.PenetrationDepth = 0.0f;
		OutHit.FaceIndex = static_cast<int32>(Hit.faceIndex);
		const FBodyInstance* Body = FBodyInstance::FromActor(Hit.actor);
		OutHit.HitComponent = Body ? Body->OwnerComponent : nullptr;
	}
}

FPhysScene::FPhysScene(UWorld* InOwningWorld)
	: OwningWorld(InOwningWorld)
{
	if (!FPhysicsCore::Get().IsInitialized())
	{
		KRS_LOG(Error, "[Physics] FPhysScene created before FPhysicsCore initialized. Physics disabled.");
		return;
	}

	physx::PxSceneDesc Desc(FPhysicsCore::Get().GetPhysics()->getTolerancesScale());
	Desc.gravity = physx::PxVec3(0, 0, -9.81f);
	Desc.cpuDispatcher = FPhysicsCore::Get().GetDispatcher();
	Desc.filterShader = KurisuSimulationFilterShader;
	Desc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS | physx::PxSceneFlag::eENABLE_PCM;
	EventCallback = MakeUnique<FPhysXSimulationEventCallback>(*this);
	Desc.simulationEventCallback = EventCallback.get();

	if (!Desc.isValid())
	{
		KRS_LOG(Error, "[Physics] FPhysScene PxSceneDesc is invalid. Physics disabled.");
		return;
	}

	Scene = FPhysicsCore::Get().GetPhysics()->createScene(Desc);
	if (!Scene)
	{
		KRS_LOG(Error, "[Physics] FPhysScene PxScene creation failed. Physics disabled.");
		return;
	}

	Scene->userData = this;

	if (physx::PxPvdSceneClient* PvdClient = Scene->getScenePvdClient())
	{
		PvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
		PvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
		PvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
	}

	FPhysicsCore::Get().OnSceneCreated();
}

FPhysScene::~FPhysScene()
{
	if(!Scene)
		return;

	const physx::PxU32 NumActors = Scene->getNbActors(physx::PxActorTypeFlag::eRIGID_STATIC | physx::PxActorTypeFlag::eRIGID_DYNAMIC);
	if (NumActors != 0)
	{
		KRS_LOG(Warning, "[Physics] FPhysScene destroyed with {} actors still in the scene.", NumActors);
	}

	Scene->release();
	Scene = nullptr;
	FPhysicsCore::Get().OnSceneDestroyed();
}

void FPhysScene::Tick(float DeltaTime)
{
	if (!Scene || DeltaTime <= 0.0f)
		return;

	TimeAccumulator += DeltaTime;

	int32 Steps = 0;
	while (TimeAccumulator >= FixedTimeStep && Steps < MaxSubSteps)
	{
		// 동기 실행: simulate가 워커 스레드에 일을 나누고, fetchResults(true)가 끝날 때까지 기다린다.
		// 이 둘 사이에는 씬을 건드리는 API(InitBody, setGlobalPose 등)를 부르면 안 된다.
		Scene->simulate(FixedTimeStep);
		Scene->fetchResults(true);
		TimeAccumulator -= FixedTimeStep;
		++Steps;
	}

	// 브레이크포인트 등으로 몇 초가 한꺼번에 들어오면 따라잡지 않고 버린다 (무한히 밀리는 것 방지).
	if (Steps == MaxSubSteps)
		TimeAccumulator = 0.0f;

	if (Steps > 0)
	{
		SyncActiveActors();
		DispatchPendingNotifies();
	}

	// simulate 중 워커 스레드에서 쌓인 PhysX 로그를 메인 스레드에서 내보낸다.
	FPhysicsCore::Get().FlushPendingLogs();
}

void FPhysScene::SyncActiveActors()
{
	// eENABLE_ACTIVE_ACTORS 덕분에 이번 스텝에 움직인(깨어 있는) 액터만 돌아온다. 잠든 바디는 건너뛴다.
	physx::PxU32 NumActive = 0;
	physx::PxActor** ActiveActors = Scene->getActiveActors(NumActive);

	for (physx::PxU32 Index = 0; Index < NumActive; ++Index)
	{
		physx::PxRigidActor* RigidActor = ActiveActors[Index]->is<physx::PxRigidActor>();
		FBodyInstance* Body = FBodyInstance::FromActor(RigidActor);
		if (!Body || !Body->OwnerComponent || !Body->IsSimulatingPhysics())
			continue;   // Kinematic은 컴포넌트가 주인이다. 물리 결과로 덮어쓰지 않는다

		const physx::PxTransform Pose = RigidActor->getGlobalPose();
		Body->OwnerComponent->SetWorldLocationAndRotation(P2KVector(Pose.p), P2KQuat(Pose.q).ToFRotator(), false);
	}
}

bool FPhysScene::RaycastSingle(const FVector& Start, const FVector& End, FHitResult& OutHit, const FCollisionQueryParams& Params) const
{
	const FVector Delta = End - Start;
	const float Distance = Delta.Size();
	if (!Scene || Distance <= 0.0f)
		return false;
	const FVector Dir = Delta / Distance;

	FKurisuQueryFilterCallback Filter(Params);
	physx::PxRaycastBuffer Buffer;
	const bool bHit = Scene->raycast(K2PVector(Start), K2PVector(Dir), Distance, Buffer,
		physx::PxHitFlag::eDEFAULT, MakeQueryFilterData(), &Filter);
	if (!bHit || !Buffer.hasBlock)
		return false;

	FillHitResult(Buffer.block, Start, End, Dir, OutHit);
	return true;
}

bool FPhysScene::SweepSingle(const FCollisionShape& Shape, const FQuat& Rotation, const FVector& Start, const FVector& End,
	FSweepResult& OutHit, const FCollisionQueryParams& Params) const
{
	const FVector Delta = End - Start;
	const float Distance = Delta.Size();
	if (!Scene || Distance <= 0.0f)
		return false;
	const FVector Dir = Delta / Distance;

	physx::PxQuat Rot = K2PQuat(Rotation);
	const physx::PxGeometryHolder Geometry = MakeGeometry(Shape, Rot);

	FKurisuQueryFilterCallback Filter(Params);
	physx::PxSweepBuffer Buffer;
	const bool bHit = Scene->sweep(Geometry.any(), physx::PxTransform(K2PVector(Start), Rot), K2PVector(Dir), Distance, Buffer,
		physx::PxHitFlag::eDEFAULT, MakeQueryFilterData(), &Filter);
	if (!bHit || !Buffer.hasBlock)
		return false;

	FillHitResult(Buffer.block, Start, End, Dir, OutHit);
	return true;
}

bool FPhysScene::OverlapMulti(const FCollisionShape& Shape, const FVector& Position, const FQuat& Rotation,
	TArray<FOverlapResult>& OutOverlaps, const FCollisionQueryParams& Params) const
{
	OutOverlaps.Empty();
	if (!Scene)
		return false;

	physx::PxQuat Rot = K2PQuat(Rotation);
	const physx::PxGeometryHolder Geometry = MakeGeometry(Shape, Rot);

	// 겹친 것을 모두 받으려면 eBLOCK이 아닌 eTOUCH로 모아야 한다. 최대 256개까지 받는다.
	constexpr physx::PxU32 MaxHits = 256;
	physx::PxOverlapHit Hits[MaxHits];
	physx::PxOverlapBuffer Buffer(Hits, MaxHits);

	physx::PxQueryFilterData FilterData = MakeQueryFilterData();
	FilterData.flags |= physx::PxQueryFlag::eNO_BLOCK;   // 모든 후보를 touch로 취급

	FKurisuQueryFilterCallback Filter(Params);
	Scene->overlap(Geometry.any(), physx::PxTransform(K2PVector(Position), Rot), Buffer, FilterData, &Filter);

	for (physx::PxU32 Index = 0; Index < Buffer.getNbTouches(); ++Index)
	{
		const physx::PxOverlapHit& Hit = Buffer.getTouch(Index);
		const FBodyInstance* Body = FBodyInstance::FromActor(Hit.actor);
		OutOverlaps.Add(FOverlapResult{ Body ? Body->OwnerComponent : nullptr, static_cast<int32>(Hit.faceIndex) });
	}
	return !OutOverlaps.IsEmpty();
}

void FPhysScene::DispatchPendingNotifies()
{
	// 디스패치 중 게임 코드가 액터를 지우거나 새 바디를 만들 수 있으므로 큐를 먼저 꺼내 온다.
	TArray<FCollisionNotifyInfo> Collisions = std::move(PendingCollisionNotifies);
	TArray<FTriggerNotifyInfo> Triggers = std::move(PendingTriggerNotifies);
	PendingCollisionNotifies.Empty();
	PendingTriggerNotifies.Empty();

	for (const FCollisionNotifyInfo& Info : Collisions)
	{
		// 앞선 이벤트 처리에서 컴포넌트가 등록 해제됐을 수 있다.
		if (!Info.ComponentA->IsRegistered() || !Info.ComponentB->IsRegistered())
			continue;
		Info.ComponentA->DispatchHit(Info.ComponentB);
		if (Info.ComponentB->IsRegistered())
			Info.ComponentB->DispatchHit(Info.ComponentA);
	}

	for (const FTriggerNotifyInfo& Info : Triggers)
	{
		if (!Info.TriggerComponent->IsRegistered() || !Info.OtherComponent->IsRegistered())
			continue;
		if (Info.bBegin)
		{
			Info.TriggerComponent->BeginComponentOverlap(Info.OtherComponent);
			Info.OtherComponent->BeginComponentOverlap(Info.TriggerComponent);
		}
		else
		{
			Info.TriggerComponent->EndComponentOverlap(Info.OtherComponent);
			Info.OtherComponent->EndComponentOverlap(Info.TriggerComponent);
		}
	}
}