#include "lighting.hlsli"

/**
 * deferredlighting.hlsl — G버퍼를 읽어 씬의 모든 라이트로 셰이딩하는 풀스크린 패스.
 *
 * 조명 식은 포워드와 **같은 함수**(lighting.hlsli 의 swShadeLights)를 부른다. 두 경로가 각자
 * 식을 들고 있으면 반드시 갈라진다("경로마다 다른 그림").
 *
 * 월드 위치는 깊이에서 복원한다(swComputeWorldPositionFromDepth). G버퍼에 위치를 저장하지 않는 이유는
 * 첨부 하나를 통째로 아끼기 때문이고, 언리얼도 같은 선택을 한다.
 */

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
};

PSInput VSMain(SwVertexInput input, uint vertexId : SV_VertexID)
{
	PSInput output;
	input.position = input.position;
	float2 clipPosition = float2((vertexId == 1) ? 3.0f : -1.0f, (vertexId == 2) ? 3.0f : -1.0f);
	output.position = float4(clipPosition, 0.0f, 1.0f);
	output.uv       = clipPosition * float2(0.5f, -0.5f) + 0.5f;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	const float4 albedoSample = swSampleAlbedo(input.uv);
	float3 albedo = albedoSample.rgb;
	float3 normal = normalize(swSampleNormal(input.uv).xyz * 2.0f - 1.0f);
	if (length(normal) < 0.1f)
		normal = float3(0.0f, 0.85f, 0.5f);

	float  depth         = swSampleDepth(input.uv).r;
	float3 worldPosition = swComputeWorldPositionFromDepth(input.uv, depth);

	// 그림자는 월드 위치를 **라이트 클립 공간으로 투영해** 읽는다(화면 UV 로 샘플하면 그늘이 물체를 따라오지 않고 화면에 붙는다).
	float  shadow = swSampleShadowAtWorld(worldPosition, normal);
	float3 lit    = swShadeLights(albedo, worldPosition, normal, shadow);

	// 아무것도 안 그린 곳(깊이 원경)은 **쓰지 않고 버린다**. G버퍼가 비어 있어 셰이딩할 표면이
	// 없고, 그대로 계산하면 검은 알베도에 림 라이트만 남아 배경이 이상한 색이 된다.
	// discard 하면 LitColor 는 자기 클리어 색 그대로 남는다 — 포워드의 SceneColor 와 같은 배경이다.
	if (depth >= 1.0f)
		discard;
	// 알베도 알파는 G버퍼 패스가 적은 셰이딩 모델이다(binding.hlsli SW_GBUFFER_SHADING) — 보기 모드 Unlit 의 표면은 알베도 그대로.
	if (albedoSample.a < 0.5f * (SW_GBUFFER_SHADING_LIT + SW_GBUFFER_SHADING_UNLIT))
		return float4(albedo, 1.0f);

	float rim = pow(1.0f - saturate(dot(normal, float3(0, 0, 1))), 3.0f) * 0.12f;
	lit += rim * g_KeyLightColor.rgb;
	return float4(lit, 1.0f);
}
