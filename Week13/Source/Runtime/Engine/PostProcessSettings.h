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

	// Src에서 bOverride_가 켜진 값만 Weight(0~1)만큼 이 값 쪽에서 Src 쪽으로 섞는다.
	void OverrideBlend(const FPostProcessSettings& Src, float Weight);
};
