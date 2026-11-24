#pragma once

#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "ClothMirrorActor.generated.h"

// 씬 내 플레이어 캐릭터의 현재 애니메이션 포즈(및 선택적 이동/회전)를 실시간 미러링해
// Cloth 시뮬레이션을 비교·확인하기 위한 테스트용 액터 (에디터 Add Actor 메뉴에는 미노출).
UCLASS()
class AClothMirrorActor : public AActor
{
	GENERATED_BODY()

public:
	AClothMirrorActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }

	// true면 플레이어 옆을 따라다니며 이동 관성까지 함께 비교, false면 제자리에서 포즈만 실시간 미러링
	UPROPERTY(EditAnywhere)
	bool bMirrorLocation = false;

	// true면 플레이어가 바라보는 회전을 그대로 따라감
	UPROPERTY(EditAnywhere)
	bool bMirrorRotation = true;

	// bMirrorLocation이 true일 때 타깃 캐릭터 기준 월드 오프셋 (m)
	UPROPERTY(EditAnywhere)
	FVector LocationOffset = FVector(0.0f, 1.5f, 0.0f);

private:
	void ResolveTargetComponent();
	void RebuildBoneMapping();

	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
	USkeletalMeshComponent* TargetMeshComponent = nullptr;
	TArray<int32> TargetBoneToLocalBone;
	bool bBoneMappingBuilt = false;
};
