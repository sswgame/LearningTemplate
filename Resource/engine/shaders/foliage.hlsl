#include "lighting.hlsli"

/**
 * foliage.hlsl — 식생(FoliageComponent). GPU 인스턴스마다 바람에 흔들고, 휘게 하는 구(캐릭터)에서 밀려나고, 먼 인스턴스는 뿌리 쪽으로 줄여 사라지게 한다.
 *
 * 머티리얼은 **정점 셰이더만** 읽는다(GL 은 구조버퍼를 두 단계에서 읽으면 링크를 거절한다) — 색 · 텍스처 인덱스는 보간 칸으로 넘긴다.
 * 흔들림 무게는 로컬 높이 / swayHeight 의 제곱이라 뿌리는 땅에 붙어 있다. 인스턴스마다 위상은 인스턴스 위치의 해시라 CPU 가 따로 싣지 않는다.
 * 밝기 흔들기는 인스턴스 색 칸(swComputeInstanceTint)이다.
 * 그림자 · 깊이 프리패스는 shadowdepth.hlsl 로 그려지므로 흔들리지 않는다 — 풀(grass.material)은 MATERIAL_SHADOW_CAST_OFF 로 그림자를 끄고,
 * 흔드는 머티리얼은 MATERIAL_VERTEX_DEFORM 으로 깊이 프리패스에서 빠진다(흔들린 정점이 프리패스 깊이와 어긋나지 않게).
 */

struct PSInput
{
	float4 position                       : SV_POSITION;
	float4 color                          : COLOR;
	float2 uv                             : TEXCOORD0;
	float3 normal                         : TEXCOORD1;
	float3 worldPosition                  : TEXCOORD2;
	nointerpolation uint albedoMap        : TEXCOORD3; // 머티리얼 텍스처 인덱스(픽셀은 머티리얼을 읽지 않는다)
};

// foliage.material · grass.material 의 프로퍼티와 같은 이름 · 타입이다.
SW_MATERIAL_BEGIN
{
	float4 tint;
	float4 windParams;  // xy = 바람 방향(xz), z = 세기(m), w = 시간(초)
	float4 windWave;    // x = 흔들림 주파수(Hz), y = 돌풍 세기, z = 돌풍 주파수(Hz), w = 흔들림이 다 차는 높이(m)
	float4 viewParams;  // xyz = 카메라 위치, w = 휘게 하는 세기
	float4 fadeParams;  // x = 줄어들기 시작하는 거리, y = 다 사라지는 거리
	float4 influencer0; // xyz = 중심, w = 반지름(0 이면 빈 칸)
	float4 influencer1;
	float4 influencer2;
	float4 influencer3;
	uint   albedoMap;
}
SW_MATERIAL_END

/**
 * @brief 위치를 [0, 1) 로 섞는다 — 인스턴스마다 다른 흔들림 위상.
 * @details 정수 해시다. `frac( sin( 큰 수 ) )` 꼴은 백엔드 · 드라이버마다 sin 정밀도가 달라 같은 장면이 백엔드마다 다르게 흔들린다.
 */
float hashPosition( float2 position )
{
	const int2 cell = (int2)floor( position * 8.0f );
	uint       seed = (uint)cell.x * 73856093u ^ (uint)cell.y * 19349663u;
	seed            = ( seed ^ 61u ) ^ ( seed >> 16 );
	seed *= 9u;
	seed = seed ^ ( seed >> 4 );
	seed *= 0x27d4eb2du;
	seed = seed ^ ( seed >> 15 );
	return (float)( seed >> 8 ) * ( 1.0f / 16777216.0f );
}

/** @brief 휘게 하는 구 하나가 이 점을 밀어내는 양(xz)이다. 구 안쪽일수록 세다. */
float2 computeBend( float4 influencer, float3 worldPosition )
{
	if ( influencer.w <= 0.0f )
		return float2( 0.0f, 0.0f );
	const float2 away     = worldPosition.xz - influencer.xz;
	const float  distance = length( away );
	const float  strength = saturate( 1.0f - distance / influencer.w );
	return ( distance > 1e-4f ) ? away / distance * strength : float2( 0.0f, 0.0f );
}

