#pragma once

// 엔진 수학 타입 <-> PhysX 타입 변환.

#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Physics/PhysXIncludes.h"

// FVector <-> PxVec3
inline physx::PxVec3 U2PVector(const FVector& V)
{
	return physx::PxVec3(V.X, V.Y, V.Z);
}

inline FVector P2UVector(const physx::PxVec3& V)
{
	return FVector(V.x, V.y, V.z);
}

// FQuat <-> PxQuat
// PhysX는 단위 쿼터니언이 아니면 checked/debug 빌드에서 에러를 낸다. 넘기기 전에 정규화한다.
inline physx::PxQuat U2PQuat(const FQuat& Q)
{
	const FQuat N = Q.Normalized();
	return physx::PxQuat(N.X, N.Y, N.Z, N.W);
}

inline FQuat P2UQuat(const physx::PxQuat& Q)
{
	return FQuat(Q.x, Q.y, Q.z, Q.w);
}

// FTransform <-> PxTransform
// PxTransform에는 스케일이 없다. 스케일은 shape 크기(반지름, 반높이, extent)에 곱해서 반영한다 (A4/A5).
inline physx::PxTransform U2PTransform(const FTransform& T)
{
	return physx::PxTransform(U2PVector(T.Location), U2PQuat(T.Rotation));
}

// 물리 결과를 컴포넌트에 되돌릴 때는 원래 스케일을 유지해야 하므로 스케일을 따로 받는다.
inline FTransform P2UTransform(const physx::PxTransform& T, const FVector& Scale = FVector(1.0f, 1.0f, 1.0f))
{
	return FTransform(P2UQuat(T.q), P2UVector(T.p), Scale);
}