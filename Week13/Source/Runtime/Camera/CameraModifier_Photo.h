#pragma once

#include "Camera/CameraModifier.h"
#include "CameraModifier_Photo.generated.h"

// 필름 카메라 한 대의 조작 값. 조리개·셔터는 실제 카메라처럼 정해진 눈금에서만 고른다.
struct FPhotoCameraState
{
	static constexpr float ApertureStops[] = { 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f, 22.0f };
	// 초 단위. 뷰파인더 눈금(15 30 60 125 250 500)과 같은 순서
	static constexpr float ShutterStops[] = { 1.0f / 15, 1.0f / 30, 1.0f / 60, 1.0f / 125, 1.0f / 250, 1.0f / 500 };
	static constexpr int32 NumApertureStops = sizeof(ApertureStops) / sizeof(float);
	static constexpr int32 NumShutterStops = sizeof(ShutterStops) / sizeof(float);

	static constexpr float MinFocusDistance = 0.8f;   // LC-A 최단 초점 거리(m)
	static constexpr float MaxFocusDistance = 100.0f; // 이보다 멀면 무한대로 본다
	// 35mm 필름 한 컷은 36x24mm(3:2). 뷰파인더 프레임이 화면 높이의 이만큼을 차지한다
	static constexpr float FilmAspect = 1.5f;
	static constexpr float FrameHeightOnScreen = 0.8f;
	static constexpr float MinFocalLength = 24.0f;
	static constexpr float MaxFocalLength = 135.0f;

	// 처음 들었을 때 Sunny 16 근처(f/16, 1/250)라서 노출이 거의 맞는다
	int32 ApertureIndex = 5;      // f/16
	int32 ShutterIndex = 4;       // 1/250
	float ISO = 200.0f;
	float FocusDistance = 5.0f;   // m
	float FocalLength = 35.0f;    // mm
	float SensorWidth = 36.0f;    // mm, 35mm 필름 가로

	float GetFstop() const { return ApertureStops[ApertureIndex]; }
	float GetShutterSeconds() const { return ShutterStops[ShutterIndex]; }
};

UCLASS()
// 사진 모드 동안 렌즈(화각)와 DOF 값을 POV에 덮어쓴다.
// 상태를 카메라 컴포넌트가 아니라 PCM 쪽에 두어서, 어떤 뷰 타깃을 보든 같은 카메라로 찍는다.
class UCameraModifier_Photo : public UCameraModifier
{
	GENERATED_BODY()

public:
	UCameraModifier_Photo();

	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

	bool IsPhotoModeActive() const { return !IsDisabled() && !IsPendingDisable(); }
	void TogglePhotoMode();

	void StepAperture(int32 Delta);
	void StepShutter(int32 Delta);
	// 휠 한 칸(=120)을 Notches 1로 받는다
	void AdjustFocus(float Notches);
	void AdjustZoom(float DeltaMm);

	const FPhotoCameraState& GetState() const { return State; }

private:
	FPhotoCameraState State;
};
