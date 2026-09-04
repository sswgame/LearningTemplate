#include "binding.hlsli"

struct VSInput
{
	float3 pos : POSITION;
	float4 col : COLOR;
};

struct PSInput
{
	float4 pos : SV_POSITION;
	float4 col : COLOR;
	float3 nrm : TEXCOORD0;
};

struct PSOutput
{
	float4 albedo : SV_TARGET0;
	float4 normal : SV_TARGET1;
};

PSInput VSMain(VSInput input, uint iid : SV_InstanceID)
{
	PSInput output;
	float4x4 world = SwLoadInstanceWorld(iid);
	float4 worldPos = mul(float4(input.pos, 1.0f), world);
	output.pos = mul(worldPos, g_ViewProj);
	output.col = input.col;
	float3 n = DemoCubeNormal(input.pos);
	output.nrm = normalize(mul(float4(n, 0.0f), world).xyz);
	return output;
}

PSOutput PSMain(PSInput input)
{
	PSOutput output;
	float3 nEnc = saturate(normalize(input.nrm) * 0.5f + 0.5f);
	output.albedo = float4(input.col.rgb, 1.0f);
	output.normal = float4(nEnc, 1.0f);
	return output;
}
