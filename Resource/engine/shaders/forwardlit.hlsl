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
	float2 uv  : TEXCOORD0;
	float3 nrm : TEXCOORD1;
};

// defaultmaterial.material 의 프로퍼티 순서·타입과 1:1 (color/roughness/albedoMap). 머티리얼 패커는
// HLSL 규칙으로 순차 패킹하므로 여기 순서를 바꾸면 에셋도 같이 바꿔야 한다.
SW_DECLARE_CBUFFER( MaterialCB, 1 )
{
	float4 color;
	float  roughness;
	uint   albedoMap;
};

PSInput VSMain(VSInput input, uint iid : SV_InstanceID)
{
	PSInput output;
	float4x4 world = SwLoadInstanceWorld(iid);
	float4 worldPos = mul(float4(input.pos, 1.0f), world);
	output.pos = mul(worldPos, g_ViewProj);
	output.col = input.col;
	output.uv  = input.pos.xy * float2(0.5f, -0.5f) + 0.5f;
	float3 n = DemoCubeNormal(input.pos);
	output.nrm = normalize(mul(float4(n, 0.0f), world).xyz);
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 N = normalize(input.nrm);
	float3 L = normalize(-g_KeyLightDirIntensity.xyz);
	float  ndotl = saturate(dot(N, L));

	float2 shadowUV = saturate(input.uv + g_ShadowParams.zw);
	float  shadowSample = SampleShadow(shadowUV).r;
	float  shadow = lerp(1.0f - g_ShadowParams.y, 1.0f, saturate(shadowSample + g_ShadowParams.x));

	// 정점 색 x 머티리얼 색 x 알베도 텍스처. 텍스처 인덱스는 MaterialCB 의 uint 슬롯으로 온다.
	// 인덱스가 없거나(SW_INVALID_INDEX) 백엔드가 bindless 에뮬(DX11/GL)이면 흰색이 곱해진다 —
	// 자세한 이유는 binding.hlsli 의 SW_SampleMaterialTexture 주석 참고.
	float4 albedo = input.col * color * SW_SampleMaterialTexture(albedoMap, input.uv);

	float3 ambient = g_KeyLightColor.rgb * g_KeyLightColor.a;
	float3 lit = albedo.rgb * (ambient + ndotl * g_KeyLightDirIntensity.w * g_KeyLightColor.rgb) * shadow;
	float rim = pow(1.0f - saturate(dot(N, float3(0, 0, 1))), 2.0f) * 0.15f;
	lit += rim * g_KeyLightColor.rgb;
	return float4(lit, albedo.a);
}
