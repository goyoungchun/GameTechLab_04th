#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"

class UPhysicsAsset;
class USkeletalMesh;

enum class EPhysAssetFitVertWeight : uint8
{
	AnyWeight,
	DominantWeight,
};

struct FPhysAssetCreateParams
{
	float MinBoneSizeRatio = 0.05f;

	int32 MinVertsPreBody = 4;

	EPhysAssetFitVertWeight VertWeight = EPhysAssetFitVertWeight::DominantWeight;

	// 캡슐 부피가 전체 바디 부피 중앙값의 이 비율보다 작으면 부모 본에 합친다 (0이면 끔).
	// 몇 그램짜리 바디(손가락 마디, 이, 보조 본)가 큰 바디에 조인트로 붙으면 질량비 때문에 랙돌이 떤다
	float MinBodyVolumeRatio = 0.1f;
};

namespace FPhysicsAssetUtils
{
	bool CreateFromSkeletalMesh(UPhysicsAsset& Asset, const USkeletalMesh& Mesh,
		const FPhysAssetCreateParams& Params, FString& OutError);
}