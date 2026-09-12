#include "lighting.hlsli"

/**
 * deferredlighting.hlsl — G버퍼를 읽어 씬의 모든 라이트로 셰이딩하는 풀스크린 패스.
 *
 * 조명 식은 포워드와 **같은 함수**(lighting.hlsli 의 SwShadeLights)를 부른다. 두 경로가 각자
 * 식을 들고 있으면 반드시 갈라진다 — 이 저장소에서 "경로마다 다른 그림"이 가장 비싼 버그였다.
 *
 * 월드 위치는 깊이에서 복원한다(SwWorldPositionFromDepth). G버퍼에 위치를 굽지 않는 이유는
 * 첨부 하나를 통째로 아끼기 때문이고, 언리얼도 같은 선택을 한다.
 */

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
	float3 albedo = SampleAlbedo(input.uv).rgb;
	float3 normal = normalize(SampleNormal(input.uv).xyz * 2.0f - 1.0f);
	if (length(normal) < 0.1f)
		normal = float3(0.0f, 0.85f, 0.5f);

	float  depth    = SampleDepth(input.uv).r;
	float3 worldPos = SwWorldPositionFromDepth(input.uv, depth);

	// 그림자는 월드 위치를 **라이트 클립 공간으로 투영해** 읽는다. 예전에는 그림자 맵을 화면 UV로
	// 그냥 샘플하고 있었다 — 카메라를 움직이면 그늘이 물체를 따라오지 않고 화면에 붙어 있었다.
	float  shadow = SwSampleShadowAtWorld(worldPos);
	float3 lit    = SwShadeLights(albedo, worldPos, normal, shadow);

	// 아무것도 안 그린 곳(깊이 원경)은 **쓰지 않고 버린다**. G버퍼가 비어 있어 셰이딩할 표면이
	// 없고, 그대로 계산하면 검은 알베도에 림 라이트만 남아 배경이 이상한 색이 된다.
	// discard 하면 LitColor 는 자기 클리어 색 그대로 남는다 — 포워드의 SceneColor 와 같은 배경이다.
	if (depth >= 1.0f)
		discard;

	float rim = pow(1.0f - saturate(dot(normal, float3(0, 0, 1))), 3.0f) * 0.12f;
	lit += rim * g_KeyLightColor.rgb;
	return float4(lit, 1.0f);
}
