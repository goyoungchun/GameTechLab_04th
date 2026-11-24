#include "EnginePCH.h"
#include "Render/PostProcess/LensFlareRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/SceneView.h"
#include "Render/Texture2D.h"

namespace
{
	FPipelineState MakeFullscreenPipeline(FShaderProgram* Shader, EBlendState BlendState)
	{
		FPipelineState State;
		State.Shader = Shader;
		State.Topology = EPrimitiveTopology::TriangleList;
		State.RasterizerState = ERasterizerState::SolidNone;
		State.BlendState = BlendState;
		State.DepthStencilState = EDepthStencilState::Disabled;
		return State;
	}
}

void FLensFlareRenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;
	BrightShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/LensFlareBright.hlsl");
	GhostShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/LensFlareGhost.hlsl");
	CompositeShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/LensFlareComposite.hlsl");
	BrightPipelineState = MakeFullscreenPipeline(BrightShader, EBlendState::Opaque);
	GhostPipelineState = MakeFullscreenPipeline(GhostShader, EBlendState::Opaque);
	// 장면을 복사하지 않고 플레어만 위에 얹으면 되므로 가산 블렌드로 그린다
	CompositePipelineState = MakeFullscreenPipeline(CompositeShader, EBlendState::Additive);
	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FLensFlareData));
	assert(BrightShader && GhostShader && CompositeShader && ConstantBuffer);
}

void FLensFlareRenderer::UploadConstants(const FSceneView& View)
{
	const FPostProcessSettings& PP = View.FinalPostProcessSettings;
	FLensFlareData Data{};
	Data.Intensity = PP.LensFlareIntensity;
	Data.Threshold = PP.LensFlareThreshold;
	Data.GhostCount = static_cast<float>(PP.LensFlareGhostCount);
	Data.GhostDispersal = PP.LensFlareGhostDispersal;
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
}

bool FLensFlareRenderer::DrawFullscreen(const FPipelineState& Pipeline, FTexture2D* Texture)
{
	if (!Pipeline.Shader || !Texture) return false;

	RenderCommand::BindPipelineState(Pipeline);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, Texture, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
	// 다음 패스가 이 텍스처를 렌더 타깃으로 쓸 수 있게 푼다
	RenderCommand::UnbindShaderResources(0, 1, EShaderBindFlagBits::Pixel);
	return true;
}

bool FLensFlareRenderer::RenderBrightPass(const FSceneView& View, FTexture2D* SceneColor)
{
	if (!ConstantBuffer) return false;
	UploadConstants(View);
	return DrawFullscreen(BrightPipelineState, SceneColor);
}

bool FLensFlareRenderer::RenderGhosts(const FSceneView& View, FTexture2D* BrightTexture)
{
	if (!ConstantBuffer) return false;
	UploadConstants(View);
	return DrawFullscreen(GhostPipelineState, BrightTexture);
}

bool FLensFlareRenderer::RenderComposite(const FSceneView& View, FTexture2D* GhostTexture)
{
	if (!ConstantBuffer) return false;
	UploadConstants(View);
	return DrawFullscreen(CompositePipelineState, GhostTexture);
}
