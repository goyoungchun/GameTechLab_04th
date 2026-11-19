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

	// Src에서 bOverride_가 켜진 값만 Weight(0~1)만큼 이 값 쪽에서 Src 쪽으로 섞는다.
	void OverrideBlend(const FPostProcessSettings& Src, float Weight);
};
