#pragma once

#include "Core/Types.h"

namespace physx
{
	class PxScene;
}

class UWorld;

// UWorld 하나가 소유하는 PhysX 씬. 생성자에서 PxScene을 만들고 소멸자에서 해제한다.
class FPhysScene
{
public:
	explicit FPhysScene(UWorld* InOwningWorld);
	~FPhysScene();

	FPhysScene(const FPhysScene&) = delete;
	FPhysScene& operator=(const FPhysScene&) = delete;

	bool IsValid() const { return Scene != nullptr; }
	physx::PxScene* GetPxScene() const { return Scene; }
	UWorld* GetOwningWorld() const { return OwningWorld; }

	// 고정 간격으로 simulate → fetchResults를 반복하고, 움직인 바디를 컴포넌트에 반영한다.
	void Tick(float DeltaTime);

private:
	void SyncActiveActors();

	static constexpr float FixedTimeStep = 1.0f / 60.0f;
	static constexpr int32 MaxSubSteps = 4;        // 한 프레임에 이보다 많이 밀리면 남은 시간은 버린다

	UWorld* OwningWorld = nullptr;
	physx::PxScene* Scene = nullptr;
	float TimeAccumulator = 0.0f;
};