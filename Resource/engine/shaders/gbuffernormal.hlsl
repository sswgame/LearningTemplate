#include "bindless.hlsli"

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

PSInput VSMain(VSInput input)
{
	PSInput output;
	PassCBData passCb = GetPassCB();
	float4 worldPos = mul(float4(input.pos, 1.0f), passCb.g_World);
	output.pos = mul(worldPos, passCb.g_ViewProj);
	float3 n = DemoCubeNormal(input.pos);
	output.nrm = normalize(mul(float4(n, 0.0f), passCb.g_World).xyz);
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 nEnc = saturate(normalize(input.nrm) * 0.5f + 0.5f);
	return float4(nEnc, 1.0f);
}
