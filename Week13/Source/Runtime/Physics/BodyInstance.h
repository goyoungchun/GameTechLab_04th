#pragma once

#include "Core/Types.h"
#include "Math/Transform.h"
#include "PhysicsFiltering.h"
#include "PhysicalMaterial.h"   // UPROPERTY 객체 포인터는 리플렉션 등록 코드가 완전한 타입을 요구한다
#include "BodyInstance.generated.h"

namespace physx
{
	class PxRigidActor;
	class PxRigidDynamic;
}

class UBodySetup;
class UPrimitiveComponent;
class FPhysScene;

UENUM()
enum class EBodyMobility : uint8
{
	Static,     // PxRigidStatic. 움직이지 않는다 (바닥, 벽).
	Movable,    // PxRigidDynamic. 시뮬레이트하거나 Kinematic으로 끌고 다닌다.
};

// 컴포넌트(또는 본) 하나가 PhysX에 가진 바디. PxRigidActor의 userData가 이 객체를 가리킨다.
// 설정은 UPROPERTY라 디테일 창에서 편집되고 레벨에 저장된다. 실행 중 상태(포인터)는 저장·복제하지 않는다.
USTRUCT()
struct FBodyInstance
{
	GENERATED_BODY()

	// 설정 (InitBody 전에 채운다. 바꾸면 컴포넌트가 바디를 다시 만든다)
	UPROPERTY(EditAnywhere)
	EBodyMobility Mobility = EBodyMobility::Movable;
	UPROPERTY(EditAnywhere)
	bool bSimulatePhysics = false;     // false면 Movable은 Kinematic이 된다.
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.1))
	float MassInKgOverride = 0.0f;     // 0 이하면 형상 부피 × 밀도로 계산한다.
	UPROPERTY(EditAnywhere, meta=(ClampMin=1, Delta=10))
	float Density = 1000.0f;           // kg/m³. MassInKgOverride가 없을 때 쓴다.
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.01))
	float LinearDamping = 0.01f;
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.01))
	float AngularDamping = 0.05f;

	// 충돌 필터 (word0 카테고리, word3 플래그)
	UPROPERTY(EditAnywhere)
	ECollisionCategory ObjectType = ECollisionCategory::WorldDynamic;
	UPROPERTY(EditAnywhere)
	bool bDrivable = false;          // word3 Drivable. 자동차 바퀴가 올라갈 수 있는 표면

	UPROPERTY(EditAnywhere)
	bool bUseCCD = false;              // 연속 충돌 검사. 빠르게 움직이는 작은 바디가 통과하는 것을 막는다.

	uint32 CollidesWith = AllCategoriesMask;   // word1. 기본은 모든 종류와 충돌
	uint32 OwnerId = 0;                        // word2. 랙돌이 InitBody 전에 채운다

	// 표면 물성 에셋(.pmat). 없으면 FPhysicsCore의 기본 머티리얼과 이 바디의 Density를 쓴다.
	UPROPERTY(EditAnywhere)
	UPhysicalMaterial* PhysMaterialOverride = nullptr;

	FMaskFilter MakeMaskFilter() const;
	float GetEffectiveDensity() const;

	// 실행 중 상태
	UPrimitiveComponent* OwnerComponent = nullptr;
	FPhysScene* Scene = nullptr;
	physx::PxRigidActor* Actor = nullptr;
	FVector Scale3D = FVector(1, 1, 1);   // InitBody 때 형상에 곱한 스케일

	FBodyInstance() = default;
	~FBodyInstance();
	FBodyInstance(const FBodyInstance&) = delete;            // 복사되면 같은 액터를 두 번 해제한다
	FBodyInstance& operator=(const FBodyInstance&) = delete;

	// Setup의 형상으로 액터를 만들어 Scene에 넣는다. 이미 있으면 먼저 TermBody한다.
	bool InitBody(UBodySetup* Setup, const FTransform& WorldTransform, UPrimitiveComponent* PrimComp, FPhysScene* InScene);
	// 씬에서 빼고 액터를 해제한다. 여러 번 불러도 안전하다.
	void TermBody();

	bool IsValidBodyInstance() const { return Actor != nullptr; }
	bool IsDynamic() const;
	bool IsSimulatingPhysics() const;

	// Movable 바디만 의미가 있다. false로 끄면 Kinematic이 된다.
	void SetSimulatePhysics(bool bSimulate);
	void SetMassOverride(float MassInKg);
	void UpdateMassProperties();

	FTransform GetWorldTransform() const;          // 스케일은 Scale3D
	void SetWorldTransform(const FTransform& NewTransform, bool bTeleport);

	// PxRigidActor::userData에서 되찾는다. userData가 없거나 다른 것이면 nullptr. 
	static FBodyInstance* FromActor(const physx::PxRigidActor* InActor);
};