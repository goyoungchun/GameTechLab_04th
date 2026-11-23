#include "EnginePCH.h"
#include "ClothComponent.h"
#include "Asset/AssetManager.h"
#include "Clothing/ClothCore.h"
#include "Engine/ClothSceneProxy.h"
#include "Render/ClothMeshObject.h"

UClothComponent::UClothComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UClothComponent::~UClothComponent()
{
	Simulation.Shutdown();
}

void UClothComponent::OnRegister()
{
	Super::OnRegister();
	EnsureClothInitialized();
}

void UClothComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureClothInitialized();
}

void UClothComponent::EnsureClothInitialized()
{
	if (MeshObject && Simulation.IsValid())
		return;

	if (!FClothCore::Get().IsInitialized())
		return;

	// 1차 테스트용 하드코딩 격자 천 (16x16, 가로 2m x 세로 2m, 윗줄 핀 고정)
	constexpr int32 Rows = 16;
	constexpr int32 Cols = 16;
	constexpr float Width = 2.0f;
	constexpr float Height = 2.0f;

	TArray<FVertex> Vertices;
	TArray<uint32> Indices;
	FClothPhysicalMeshData PhysData;

	const int32 NumVerts = (Rows + 1) * (Cols + 1);
	Vertices.Reserve(NumVerts);
	PhysData.Vertices.Reserve(NumVerts);
	PhysData.InverseMasses.Reserve(NumVerts);

	for (int32 Row = 0; Row <= Rows; ++Row)
	{
		const float V = static_cast<float>(Row) / static_cast<float>(Rows);
		for (int32 Col = 0; Col <= Cols; ++Col)
		{
			const float U = static_cast<float>(Col) / static_cast<float>(Cols);
			const FVector Pos(0.0f, (U - 0.5f) * Width, (1.0f - V) * Height);

			FVertex Vertex;
			Vertex.Position = Pos;
			Vertex.Normal = FVector(1.0f, 0.0f, 0.0f);
			Vertex.Tangent = FVector4(0.0f, 1.0f, 0.0f, 1.0f);
			Vertex.UV = FVector2(U, V);
			Vertex.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

			Vertices.Add(Vertex);
			PhysData.Vertices.Add(Pos);
			// 맨 윗줄(Row == 0)만 고정(invMass = 0), 나머지는 자유 시뮬레이션(invMass = 1)
			PhysData.InverseMasses.Add(Row == 0 ? 0.0f : 1.0f);
		}
	}

	Indices.Reserve(Rows * Cols * 6);
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Col = 0; Col < Cols; ++Col)
		{
			const uint32 I00 = static_cast<uint32>(Row * (Cols + 1) + Col);
			const uint32 I01 = I00 + 1;
			const uint32 I10 = I00 + static_cast<uint32>(Cols + 1);
			const uint32 I11 = I10 + 1;

			Indices.Add(I00);
			Indices.Add(I10);
			Indices.Add(I11);

			Indices.Add(I00);
			Indices.Add(I11);
			Indices.Add(I01);
		}
	}
	PhysData.Indices = Indices;

	if (Simulation.CreateCloth(PhysData))
	{
		Simulation.SetGravity(Gravity);
		Simulation.SetWindVelocity(WindVelocity, DragCoefficient, LiftCoefficient);
	}

	MeshObject = MakeUnique<FClothMeshObject>(std::move(Vertices), std::move(Indices));
	OnTransformDirty();
}

void UClothComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	if (!MeshObject || !Simulation.IsValid())
		return;

	Simulation.SetGravity(Gravity);
	Simulation.SetWindVelocity(WindVelocity, DragCoefficient, LiftCoefficient);
	Simulation.Simulate(DeltaTime);
	Simulation.GetSimulationData(SimData);

	MeshObject->Update(SimData);
	if (MeshObject->UpdateBounds())
		OnTransformDirty();
}

FPrimitiveSceneProxy* UClothComponent::CreateSceneProxy()
{
	EnsureClothInitialized();
	return new FClothSceneProxy(this);
}

FBox UClothComponent::CalcLocalBounds() const
{
	return MeshObject ? MeshObject->GetBounds() : Super::CalcLocalBounds();
}

UMaterial* UClothComponent::GetDefaultMaterial(int32 /*SlotIndex*/) const
{
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}
