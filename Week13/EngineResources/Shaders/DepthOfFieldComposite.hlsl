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
    int3 PixelPosition = int3(int2(Input.Position.xy), 0);
    float3 Sharp = SceneColorTexture.Load(PixelPosition).rgb;

    // TODO: CoC로 Sharp와 Blur 섞기 (Step 3)
    return float4(Sharp, 1);
}
