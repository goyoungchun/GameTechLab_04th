#include "EnginePCH.h"
#include "Camera/CameraModifier_Photo.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/PhotoCapture.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float FreeCamSpeed = 3.0f;       // m/s
	constexpr float FreeCamFastScale = 3.0f;   // Shift를 누르면 이만큼 빠르게
	constexpr float FreeCamMaxPitch = 89.0f;

	// -180~180으로 접는다. 359°→1°를 -358°가 아니라 +2°로 보간하려고 쓴다
	float WrapDegrees(float Angle)
	{
		Angle = std::fmod(Angle + 180.0f, 360.0f);
		if (Angle < 0.0f) Angle += 360.0f;
		return Angle - 180.0f;
	}

	FRotator LerpRotator(const FRotator& A, const FRotator& B, float T)
	{
		return FRotator(A.Pitch + WrapDegrees(B.Pitch - A.Pitch) * T,
			A.Yaw + WrapDegrees(B.Yaw - A.Yaw) * T,
			A.Roll + WrapDegrees(B.Roll - A.Roll) * T);
	}
}

UCameraModifier_Photo::UCameraModifier_Photo()
{
	// 카메라를 눈에 갖다 대는 느낌만 주는 짧은 화각 전환
	AlphaInTime = 0.15f;
	AlphaOutTime = 0.15f;
}

bool UCameraModifier_Photo::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	Super::ModifyCamera(DeltaTime, InOutPOV);
	TimeSinceShutter += DeltaTime;
	if (Alpha <= 0.0f)
	{
		bFreeCamValid = false;
		return false;
	}

	// 들어가는 첫 프레임의 3인칭 시점에서 출발해야 화면이 튀지 않는다
	if (!bFreeCamValid)
	{
		FreeCamLocation = InOutPOV.Location;
		FreeCamRotation = InOutPOV.Rotation;
		FreeCamRotation.Roll = 0.0f;
		bFreeCamValid = true;
	}

	// 나가는 중(Alpha가 줄어드는 중)에는 카메라를 멈춰 두고 3인칭 자리로 돌아가게만 한다
	if (IsPhotoModeActive())
	{
		FreeCamRotation.Yaw += PendingYaw;
		FreeCamRotation.Pitch = std::clamp(FreeCamRotation.Pitch + PendingPitch, -FreeCamMaxPitch, FreeCamMaxPitch);

		const FQuat Rotation = FreeCamRotation.Quaternion();
		// 앞뒤는 카메라가 보는 방향 그대로(드론처럼), 위아래는 월드 위쪽으로 움직인다
		const FVector Velocity = Rotation.GetForwardVector() * PendingMove.X + Rotation.GetRightVector() * PendingMove.Y +
			FVector(0.0f, 0.0f, 1.0f) * PendingMove.Z;
		FreeCamLocation += Velocity * (FreeCamSpeed * (bPendingFast ? FreeCamFastScale : 1.0f) * DeltaTime);
	}
	PendingMove = FVector(0.0f, 0.0f, 0.0f);
	PendingYaw = PendingPitch = 0.0f;

	// 들어오고 나갈 때 Alpha로 섞으므로, 3인칭 카메라와 자유 카메라 사이를 부드럽게 오간다
	InOutPOV.Location = FMath::Lerp(InOutPOV.Location, FreeCamLocation, Alpha);
	InOutPOV.Rotation = LerpRotator(InOutPOV.Rotation, FreeCamRotation, Alpha);

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

void UCameraModifier_Photo::AddFreeCameraInput(const FVector& Move, float YawDelta, float PitchDelta, bool bFast)
{
	PendingMove = Move;
	PendingYaw += YawDelta;
	PendingPitch += PitchDelta;
	bPendingFast = bFast;
}

void UCameraModifier_Photo::TriggerShutter()
{
	if (!IsPhotoModeActive()) return;
	FPhotoCapture::Request(FPhotoCameraState::FrameHeightOnScreen, FPhotoCameraState::FilmAspect);
	TimeSinceShutter = 0.0f;
	++ShotCount;
}

void UCameraModifier_Photo::AdjustZoom(float DeltaMm)
{
	State.FocalLength = std::clamp(State.FocalLength + DeltaMm,
		FPhotoCameraState::MinFocalLength, FPhotoCameraState::MaxFocalLength);
}
