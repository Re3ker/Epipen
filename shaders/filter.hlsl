Texture2D<float4> PreviousHistory : register(t0);
Texture2D<float4> CapturedFrame : register(t1);
Texture2D<float4> DisplayHistory : register(t3);
RWTexture2D<float4> NextHistory : register(u0);

cbuffer FilterConstants : register(b0)
{
    uint2 ImageSize;
    float DeltaSeconds;
    float ChangeRate;
    float BlackFloor;
    float WhiteCeiling;
    uint TemporalEnabled;
    uint HistoryInitialized;
    uint4 Padding;
};

float SrgbToLinear(float value)
{
    value = saturate(value);
    return value <= 0.04045f ? value / 12.92f : pow((value + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float value)
{
    value = saturate(value);
    return value <= 0.0031308f ? value * 12.92f : 1.055f * pow(value, 1.0f / 2.4f) - 0.055f;
}

[numthreads(8, 8, 1)]
void FilterCS(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= ImageSize.x || dispatchId.y >= ImageSize.y)
        return;

    // DXGI_FORMAT_B8G8R8A8_UNORM is exposed in logical RGBA component order to HLSL.
    const float3 capturedSrgb = CapturedFrame.Load(int3(dispatchId.xy, 0)).rgb;
    const float3 sourceLinear = float3(SrgbToLinear(capturedSrgb.r),
                                       SrgbToLinear(capturedSrgb.g),
                                       SrgbToLinear(capturedSrgb.b));
    const float floorValue = SrgbToLinear(BlackFloor);
    const float ceilingValue = SrgbToLinear(WhiteCeiling);
    const float3 target = floorValue + (ceilingValue - floorValue) * sourceLinear;
    // Clamp history first so tighter limits take effect in the very next output frame.
    const float3 previous = clamp(PreviousHistory.Load(int3(dispatchId.xy, 0)).rgb,
                                  floorValue, ceilingValue);

    const float3 delta = target - previous;
    const float magnitude = max(abs(delta.r), max(abs(delta.g), abs(delta.b)));
    const float budget = ChangeRate * clamp(DeltaSeconds, 0.0f, 1.0f / 30.0f);
    const float factor = (TemporalEnabled == 0 || magnitude <= 1e-7f)
                       ? 1.0f : min(1.0f, budget / magnitude);
    NextHistory[dispatchId.xy] = float4(previous + factor * delta, 1.0f);
}

struct VertexOutput
{
    float4 position : SV_Position;
};

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    const float2 position = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(position * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    const float3 linearValue = DisplayHistory.Load(int3(pixel, 0)).rgb;
    return float4(LinearToSrgb(linearValue.r), LinearToSrgb(linearValue.g), LinearToSrgb(linearValue.b), 1.0f);
}
