#include "EnginePCH.h"
#include "PhysScene.h"
#include "PhysicsCore.h"
#include "PhysXIncludes.h"

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
