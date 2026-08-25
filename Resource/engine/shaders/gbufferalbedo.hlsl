#include "bindless.hlsli"

struct VSInput
{
	float3 pos : POSITION;
	float4 col : COLOR;
};

struct PSInput
{
	float4 pos : SV_POSITION;
	float4 col : COLOR;
};

PSInput VSMain(VSInput input)
{
	PSInput output;
	PassCBData passCb = GetPassCB();
	float4 worldPos = mul(float4(input.pos, 1.0f), passCb.g_World);
	output.pos = mul(worldPos, passCb.g_ViewProj);
	output.col = input.col;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(input.col.rgb, 1.0f);
}
