#include "bindless.hlsli"

struct VSInput
{
	float3 pos : POSITION;
	float4 col : COLOR;
};

struct PSInput
{
	float4 pos : SV_POSITION;
	float2 uv  : TEXCOORD0;
};

PSInput VSMain(VSInput input, uint vid : SV_VertexID)
{
	PSInput output;
	input.pos = input.pos;
	float2 p = float2((vid == 1) ? 3.0f : -1.0f, (vid == 2) ? 3.0f : -1.0f);
	output.pos = float4(p, 0.0f, 1.0f);
	output.uv  = p * float2(0.5f, -0.5f) + 0.5f;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	PassCBData passCb = GetPassCB();
	float2 texel = passCb.g_OutlineParams.yz;
	float  d0 = SampleSourceDepth(input.uv).r;
	float  d1 = SampleSourceDepth(input.uv + float2(texel.x, 0)).r;
	float  d2 = SampleSourceDepth(input.uv + float2(0, texel.y)).r;
	float  d3 = SampleSourceDepth(input.uv + float2(-texel.x, 0)).r;
	float  d4 = SampleSourceDepth(input.uv + float2(0, -texel.y)).r;
	float  edge = saturate((abs(d0 - d1) + abs(d0 - d2) + abs(d0 - d3) + abs(d0 - d4)) * 4.0f - passCb.g_OutlineParams.x);
	edge *= passCb.g_OutlineColor.a;

	float3 color = SampleSource(input.uv).rgb;
	return float4(lerp(color, passCb.g_OutlineColor.rgb, edge), 1.0f);
}
