#include "binding.hlsli"

struct VSInput
{
	float3 pos : POSITION;
	float4 col : COLOR;
};

struct PSInput
{
	float4 pos : SV_POSITION;
	float3 nrm : TEXCOORD0;
};

PSInput VSMain(VSInput input, uint iid : SV_InstanceID)
{
	PSInput output;
	float4x4 world = SwLoadInstanceWorld(iid);
	float4 worldPos = mul(float4(input.pos, 1.0f), world);
	output.pos = mul(worldPos, g_ViewProj);
	float3 n = DemoCubeNormal(input.pos);
	output.nrm = normalize(mul(float4(n, 0.0f), world).xyz);
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 nEnc = saturate(normalize(input.nrm) * 0.5f + 0.5f);
	return float4(nEnc, 1.0f);
}
