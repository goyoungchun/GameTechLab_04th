// 선명한 원본과 반해상도 블러를 CoC 크기에 따라 섞어 SceneColor에 쓴다.
Texture2D SceneColorTexture : register(t0);
Texture2D<float> CoCTexture : register(t1);
Texture2D BlurTexture : register(t2);
SamplerState LinearClampSampler : register(s0);

struct VSOutput
{
    float4 Position : SV_Position;
};

VSOutput mainVS(uint VertexID : SV_VertexID)
{
    static const float2 Positions[6] =
    {
        float2(-1, 1),
        float2(1, 1),
        float2(-1, -1),

        float2(-1, -1),
        float2(1, 1),
        float2(1, -1)
    };

    VSOutput Output;
    Output.Position = float4(Positions[VertexID], 0, 1);
    return Output;
}

float4 mainPS(VSOutput Input) : SV_Target
{
    static const float BlendRadiusPx = 8.f;
    
    int3 PixelPosition = int3(int2(Input.Position.xy), 0);
    float3 SharpColor = SceneColorTexture.Load(PixelPosition).rgb;
    
    // TODO(human): 반해상도 Blur를 이 픽셀 위치에서 읽고, CoC 크기로 Sharp와 섞어 반환
    float W, H;
    SceneColorTexture.GetDimensions(W, H);
    float2 UV = Input.Position.xy / float2(W, H);
    float4 Half = BlurTexture.SampleLevel(LinearClampSampler, UV, 0);
  
    float3 BlurColor = Half.rgb;
    float CoCAmount = saturate(abs(CoCTexture.Load(PixelPosition)) / BlendRadiusPx);
    
    float3 Color = lerp(SharpColor, BlurColor, CoCAmount);
    
    return float4(Color, 1);
}
