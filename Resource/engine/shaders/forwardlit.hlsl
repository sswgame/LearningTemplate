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
	float2 uv  : TEXCOORD0;
	float3 nrm : TEXCOORD1;
};

PSInput VSMain(VSInput input)
{
	PSInput output;
	PassCBData passCb = GetPassCB();
	float4 worldPos = mul(float4(input.pos, 1.0f), passCb.g_World);
	output.pos = mul(worldPos, passCb.g_ViewProj);
	output.col = input.col;
	output.uv  = input.pos.xy * float2(0.5f, -0.5f) + 0.5f;
	float3 n = DemoCubeNormal(input.pos);
	output.nrm = normalize(mul(float4(n, 0.0f), passCb.g_World).xyz);
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	PassCBData passCb = GetPassCB();
	float3 N = normalize(input.nrm);
	float3 L = normalize(-passCb.g_KeyLightDirIntensity.xyz);
	float  ndotl = saturate(dot(N, L));

	float2 shadowUV = saturate(input.uv + passCb.g_ShadowParams.zw);
	float  shadowSample = SampleShadow(shadowUV).r;
	float  shadow = lerp(1.0f - passCb.g_ShadowParams.y, 1.0f, saturate(shadowSample + passCb.g_ShadowParams.x));

	float3 ambient = passCb.g_KeyLightColor.rgb * passCb.g_KeyLightColor.a;
	float3 lit = input.col.rgb * (ambient + ndotl * passCb.g_KeyLightDirIntensity.w * passCb.g_KeyLightColor.rgb) * shadow;
	float rim = pow(1.0f - saturate(dot(N, float3(0, 0, 1))), 2.0f) * 0.15f;
	lit += rim * passCb.g_KeyLightColor.rgb;
	return float4(lit, input.col.a);
}
