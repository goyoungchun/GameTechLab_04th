#include "EnginePCH.h"
#include "Engine/PostProcessSettings.h"

#include <algorithm>

void FPostProcessSettings::OverrideBlend(const FPostProcessSettings& Src, float Weight)
{
	Weight = std::clamp(Weight, 0.0f, 1.0f);
	if (Weight <= 0.0f) return;

	auto BlendFloat = [Weight](bool bOverride, float& Dest, float Value)
	{
		if (bOverride) Dest += (Value - Dest) * Weight;
	};

	BlendFloat(Src.bOverride_VignetteIntensity, VignetteIntensity, Src.VignetteIntensity);
	BlendFloat(Src.bOverride_DepthOfFieldFocalDistance, DepthOfFieldFocalDistance, Src.DepthOfFieldFocalDistance);
	BlendFloat(Src.bOverride_DepthOfFieldFocusRange, DepthOfFieldFocusRange, Src.DepthOfFieldFocusRange);
	BlendFloat(Src.bOverride_DepthOfFieldFstop, DepthOfFieldFstop, Src.DepthOfFieldFstop);
	BlendFloat(Src.bOverride_DepthOfFieldSensorWidth, DepthOfFieldSensorWidth, Src.DepthOfFieldSensorWidth);
}
