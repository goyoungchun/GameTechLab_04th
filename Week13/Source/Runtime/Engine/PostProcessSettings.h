#pragma once

#include "Core/Types.h"
#include "UObject/Class.h"
#include "PostProcessSettings.generated.h"

// 카메라가 화면 후처리에 넘기는 값. 값마다 bOverride_ 플래그가 있어서,
// 켜진 값만 아래(기본값이나 앞서 섞인 값) 위에 덮어쓴다.
USTRUCT()
struct FPostProcessSettings
{
	GENERATED_BODY()

	// ---------- Vignette ----------
	UPROPERTY(EditAnywhere)
	bool bOverride_VignetteIntensity = false;
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	float VignetteIntensity = 0.0f;

	// ---------- Depth of Field ----------
	UPROPERTY(EditAnywhere)
	bool bOverride_DepthOfFieldFocalDistance = false;
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float DepthOfFieldFocalDistance = 10.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_DepthOfFieldFocusRange = false;
	// 셰이더에서 분모로 쓰이므로 0이 되지 않게 막는다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0.01))
	float DepthOfFieldFocusRange = 5.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_DepthOfFieldFstop = false;
	// 렌즈의 F-Stop(N). 조리개 지름 = 초점 거리 / N
	UPROPERTY(EditAnywhere, meta=(ClampMin=1, ClampMax=32, Delta=0.1))
	float DepthOfFieldFstop = 2.8f;
	UPROPERTY(EditAnywhere)
	bool bOverride_DepthOfFieldSensorWidth = false;
	// 센서 가로 폭(mm). 36 = 풀프레임
	UPROPERTY(EditAnywhere, meta=(ClampMin=1))
	float DepthOfFieldSensorWidth = 36.0f;

	// ---------- Camera (물리 노출) ----------
	// 조리개는 DepthOfFieldFstop을 같이 쓴다 (UE도 노출과 DOF가 같은 F-Stop을 본다)
	UPROPERTY(EditAnywhere)
	bool bOverride_CameraShutterSpeed = false;
	// 셔터 속도의 분모(1/s). 125면 1/125초
	UPROPERTY(EditAnywhere, meta=(ClampMin=1, ClampMax=8000))
	float CameraShutterSpeed = 60.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_CameraISO = false;
	UPROPERTY(EditAnywhere, meta=(ClampMin=25, ClampMax=6400))
	float CameraISO = 100.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_bApplyPhysicalCameraExposure = false;
	// 켜면 F-Stop·셔터·ISO로 노출 배율을 정한다. 끄면 배율 1 (UE의 Apply Physical Camera Exposure)
	UPROPERTY(EditAnywhere)
	bool bApplyPhysicalCameraExposure = false;

	// 조리개·셔터·ISO를 ISO 100 기준 노출값(EV100)으로 바꾼다. 클수록 빛을 적게 받는다
	float GetCameraEV100() const;
	// 톤매핑 전에 곱할 배율. Sunny 16(f/16, 1/200초, ISO 200)에서 1이 되도록 맞췄다
	float GetPhysicalCameraExposureScale() const;

	// Src에서 bOverride_가 켜진 값만 Weight(0~1)만큼 이 값 쪽에서 Src 쪽으로 섞는다.
	void OverrideBlend(const FPostProcessSettings& Src, float Weight);
};
