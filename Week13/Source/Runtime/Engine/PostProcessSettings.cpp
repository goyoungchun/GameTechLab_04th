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
	// 노출계가 평균 밝기를 맞추려는 목표(톤매핑 전 선형 값). 실제 18%로 두면 이 씬은 원래보다 어둡게 맞춰져서,
	// 한 스톱 올려 원래 게임 화면 밝기에 가깝게 맞췄다
	constexpr float MiddleGray = 0.36f;

	float GetReferenceEV100()
	{
		static const float Reference = FPostProcessSettings::ComputeEV100(SunnySixteenFstop, SunnySixteenShutter, SunnySixteenISO);
		return Reference;
	}
}

float FPostProcessSettings::ComputeEV100(float Fstop, float ShutterSpeed, float ISO)
{
	// EV100 = log2(N^2 / t) - log2(ISO / 100), t = 1 / ShutterSpeed
	return std::log2(Fstop * Fstop * ShutterSpeed * 100.0f / ISO);
}

float FPostProcessSettings::GetMeteredEV100(float AverageLuminance)
{
	// 화면 밝기 = 평균 휘도 x 2^(기준EV - EV) 이므로, 이것이 중간 회색이 되는 EV를 거꾸로 푼다
	return GetReferenceEV100() + std::log2(std::max(AverageLuminance, 1e-6f) / MiddleGray);
}

float FPostProcessSettings::GetCameraEV100() const
{
	return ComputeEV100(std::max(DepthOfFieldFstop, 0.5f), std::max(CameraShutterSpeed, 1.0f), std::max(CameraISO, 1.0f));
}

float FPostProcessSettings::GetPhysicalCameraExposureScale() const
{
	if (!bApplyPhysicalCameraExposure) return 1.0f;
	// EV가 1 커지면(한 스톱 조이면) 빛이 절반이 된다
	return std::exp2(GetReferenceEV100() - GetCameraEV100());
}
