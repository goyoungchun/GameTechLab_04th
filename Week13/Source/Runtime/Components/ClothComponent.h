#pragma once

#include "MeshComponent.h"
#include "Clothing/ClothingSimulationNv.h"
#include "ClothComponent.generated.h"

class FClothMeshObject;

UCLASS()
class UClothComponent : public UMeshComponent
{
	GENERATED_BODY()
public:
	UClothComponent();
	~UClothComponent() override;

	void OnRegister() override;
	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FBox CalcLocalBounds() const override;

	int32 GetNumMaterials() const override { return 1; }
	UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	FClothMeshObject* GetMeshObject() const { return MeshObject.get(); }

	UPROPERTY()
	FVector Gravity = FVector(0.0f, 0.0f, -9.8f);

	UPROPERTY()
	FVector WindVelocity = FVector(3.0f, 0.5f, 0.0f);

	UPROPERTY()
	float DragCoefficient = 0.3f;

	UPROPERTY()
	float LiftCoefficient = 0.5f;

private:
	void EnsureClothInitialized();

	FClothingSimulationNv Simulation;
	FClothingSimulationData SimData;
	TUniquePtr<FClothMeshObject> MeshObject;
};
