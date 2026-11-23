#include "EnginePCH.h"
#include "ClothingSimulationNv.h"
#include "ClothCore.h"
#include "NvClothIncludes.h"
#include <algorithm>

bool FClothingSimulationNv::CreateCloth(const FClothPhysicalMeshData& MeshData)
{
	Shutdown();

	// Factory는 FClothCore에서 관리한다.
	nv::cloth::Factory* Factory = FClothCore::Get().GetCpuFactory();
	if(!Factory)
	{
		return false;
	}

	// MeshData 검증
	const uint32 NumVertices = static_cast<uint32>(MeshData.Vertices.Num());
	const uint32 NumIndices = static_cast<uint32>(MeshData.Indices.Num());
	if (NumVertices == 0 || NumIndices < 3 || (NumIndices % 3 != 0) || MeshData.InverseMasses.Num() != MeshData.Vertices.Num())
	{
		return false;
	}

	// NvCloth ClothMeshDesc 설정
	nv::cloth::ClothMeshDesc Desc;
	Desc.setToDefault();
	Desc.points.data = MeshData.Vertices.GetData();
	Desc.points.stride = sizeof(FVector);
	Desc.points.count = NumVertices;
	Desc.invMasses.data = MeshData.InverseMasses.GetData();
	Desc.invMasses.stride = sizeof(float);
	Desc.invMasses.count = NumVertices;
	Desc.triangles.data = MeshData.Indices.GetData();
	Desc.triangles.stride = sizeof(uint32) * 3;
	Desc.triangles.count = NumIndices / 3;

	// Fabric 생성
	nv::cloth::Vector<int32_t>::Type PhaseTypes;
	Fabric = NvClothCookFabricFromMesh(Factory, Desc, physx::PxVec3(0.0f, 0.0f, -1.0f), &PhaseTypes, true);
	if (!Fabric)
	{
		return false;
	}

	TArray<physx::PxVec4> Particles;
	Particles.SetNum(NumVertices);
	for (uint32 i = 0; i < NumVertices; ++i)
	{
		const FVector& Position = MeshData.Vertices[i];
		Particles[i] = physx::PxVec4(Position.X, Position.Y, Position.Z, MeshData.InverseMasses[i]);
	}

	// Cloth 생성
	Cloth = Factory->createCloth(nv::cloth::Range<physx::PxVec4>(Particles.GetData(), Particles.GetData() + NumVertices), *Fabric);
	if (!Cloth)
	{
		Shutdown();
		return false;
	}

	// PhaseConfig 설정, 제약 조건 설정
	const uint32 NumPhases = Fabric->getNumPhases();
	TArray<nv::cloth::PhaseConfig> Phases;
	Phases.SetNum(NumPhases);
	for (uint32 i = 0; i < NumPhases; ++i)
	{
		Phases[i] = nv::cloth::PhaseConfig(static_cast<uint16_t>(i));
		Phases[i].mStiffness = 1.0f;
		Phases[i].mStiffnessMultiplier = 1.0f;
		Phases[i].mCompressionLimit = 1.0f;
		Phases[i].mStretchLimit = 1.0f;
	}
	Cloth->setPhaseConfig(nv::cloth::Range<nv::cloth::PhaseConfig>(Phases.GetData(), Phases.GetData() + NumPhases));

	// 기본 물리 파리미터 설정
	Cloth->setGravity(physx::PxVec3(0.0f, 0.0f, -9.8f));
	Cloth->setDamping(physx::PxVec3(0.05f, 0.05f, 0.05f));
	Cloth->setSolverFrequency(60.0f);
	Cloth->setTetherConstraintScale(1.0f);
	Cloth->setTetherConstraintStiffness(1.0f);

	// 솔버 생성 및 등록
	Solver = Factory->createSolver();
	if (!Solver)
	{
		Shutdown();
		return false;
	}
	Solver->addCloth(Cloth);
	return true;
}

void FClothingSimulationNv::Shutdown()
{
	if (Solver)
	{
		if (Cloth)
			Solver->removeCloth(Cloth);
		delete Solver;
		Solver = nullptr;
	}
	if (Cloth)
	{
		delete Cloth;
		Cloth = nullptr;
	}
	if (Fabric)
	{
		Fabric->decRefCount();
		Fabric = nullptr;
	}
}

void FClothingSimulationNv::Simulate(float DeltaTime)
{
	if (!Solver || !Cloth || DeltaTime <= 0.0f)
		return;

	const float ClampedDt = std::min(DeltaTime, 1.0f / 30.0f); // 최대 30fps로 제한
	if (Solver->beginSimulation(ClampedDt))
	{
		const int ChunkCount = Solver->getSimulationChunkCount();
		for (int i = 0; i < ChunkCount; ++i)
		{
			Solver->simulateChunk(i);
		}
		Solver->endSimulation();
	}
}

void FClothingSimulationNv::GetSimulationData(FClothingSimulationData& OutData) const
{
	if (!Cloth)
		return;
	const nv::cloth::Cloth* ConstCloth = Cloth;
	nv::cloth::MappedRange<const physx::PxVec4> Particles = ConstCloth->getCurrentParticles();
	const uint32 Count = Particles.size();

	OutData.Positions.SetNum(Count);
	for (uint32 i = 0; i < Count; ++i)
	{
		const physx::PxVec4& P = Particles[i];
		OutData.Positions[i] = FVector(P.x, P.y, P.z);
	}
}

void FClothingSimulationNv::SetGravity(const FVector& InGravity)
{
	if (Cloth)
	{
		Cloth->setGravity(physx::PxVec3(InGravity.X, InGravity.Y, InGravity.Z));
	}
}

void FClothingSimulationNv::SetWindVelocity(const FVector& InWind, float InDrag, float InLift)
{
	if (Cloth)
	{
		Cloth->setWindVelocity(physx::PxVec3(InWind.X, InWind.Y, InWind.Z));
		Cloth->setDragCoefficient(InDrag);
		Cloth->setLiftCoefficient(InLift);
	}
}