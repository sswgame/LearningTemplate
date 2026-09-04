#include "binding.hlsli"

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
	float2 texel = g_OutlineParams.yz;
	float3 c = SampleSource(input.uv).rgb;
	float3 blur = 0;
	blur += SampleSource(input.uv + float2(-texel.x, 0)).rgb;
	blur += SampleSource(input.uv + float2(texel.x, 0)).rgb;
	blur += SampleSource(input.uv + float2(0, -texel.y)).rgb;
	blur += SampleSource(input.uv + float2(0, texel.y)).rgb;
	blur *= 0.25f;

	float lum = max(max(blur.r, blur.g), blur.b);
	float soft = saturate((lum - g_BloomParams.x + g_BloomParams.z) / max(g_BloomParams.z, 1e-4));
	float3 bright = blur * soft * soft * g_BloomParams.y;
	return float4(c + bright, 1.0f);
}
