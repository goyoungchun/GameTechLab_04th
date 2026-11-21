#include "EnginePCH.h"
#include "PhysScene.h"
#include "PhysicsCore.h"
#include "PhysXIncludes.h"
#include "BodyInstance.h"
#include "PhysXConversions.h"
#include "Components/PrimitiveComponent.h"

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
	Desc.filterShader = physx::PxDefaultSimulationFilterShader;
	Desc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS | physx::PxSceneFlag::eENABLE_PCM;

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
		SyncActiveActors();

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
