#include "binding.hlsli"

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
};

PSInput VSMain(SwVertexInput input, uint vertexId : SV_VertexID)
{
	PSInput output;
	input.position = input.position;
	float2 clipPosition = float2((vertexId == 1) ? 3.0f : -1.0f, (vertexId == 2) ? 3.0f : -1.0f);
	output.position = float4(clipPosition, 0.0f, 1.0f);
	output.uv       = clipPosition * float2(0.5f, -0.5f) + 0.5f;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float depth = swSampleDepth(input.uv).r;
	float3 normal = normalize(swSampleNormal(input.uv).xyz * 2.0f - 1.0f);
	float2 texel = g_OutlineParams.yz;
	float occlusion = 0.0f;
	const int kSampleCount = 4;
	float2 arrOffset[4] = {
		float2(-texel.x, 0), float2(texel.x, 0), float2(0, -texel.y), float2(0, texel.y)
	};
	[unroll]
	for (int sampleIndex = 0; sampleIndex < kSampleCount; ++sampleIndex)
	{
		float sampleDepth = swSampleDepth(input.uv + arrOffset[sampleIndex] * 2.0f).r;
		float3 sampleNormal = normalize(swSampleNormal(input.uv + arrOffset[sampleIndex] * 2.0f).xyz * 2.0f - 1.0f);
		float depthDifference = saturate((sampleDepth - depth) * 40.0f);
		float normalAgreement = saturate(dot(normal, sampleNormal));
		occlusion += (1.0f - depthDifference) * (0.5f + 0.5f * normalAgreement);
	}
	occlusion = saturate(occlusion / kSampleCount);
	return float4(occlusion, occlusion, occlusion, 1.0f);
}
