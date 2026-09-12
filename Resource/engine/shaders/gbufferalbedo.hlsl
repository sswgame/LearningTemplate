#include "binding.hlsli"

struct PSInput
{
	float4 pos : SV_POSITION;
	float4 col : COLOR;
};

PSInput VSMain(SwVertexInput input, uint iid : SV_InstanceID)
{
	PSInput output;
	float4 worldPos = mul(float4(input.pos, 1.0f), SwLoadInstanceWorld(iid));
	output.pos = mul(worldPos, g_ViewProj);
	output.col = input.col;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(input.col.rgb, 1.0f);
}
