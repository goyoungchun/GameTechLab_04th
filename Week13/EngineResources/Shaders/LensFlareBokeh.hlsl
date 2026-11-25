// 반해상도 밝은 부분 → 반해상도. 광원 점 하나하나를 조리개 모양(BladeCount 각형)으로 번지게 한다.
// Ghost 패스가 이 결과를 배율대로 복사하므로 고스트도 같은 N각형이 된다.
cbuffer LensFlareConstants : register(b0)
{
    float Intensity;
    float Threshold;
    float GhostCount;
    float GhostDispersal;
    float HaloWidth;
    float ChromaticDistortion;
    float DirtIntensity;
    float HaloIntensity;
    float BladeCount;
    float BokehRadius; // 반해상도 px
    float Padding0;
    float Padding1;
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
    static const float PI = 3.14159265f;
    static const int SampleCount = 128; // 반경이 커져도 샘플 간격이 벌어져 점박이가 되지 않게

    float2 Size;
    BrightTexture.GetDimensions(Size.x, Size.y);
    float2 UV = Input.Position.xy / Size;
    float2 TexelSize = 1.0f / Size;

    float3 Result = 0;
    // BokehRadius 반경의 N각형(BladeCount) 안을 고르게 샘플링해 평균을 Result에 담는다
    [unroll]
    for (int i = 0; i < SampleCount; ++i)
    {
        float SampleDistHalfPx = sqrt((i + 0.5) / SampleCount) * BokehRadius; 
        float SampleAngle = i * 2.39996; // 황금각
        
        // SampleAngle로 다각형 배율을 구해 SampleDistHalfPx에 곱한다 (BladeCount 사용)
        float Wedge = 2 * PI / BladeCount;
        float Phi = fmod(SampleAngle, Wedge) - Wedge * 0.5f;
        float PolyScale = cos(PI / BladeCount) / cos(Phi);
        float SamplePolyDistHalfPx = SampleDistHalfPx * PolyScale;
        
        float2 SampleOffsetHalfPx = float2(cos(SampleAngle), sin(SampleAngle)) * SamplePolyDistHalfPx;
        float4 Neighbor = BrightTexture.SampleLevel(LinearClamp, UV + SampleOffsetHalfPx * TexelSize, 0);
        
        Result += Neighbor.rgb;
    }
    
    return float4(Result / SampleCount, 1);
}
