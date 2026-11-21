#pragma once

#include "UObject/Object.h"
#include "Math/Vector.h"
#include "Math/Quat.h"
#include "BodySetup.generated.h"

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

// 바디 하나를 이루는 형상 묶음. Convex는 Cooking이 필요해 이후에 추가한다.
struct FKAggregateGeom
{
	TArray<FKSphereElem> SphereElems;
	TArray<FKBoxElem> BoxElems;
	TArray<FKSphylElem> SphylElems;

	int32 GetElementCount() const { return SphereElems.Num() + BoxElems.Num() + SphylElems.Num(); }
};

// 바디의 형상 정의. StaticMesh는 하나, PhysicsAsset은 본마다 하나 갖는다.
UCLASS()
class UBodySetup : public UObject
{
	GENERATED_BODY()
public:
	FName BoneName;            // PhysicsAsset에서 이 바디가 붙는 본. StaticMesh에서는 비워 둔다.
	FKAggregateGeom AggGeom;
};