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

	// 필름(36x24)은 화면 전체가 아니라 뷰파인더 프레임에 맞아야 한다.
	// 그래서 화면 전체에 대응하는 "가상 필름" 크기를 구해서 화각과 DOF에 같이 쓴다
	const float Aspect = std::max(InOutPOV.AspectRatio, 0.01f);
	const float ScreenFilmHeight = State.SensorWidth / FPhotoCameraState::FilmAspect / FPhotoCameraState::FrameHeightOnScreen;
	const float ScreenFilmWidth = ScreenFilmHeight * Aspect;
	const float TanHalfVertical = ScreenFilmHeight / (2.0f * State.FocalLength);
	const float PhotoFOV = FMath::RadiansToDegrees(2.0f * atanf(TanHalfVertical));
	InOutPOV.FOV = FMath::Lerp(InOutPOV.FOV, PhotoFOV, Alpha);

	// DOF 렌더러는 이 값만 읽으므로, Phase 4·5에서 DOF가 좋아지면 사진 모드도 같이 좋아진다
	FPostProcessSettings& PP = InOutPOV.PostProcessSettings;
	PP.bOverride_DepthOfFieldFstop = true;
	PP.DepthOfFieldFstop = State.GetFstop();
	PP.bOverride_DepthOfFieldFocalDistance = true;
	PP.DepthOfFieldFocalDistance = State.FocusDistance;
	PP.bOverride_DepthOfFieldSensorWidth = true;
	// DOF 렌더러는 센서 폭과 투영 행렬로 렌즈 길이를 되찾으므로, 화면 전체 기준 폭을 넘겨야 같은 mm가 나온다
	PP.DepthOfFieldSensorWidth = ScreenFilmWidth;
	PP.bOverride_CameraShutterSpeed = true;
	PP.CameraShutterSpeed = 1.0f / State.GetShutterSeconds();
	PP.bOverride_CameraISO = true;
	PP.CameraISO = State.ISO;
	PP.bOverride_bApplyPhysicalCameraExposure = true;
	PP.bApplyPhysicalCameraExposure = true;
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
