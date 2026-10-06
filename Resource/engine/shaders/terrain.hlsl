#include "lighting.hlsli"

/**
 * terrain.hlsl — 지형(TerrainComponent). 스플랫 맵(RGBA = 레이어 0..3 가중치)으로 레이어 넷을 섞는다.
 *
 * 레이어 하나 = 색(layerNColor) × 디테일 맵의 자기 채널(타일링 무늬, 0.5 가 중립). 디테일은 위에서 내려 찍거나(평면), 세 축에서 찍어
 * 노멀로 섞는다(3 축 — 절벽에서 늘어지지 않는다, layerTriplanar 가 레이어마다 고른다). 가파른 면은 절벽 레이어가 덮는다(terrainParams.zw).
 * 가중치 합을 1 로 나누는 식은 CPU `TerrainHeightfield::normalizeWeights` 와 같다 — 배치의 레이어 필터가 그려진 것과 같은 값을 본다
 * (절벽 덮기는 그림에만 있다 — 배치는 경사 필터로 가린다).
 * 정점 UV 는 지형 정규 좌표(0..1)다. 스플랫 텍셀 중심을 지형 가장자리에 맞추려고 너비로 반 텍셀 옮긴다(CPU 와 같은 규칙).
 */

struct PSInput
{
	float4 position                    : SV_POSITION;
	float2 uv                          : TEXCOORD0;
	float3 normal                      : TEXCOORD1;
	nointerpolation uint materialIndex : TEXCOORD2;
	float3 worldPosition               : TEXCOORD3;
};

// terrain.material 의 프로퍼티와 같은 이름 · 타입이다.
SW_MATERIAL_BEGIN
{
	float4 layer0Color;
	float4 layer1Color;
	float4 layer2Color;
	float4 layer3Color;
	float4 layerDetail;    // 레이어마다 디테일 세기(0..1)
	float4 layerTriplanar; // 레이어마다 1 이면 3 축 투영
	float4 terrainParams;  // x = 스플랫 너비(텍셀), y = 디테일 반복(1/m), z = 절벽 문턱(노멀 y), w = 절벽 레이어(−1 = 끔)
	uint   splatMap;
	uint   detailMap;
}
SW_MATERIAL_END

PSInput VSMain( SwVertexInput input )
{
	PSInput        output;
	SwInstanceData instance = swLoadInstance( input.instanceSlot );
	float4 worldPosition    = swComputeWorldPosition( input.position, instance.world );
	output.position         = swComputeClipPosition( worldPosition, g_ViewProj );
	output.worldPosition    = worldPosition.xyz;
	output.uv               = input.uv;
	output.normal           = swComputeWorldNormal( input.normal, instance.world );
	output.materialIndex    = instance.materialIndex;
	return output;
}

/** @brief 가중치 넷을 합 1 로 나눈다. 합이 0 이면 레이어 0 이다(C++ TerrainHeightfield::normalizeWeights 와 같다). */
float4 normalizeWeights( float4 weight )
{
	const float total = weight.x + weight.y + weight.z + weight.w;
	return ( total <= 1e-5f ) ? float4( 1.0f, 0.0f, 0.0f, 0.0f ) : weight / total;
}

SW_SURFACE_OUTPUT PSMain( PSInput input )
{
	const float3   normal   = normalize( input.normal );
	SwMaterialData material = SW_MATERIAL( input.materialIndex );

	// 스플랫 — 텍셀 중심을 지형 가장자리에 맞춘다.
	const float  splatWidth = material.terrainParams.x;
	const float2 splatUv    = ( splatWidth > 1.0f ) ? input.uv * ( ( splatWidth - 1.0f ) / splatWidth ) + 0.5f / splatWidth : input.uv;
	float4 weight = ( material.splatMap == kInvalidIndex ) ? float4( 1.0f, 0.0f, 0.0f, 0.0f ) : swSampleMaterialTexture( material.splatMap, splatUv );
	weight        = normalizeWeights( weight );

	// 절벽 — 노멀 y 가 문턱 아래로 내려갈수록 절벽 레이어가 덮는다.
	const float cliffLayer = material.terrainParams.w;
	if ( cliffLayer >= 0.0f )
	{
		const float  cliff   = saturate( ( material.terrainParams.z - normal.y ) / 0.12f );
		const float4 onlyCliff = float4( cliffLayer < 0.5f ? 1.0f : 0.0f, ( cliffLayer >= 0.5f && cliffLayer < 1.5f ) ? 1.0f : 0.0f,
		                                 ( cliffLayer >= 1.5f && cliffLayer < 2.5f ) ? 1.0f : 0.0f, cliffLayer >= 2.5f ? 1.0f : 0.0f );
		weight = lerp( weight, onlyCliff, cliff );
	}

	// 디테일 — 평면(위에서) 하나와 3 축 셋. 평면 표본은 3 축의 xz 면과 같다.
	const float  tiling      = material.terrainParams.y;
	const float4 detailTop   = swSampleMaterialTexture( material.detailMap, input.worldPosition.xz * tiling );
	const float4 detailFront = swSampleMaterialTexture( material.detailMap, input.worldPosition.xy * tiling );
	const float4 detailSide  = swSampleMaterialTexture( material.detailMap, input.worldPosition.zy * tiling );
	float3       axisWeight  = pow( abs( normal ), 4.0f );
	axisWeight /= max( axisWeight.x + axisWeight.y + axisWeight.z, 1e-5f );
	const float4 detailTriplanar = detailSide * axisWeight.x + detailTop * axisWeight.y + detailFront * axisWeight.z;
	const float4 detail          = lerp( detailTop, detailTriplanar, material.layerTriplanar );
	// 0.5 가 중립 — 디테일 세기만큼 색을 0..2 배로 흔든다.
	const float4 shade = lerp( float4( 1.0f, 1.0f, 1.0f, 1.0f ), detail * 2.0f, material.layerDetail );

	float3 albedo = material.layer0Color.rgb * shade.x * weight.x + material.layer1Color.rgb * shade.y * weight.y + material.layer2Color.rgb * shade.z * weight.z +
	                material.layer3Color.rgb * shade.w * weight.w;
	// 큰 무늬 — 같은 디테일 맵을 훨씬 넓게 찍어 타일 반복을 깬다.
	const float macro = swSampleMaterialTexture( material.detailMap, input.worldPosition.xz * tiling * 0.071f ).x;
	albedo *= lerp( 0.85f, 1.12f, macro );

#if defined( SW_PASS_GBUFFER )
	return swStoreSurface( float4( 0.0f, 0.0f, 0.0f, 0.0f ), float4( albedo, 1.0f ), normal );
#elif SW_VIEWMODE_SKIPS_LIGHTING
	return swStoreSurface( float4( albedo, 1.0f ), float4( albedo, 1.0f ), normal );
#else
	const float  shadow = swSampleShadowAtWorld( input.worldPosition, normal );
	const float3 lit    = swShadeLights( albedo, input.worldPosition, normal, shadow );
	return swStoreSurface( float4( lit, 1.0f ), float4( albedo, 1.0f ), normal );
#endif
}
