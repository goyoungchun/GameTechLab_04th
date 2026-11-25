#include "EnginePCH.h"
#include "GameFramework/Actor/ClothMirrorActor.h"
#include "GameFramework/Character.h"
#include "Asset/AssetManager.h"
#include "Core/Misc/Paths.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectIterator.h"

AClothMirrorActor::AClothMirrorActor()
{
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	SkeletalMeshComponent->SetupAttachment(GetRootComponent());
	SkeletalMeshComponent->SetRelativeScale3D(FVector(0.01f, 0.01f, 0.01f));

	// 플레이어 애니메이션(PrePhysics) 이후 포즈 복사(PostPhysics) -> 이어서 스키닝/클로스 시뮬(PostUpdateWork)
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = ETickingGroup::PostPhysics;
	SkeletalMeshComponent->PrimaryComponentTick.TickGroup = ETickingGroup::PostUpdateWork;
}

void AClothMirrorActor::BeginPlay()
{
	Super::BeginPlay();

	if (SkeletalMeshComponent && !SkeletalMeshComponent->GetSkeletalMesh() && FPaths::HasProject())
	{
		if (USkeletalMesh* ClothMesh = UAssetManager::GetAssetByPath<USkeletalMesh>("FBX/Kurisu_Cloth/Kurisu_Cloth.fbx.bin"))
		{
			SkeletalMeshComponent->SetSkeletalMesh(ClothMesh);
		}
	}

	ResolveTargetComponent();
	RebuildBoneMapping();
}

void AClothMirrorActor::ResolveTargetComponent()
{
	TargetMeshComponent = nullptr;
	bBoneMappingBuilt = false;

	UWorld* MyWorld = GetWorld();
	if (!MyWorld)
		return;

	for (TObjectIterator<ACharacter> It; It; ++It)
	{
		ACharacter* Char = *It;
		if (!Char || Char->GetWorld() != MyWorld)
			continue;

		if (USkeletalMeshComponent* MeshComp = Char->SkeletalMeshComponent)
		{
			TargetMeshComponent = MeshComp;
			return;
		}
	}
}

void AClothMirrorActor::RebuildBoneMapping()
{
	TargetBoneToLocalBone.Empty();
	bBoneMappingBuilt = false;

	if (!TargetMeshComponent || !SkeletalMeshComponent)
		return;

	const USkeletalMesh* TargetMesh = TargetMeshComponent->GetSkeletalMesh();
	const USkeletalMesh* LocalMesh = SkeletalMeshComponent->GetSkeletalMesh();
	if (!TargetMesh || !LocalMesh)
		return;

	const TArray<FBoneInfo>& TargetBones = TargetMesh->GetRefSkeleton();
	TargetBoneToLocalBone.SetNum(TargetBones.Num());
	for (int32 i = 0; i < TargetBones.Num(); ++i)
	{
		TargetBoneToLocalBone[i] = LocalMesh->FindBoneIndex(TargetBones[i].Name);
	}
	bBoneMappingBuilt = true;
}

void AClothMirrorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!SkeletalMeshComponent)
		return;

	if (!TargetMeshComponent || !TargetMeshComponent->GetOwner() || TargetMeshComponent->GetWorld() != GetWorld())
	{
		ResolveTargetComponent();
		RebuildBoneMapping();
	}
	if (!TargetMeshComponent)
		return;

	if (!bBoneMappingBuilt)
	{
		RebuildBoneMapping();
	}

	// 1. 플레이어의 현재 프레임 본 포즈를 그대로 복사
	const TArray<FTransform>& TargetPose = TargetMeshComponent->GetBoneSpaceTransforms();
	const int32 Count = FMath::Min(TargetPose.Num(), TargetBoneToLocalBone.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 LocalIdx = TargetBoneToLocalBone[i];
		if (LocalIdx >= 0)
		{
			SkeletalMeshComponent->SetBoneLocalTransform(LocalIdx, TargetPose[i]);
		}
	}

	// 2. 선택적으로 위치/회전 미러링
	if (USceneComponent* MyRoot = GetRootComponent())
	{
		if (bMirrorLocation)
		{
			const FVector TargetFootWorld = TargetMeshComponent->GetWorldLocation();
			MyRoot->SetRelativeLocation(TargetFootWorld + LocationOffset);
		}
		if (bMirrorRotation)
		{
			MyRoot->SetRelativeRotation(TargetMeshComponent->GetWorldRotation());
		}
	}
}
