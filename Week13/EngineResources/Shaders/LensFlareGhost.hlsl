// 반해상도 밝은 부분 → 반해상도 고스트. 화면 중앙 기준으로 고스트마다 다른 배율·색으로 샘플링한다.
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

// 고스트 i는 광원을 화면 중앙 기준으로 1/Scale배 옮기고 1/Scale배 키운 상이다.
// 음수면 중앙 반대편, |Scale| < 1이면 바깥쪽에 크게, > 1이면 중앙 가까이 작게 생긴다. 1 근처는 광원과 겹쳐서 피했다
static const float GhostScales[16] = { -1.0, -0.45, -2.4, 1.8, -0.7, -3.5, 2.8, -0.32, -1.4, 1.35, -0.58, -5.0, 2.2, -0.85, -1.9, 4.0 };
// 렌즈 면마다 코팅이 달라 반사되는 파장이 다르므로 고스트마다 색을 다르게 준다
static const float3 GhostTints[16] =
{
    float3(1.0, 0.9, 0.8),  float3(0.5, 0.8, 1.0),  float3(1.0, 0.6, 0.3),  float3(0.6, 1.0, 0.6),
    float3(0.9, 0.5, 1.0),  float3(1.0, 1.0, 0.6),  float3(0.4, 0.7, 1.0),  float3(1.0, 0.5, 0.5),
    float3(0.7, 1.0, 0.9),  float3(1.0, 0.8, 0.4),  float3(0.6, 0.6, 1.0),  float3(1.0, 0.7, 0.9),
    float3(0.5, 1.0, 0.8),  float3(1.0, 0.85, 0.6), float3(0.8, 0.6, 1.0),  float3(0.9, 1.0, 0.7)
};

float3 SampleChromatic(float2 UV, float2 Dir)
{
    return float3(BrightTexture.Sample(LinearClamp, UV - Dir * ChromaticDistortion).r,
            BrightTexture.Sample(LinearClamp, UV).g,
            BrightTexture.Sample(LinearClamp, UV + Dir * ChromaticDistortion).b);
}

float4 mainPS(VSOutput Input) : SV_Target
{
    float2 Size;
    BrightTexture.GetDimensions(Size.x, Size.y);
    float2 UV = Input.Position.xy / Size;

    // Dispersal 기본값 0.25에서 표의 배율이 그대로 쓰이도록 맞춘다. 클수록 고스트가 바깥으로 퍼지고 커진다
    const float Spread = max(GhostDispersal * 4.0f, 0.05f);

    const float Aspect = Size.x / Size.y;
    float2 ToCenter = (0.5f - UV) * float2(Aspect, 1);
    float2 HaloDir = ToCenter / max(length(ToCenter), 1e-4);
    float2 ChromaDir = HaloDir / float2(Aspect, 1); // UV 공간으로 되돌린 방향
    
    float3 Result = 0;
    const int Count = min((int)GhostCount, 16);
    for (int i = 0; i < Count; ++i)
    {
        // P에 찍힐 고스트의 광원은 중앙에서 Scale배 떨어진 곳에 있다 (gather)
        const float Scale = GhostScales[i] / Spread;
        float2 SampleUV = 0.5f + (UV - 0.5f) * Scale;
        // 고스트는 화면에 1/|Scale|배로 커져 찍히므로, 읽는 쪽 오프셋에 |Scale|을 곱해 화면상 벌어짐을 할로와 같게 맞춘다
        float3 SampleColor = SampleChromatic(SampleUV, ChromaDir * abs(Scale));
        float EdgeWeight = pow(1 - saturate(length(0.5 - SampleUV) / 0.7071), 2);
        // 화면 밖을 읽으면 Clamp가 테두리 픽셀을 늘려 돌려줘서 고스트에 꼬리가 생기므로, 테두리 근처에서 0으로 줄인다
        float2 Inside = saturate(min(SampleUV, 1.0f - SampleUV) * 20.0f);
        EdgeWeight *= Inside.x * Inside.y;
        // 1/Scale배 커진 고스트는 같은 빛이 넓이 1/Scale²에 퍼지므로 그만큼 어두워진다
        float Energy = saturate(Scale * Scale);
        Result += SampleColor * GhostTints[i] * EdgeWeight * Energy;
    }

    // Halo — 중앙 방향으로 HaloWidth만큼 옮긴 곳을 읽어 링 가중치를 곱해 Result에 더한다

    float2 HaloUV = UV + HaloDir * HaloWidth / float2(Aspect, 1); // UV로 되돌린다

    float3 HaloColor = SampleChromatic(HaloUV, ChromaDir);
    float HaloW = pow(1 - saturate(length(0.5f - HaloUV) / 0.7071), 5);
    float2 HaloInside = saturate(min(HaloUV, 1.0f - HaloUV) * 20.0f);
    HaloW *= HaloInside.x * HaloInside.y; // 화면 밖 Clamp 꼬리 제거

    Result += HaloColor * HaloW * HaloIntensity;
    
    return float4(Result, 1);
}
