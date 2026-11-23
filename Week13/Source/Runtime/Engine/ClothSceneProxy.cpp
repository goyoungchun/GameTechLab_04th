#include "EnginePCH.h"
#include "ClothSceneProxy.h"
#include "Components/ClothComponent.h"
#include "Render/ClothMeshObject.h"

FClothSceneProxy::FClothSceneProxy(UClothComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
}

void FClothSceneProxy::UpdateRenderState()
{
	UpdateCommonFlags();
}

void FClothSceneProxy::GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View)
{
	UClothComponent* ClothComp = static_cast<UClothComponent*>(GetComponent());
	FClothMeshObject* MeshObject = ClothComp->GetMeshObject();
	if (!MeshObject || !MeshObject->IsValid())
		return;

	const FVertexFactory* VF = MeshObject->GetVertexFactory();
	UMaterial* Mat = ClothComp->GetMaterial(0);
	if (!VF || !VF->IsValid() || !Mat)
		return;

	const FVector Delta = GetBounds().Center - View.CameraPosition;

	FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
	Packet.Proxy = this;
	Packet.VertexFactory = VF;
	Packet.Material = Mat;
	Packet.StartIndex = 0;
	Packet.IndexCount = MeshObject->GetIndexCount();
	Packet.CameraToParticleDistance = Delta.Dot(Delta);
}
