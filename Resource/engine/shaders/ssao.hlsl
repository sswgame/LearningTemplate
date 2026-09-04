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
	float depth = SampleDepth(input.uv).r;
	float3 n = normalize(SampleNormal(input.uv).xyz * 2.0f - 1.0f);
	float2 texel = g_OutlineParams.yz;
	float ao = 0.0f;
	const int kSamples = 4;
	float2 offsets[4] = {
		float2(-texel.x, 0), float2(texel.x, 0), float2(0, -texel.y), float2(0, texel.y)
	};
	[unroll]
	for (int i = 0; i < kSamples; ++i)
	{
		float sampleDepth = SampleDepth(input.uv + offsets[i] * 2.0f).r;
		float3 sampleN = normalize(SampleNormal(input.uv + offsets[i] * 2.0f).xyz * 2.0f - 1.0f);
		float diff = saturate((sampleDepth - depth) * 40.0f);
		float ndot = saturate(dot(n, sampleN));
		ao += (1.0f - diff) * (0.5f + 0.5f * ndot);
	}
	ao = saturate(ao / kSamples);
	return float4(ao, ao, ao, 1.0f);
}
