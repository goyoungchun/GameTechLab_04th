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
    int3 HalfPixel = int3(int2(Input.Position.xy), 0);
    float4 Center = HalfResTexture.Load(HalfPixel);

    // A의 CoC는 풀해상도 픽셀 반경이고, Visualize와 같은 최대 반경으로 자른다
    static const float MaxCoCRadiusPx = 32.f;
    static const int SampleCount = 32;

    // TODO(human): 반해상도 기준 반경 안에서 SampleCount개를 원판으로 모아 평균 색 반환
    return Center;
}
