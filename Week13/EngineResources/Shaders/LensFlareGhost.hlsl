// 반해상도 밝은 부분 → 반해상도 고스트. 화면 중앙을 기준으로 뒤집어 여러 번 샘플링한다.
cbuffer LensFlareConstants : register(b0)
{
    float Intensity;
    float Threshold;
    float GhostCount;
    float GhostDispersal;
};

Texture2D BrightTexture : register(t0);
SamplerState LinearClamp : register(s0);

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
    float2 Size;
    BrightTexture.GetDimensions(Size.x, Size.y);
    float2 UV = Input.Position.xy / Size;

    // 뒤집은 UV에서 중앙 방향으로 GhostDispersal 간격씩 GhostCount번 샘플링해 더한다
    float2 FlipUV = 1 - UV;
    float2 GhostVector = (0.5f - FlipUV) * GhostDispersal;
    
    float3 Result = 0;
    for (int i = 0; i < (int)GhostCount; ++i)
    {
        float2 SampleUV = FlipUV + GhostVector * i;
        float3 SampleColor = BrightTexture.Sample(LinearClamp, SampleUV).rgb;
        float EdgeWeight = pow(1 - saturate(length(0.5 - SampleUV) / 0.7071), 2);
        Result += SampleColor * EdgeWeight;
    }
    
    return float4(Result, 1);
}
