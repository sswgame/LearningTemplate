#include "binding.hlsli"

struct PSInput
{
	float4 pos : SV_POSITION;
	float3 nrm : TEXCOORD0;
};

PSInput VSMain(SwVertexInput input)
{
	PSInput output;
	float4x4 world = SwLoadInstanceWorld(input.instanceSlot);
	float4 worldPos = mul(float4(input.pos, 1.0f), world);
	output.pos = mul(worldPos, g_ViewProj);
	output.nrm = normalize(mul(float4(input.nrm, 0.0f), world).xyz);
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 nEnc = saturate(normalize(input.nrm) * 0.5f + 0.5f);
	return float4(nEnc, 1.0f);
}
