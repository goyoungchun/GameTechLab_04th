// 반해상도 (색, CoC) 텍스처에서 CoC 반경만큼 원판으로 샘플을 모아 흐린 색을 만든다.
cbuffer DepthOfFieldGatherConstants : register(b0)
{
    float BladeCount; // 조리개 날 수 (보케 다각형 꼭짓점 수)
    float3 GatherPadding;
};

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

struct PSOutput
{
    float4 Far : SV_Target0;  // RGB=far 블러 색, A=가운데 CoC
    float4 Near : SV_Target1; // RGB=near 블러 색, A=near가 이 픽셀을 덮는 비율(0~1)
};

PSOutput mainPS(VSOutput Input)
{
    static const float MaxCoCRadiusPx = 32.f;
    static const int SampleCount = 64;

    int3 HalfPixel = int3(int2(Input.Position.xy), 0);
    float4 CenterSample = HalfResTexture.Load(HalfPixel);
    float GatherRadiusHalfPx = min(abs(CenterSample.a), MaxCoCRadiusPx) * 0.5f;

    float HalfWidth, HalfHeight;
    HalfResTexture.GetDimensions(HalfWidth, HalfHeight);
    float2 HalfTexelSize = 1.0 / float2(HalfWidth, HalfHeight);
    float2 CenterUV = Input.Position.xy * HalfTexelSize;

    // 최대 반경 원판을 한 번 훑어, 나까지 닿는 near 이웃의 CoC로 GatherRadiusHalfPx를 키운다
    const float SearchRadiusHalfPx = MaxCoCRadiusPx * 0.5f; // 탐색은 항상 최대 반경으로
    float DilatedRadiusHalfPx = GatherRadiusHalfPx; // 루프 중에 원본 반경을 건드리지 않게 따로 둔다
    for (int s = 0; s < SampleCount; ++s)
    {
        float SearchDistHalfPx = sqrt((s + 0.5) / SampleCount) * SearchRadiusHalfPx;
        float SearchAngle = s * 2.39996;
        float2 SearchOffsetHalfPx = float2(cos(SearchAngle), sin(SearchAngle)) * SearchDistHalfPx;
        float4 Neighbor = HalfResTexture.SampleLevel(LinearClampSampler, CenterUV + SearchOffsetHalfPx * HalfTexelSize, 0);

        if (Neighbor.a < 0)
        {
            float NeighborNearHalfPx = min(abs(Neighbor.a) * 0.5f, MaxCoCRadiusPx * 0.5f);
            if (SearchDistHalfPx < NeighborNearHalfPx)
                DilatedRadiusHalfPx = max(DilatedRadiusHalfPx, NeighborNearHalfPx);
        }
    }
    GatherRadiusHalfPx = DilatedRadiusHalfPx;
    
    float3 FarColorSum = 0, NearColorSum = 0;
    float FarWeightSum = 0, NearWeightSum = 0;
    float MaxNearCoCPx = 0; // near가 얼마나 흐린지: 살짝 앞인 픽셀은 덮지 않으려고 쓴다
    if (CenterSample.a < 0)
    {
        NearColorSum = CenterSample.rgb;
        NearWeightSum = 1;
        MaxNearCoCPx = -CenterSample.a;
    }
    else
    {
        FarColorSum = CenterSample.rgb;
        FarWeightSum = 1;
    }
    for (int i = 0; i < SampleCount; ++i)
    {
        float SampleDistHalfPx = sqrt((i + 0.5) / SampleCount) * GatherRadiusHalfPx; // 면적 균등
        float SampleAngle = i * 2.39996; // 황금각
        float2 SampleOffsetHalfPx = float2(cos(SampleAngle), sin(SampleAngle)) * SampleDistHalfPx;
        float4 Neighbor = HalfResTexture.SampleLevel(LinearClampSampler, CenterUV + SampleOffsetHalfPx * HalfTexelSize, 0);

        float NeighborCoCHalfPx = abs(Neighbor.a) * 0.5f;
        float ReachWeight = saturate(NeighborCoCHalfPx - SampleDistHalfPx + 1.f); // 이웃 원판이 나까지 닿는가

        // Neighbor.a 부호로 far/near를 나눠 각각 누적한다 (near는 덮는 비율도 계산)
        // 가운데 샘플과 같은 기준: CoC 0은 far 쪽
        if (Neighbor.a >= 0)
        {
            FarColorSum += Neighbor.rgb * ReachWeight;
            FarWeightSum += ReachWeight;
        }
        else
        {
            NearColorSum += Neighbor.rgb * ReachWeight;
            NearWeightSum += ReachWeight;
            if (ReachWeight > 0)
                MaxNearCoCPx = max(MaxNearCoCPx, -Neighbor.a);
        }
    }

    // Composite의 BlendRadiusPx와 같은 값: near도 충분히 흐릴 때만 원본을 덮는다
    static const float NearBlendRadiusPx = 8.f;
    float NearCoverage = saturate(NearWeightSum / SampleCount);
    float NearBlurAmount = saturate(MaxNearCoCPx / NearBlendRadiusPx);

    // far 이웃이 없으면 검은색 대신 가운데 색을 내보내, bilinear로 읽는 옆 픽셀이 어두워지지 않게 한다
    float3 FarColor = FarWeightSum > 0 ? FarColorSum / FarWeightSum : CenterSample.rgb;
    float3 NearColor = NearWeightSum > 0 ? NearColorSum / NearWeightSum : CenterSample.rgb;

    PSOutput Output;
    Output.Far = float4(FarColor, CenterSample.a);
    Output.Near = float4(NearColor, NearCoverage * NearBlurAmount);
    return Output;
}