PSInput VSMain( SwVertexInput input )
{
	PSInput        output;
	SwInstanceData instance = swLoadInstance( input.instanceSlot );
	SwMaterialData material = SW_MATERIAL( instance.materialIndex );

	const float3 instancePosition = instance.world[3].xyz;
	const float3 toView           = material.viewParams.xyz - instancePosition;
	const float  viewDistance     = length( toView );

	// 거리 페이드 — 페이드 구간에서 인스턴스를 뿌리(로컬 원점) 쪽으로 줄인다. 끝을 넘으면 점 하나가 되어 그려지지 않는다.
	const float fadeRange = max( material.fadeParams.y - material.fadeParams.x, 1e-3f );
	const float fade      = saturate( ( material.fadeParams.y - viewDistance ) / fadeRange );
	const float3 localPosition = input.position * fade;

	float4 worldPosition = swComputeWorldPosition( localPosition, instance.world );

	// 바람 — 높이 무게(뿌리 0, swayHeight 에서 1) × (흔들림 + 돌풍). 위상은 인스턴스 위치 해시와 바람 방향 거리.
	const float  swayWeight = saturate( input.position.y / max( material.windWave.w, 1e-3f ) );
	const float  weight     = swayWeight * swayWeight;
	const float2 windDirection = material.windParams.xy;
	const float  time          = material.windParams.w;
	const float  phase         = hashPosition( instancePosition.xz ) * kTwoPi + dot( instancePosition.xz, windDirection ) * 0.15f;
	const float  sway          = sin( time * material.windWave.x * kTwoPi + phase ) * 0.5f + 0.5f;
	const float  gust          = sin( time * material.windWave.z * kTwoPi + dot( instancePosition.xz, windDirection ) * 0.05f ) * 0.5f + 0.5f;
	const float  push          = ( material.windParams.z * sway + material.windWave.y * gust ) * weight;
	worldPosition.xz += windDirection * push;

	// 휘게 하는 구(캐릭터 · 탈것) — 바깥으로 밀고 그만큼 눕힌다.
	float2 bend = computeBend( material.influencer0, worldPosition.xyz ) + computeBend( material.influencer1, worldPosition.xyz ) +
	              computeBend( material.influencer2, worldPosition.xyz ) + computeBend( material.influencer3, worldPosition.xyz );
	bend *= material.viewParams.w * weight;
	worldPosition.xz += bend;
	worldPosition.y -= length( bend ) * 0.5f;

	output.position      = swComputeClipPosition( worldPosition, g_ViewProj );
	output.worldPosition = worldPosition.xyz;
	output.normal        = swComputeWorldNormal( input.normal, instance.world );
	// 뿌리는 어둡고 끝은 밝게 — 정점 색 × 머티리얼 색 × 인스턴스 밝기.
	const float rootShade = lerp( 0.55f, 1.0f, swayWeight );
	output.color          = input.color * material.tint * swComputeInstanceTint( instance ) * float4( rootShade, rootShade, rootShade, 1.0f );
	output.uv             = input.uv;
	output.albedoMap      = material.albedoMap;
	return output;
}

SW_SURFACE_OUTPUT PSMain( PSInput input )
{
	const float3 normal = normalize( input.normal );
	const float4 albedo = input.color * swSampleMaterialTexture( input.albedoMap, input.uv );
#if defined( SW_PASS_GBUFFER )
	return swStoreSurface( float4( 0.0f, 0.0f, 0.0f, 0.0f ), albedo, normal );
#elif SW_VIEWMODE_SKIPS_LIGHTING
	return swStoreSurface( float4( albedo.rgb, 1.0f ), albedo, normal );
#else
	const float  shadow = swSampleShadowAtWorld( input.worldPosition, normal );
	const float3 lit    = swShadeLights( albedo.rgb, input.worldPosition, normal, shadow );
	return swStoreSurface( float4( lit, 1.0f ), albedo, normal );
#endif
}
