#include "lighting.hlsli"
#include "gerstner.hlsli"

/**
 * water.hlsl — 물(WaterBodyComponent). 거스트너 파도로 정점을 옮기고, 깊이 색 · 프레넬 · 반사광 · 잔물결 · 물가 거품으로 칠한다.
 *
 * 머티리얼은 **정점 셰이더만** 읽는다 — GL(ARB_gl_spirv)은 구조버퍼를 정점 · 픽셀 두 단계에서 읽으면 링크를 거절한다. 픽셀이 필요한 값은
 * 보간 칸으로 넘긴다. 반투명 패스에는 장면 깊이 · 색 입력이 없어 굴절 · 화면 공간 두께는 없다: 깊이는 컴포넌트가 지형에서 재어 정점 색 알파에
 * 구워 둔다(m). 정점 색 rg 는 강의 흐름 방향이다(호수는 0).
 */

struct PSInput
{
	float4 position      : SV_POSITION;
	float3 worldPosition : TEXCOORD0;
	float3 normal        : TEXCOORD1;
	float4 waterColor    : TEXCOORD2; // rgb = 깊이로 섞은 물 색, a = 불투명도
	float3 toView        : TEXCOORD3; // 표면에서 카메라 쪽(정규화 전)
	float4 shading       : TEXCOORD4; // x = 거품, y = 파도 시간, z = 잔물결 세기, w = 깊이(m)
	float3 skyColor      : TEXCOORD5;
	float2 flow          : TEXCOORD6;
};

// water.material 의 프로퍼티와 같은 이름 · 타입이다.
SW_MATERIAL_BEGIN
{
	float4 wave0;
	float4 wave1;
	float4 wave2;
	float4 wave3;
	float4 waterParams; // x = 파도 시간, y = 깊은 색이 다 차는 깊이, z = 거품 폭, w = 잔물결 세기
	float4 shallowColor;
	float4 deepColor;
	float4 skyColor;
	float4 waveParams; // x = 파도 분산의 중력(m/s²) — 컴포넌트가 설정된 물리 중력을 싣는다
}
SW_MATERIAL_END

PSInput VSMain( SwVertexInput input )
{
	PSInput        output;
	SwInstanceData instance = swLoadInstance( input.instanceSlot );
	SwMaterialData material = SW_MATERIAL( instance.materialIndex );

	float4 arrWave[SW_GERSTNER_WAVE_COUNT] = { material.wave0, material.wave1, material.wave2, material.wave3 };
	const float time = material.waterParams.x;

	// 파도는 월드 (x, z) 의 함수다 — CPU 질의(WaterWaveMath)가 같은 원점으로 같은 값을 낸다.
	float4       worldPosition = swComputeWorldPosition( input.position, instance.world );
	const float2 origin        = worldPosition.xz;
	worldPosition.xyz += swComputeGerstnerDisplacement( origin, time, material.waveParams.x, arrWave );
	output.position      = swComputeClipPosition( worldPosition, g_ViewProj );
	output.worldPosition = worldPosition.xyz;
	output.normal        = swComputeGerstnerNormal( origin, time, material.waveParams.x, arrWave );

	const float depth       = max( input.color.a, 0.0f );
	const float depthFactor = saturate( depth / max( material.waterParams.y, 1e-3f ) );
	output.waterColor       = lerp( material.shallowColor, material.deepColor, depthFactor );
	const float foamWidth   = material.waterParams.z;
	const float foam        = ( foamWidth > 0.0f ) ? saturate( 1.0f - depth / foamWidth ) : 0.0f;
	output.shading          = float4( foam, time, material.waterParams.w, depth );
	output.skyColor         = material.skyColor.rgb;
	output.flow             = input.color.rg;

	// 카메라 쪽 방향: 이 정점의 화면 자리를 가까운 면과 먼 면으로 되돌려 그 차를 쓴다 — 원근 · 직교 모두 맞다.
	const float2 ndc       = output.position.xy / max( output.position.w, 1e-5f );
	const float4 nearPoint = mul( float4( ndc, 0.0f, 1.0f ), g_InvViewProj );
	const float4 farPoint  = mul( float4( ndc, 1.0f, 1.0f ), g_InvViewProj );
	output.toView          = nearPoint.xyz / nearPoint.w - farPoint.xyz / farPoint.w;
	return output;
}

/** @brief 작은 물결 노멀 — 흐름 방향으로 흘러가는 사인 셋의 기울기. */
float3 computeRippleNormal( float2 position, float time, float2 flow )
{
	const float2 drift   = position - flow * time * 0.8f;
	const float2 slopeA  = float2( cos( drift.x * 2.3f + time * 1.7f ), cos( drift.y * 2.9f - time * 1.3f ) );
	const float2 slopeB  = float2( cos( ( drift.x + drift.y ) * 4.1f + time * 2.6f ), cos( ( drift.x - drift.y ) * 3.7f - time * 2.2f ) ) * 0.5f;
	const float2 slope   = ( slopeA + float2( slopeB.x + slopeB.y, slopeB.x - slopeB.y ) ) * 0.12f;
	return normalize( float3( -slope.x, 1.0f, -slope.y ) );
}

SW_SURFACE_OUTPUT PSMain( PSInput input )
{
	const float3 rippleNormal = computeRippleNormal( input.worldPosition.xz, input.shading.y, input.flow );
	const float3 normal       = normalize( lerp( input.normal, normalize( input.normal + rippleNormal - float3( 0.0f, 1.0f, 0.0f ) ), input.shading.z ) );
	const float3 toView       = normalize( input.toView );

	// 프레넬(슐릭, 물 F0 = 0.02) — 비스듬히 볼수록 하늘이 비친다.
	const float  facing  = saturate( dot( normal, toView ) );
	const float  fresnel = 0.02f + 0.98f * pow( 1.0f - facing, 5.0f );
	const float  shadow  = swSampleShadowAtWorld( input.worldPosition, normal );
	const float3 body    = swShadeLights( input.waterColor.rgb, input.worldPosition, normal, shadow );

	// 주광의 반사(블린-퐁) — 물은 매끈하다.
	const float3 toLight  = normalize( -g_KeyLightDirIntensity.xyz );
	const float3 halfway  = normalize( toLight + toView );
	const float  specular = pow( saturate( dot( normal, halfway ) ), 180.0f ) * g_KeyLightDirIntensity.w * shadow;

	float3 color = lerp( body, input.skyColor, fresnel ) + specular * g_KeyLightColor.rgb;
	float  alpha = saturate( max( input.waterColor.a, fresnel ) + specular );

	// 물가 거품 — 얕을수록 희다. 잔물결 무늬로 끊어 띠가 아니라 거품처럼 보이게 한다.
	const float foamPattern = saturate( 0.5f + 0.5f * sin( input.worldPosition.x * 7.0f + input.worldPosition.z * 5.0f + input.shading.y * 1.5f ) );
	const float foam        = input.shading.x * ( 0.55f + 0.45f * foamPattern );
	color                   = lerp( color, float3( 0.92f, 0.95f, 0.97f ), foam );
	alpha                   = max( alpha, foam );

#if defined( MATERIAL_BLEND_TRANSLUCENT )
	return swStoreSurface( float4( color, alpha ), float4( input.waterColor.rgb, 1.0f ), normal );
#else
	return swStoreSurface( float4( color, 1.0f ), float4( input.waterColor.rgb, 1.0f ), normal );
#endif
}
