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

// 실제 렌즈는 면마다 곡률이 달라 고스트가 제각각 떨어지므로, i 대신 이 표에서 간격을 꺼낸다.
// 음수는 거울상보다 중앙 쪽에 생긴다. 4에 가까우면(Dispersal 0.25 기준) 고스트가 너무 커져서 피했다
static const float GhostOffsets[16] = { 0.0, 1.35, -0.55, 2.1, 0.6, -1.2, 2.7, 1.7, -0.25, 0.95, 2.4, -0.85, 1.15, 3.0, 0.3, -1.5 };
// 크게 퍼지는 고스트일수록 빛이 넓게 흩어져 어두워 보이도록 세기도 따로 준다
static const float GhostWeights[16] = { 1.0, 0.55, 0.8, 0.35, 0.7, 0.45, 0.25, 0.5, 0.9, 0.6, 0.3, 0.5, 0.65, 0.2, 0.75, 0.4 };

float4 mainPS(VSOutput Input) : SV_Target
{
    float2 Size;
    BrightTexture.GetDimensions(Size.x, Size.y);
    float2 UV = Input.Position.xy / Size;

    // 뒤집은 UV에서 중앙 방향으로 GhostDispersal 간격씩 GhostCount번 샘플링해 더한다
    float2 FlipUV = 1 - UV;
    float2 GhostVector = (0.5f - FlipUV) * GhostDispersal;
    
    float3 Result = 0;
    const int Count = min((int)GhostCount, 16);
    for (int i = 0; i < Count; ++i)
    {
        float2 SampleUV = FlipUV + GhostVector * GhostOffsets[i];
        float3 SampleColor = BrightTexture.Sample(LinearClamp, SampleUV).rgb;
        float EdgeWeight = pow(1 - saturate(length(0.5 - SampleUV) / 0.7071), 2);
        Result += SampleColor * EdgeWeight * GhostWeights[i];
    }
    
    return float4(Result, 1);
}
