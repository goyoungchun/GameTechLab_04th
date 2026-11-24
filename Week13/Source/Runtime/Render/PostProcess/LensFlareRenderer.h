#pragma once
#include "Render/Renderer.h"
#include "Render/PipelineState.h"

struct FSceneView;
class FTexture2D;

// LensFlare*.hlsl의 cbuffer b0과 같은 배치. 세 패스가 같은 값을 나눠 쓴다
struct alignas(16) FLensFlareData
{
	float Intensity;
	float Threshold;
	float GhostCount;
	float GhostDispersal;
	float HaloWidth;
	float ChromaticDistortion;
	float DirtIntensity;
	float Padding0;
};
static_assert(sizeof(FLensFlareData) == 32);

// 렌즈 플레어 셰이더를 돌릴 준비(상수, 바인딩, Draw)만 한다. 언제 어떤 텍스처로 부를지는 FSceneRenderer가 정한다.
class FLensFlareRenderer
{
public:
	void Init(FRenderer* InRenderer);
	// 풀해상도 HDR 장면 → 현재 바인딩된 반해상도 타깃(Threshold를 넘은 밝은 부분만)
	bool RenderBrightPass(const FSceneView& View, FTexture2D* SceneColor);
	// 반해상도 밝은 부분 → 현재 바인딩된 반해상도 타깃(중앙 반전 고스트)
	bool RenderGhosts(const FSceneView& View, FTexture2D* BrightTexture);
	// 고스트 → 현재 바인딩된 SceneColor에 더한다
	bool RenderComposite(const FSceneView& View, FTexture2D* GhostTexture);

private:
	void UploadConstants(const FSceneView& View);
	bool DrawFullscreen(const FPipelineState& Pipeline, FTexture2D* Texture);

	FRenderer* Renderer = nullptr;
	FShaderProgram* BrightShader = nullptr;
	FShaderProgram* GhostShader = nullptr;
	FShaderProgram* CompositeShader = nullptr;
	FPipelineState BrightPipelineState;
	FPipelineState GhostPipelineState;
	FPipelineState CompositePipelineState;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
};
