#include "EnginePCH.h"
#include "Camera/CameraModifier_Photo.h"
#include "Camera/PlayerCameraManager.h"

#include <algorithm>
#include <cmath>

UCameraModifier_Photo::UCameraModifier_Photo()
{
	// 카메라를 눈에 갖다 대는 느낌만 주는 짧은 화각 전환
	AlphaInTime = 0.15f;
	AlphaOutTime = 0.15f;
}

bool UCameraModifier_Photo::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	Super::ModifyCamera(DeltaTime, InOutPOV);
	if (Alpha <= 0.0f) return false;

	// 센서 폭은 가로 기준이라 가로 화각이 나오고, POV.FOV는 세로 화각이라 종횡비로 나눈다
	const float Aspect = std::max(InOutPOV.AspectRatio, 0.01f);
	const float TanHalfVertical = State.SensorWidth / (2.0f * State.FocalLength * Aspect);
	const float PhotoFOV = FMath::RadiansToDegrees(2.0f * atanf(TanHalfVertical));
	InOutPOV.FOV = FMath::Lerp(InOutPOV.FOV, PhotoFOV, Alpha);

	// DOF 렌더러는 이 값만 읽으므로, Phase 4·5에서 DOF가 좋아지면 사진 모드도 같이 좋아진다
	FPostProcessSettings& PP = InOutPOV.PostProcessSettings;
	PP.bOverride_DepthOfFieldFstop = true;
	PP.DepthOfFieldFstop = State.GetFstop();
	PP.bOverride_DepthOfFieldFocalDistance = true;
	PP.DepthOfFieldFocalDistance = State.FocusDistance;
	PP.bOverride_DepthOfFieldSensorWidth = true;
	PP.DepthOfFieldSensorWidth = State.SensorWidth;
	// 카메라가 후처리를 꺼 둔 상태(가중치 0)여도 사진 모드 값은 들어가야 한다
	InOutPOV.PostProcessBlendWeight = std::max(InOutPOV.PostProcessBlendWeight, Alpha);
	return false;
}

void UCameraModifier_Photo::TogglePhotoMode()
{
	if (IsPhotoModeActive()) DisableModifier();
	else EnableModifier();
}

void UCameraModifier_Photo::StepAperture(int32 Delta)
{
	State.ApertureIndex = std::clamp(State.ApertureIndex + Delta, 0, FPhotoCameraState::NumApertureStops - 1);
}

void UCameraModifier_Photo::StepShutter(int32 Delta)
{
	State.ShutterIndex = std::clamp(State.ShutterIndex + Delta, 0, FPhotoCameraState::NumShutterStops - 1);
}

void UCameraModifier_Photo::AdjustFocus(float Notches)
{
	// 실제 초점 링처럼 가까울수록 촘촘하고 멀수록 성기게 움직이도록 배율로 바꾼다
	const float Scaled = State.FocusDistance * powf(1.15f, Notches);
	State.FocusDistance = std::clamp(Scaled, FPhotoCameraState::MinFocusDistance, FPhotoCameraState::MaxFocusDistance);
}

void UCameraModifier_Photo::AdjustZoom(float DeltaMm)
{
	State.FocalLength = std::clamp(State.FocalLength + DeltaMm,
		FPhotoCameraState::MinFocalLength, FPhotoCameraState::MaxFocalLength);
}
