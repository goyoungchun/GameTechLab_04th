#include "EnginePCH.h"
#include "ClothMeshObject.h"
#include "Clothing/ClothingTypes.h"
#include "Render/RenderCommand.h"

#include <cfloat>
#include <cstring>

FClothMeshObject::FClothMeshObject(TArray<FVertex>&& InVertices, TArray<uint32>&& InIndices)
	: Vertices(std::move(InVertices))
	, Indices(std::move(InIndices))
{
	if (Vertices.IsEmpty() || Indices.IsEmpty())
		return;

	RecalculateNormalsAndBounds();
	Bounds = SimulatedBounds;

	const uint32 VBSize = static_cast<uint32>(sizeof(FVertex) * Vertices.Num());
	DynamicVB = RenderCommand::CreateDynamicVertexBuffer(VBSize, sizeof(FVertex));
	StaticIB = RenderCommand::CreateStaticIndexBuffer(Indices.GetData(), static_cast<uint32>(Indices.Num()));
	if (DynamicVB && StaticIB)
	{
		VertexFactory.Init(DynamicVB.get(), StaticIB.get());
		UploadVertices();
	}
}

void FClothMeshObject::Update(const FClothingSimulationData& SimData)
{
	const int32 Count = FMath::Min(Vertices.Num(), SimData.Positions.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		Vertices[i].Position = SimData.Positions[i];
	}

	RecalculateNormalsAndBounds();
	UploadVertices();
}

bool FClothMeshObject::UpdateBounds()
{
	bool bInside = true;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (SimulatedBounds.Min[Axis] < Bounds.Min[Axis] || SimulatedBounds.Max[Axis] > Bounds.Max[Axis])
			bInside = false;
	}
	if (bInside)
		return false;

	const FVector Margin = (SimulatedBounds.Max - SimulatedBounds.Min) * 0.25f + FVector(0.1f, 0.1f, 0.1f);
	Bounds.Min = SimulatedBounds.Min - Margin;
	Bounds.Max = SimulatedBounds.Max + Margin;
	return true;
}

void FClothMeshObject::RecalculateNormalsAndBounds()
{
	const int32 NumVerts = Vertices.Num();
	if (NumVerts == 0)
		return;

	FBox NewBounds{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
	for (int32 i = 0; i < NumVerts; ++i)
	{
		Vertices[i].Normal = FVector::ZeroVector;
		const FVector& P = Vertices[i].Position;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			NewBounds.Min[Axis] = FMath::Min(NewBounds.Min[Axis], P[Axis]);
			NewBounds.Max[Axis] = FMath::Max(NewBounds.Max[Axis], P[Axis]);
		}
	}
	SimulatedBounds = NewBounds;

	// 삼각형 면 법선을 각 정점에 면적 가중 누적한다.
	const int32 NumIndices = Indices.Num();
	for (int32 i = 0; i + 2 < NumIndices; i += 3)
	{
		const uint32 I0 = Indices[i];
		const uint32 I1 = Indices[i + 1];
		const uint32 I2 = Indices[i + 2];
		if (I0 >= static_cast<uint32>(NumVerts) || I1 >= static_cast<uint32>(NumVerts) || I2 >= static_cast<uint32>(NumVerts))
			continue;

		const FVector E1 = Vertices[I1].Position - Vertices[I0].Position;
		const FVector E2 = Vertices[I2].Position - Vertices[I0].Position;
		const FVector FaceNormal = FVector::Cross(E1, E2);

		Vertices[I0].Normal += FaceNormal;
		Vertices[I1].Normal += FaceNormal;
		Vertices[I2].Normal += FaceNormal;
	}

	for (int32 i = 0; i < NumVerts; ++i)
	{
		const FVector N = Vertices[i].Normal.GetSafeNormal(SMALL_NUMBER, FVector(1.0f, 0.0f, 0.0f));
		Vertices[i].Normal = N;

		// 노멀맵 대응을 위해 탄젠트를 새 노멀에 직교화한다.
		const FVector T(Vertices[i].Tangent.X, Vertices[i].Tangent.Y, Vertices[i].Tangent.Z);
		const FVector OrthoT = (T - N * T.Dot(N)).GetSafeNormal(SMALL_NUMBER, FVector(0.0f, 1.0f, 0.0f));
		Vertices[i].Tangent = FVector4(OrthoT.X, OrthoT.Y, OrthoT.Z, Vertices[i].Tangent.W);
	}
}

void FClothMeshObject::UploadVertices()
{
	if (!DynamicVB || Vertices.IsEmpty())
		return;

	void* Dst = RenderCommand::MapWriteDiscard(DynamicVB.get());
	if (!Dst)
		return;

	std::memcpy(Dst, Vertices.GetData(), sizeof(FVertex) * Vertices.Num());
	RenderCommand::Unmap(DynamicVB.get());
}
