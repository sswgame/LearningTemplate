#include "binding.hlsli"

struct VSInput
{
	float3 pos : POSITION;
	float4 col : COLOR;
};

struct PSInput
{
	float4 pos : SV_POSITION;
};

PSInput VSMain(VSInput input, uint iid : SV_InstanceID)
{
	PSInput output;
	float4 worldPos = mul(float4(input.pos, 1.0f), SwLoadInstanceWorld(iid));
	output.pos = mul(worldPos, g_LightViewProj);
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(input.pos.z, 0, 0, 1);
}
