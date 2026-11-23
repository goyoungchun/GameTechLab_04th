#include "EnginePCH.h"
#include "BodySetup.h"
#include "PhysicsCore.h"
#include "PhysXConversions.h"
#include "PhysXIncludes.h"
#include "Serialization/MathSerialization.h"

namespace
{
	void SerializeQuat(FStructuredArchive::FSlot Slot, FQuat& Q)
	{
		FStructuredArchive::FRecord R = Slot.EnterRecord();
		R << SA_VALUE("X", Q.X) << SA_VALUE("Y", Q.Y) << SA_VALUE("Z", Q.Z) << SA_VALUE("W", Q.W);
	}

	void SerializeShapeBase(FStructuredArchive::FRecord R, FKShapeElem& E)
	{
		R << SA_VALUE("Center", E.Center);
		SerializeQuat(R.EnterField("Rotation"), E.Rotation);
	}

	void SerializeElem(FStructuredArchive::FRecord R, FKSphereElem& E)
	{
		SerializeShapeBase(R, E);
		R << SA_VALUE("Radius", E.Radius);
	}

	void SerializeElem(FStructuredArchive::FRecord R, FKBoxElem& E)
	{
		SerializeShapeBase(R, E);
		R << SA_VALUE("Extent", E.Extent);
	}

	void SerializeElem(FStructuredArchive::FRecord R, FKSphylElem& E)
	{
		SerializeShapeBase(R, E);
		R << SA_VALUE("Radius", E.Radius) << SA_VALUE("Length", E.Length);
	}

	void SerializeElem(FStructuredArchive::FRecord R, FKConvexElem& E)
	{
		SerializeShapeBase(R, E);
		int32 Num = E.VertexData.Num();
		FStructuredArchive::FArray Verts = R.EnterArray("Vertices", Num);
		if (R.GetUnderlyingArchive().IsLoading())
		{
			E.VertexData.SetNum(Num);
		}

		for (int32 i = 0; i < Num; ++i)
		{
			Verts.EnterElement() << E.VertexData[i];
		}
	}

	template<typename ElemT>
	void SerializeElemArray(FStructuredArchive::FRecord Parent, const char* Name, TArray<ElemT>& Elems)
	{
		const bool bLoading = Parent.GetUnderlyingArchive().IsLoading();
		int32 Num = bLoading ? 0 : Elems.Num();
		FStructuredArchive::FArray Arr = Parent.EnterArray(Name, Num);
		if (bLoading)
		{
			Elems.SetNum(Num);
		}

		for (int32 i = 0; i < Num; ++i)
		{
			SerializeElem(Arr.EnterElement().EnterRecord(), Elems[i]);
		}
	}
}

UBodySetup::~UBodySetup()
{
	// UObject는 PhysX SDK 정리(FPhysicsCore::Shutdown)보다 먼저 지워지므로 여기서 release해도 된다.
	ClearPhysicsMeshes();
}

void UBodySetup::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FStructuredArchive::FRecord Geom = Record.EnterRecord("AggGeom");

	SerializeElemArray(Geom, "Sphere", AggGeom.SphereElems);
	SerializeElemArray(Geom, "Box", AggGeom.BoxElems);
	SerializeElemArray(Geom, "Spyls", AggGeom.SphylElems);
	SerializeElemArray(Geom, "Convexes", AggGeom.ConvexElems);

	if (Record.GetUnderlyingArchive().IsLoading())
	{
		ClearPhysicsMeshes();
	}
}

bool UBodySetup::CreatePhysicsMeshes()
{
	if (CookedConvexMeshes.Num() == AggGeom.ConvexElems.Num())
		return true;   // 이미 만들었다

	ClearPhysicsMeshes();

	physx::PxCooking* Cooking = FPhysicsCore::Get().GetCooking();
	physx::PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
	if (!Cooking || !Physics)
		return false;

	bool bAllSucceeded = true;
	for (const FKConvexElem& Elem : AggGeom.ConvexElems)
	{
		physx::PxConvexMeshDesc Desc;
		Desc.points.count = static_cast<physx::PxU32>(Elem.VertexData.Num());
		Desc.points.stride = sizeof(FVector);
		Desc.points.data = Elem.VertexData.GetData();
		// 점 목록에서 볼록 껍질을 직접 계산한다. 결과 정점은 vertexLimit(기본 255)개 이하로 줄인다.
		Desc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX | physx::PxConvexFlag::eSHIFT_VERTICES;

		// 스트림으로 저장하지 않고 바로 PhysX 객체로 만든다
		physx::PxConvexMesh* Mesh = Cooking->createConvexMesh(Desc, Physics->getPhysicsInsertionCallback());
		if (!Mesh)
		{
			// 점이 4개 미만이거나 한 평면 위에 있으면 부피가 없어 실패한다.
			KRS_LOG(Warning, "[Physics] Convex cooking failed ({} points).", Elem.VertexData.Num());
			bAllSucceeded = false;
		}
		CookedConvexMeshes.Add(Mesh);   // 실패해도 nullptr로 자리를 채워 인덱스를 맞춘다
	}
	return bAllSucceeded;
}

void UBodySetup::ClearPhysicsMeshes()
{
	for (physx::PxConvexMesh* Mesh : CookedConvexMeshes)
	{
		if (Mesh)
			Mesh->release();   // 이 메시를 쓰는 shape가 남아 있으면 PhysX가 참조 수로 살려 둔다
	}
	CookedConvexMeshes.Empty();

	if (CookedTriMesh)
	{
		CookedTriMesh->release();
		CookedTriMesh = nullptr;
	}
	bTriMeshCookAttempted = false;
}

physx::PxTriangleMesh* UBodySetup::GetOrCreateTriMesh()
{
	if (bTriMeshCookAttempted)
		return CookedTriMesh;
	bTriMeshCookAttempted = true;

	physx::PxCooking* Cooking = FPhysicsCore::Get().GetCooking();
	physx::PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
	if (!Cooking || !Physics || TriMeshVertices.IsEmpty() || TriMeshIndices.Num() < 3)
		return nullptr;

	physx::PxTriangleMeshDesc Desc;
	Desc.points.count = static_cast<physx::PxU32>(TriMeshVertices.Num());
	Desc.points.stride = sizeof(FVector);
	Desc.points.data = TriMeshVertices.GetData();
	Desc.triangles.count = static_cast<physx::PxU32>(TriMeshIndices.Num() / 3);
	Desc.triangles.stride = sizeof(uint32) * 3;
	Desc.triangles.data = TriMeshIndices.GetData();
	// 감김 순서가 반대면 면 법선이 안쪽을 향해, 위에서 떨어진 물체를 아래로 밀어낸다.
	if (bFlipTriMeshNormals)
		Desc.flags |= physx::PxMeshFlag::eFLIPNORMALS;

	CookedTriMesh = Cooking->createTriangleMesh(Desc, Physics->getPhysicsInsertionCallback());
	if (!CookedTriMesh)
		KRS_LOG(Warning, "[Physics] TriangleMesh cooking failed ({} triangles).", Desc.triangles.count);
	return CookedTriMesh;
}

physx::PxConvexMesh* UBodySetup::GetConvexMesh(int32 Index) const
{
	return (Index >= 0 && Index < CookedConvexMeshes.Num()) ? CookedConvexMeshes[Index] : nullptr;
}