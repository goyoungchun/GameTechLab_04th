// 반해상도 (색, CoC) 텍스처에서 CoC 반경만큼 원판으로 샘플을 모아 흐린 색을 만든다.
Texture2D HalfResTexture : register(t0);
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
    static const float MaxCoCRadiusPx = 32.f;
    static const int SampleCount = 32;

    int3 HalfPixel = int3(int2(Input.Position.xy), 0);
    float4 CenterSample = HalfResTexture.Load(HalfPixel);
    float GatherRadiusHalfPx = min(abs(CenterSample.a), MaxCoCRadiusPx) * 0.5f;

    float HalfWidth, HalfHeight;
    HalfResTexture.GetDimensions(HalfWidth, HalfHeight);
    float2 HalfTexelSize = 1.0 / float2(HalfWidth, HalfHeight);
    float2 CenterUV = Input.Position.xy * HalfTexelSize;
    
    float3 ColorSum = CenterSample.rgb;
    float WeightSum = 1.f;
    for (int i = 0; i < SampleCount; ++i)
    {
        float SampleDistHalfPx = sqrt((i + 0.5) / SampleCount) * GatherRadiusHalfPx; // 면적 균등
        float SampleAngle = i * 2.39996; // 황금각
        float2 SampleOffsetHalfPx = float2(cos(SampleAngle), sin(SampleAngle)) * SampleDistHalfPx;
        float4 Neighbor = HalfResTexture.SampleLevel(LinearClampSampler, CenterUV + SampleOffsetHalfPx * HalfTexelSize, 0);

        float NeighborCoCHalfPx = abs(Neighbor.a) * 0.5f;
        float ReachWeight = saturate(NeighborCoCHalfPx - SampleDistHalfPx + 1.f); // 이웃 원판이 나까지 닿는가
        ColorSum += Neighbor.rgb * ReachWeight;
        WeightSum += ReachWeight;
    }
    return float4(ColorSum / WeightSum, CenterSample.a);
}