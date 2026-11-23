#pragma once
#include "Render/Renderer.h"
#include "Render/PipelineState.h"

struct FSceneView;
class FTexture2D;

// DepthOfFieldSetup.hlsl의 cbuffer b0과 같은 배치
struct alignas(16) FDepthOfFieldData
{
	float ZScale;
	float ZOffset;
	float FocalDistance;
	float FocusRange;

	// 얇은 렌즈 CoC 입력. 셰이더가 단위를 신경 쓰지 않도록 렌즈 값도 월드 단위로 넘긴다.
	float FocalLength;
	float Fstop;
	float SensorWidth;
	float ViewWidthPx;
};
static_assert(sizeof(FDepthOfFieldData) == 32);

// DOF 셰이더를 돌릴 준비(상수, 바인딩, Draw)만 한다. 언제 어떤 텍스처로 부를지는 FSceneRenderer가 정한다.
class FDepthOfFieldRenderer
{
public:
	void Init(FRenderer* InRenderer);
	// 깊이를 읽어 현재 바인딩된 렌더 타깃(R16_Float CoC)에 부호 있는 CoC를 쓴다.
	bool RenderSetup(const FSceneView& View, FTexture2D* DepthTarget);
	// CoC와 장면 복사본을 읽어 현재 바인딩된 렌더 타깃에 CoC 맵을 칠한다.
	bool RenderVisualize(FTexture2D* CoCTexture, FTexture2D* SceneColorCopy);

private:
	FRenderer* Renderer = nullptr;
	FShaderProgram* SetupShader = nullptr;
	FShaderProgram* VisualizeShader = nullptr;
	FPipelineState SetupPipelineState;
	FPipelineState VisualizePipelineState;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
};
