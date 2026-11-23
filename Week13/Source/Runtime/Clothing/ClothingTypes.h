#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/EngineMath.h"

struct FClothPhysicalMeshData
{
	TArray<FVector> Vertices;
	TArray<uint32> Indices;
	TArray<float> InverseMasses;
};

struct FClothingSimulationData
{
	TArray<FVector> Positions;
	TArray<FVector> Normals;
};
