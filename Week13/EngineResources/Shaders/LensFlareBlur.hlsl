// 반해상도 고스트 → 반해상도. 한 방향(가로 또는 세로)으로만 가우시안 블러한다. 두 번 부르면 2D 블러가 된다.
cbuffer LensFlareBlurConstants : register(b1)
{
    float2 BlurDirection; // (1,0)이면 가로, (0,1)이면 세로
    float2 BlurPadding;
};

Texture2D SourceTexture : register(t0);
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
    static const int BlurRadius = 8; // R
    static const float BlurSigma = (float)BlurRadius / 2.5f; // σ (R의 1/2~1/3)
    
    float2 Size;
    SourceTexture.GetDimensions(Size.x, Size.y);
    float2 UV = Input.Position.xy / Size;
    float2 TexelStep = BlurDirection / Size;

    
    // BlurDirection 축으로 -R..R 텍셀을 가우시안 가중치로 모아 Result에 담는다
    float3 Result = 0;
    float WeightSum = 0;

    [unroll]
    for (int i = -BlurRadius; i <= BlurRadius; ++i)
    {
        float Weight = exp(-i * i / (2 * BlurSigma * BlurSigma)); // i 텍셀 떨어진 샘플의 가중치
        float2 SampleUV = UV + BlurDirection * (i / Size); // UV에서 BlurDirection 방향으로 i칸 이동
        Result += SourceTexture.Sample(LinearClamp, SampleUV).rgb * Weight;
        WeightSum += Weight;
    }

    Result /= WeightSum; // 가중치 합이 1이 되도록

    return float4(Result, 1);
}
