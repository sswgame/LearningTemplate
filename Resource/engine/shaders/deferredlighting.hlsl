#include "bindless.hlsli"

struct VSInput
{
	float3 pos : POSITION;
	float4 col : COLOR;
};

struct PSInput
{
	float4 pos : SV_POSITION;
	float2 uv  : TEXCOORD0;
};

PSInput VSMain(VSInput input, uint vid : SV_VertexID)
{
	PSInput output;
	input.pos = input.pos;
	float2 p = float2((vid == 1) ? 3.0f : -1.0f, (vid == 2) ? 3.0f : -1.0f);
	output.pos = float4(p, 0.0f, 1.0f);
	output.uv  = p * float2(0.5f, -0.5f) + 0.5f;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	PassCBData passCb = GetPassCB();
	float3 albedo = SampleAlbedo(input.uv).rgb;
	float3 normal = normalize(SampleNormal(input.uv).xyz * 2.0f - 1.0f);
	if (length(normal) < 0.1f)
		normal = float3(0.0f, 0.85f, 0.5f);

	float depth = SampleDepth(input.uv).r;
	float shadowSample = SampleShadow(input.uv).r;
	float shadow = lerp(1.0f - passCb.g_ShadowParams.y, 1.0f, saturate(shadowSample + passCb.g_ShadowParams.x));

	float3 L = normalize(-passCb.g_KeyLightDirIntensity.xyz);
	float  ndotl = saturate(dot(normal, L));
	float3 ambient = passCb.g_KeyLightColor.rgb * passCb.g_KeyLightColor.a;
	float3 lit = albedo * (ambient + ndotl * passCb.g_KeyLightDirIntensity.w * passCb.g_KeyLightColor.rgb) * shadow;
	lit *= saturate(1.0f - depth * 0.12f);
	float rim = pow(1.0f - saturate(ndotl), 3.0f) * 0.12f;
	lit += rim * passCb.g_KeyLightColor.rgb;
	return float4(lit, 1.0f);
}
