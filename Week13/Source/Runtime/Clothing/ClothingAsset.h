#pragma once

#include "UObject/Object.h"
#include "Clothing/ClothingTypes.h"
#include "ClothingAsset.generated.h"

class USkeletalMesh;

struct FClothConfigNv
{
	FVector Gravity = FVector(0.0f, 0.0f, -9.8f);
	FVector WindVelocity = FVector::ZeroVector;
	float DragCoefficient = 0.25f;
	float LiftCoefficient = 0.35f;
	float Damping = 0.02f;

	float StretchStiffness = 1.0f;
	float BendStiffness = 0.25f;
	float ShearStiffness = 0.8f;

	float TetherStiffness = 1.0f;
	float TetherScale = 1.0f;
	float MaxDistanceScale = 1.0f;
	float SolverFrequency = 60.0f;
	float LinearInertiaScale = 0.5f;
	float AngularInertiaScale = 0.5f;
};

UCLASS()
class UClothingAssetCommon : public UObject
{
	GENERATED_BODY()
public:
	// 렌더 정점별 MaxDistance 배열로 시뮬 서브메시 + UV Weld + 렌더 매핑 생성
	bool BuildFromMaxDistances(const USkeletalMesh& Mesh, const TArray<float>& PerRenderVertMaxDist);

	// 본 선택 기반 자동 마스크 생성기
	static void GenerateMaxDistancesFromBones(
		const USkeletalMesh& Mesh,
		const TArray<int32>& RootBoneIndices,
		float MaxDistValue,
		TArray<float>& OutMaxDistances);

	const FClothPhysicalMeshData& GetPhysicalMeshData() const { return PhysicalMeshData; }
	const TArray<FClothVertMapping>& GetRenderToSimMapping() const { return RenderToSimMapping; }
	FClothConfigNv& GetConfig() { return ClothConfig; }
	const FClothConfigNv& GetConfig() const { return ClothConfig; }
	bool IsValid() const { return !PhysicalMeshData.Vertices.IsEmpty() && !PhysicalMeshData.Indices.IsEmpty(); }

private:
	FClothPhysicalMeshData PhysicalMeshData;
	TArray<FClothVertMapping> RenderToSimMapping;
	FClothConfigNv ClothConfig;
};