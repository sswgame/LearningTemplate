#include "binding.hlsli"

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color    : COLOR;
	float3 normal   : TEXCOORD0;
	SW_APPEARANCE_VARYING // 등장 디더 변형만 등장 비율을 넘긴다(binding.hlsli 8-1)
};

// 머티리얼 셰이더가 없는 배치의 표면 — 거친 유전체다(terrain · foliage 와 같은 값).
static const float kSurfaceRoughness = 1.0f;
static const float kSurfaceMetallic  = 0.0f;

struct PSOutput
{
	float4 albedo   : SV_TARGET0;
	float4 normal   : SV_TARGET1;
	float4 material : SV_TARGET2;
};

PSInput VSMain(SwVertexInput input, uint vertexId : SV_VertexID)
{
	PSInput output;
	SwInstanceData instance = swLoadInstance(input.instanceSlot);
	float3 localPosition;
	float3 localNormal;
	swLoadAnimatedVertex(instance, vertexId, input.position, input.normal, localPosition, localNormal);
	float4x4 world = instance.world;
	// 위치는 깊이 프리패스와 **같은 함수**로 만든다(binding.hlsli swComputeWorldPosition).
	float4 worldPosition = swComputeWorldPosition(localPosition, world);
	output.position = swComputeClipPosition(worldPosition, g_ViewProj);
	output.color = input.color;
	// 노멀은 여인수 행렬로 옮긴다(binding.hlsli swComputeWorldNormal) — 월드 행렬을 곱하면 비균등 스케일에서 기운다.
	output.normal = swComputeWorldNormal(localNormal, world);
	SW_APPEARANCE_WRITE( output, instance )
	return output;
}

PSOutput PSMain(PSInput input)
{
	SW_APPEARANCE_CLIP( input )
	PSOutput output;
	float3 encodedNormal = saturate(normalize(input.normal) * 0.5f + 0.5f);
	// 알파는 셰이딩 모델(binding.hlsli SW_GBUFFER_SHADING — 보기 모드 Unlit 이면 조명 없이)이다.
	output.albedo = float4(input.color.rgb, SW_GBUFFER_SHADING);
	output.normal = float4(encodedNormal, 1.0f);
	output.material = swEncodeGbufferMaterial(kSurfaceRoughness, kSurfaceMetallic);
	return output;
}
