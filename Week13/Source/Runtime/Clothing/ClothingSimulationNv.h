#pragma once

#include "Core/Types.h"
#include "Clothing/ClothingTypes.h"

namespace nv
{
	namespace cloth
	{
		class Fabric;
		class Cloth;
		class Solver;
	}
}

class FClothingSimulationNv
{
public:
	FClothingSimulationNv() = default;
	~FClothingSimulationNv() { Shutdown(); }

	FClothingSimulationNv(const FClothingSimulationNv&) = delete;
	FClothingSimulationNv& operator=(const FClothingSimulationNv&) = delete;

	bool CreateCloth(const FClothPhysicalMeshData& MeshData);
	void Shutdown();

	void Simulate(float DeltaTime);
	void GetSimulationData(FClothingSimulationData& OutData) const;

	void SetGravity(const FVector& InGravity);
	void SetWindVelocity(const FVector& InWindVelocity, float InDrag, float InLift);

	bool IsValid() const { return Cloth != nullptr && Solver != nullptr; }

private:
	nv::cloth::Fabric* Fabric = nullptr;
	nv::cloth::Cloth* Cloth = nullptr;
	nv::cloth::Solver* Solver = nullptr;
};