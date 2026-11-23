// Setup 패스가 만든 부호 있는 CoC(음수 = near, 양수 = far)를 색으로 칠한다.
Texture2D<float> CoCTexture : register(t0);
Texture2D SceneColorTexture : register(t1);

// CoC는 픽셀 반경이라, 블러가 허용할 최대 반경으로 나눠야 near/far 모두 0~1 색으로 구별된다
static const float MaxCoCRadiusPx = 32.f;

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
    int3 PixelPosition = int3(int2(Input.Position.xy), 0);
    float SignedCoC = clamp(CoCTexture.Load(PixelPosition) / MaxCoCRadiusPx, -1.f, 1.f);
    float3 SceneColor = SceneColorTexture.Load(PixelPosition).rgb;

    // 장면을 어둡게 깔아야 CoC 색과 원래 물체 색이 헷갈리지 않는다
    float Luma = dot(SceneColor, float3(0.2126, 0.7152, 0.0722));
    float3 Gray = Luma.xxx * 0.3;

    float NearAmount = saturate(-SignedCoC);
    float FarAmount = saturate(SignedCoC);
    float3 CoCColor = float3(0, NearAmount, FarAmount);

    // 많이 흐릴수록 장면이 사라지고 CoC 색만 남게 해서 초점 근처만 흑백으로 보인다
    float3 Result = lerp(Gray, CoCColor, abs(SignedCoC));
    return float4(Result, 1);
}
