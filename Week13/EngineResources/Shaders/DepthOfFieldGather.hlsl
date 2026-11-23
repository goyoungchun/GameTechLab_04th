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
    // A의 CoC는 풀해상도 픽셀 반경이고, Visualize와 같은 최대 반경으로 자른다
    static const float MaxCoCRadiusPx = 32.f;
    static const int SampleCount = 32;
    
    int3 HalfPixel = int3(int2(Input.Position.xy), 0);
    float4 Center = HalfResTexture.Load(HalfPixel);
    float Radius = clamp(Center.a, -MaxCoCRadiusPx, MaxCoCRadiusPx) * 0.5f;
    
    float W, H;
    HalfResTexture.GetDimensions(W, H);
    float2 InvSize = 1.0 / float2(W, H);
    float2 UV = Input.Position.xy * InvSize;

    // 반해상도 기준 반경 안에서 SampleCount개를 원판으로 모아 평균 색 반환
    float3 Sum = 0;
    for (int i = 0; i < SampleCount; ++i)
    {
        float r = sqrt((i + 0.5) / SampleCount) * Radius; // 0~Radius, 면적 균등
        float a = i * 2.39996; // 황금각
        float2 Offset = float2(cos(a), sin(a)) * r;
        Sum += HalfResTexture.SampleLevel(LinearClampSampler, UV + Offset * InvSize, 0).rgb;
    }
    return float4(Sum / SampleCount, Center.a);
}
