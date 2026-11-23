#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/Box.h"
#include "Render/Vertex.h"
#include "Render/Buffer.h"
#include "Render/VertexFactory.h"

struct FClothingSimulationData;

// Cloth 인스턴스 1개의 동적 GPU 버퍼·정점 배열·바운드를 전담하는 렌더 객체 (FSkeletalMeshObject와 동일 역할)
class FClothMeshObject
{
public:
	FClothMeshObject(TArray<FVertex>&& InVertices, TArray<uint32>&& InIndices);
	~FClothMeshObject() = default;

	FClothMeshObject(const FClothMeshObject&) = delete;
	FClothMeshObject& operator=(const FClothMeshObject&) = delete;

	// 시뮬레이션 결과를 반영해 노멀·탄젠트·바운드를 재계산하고 DynamicVB를 갱신한다.
	void Update(const FClothingSimulationData& SimData);

	// 현재 시뮬 바운드가 캐시된 Bounds 밖으로 나가면 여유를 두고 넓히고 true를 돌려준다.
	bool UpdateBounds();

	const FVertexFactory* GetVertexFactory() const { return &VertexFactory; }
	uint32 GetIndexCount() const { return static_cast<uint32>(Indices.Num()); }
	const FBox& GetBounds() const { return Bounds; }
	const TArray<FVertex>& GetVertices() const { return Vertices; }
	const TArray<uint32>& GetIndices() const { return Indices; }
	bool IsValid() const { return VertexFactory.IsValid() && !Indices.IsEmpty(); }

private:
	void RecalculateNormalsAndBounds();
	void UploadVertices();

	TArray<FVertex> Vertices;
	TArray<uint32> Indices;
	TUniquePtr<FVertexBuffer> DynamicVB;
	TUniquePtr<FIndexBuffer> StaticIB;
	FLocalVertexFactory VertexFactory;
	FBox Bounds;
	FBox SimulatedBounds;
};
