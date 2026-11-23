#include "EnginePCH.h"
#include "Render/PostProcess/DepthOfFieldRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/SceneView.h"
#include "Render/Texture2D.h"

namespace
{
	FPipelineState MakeFullscreenPipeline(FShaderProgram* Shader)
	{
		FPipelineState State;
		State.Shader = Shader;
		State.Topology = EPrimitiveTopology::TriangleList;
		State.RasterizerState = ERasterizerState::SolidNone;
		// 화면 전체를 새 값으로 덮어쓰므로 섞지 않는다
		State.BlendState = EBlendState::Opaque;
		State.DepthStencilState = EDepthStencilState::Disabled;
		return State;
	}
}

void FDepthOfFieldRenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;
	SetupShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/DepthOfFieldSetup.hlsl");
	VisualizeShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/DepthOfFieldVisualize.hlsl");
	SetupPipelineState = MakeFullscreenPipeline(SetupShader);
	VisualizePipelineState = MakeFullscreenPipeline(VisualizeShader);
	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDepthOfFieldData));
	assert(SetupShader && VisualizeShader && ConstantBuffer);
}

bool FDepthOfFieldRenderer::RenderSetup(const FSceneView& View, FTexture2D* DepthTarget)
{
	if (!SetupShader || !ConstantBuffer || !DepthTarget) return false;

	FDepthOfFieldData Data{};
	// 셰이더가 식을 따로 갖지 않도록, 실제 투영 행렬에서 깊이 변환 값을 꺼낸다
	Data.ZScale = View.ProjectionMatrix.M[0][2];
	Data.ZOffset = View.ProjectionMatrix.M[3][2];
	Data.FocalDistance = View.FinalPostProcessSettings.DepthOfFieldFocalDistance;
	Data.FocusRange = View.FinalPostProcessSettings.DepthOfFieldFocusRange;
	Data.Fstop = View.FinalPostProcessSettings.DepthOfFieldFstop;
	Data.ViewWidthPx = View.ViewRect.Width;
	Data.FocalLength = View.FinalPostProcessSettings.DepthOfFieldSensorWidth / 2 * View.ProjectionMatrix.M[1][0] * 0.001f;
	Data.SensorWidth = View.FinalPostProcessSettings.DepthOfFieldSensorWidth * 0.001f;

	RenderCommand::BindPipelineState(SetupPipelineState);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, DepthTarget, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
	// 다음 패스가 깊이를 다시 깊이 타깃으로 꽂을 때 충돌하지 않게 푼다
	RenderCommand::BindShaderResource(0, static_cast<FTexture2D*>(nullptr), EShaderBindFlagBits::Pixel);
	return true;
}

bool FDepthOfFieldRenderer::RenderVisualize(FTexture2D* CoCTexture, FTexture2D* SceneColorCopy)
{
	if (!VisualizeShader || !CoCTexture || !SceneColorCopy) return false;

	RenderCommand::BindPipelineState(VisualizePipelineState);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindShaderResource(0, CoCTexture, EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(1, SceneColorCopy, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
	RenderCommand::UnbindShaderResources(0, 2, EShaderBindFlagBits::Pixel);
	return true;
}
