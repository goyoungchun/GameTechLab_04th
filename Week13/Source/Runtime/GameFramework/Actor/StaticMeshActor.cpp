#include "EnginePCH.h"
#include "StaticMeshActor.h"

#include "Components/PrimitiveComponent.h"
#include "Asset/AssetManager.h"

AStaticMeshActor::AStaticMeshActor()
{
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("UStaticMeshComponent");
	StaticMeshComponent->SetupAttachment(GetRootComponent());
	// 레벨에 놓는 메시는 대부분 배경이라 Static으로 시작한다 (삼각형 충돌, 낮은 비용).
	// Simulate Physics를 켜면 UPrimitiveComponent가 Movable로 바꾼다.
	StaticMeshComponent->BodyInstance.Mobility = EBodyMobility::Static;
}

void AStaticMeshActor::SetPrimitiveType(EPrimitiveType Type)
{

}


void AStaticMeshActor::BeginPlay()
{
	Super::BeginPlay();
}
