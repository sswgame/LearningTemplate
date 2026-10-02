#include "binding.hlsli"

struct PSInput
{
	float4 position : SV_POSITION;
	float3 normal   : TEXCOORD0;
};

PSInput VSMain(SwVertexInput input)
{
	PSInput output;
	float4x4 world = swLoadInstanceWorld(input.instanceSlot);
	float4 worldPosition = mul(float4(input.position, 1.0f), world);
	output.position = mul(worldPosition, g_ViewProj);
	output.normal = swComputeWorldNormal(input.normal, world);
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 encodedNormal = saturate(normalize(input.normal) * 0.5f + 0.5f);
	return float4(encodedNormal, 1.0f);
}
