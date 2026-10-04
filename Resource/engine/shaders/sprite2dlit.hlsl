#include "lighting2d.hlsli"

/**
 * sprite2dlit.hlsl — 2D 빛을 받는 스프라이트(유니티 Sprite-Lit-Default · Godot CanvasItem + Light2D).
 * 정점 단계는 sprite2d.hlsl 과 같다(인스턴스의 아틀라스 프레임 · 색 · 픽셀 스냅). 픽셀 단계는 알베도에 2D 빛(lighting2d.hlsli)을 곱하고,
 * 노멀 맵이 있으면 N·L 을 곱한다. 노멀 맵은 탄젠트 공간(빨강 = 스프라이트의 +X, 초록 = +Y, 파랑 = 카메라 쪽)이다.
 */

struct PSInput
{
	float4 position                    : SV_POSITION;
	float4 color                       : COLOR;
	float2 uv                          : TEXCOORD0;
	nointerpolation uint materialIndex : TEXCOORD1;
	float2 worldPosition               : TEXCOORD2;
	float2 tangent                     : TEXCOORD3; // 스프라이트 +X 의 월드 방향(XY) — 노멀 맵 빨강
	float2 bitangent                   : TEXCOORD4; // 스프라이트 +Y 의 월드 방향(XY) — 노멀 맵 초록
};

SW_MATERIAL_BEGIN
{
	float4 color;
	float4 uvRect;
	uint albedoMap;
	uint normalMap;
	uint pointFilter;
}
SW_MATERIAL_END

PSInput VSMain(SwVertexInput input)
{
	PSInput output;

	SwInstanceData instance = swLoadInstance(input.instanceSlot);
	float4 worldPosition = mul(float4(input.position, 1.0f), instance.world);
	// 픽셀 스냅 — sprite2d.hlsl 과 같다(인스턴스 원점을 자산 픽셀 격자에).
	if (instance.pixelSnap > 0.0f)
	{
		const float2 origin  = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), instance.world).xy;
		const float2 snapped = floor(origin / instance.pixelSnap + 0.5f) * instance.pixelSnap;
		worldPosition.xy += snapped - origin;
	}
	output.position = mul(worldPosition, g_ViewProj);

	const float4 frame = swComputeInstanceUvRect(instance);
	output.uv            = input.uv * frame.zw + frame.xy;
	output.color         = input.color * swComputeInstanceTint(instance);
	output.materialIndex = instance.materialIndex;
	output.worldPosition = worldPosition.xy;
	const float2 axisX   = mul(float4(1.0f, 0.0f, 0.0f, 0.0f), instance.world).xy;
	const float2 axisY   = mul(float4(0.0f, 1.0f, 0.0f, 0.0f), instance.world).xy;
	output.tangent       = axisX / max(length(axisX), 1e-6f);
	output.bitangent     = axisY / max(length(axisY), 1e-6f);
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	SwMaterialData material = SW_MATERIAL(input.materialIndex);
	const float2 uv = input.uv * material.uvRect.zw + material.uvRect.xy;
	float4 textureColor = float4(1, 1, 1, 1);
	if (material.albedoMap != kInvalidIndex)
		textureColor = (material.pointFilter != 0u) ? swSampleMaterialTexturePoint(material.albedoMap, uv) : swSampleMaterialTexture(material.albedoMap, uv);
	const float4 albedo = textureColor * material.color * input.color;

	float3 normal     = float3(0.0f, 0.0f, -1.0f);
	const bool bHasNormal = material.normalMap != kInvalidIndex;
	if (bHasNormal)
	{
		const float3 encoded = (material.pointFilter != 0u) ? swSampleMaterialTexturePoint(material.normalMap, uv).xyz : swSampleMaterialTexture(material.normalMap, uv).xyz;
		const float3 tangentNormal = encoded * 2.0f - 1.0f;
		normal = normalize(float3(input.tangent * tangentNormal.x + input.bitangent * tangentNormal.y, -tangentNormal.z));
	}

	const float3 lit = swShadeLights2d(albedo.rgb, input.worldPosition, normal, bHasNormal);
	return float4(lit, albedo.a);
}
