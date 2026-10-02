#include "binding.hlsli"

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color    : COLOR;
};

PSInput VSMain(SwVertexInput input)
{
	PSInput output;
	float4 worldPosition = mul(float4(input.position, 1.0f), swLoadInstanceWorld(input.instanceSlot));
	output.position = mul(worldPosition, g_ViewProj);
	output.color = input.color;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(input.color.rgb, 1.0f);
}
