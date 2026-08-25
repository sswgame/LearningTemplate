#include "common.hlsli"

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

SW_DECLARE_CBUFFER( MaterialCB, 0 )
{
	float4 g_MaterialColor;
};

PSInput VSMain(VSInput input)
{
	PSInput output;
	output.pos = float4(input.pos, 1.0f);
	output.col = input.col;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return input.col * g_MaterialColor;
}
