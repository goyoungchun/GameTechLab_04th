#pragma once

#include "UObject/Object.h"
#include "Core/NameTypes.h"
#include "BodySetup.h"
#include "Containers/Array.h"
#include "PhysicsAsset.generated.h"

UCLASS()
class UPhysicsAsset : public UObject
{
	GENERATED_BODY()

public:
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	TArray<UBodySetup*> BodySetup;

	int32 FindBodyIndex(FName BoneName) const;
	UBodySetup* FindBodySetup(FName BoneName) const;

	UBodySetup* AddBodySetup(FName BoneName);
	void RemoveBodySetup(FName BoneName);
	void ClearAllBodies();

	void SetAssetPath(const FString& Path) { AssetPath = Path; }
	const FString& GetAssetPath() const { return AssetPath; }

private:
	FString AssetPath;
};