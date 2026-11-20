#pragma once

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

private:
	UWorld* OwningWorld = nullptr;
	physx::PxScene* Scene = nullptr;
};