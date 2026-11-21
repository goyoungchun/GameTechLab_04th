#include "EnginePCH.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Physics/BodySetup.h"
#include <algorithm>
#include <cmath>
#include <limits>

UStaticMesh::~UStaticMesh()
{
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
        Proxy->OnMeshDestroyed();
	VertexBuffer = nullptr;
	IndexBuffer = nullptr;
}

UMaterial* UStaticMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex]) {
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

size_t UStaticMesh::FMaterialSlotsHash::operator()(const std::vector<UMaterial*>& Slots) const
{
    size_t Hash = 0;
    for (const UMaterial* Material : Slots)
        Hash ^= std::hash<const UMaterial*>{}(Material) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
    return Hash;
}

void UStaticMesh::RebuildRenderData()
{
    RenderBounds = MeshData.AABB;
    RenderStates.clear();
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
        Proxy->OnMeshRenderDataChanged();
    // 바운드가 바뀌었을 수 있으므로 충돌 박스도 다음 GetBodySetup에서 새로 만든다.
    InvalidateBodySetup();
}

UBodySetup* UStaticMesh::GetBodySetup()
{
    if (BodySetup)
        return BodySetup;

    const FBox& Bounds = MeshData.AABB;
    if (Bounds.Max.X < Bounds.Min.X)
        return nullptr;   // 비어 있는 메시

    BodySetup = NewObject<UBodySetup>(this);
    FKBoxElem& Box = BodySetup->AggGeom.BoxElems.Emplace_GetRef();
    Box.Center = (Bounds.Min + Bounds.Max) * 0.5f;
    Box.Extent = (Bounds.Max - Bounds.Min) * 0.5f;

    // 평면 메시는 한 축 두께가 0이다. PhysX 박스는 반 크기가 0이면 만들어지지 않으므로 최소 두께를 준다.
    constexpr float MinHalfExtent = 0.01f;   // 1cm
    Box.Extent = FVector(std::max(Box.Extent.X, MinHalfExtent), std::max(Box.Extent.Y, MinHalfExtent), std::max(Box.Extent.Z, MinHalfExtent));
    return BodySetup;
}

TSharedPtr<const FMeshRenderState> UStaticMesh::GetRenderState(const std::vector<UMaterial*>& Materials)
{
    auto& WeakState = RenderStates[Materials];
    if (auto State = WeakState.lock()) return State;
    auto State = MakeShared<FMeshRenderState>();
    for (const FStaticMeshSection& Section : MeshData.Sections)
    {
        UMaterial* Material = Section.MaterialSlotIndex < Materials.size()
            ? Materials[Section.MaterialSlotIndex] : GetMaterial(Section.MaterialSlotIndex);
        if (Material) State->Sections.Add({ Material, Section.StartIndex, Section.IndexCount });
    }
    WeakState = State;
    return State;
}

void UStaticMesh::RegisterProxy(FPrimitiveSceneProxy* Proxy)
{
    RenderProxies.Add(Proxy);
}

void UStaticMesh::UnregisterProxy(FPrimitiveSceneProxy* Proxy)
{
    const auto It = std::find(RenderProxies.begin(), RenderProxies.end(), Proxy);
    if (It != RenderProxies.end())
        RenderProxies.RemoveAtSwap(static_cast<uint32>(It - RenderProxies.begin()));
}
