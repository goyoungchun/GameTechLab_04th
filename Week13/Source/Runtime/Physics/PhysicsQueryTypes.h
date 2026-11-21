#pragma once

#include "Core/Types.h"
#include "Math/Vector.h"
#include "Collision/HitResult.h"
#include "PhysicsFiltering.h"

class UPrimitiveComponent;

// PxSweepHit 대응. Raycast와 같은 정보(위치·법선·거리 + 시작 겹침)라 FHitResult를 그대로 쓴다.
using FSweepResult = FHitResult;

// PxOverlapHit 대응. 위치·법선이 없고 "무엇과 겹쳤나"만 담는다.
struct FOverlapResult
{
	UPrimitiveComponent* Component = nullptr;
	int32 FaceIndex = -1;
};

// Sweep·Overlap에 쓰는 모양. 크기는 월드 단위. 캡슐은 Z축으로 선다.
struct FCollisionShape
{
	enum class EType : uint8 { Sphere, Box, Capsule };

	EType Type = EType::Sphere;
	FVector HalfExtent = FVector(0.5f, 0.5f, 0.5f);   // Box
	float Radius = 0.5f;                               // Sphere, Capsule
	float HalfHeight = 0.5f;                           // Capsule: 반구 끝까지 (CapsuleComponent와 같은 규약)

	static FCollisionShape MakeSphere(float InRadius) { FCollisionShape S; S.Type = EType::Sphere; S.Radius = InRadius; return S; }
	static FCollisionShape MakeBox(const FVector& InHalfExtent) { FCollisionShape S; S.Type = EType::Box; S.HalfExtent = InHalfExtent; return S; }
	static FCollisionShape MakeCapsule(float InRadius, float InHalfHeight) { FCollisionShape S; S.Type = EType::Capsule; S.Radius = InRadius; S.HalfHeight = InHalfHeight; return S; }
};

// 무엇을 맞힐지. 기본은 모든 종류.
struct FCollisionQueryParams
{
	uint32 ObjectTypesMask = AllCategoriesMask;   // 맞힐 ECollisionCategory (CategoryBit OR)
	uint32 RequiredFlags = 0;                     // shape의 word3에 모두 있어야 맞는다 (예: Drivable)
	const UPrimitiveComponent* IgnoreComponent = nullptr;   // 자기 자신(차체 등) 제외
};