#pragma once

#include "UObject/Object.h"
#include "Math/Vector.h"
#include "Math/Quat.h"
#include "BodySetupCore.h"
#include "BodySetup.generated.h"

namespace physx 
{ 
	class PxConvexMesh;
	class PxTriangleMesh;
}

// 충돌 형상 하나. Center/Rotation은 바디(컴포넌트 또는 본) 로컬 공간 기준이다.
struct FKShapeElem
{
	FVector Center = FVector(0, 0, 0);
	FQuat Rotation = FQuat::Identity();
};

struct FKSphereElem : FKShapeElem
{
	float Radius = 0.5f;
};

struct FKBoxElem : FKShapeElem
{
	FVector Extent = FVector(0.5f, 0.5f, 0.5f);   // 반 크기
};

// 캡슐. 로컬 Z축 방향으로 눕지 않고 선다. Length는 양 끝 반구를 뺀 원통 길이.
struct FKSphylElem : FKShapeElem
{
	float Radius = 0.25f;
	float Length = 1.0f;
};

struct FKConvexElem : FKShapeElem
{
	TArray<FVector> VertexData;
};

struct FKAggregateGeom
{
	TArray<FKSphereElem> SphereElems;
	TArray<FKBoxElem> BoxElems;
	TArray<FKSphylElem> SphylElems;
	TArray<FKConvexElem> ConvexElems;

	int32 GetElementCount() const { return SphereElems.Num() + BoxElems.Num() + SphylElems.Num() + ConvexElems.Num(); }
};

// 바디의 형상 정의. StaticMesh는 하나, PhysicsAsset은 본마다 하나 갖는다.
UCLASS()
class UBodySetup : public UBodySetupCore
{
	GENERATED_BODY()
public:
	virtual ~UBodySetup() override;   // Cooking한 PxConvexMesh·PxTriangleMesh를 해제한다

	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	FKAggregateGeom AggGeom;

	// ConvexElems를 Cooking해 PxConvexMesh로 만든다. 이미 있으면 그대로 둔다. 하나라도 실패하면 false.
	bool CreatePhysicsMeshes();
	// Cooking 결과를 버린다. VertexData를 바꿨으면 부른 뒤 다시 CreatePhysicsMeshes한다.
	void ClearPhysicsMeshes();
	// ConvexElems[Index]의 Cooking 결과. 실패했거나 아직 안 만들었으면 nullptr.
	physx::PxConvexMesh* GetConvexMesh(int32 Index) const;

	// 삼각형 그대로의 충돌(Complex). Static 바디만 쓴다. PhysX는 동적 시뮬레이션에 TriangleMesh를 허용하지 않는다.
	TArray<FVector> TriMeshVertices;
	TArray<uint32> TriMeshIndices;          // 3개씩 삼각형
	bool bFlipTriMeshNormals = false;       // 엔진 감김 순서가 PhysX와 반대면 true (메시를 넣는 쪽이 판정한다)

	// 처음 불릴 때 Cooking한다. 데이터가 없거나 실패하면 nullptr.
	physx::PxTriangleMesh* GetOrCreateTriMesh();

private:
	// ConvexElems와 인덱스가 같다. 같은 BodySetup을 쓰는 모든 바디가 이 메시를 공유한다 (PhysX 참조 수).
	TArray<physx::PxConvexMesh*> CookedConvexMeshes;

	physx::PxTriangleMesh* CookedTriMesh = nullptr;
	bool bTriMeshCookAttempted = false;     // 실패한 메시를 매번 다시 Cooking하지 않는다
};