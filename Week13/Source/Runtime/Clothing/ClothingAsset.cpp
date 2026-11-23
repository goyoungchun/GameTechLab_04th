#include "EnginePCH.h"
#include "ClothingAsset.h"
#include "Render/SkeletalMesh.h"
#include "Core/EngineLog.h"

#include <cmath>
#include <unordered_map>

namespace
{
	struct FPosKey
	{
		int64 X = 0;
		int64 Y = 0;
		int64 Z = 0;

		bool operator==(const FPosKey& Other) const
		{
			return X == Other.X && Y == Other.Y && Z == Other.Z;
		}
	};

	struct FPosKeyHash
	{
		size_t operator()(const FPosKey& K) const noexcept
		{
			size_t H = std::hash<int64>{}(K.X);
			H ^= std::hash<int64>{}(K.Y) + 0x9e3779b9 + (H << 6) + (H >> 2);
			H ^= std::hash<int64>{}(K.Z) + 0x9e3779b9 + (H << 6) + (H >> 2);
			return H;
		}
	};

	// 0.01cm 단위로 양자화해 UV Seam 중복 정점을 같은 키로 묶는다.
	FPosKey MakePosKey(const FVector& P)
	{
		return {
			static_cast<int64>(std::llround(P.X * 100.0f)),
			static_cast<int64>(std::llround(P.Y * 100.0f)),
			static_cast<int64>(std::llround(P.Z * 100.0f))
		};
	}
}

bool UClothingAssetCommon::BuildFromMaxDistances(const USkeletalMesh& Mesh, const TArray<float>& PerRenderVertMaxDist)
{
	PhysicalMeshData = {};
	RenderToSimMapping.Empty();

	const auto& LODs = Mesh.GetRenderData().LODRenderData;
	if (LODs.IsEmpty())
		return false;

	const FSkeletalMeshLODRenderData& LOD = LODs[0];
	const int32 NumRenderVerts = LOD.BindVertices.Num();
	const int32 NumRenderIndices = LOD.Indices.Num();
	if (NumRenderVerts == 0 || NumRenderIndices < 3 || PerRenderVertMaxDist.Num() != NumRenderVerts)
		return false;

	// 렌더 정점 -> 시뮬 정점 매핑 초기화 (기본 BlendWeight = 0)
	RenderToSimMapping.SetNum(NumRenderVerts);

	std::unordered_map<FPosKey, uint32, FPosKeyHash> WeldMap;
	TArray<int32> RenderToWelded;
	RenderToWelded.SetNum(NumRenderVerts);
	for (int32 i = 0; i < NumRenderVerts; ++i)
		RenderToWelded[i] = -1;

	auto GetOrAddSimVertex = [&](uint32 RenderIdx) -> uint32
		{
			if (RenderToWelded[RenderIdx] >= 0)
				return static_cast<uint32>(RenderToWelded[RenderIdx]);

			const FVector& Pos = LOD.BindVertices[RenderIdx].Position;
			const float MaxDist = FMath::Max(0.0f, PerRenderVertMaxDist[RenderIdx]);
			const FPosKey Key = MakePosKey(Pos);

			auto It = WeldMap.find(Key);
			if (It != WeldMap.end())
			{
				const uint32 SimIdx = It->second;
				// 경계 정점이 찢어지지 않도록 더 작은 MaxDist로 보수적 병합
				PhysicalMeshData.MaxDistances[SimIdx] = FMath::Min(PhysicalMeshData.MaxDistances[SimIdx], MaxDist);
				PhysicalMeshData.InverseMasses[SimIdx] = (PhysicalMeshData.MaxDistances[SimIdx] > 0.0f) ? 1.0f : 0.0f;
				RenderToWelded[RenderIdx] = static_cast<int32>(SimIdx);
				return SimIdx;
			}

			const uint32 NewSimIdx = static_cast<uint32>(PhysicalMeshData.Vertices.Num());
			PhysicalMeshData.Vertices.Add(Pos);
			PhysicalMeshData.MaxDistances.Add(MaxDist);
			PhysicalMeshData.InverseMasses.Add(MaxDist > 0.0f ? 1.0f : 0.0f);
			PhysicalMeshData.SkinWeights.Add(LOD.SkinWeights[RenderIdx]);

			WeldMap.emplace(Key, NewSimIdx);
			RenderToWelded[RenderIdx] = static_cast<int32>(NewSimIdx);
			return NewSimIdx;
		};

	// 1. MaxDist > 0 정점이 하나라도 포함된 삼각형만 수집 + UV Weld
	for (int32 i = 0; i + 2 < NumRenderIndices; i += 3)
	{
		const uint32 R0 = LOD.Indices[i];
		const uint32 R1 = LOD.Indices[i + 1];
		const uint32 R2 = LOD.Indices[i + 2];

		if (PerRenderVertMaxDist[R0] <= 0.0f &&
			PerRenderVertMaxDist[R1] <= 0.0f &&
			PerRenderVertMaxDist[R2] <= 0.0f)
		{
			continue;
		}

		const uint32 S0 = GetOrAddSimVertex(R0);
		const uint32 S1 = GetOrAddSimVertex(R1);
		const uint32 S2 = GetOrAddSimVertex(R2);

		if (S0 == S1 || S1 == S2 || S2 == S0)
			continue;

		PhysicalMeshData.Indices.Add(S0);
		PhysicalMeshData.Indices.Add(S1);
		PhysicalMeshData.Indices.Add(S2);
	}

	// 2. RenderToSimMapping 채우기
	for (int32 v = 0; v < NumRenderVerts; ++v)
	{
		if (RenderToWelded[v] >= 0 && PerRenderVertMaxDist[v] > 0.0f)
		{
			RenderToSimMapping[v].SimIndex = static_cast<uint32>(RenderToWelded[v]);
			RenderToSimMapping[v].BlendWeight = 1.0f;
		}
	}

	KRS_LOG(Info, "[ClothAsset] Built sim mesh: {} verts, {} tris (from {} render verts)",
		PhysicalMeshData.Vertices.Num(),
		PhysicalMeshData.Indices.Num() / 3,
		NumRenderVerts);

	return IsValid();
}

