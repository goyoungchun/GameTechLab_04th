#include "EnginePCH.h"
#include "Engine/PostProcessSettings.h"

#include <algorithm>
#include <cmath>

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
	BlendFloat(Src.bOverride_CameraShutterSpeed, CameraShutterSpeed, Src.CameraShutterSpeed);
	BlendFloat(Src.bOverride_CameraISO, CameraISO, Src.CameraISO);
	// 켜고 끄는 값은 섞을 수 없으니 절반을 넘으면 넘어간다
	if (Src.bOverride_bApplyPhysicalCameraExposure && Weight >= 0.5f)
		bApplyPhysicalCameraExposure = Src.bApplyPhysicalCameraExposure;
}

namespace
{
	// 씬 라이트가 물리 단위가 아니라서 "지금 화면 밝기 = 맑은 날 낮"이라고 정한 기준점
	constexpr float SunnySixteenFstop = 16.0f;
	constexpr float SunnySixteenShutter = 200.0f;
	constexpr float SunnySixteenISO = 200.0f;

	float ComputeEV100(float Fstop, float ShutterSpeed, float ISO)
	{
		// EV100 = log2(N^2 / t) - log2(ISO / 100), t = 1 / ShutterSpeed
		return std::log2(Fstop * Fstop * ShutterSpeed * 100.0f / ISO);
	}
}

float FPostProcessSettings::GetCameraEV100() const
{
	return ComputeEV100(std::max(DepthOfFieldFstop, 0.5f), std::max(CameraShutterSpeed, 1.0f), std::max(CameraISO, 1.0f));
}

float FPostProcessSettings::GetPhysicalCameraExposureScale() const
{
	if (!bApplyPhysicalCameraExposure) return 1.0f;
	static const float ReferenceEV100 = ComputeEV100(SunnySixteenFstop, SunnySixteenShutter, SunnySixteenISO);
	// EV가 1 커지면(한 스톱 조이면) 빛이 절반이 된다
	return std::exp2(ReferenceEV100 - GetCameraEV100());
}
