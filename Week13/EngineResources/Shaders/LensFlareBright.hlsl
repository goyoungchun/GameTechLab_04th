// 풀해상도 HDR 장면 → 반해상도. Threshold를 넘은 밝은 부분만 남긴다.
cbuffer LensFlareConstants : register(b0)
{
    float Intensity;
    float Threshold;
    float GhostCount;
    float GhostDispersal;
};

Texture2D SceneColorTexture : register(t0);
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
    // 타깃이 반해상도이므로 출력 픽셀 위치를 출력 크기로 나눠 UV를 얻는다
    float2 OutputSize;
    SceneColorTexture.GetDimensions(OutputSize.x, OutputSize.y);
    OutputSize *= 0.5f;
    float2 UV = Input.Position.xy / OutputSize;

    // 장면 색을 읽어 Threshold를 넘은 만큼만 남긴다
    float4 Color = SceneColorTexture.Sample(LinearClamp, UV);
    float Luma = dot(Color.rgb, float3(0.2126, 0.7152, 0.0722));
    float Excess = max(Luma - Threshold, 0);
    Color.rgb *= Excess / max(Luma, 1e-4);
    return float4(Color.rgb, 1);
}