void UClothingAssetCommon::GenerateMaxDistancesFromBones(
	const USkeletalMesh& Mesh,
	const TArray<int32>& RootBoneIndices,
	float MaxDistValue,
	TArray<float>& OutMaxDistances)
{
	const auto& LODs = Mesh.GetRenderData().LODRenderData;
	if (LODs.IsEmpty())
		return;

	const FSkeletalMeshLODRenderData& LOD = LODs[0];
	const TArray<FBoneInfo>& Bones = Mesh.GetRefSkeleton();
	const int32 NumBones = Bones.Num();
	const int32 NumVerts = LOD.BindVertices.Num();

	OutMaxDistances.SetNum(NumVerts);
	for (int32 v = 0; v < NumVerts; ++v)
		OutMaxDistances[v] = 0.0f;

	// 1. 각 본의 체인 깊이 계산
	TArray<int32> BoneDepth;
	BoneDepth.SetNum(NumBones);
	for (int32 b = 0; b < NumBones; ++b)
		BoneDepth[b] = -1;

	for (int32 RootIdx : RootBoneIndices)
	{
		if (RootIdx >= 0 && RootIdx < NumBones)
			BoneDepth[RootIdx] = 0;
	}

	int32 MaxDepth = 1;
	for (int32 b = 0; b < NumBones; ++b)
	{
		if (BoneDepth[b] >= 0)
			continue;

		const int32 Parent = Bones[b].ParentIndex;
		if (Parent >= 0 && BoneDepth[Parent] >= 0)
		{
			BoneDepth[b] = BoneDepth[Parent] + 1;
			MaxDepth = FMath::Max(MaxDepth, BoneDepth[b]);
		}
	}

	// 2. 정점별 가중 평균 깊이로 0 -> MaxDistValue 그라데이션 부여
	for (int32 v = 0; v < NumVerts; ++v)
	{
		const FSkinWeight& W = LOD.SkinWeights[v];
		float ClothWeight = 0.0f;
		float WeightedDepth = 0.0f;

		for (int32 k = 0; k < MaxBoneInfluences; ++k)
		{
			const float Weight = W.Weight[k];
			const int32 BoneIdx = static_cast<int32>(W.BoneIndex[k]);
			if (Weight > 0.0f && BoneIdx < NumBones && BoneDepth[BoneIdx] >= 0)
			{
				ClothWeight += Weight;
				WeightedDepth += Weight * static_cast<float>(BoneDepth[BoneIdx]);
			}
		}

		if (ClothWeight > 0.25f)
		{
			const float AvgDepth = WeightedDepth / ClothWeight;
			const float Ratio = FMath::Clamp(AvgDepth / static_cast<float>(MaxDepth), 0.0f, 1.0f);
			OutMaxDistances[v] = Ratio * MaxDistValue;
		}
	}
}