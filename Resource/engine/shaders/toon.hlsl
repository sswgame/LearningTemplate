/**
 * @file toon.hlsl
 * @brief 셀 셰이딩(툰) 머티리얼 셰이더 — 파라미터 체계는 VRM MToon 1.0(`VRMC_materials_mtoon`)을 따른다.
 * @details 셰이딩은 MToon 과 같은 식이다.
 *            shading = linearstep( -1 + toony, 1 - toony, dot( N, L ) + shift ) × 그림자
 *            색      = lerp( 그림자색, 기본색, shading ) × 빛 + 기본색 × 환경광 + 림(프레넬 · 맷캡) + 발광
 *          빛마다 같은 계단을 쓴다(주광 · 점광 · 스폿, 포워드 경로가 보는 빛 목록 그대로). 그림자 맵은 그림자를 드리우는 빛 하나에만 곱한다.
 *
 *          머티리얼은 픽셀 단계에서만 읽는다(GL 은 구조버퍼를 두 단계가 읽으면 링크를 거절한다).
 *          외곽선 값(outline*)은 머티리얼이 들고만 있다 — 그리는 패스는 따로 있다.
 *
 *          머티리얼 define: `MATERIAL_ALPHA_CUTOFF`(알파 컷오프) · `MATERIAL_TWO_SIDED`(양면 —
 *          컬링을 끄는 것은 PSO 변형이 하고, 셰이더는 뒷면의 노멀을 뒤집는다) · `MATERIAL_BLEND_TRANSLUCENT`(반투명, 머티리얼 blendMode 와 짝).
 */
#include "lighting.hlsli"

// toon.material 의 프로퍼티와 이름 · 순서가 같다. 머티리얼 패커는 리플렉션 오프셋(이름)으로 채운다.
SW_MATERIAL_BEGIN
{
	float4 baseColor;          // 빛을 받는 쪽 색(MToon baseColorFactor · 0.x _Color). a 는 알파
	float4 shadeColor;         // 빛을 못 받는 쪽 색(shadeColorFactor · 0.x _ShadeColor)
	float4 emissiveColor;      // 발광 색(emissiveFactor · 0.x _EmissionColor)
	float4 rimColor;           // 프레넬 림 색(parametricRimColorFactor · 0.x _RimColor)
	float4 matcapColor;        // 맷캡에 곱하는 색(matcapFactor). 맷캡 텍스처가 없으면 맷캡이 없다
	float4 outlineColor;       // 외곽선 색(outlineColorFactor · 0.x _OutlineColor)
	float  shadingShift;       // 계단 위치(shadingShiftFactor). 음수일수록 빛 쪽이 넓다
	float  shadingToony;       // 계단의 날카로움(shadingToonyFactor). 1 이면 칼같이, 0 이면 램버트처럼 부드럽다
	float  shadowReceive;      // 그림자 맵을 얼마나 받는가(0.x _ReceiveShadowRate). 1.0 은 늘 1
	float  emissiveStrength;   // 발광 배율(KHR_materials_emissive_strength)
	float  rimFresnelPower;    // 림 지수(parametricRimFresnelPowerFactor)
	float  rimLift;            // 림 들어올림(parametricRimLiftFactor)
	float  rimLightingMix;     // 림 · 맷캡에 빛을 섞는 정도(rimLightingMixFactor). 0 이면 빛과 무관하게 빛난다
	float  outlineWidth;       // 외곽선 두께 — 월드 모드는 미터, 화면 모드는 화면 높이 비율(outlineWidthFactor)
	float  outlineWidthMode;   // 0 월드 · 1 화면(outlineWidthMode)
	float  outlineLightingMix; // 외곽선 색에 빛을 곱하는 정도(outlineLightingMixFactor)
	float  outlineMaxDistance; // 화면 모드 두께가 거리와 무관하게 유지되는 거리 상한(0.x _OutlineScaledMaxDistance). 넘으면 멀수록 가늘어진다
	float  alphaCutoff;        // MATERIAL_ALPHA_CUTOFF 일 때 이 값보다 작은 알파를 버린다(alphaCutoff · 0.x _Cutoff)
	uint   baseColorMap;       // 기본 텍스처(baseColorTexture · 0.x _MainTex)
	uint   shadeMap;           // 그림자 텍스처(shadeMultiplyTexture · 0.x _ShadeTexture)
	uint   emissiveMap;        // 발광 텍스처(emissiveTexture · 0.x _EmissionMap)
	uint   matcapMap;          // 맷캡 텍스처(matcapTexture · 0.x _SphereAdd)
}
SW_MATERIAL_END

struct PSInput
{
	float4 position                    : SV_POSITION;
	float4 color                       : COLOR;
	float2 uv                          : TEXCOORD0;
	float3 normal                      : TEXCOORD1;
	nointerpolation uint materialIndex : TEXCOORD2;
	float3 worldPosition               : TEXCOORD3;
};

/**
 * @brief 월드 위치에서 카메라 쪽을 향한 단위 방향입니다. 원근 · 직교 모두 맞다.
 * @details PassCB 에 카메라 위치가 없으므로 그 픽셀을 지나는 시선을 역행렬로 되짚는다 — 같은 화면 점의 가까운 평면(깊이 0) ·
 *          먼 평면(깊이 1) 두 점의 차가 시선이다. 직교 카메라는 모든 픽셀이 같은 방향을 받는다.
 */
float3 computeViewDirection( float3 worldPosition )
{
	const float4 clipPosition = mul( float4( worldPosition, 1.0f ), g_ViewProj );
	const float2 ndc          = clipPosition.xy / max( abs( clipPosition.w ), 1e-6f ) * sign( clipPosition.w );
	const float4 nearPoint    = mul( float4( ndc, 0.0f, 1.0f ), g_InvViewProj );
	const float4 farPoint     = mul( float4( ndc, 1.0f, 1.0f ), g_InvViewProj );
	const float3 toCamera     = nearPoint.xyz / nearPoint.w - farPoint.xyz / farPoint.w;
	const float  len          = length( toCamera );
	return ( len > 1e-6f ) ? toCamera / len : float3( 0.0f, 0.0f, -1.0f );
}

/** @brief MToon 의 linearstep — [a, b] 를 [0, 1] 로 옮기고 자른다. a == b 면 계단 하나다. */
float linearStep( float lower, float upper, float value )
{
	return saturate( ( value - lower ) / max( upper - lower, 1e-4f ) );
}

/** @brief 환경광(PassCB 키라이트의 앰비언트 — swShadeLights 와 같은 값)입니다. */
float3 computeAmbient()
{
	return g_KeyLightColor.rgb * g_KeyLightColor.a;
}

/**
 * @brief 빛 목록 전부의 툰 셰이딩 합과 직접광의 합을 구합니다.
 * @param outDirectLighting 빛마다의 색 × 세기 × 감쇠 합(그림자 · 노멀과 무관) — 림 · 외곽선이 빛을 섞을 때 쓴다
 * @details 빛이 하나도 없으면 키라이트 하나로 폴백한다(swShadeLights 와 같은 규칙).
 */
float3 shadeToonLights( float3 litColor, float3 shadeColor, float3 worldPosition, float3 normal, float shadow, float shadingShift, float shadingToony,
                        float shadowReceive, out float3 outDirectLighting )
{
	float3 color          = float3( 0.0f, 0.0f, 0.0f );
	float3 directLighting = float3( 0.0f, 0.0f, 0.0f );
	const float lower      = -1.0f + shadingToony;
	const float upper      = 1.0f - shadingToony;
	const uint  lightCount  = swComputeLightCount();
	bool        bAnyLight3d = false;
	for ( uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex )
	{
		const SwLightData light = g_SwLights[lightIndex];
		float3 toLight;
		float  attenuation;
		if ( swComputeLightIncidence( light, worldPosition, toLight, attenuation ) == false )
			continue;
		bAnyLight3d = true;

		// 그림자 맵은 그것을 받는 빛(params.x) 하나의 것이다. 계단을 지난 뒤에 곱한다(MToon 1.0) — 그림자 경계도 그림자색으로 떨어진다.
		const float shadowTerm = ( light.params.x > 0.5f ) ? lerp( 1.0f, shadow, shadowReceive ) : 1.0f;
		const float shading    = linearStep( lower, upper, dot( normal, toLight ) + shadingShift ) * shadowTerm;
		const float3 radiance  = light.colorIntensity.rgb * light.colorIntensity.a * attenuation;
		color += lerp( shadeColor, litColor, shading ) * radiance;
		directLighting += radiance;
	}

	if ( bAnyLight3d == false )
	{
		const float3 keyDirection = normalize( -g_KeyLightDirIntensity.xyz );
		const float  shading      = linearStep( lower, upper, dot( normal, keyDirection ) + shadingShift ) * lerp( 1.0f, shadow, shadowReceive );
		const float3 radiance     = g_KeyLightColor.rgb * g_KeyLightDirIntensity.w;
		color += lerp( shadeColor, litColor, shading ) * radiance;
		directLighting += radiance;
	}
	outDirectLighting = directLighting;
	return color;
}

/**
 * @brief 맷캡 UV — 시선 공간의 노멀 xy 입니다(MToon 과 같은 식). 텍스처는 위가 v = 0 이라 v 를 뒤집는다.
 */
float2 computeMatcapUv( float3 normal, float3 viewDirection )
{
	const float3 worldUp   = float3( 0.0f, 1.0f, 0.0f );
	const float3 viewUp    = normalize( worldUp - viewDirection * dot( viewDirection, worldUp ) + float3( 0.0f, 0.0f, 1e-5f ) );
	const float3 viewRight = normalize( cross( viewDirection, viewUp ) );
	const float2 matcapUv  = float2( dot( viewRight, normal ), dot( viewUp, normal ) ) * 0.5f + 0.5f;
	return float2( matcapUv.x, 1.0f - matcapUv.y );
}

PSInput VSMain( SwVertexInput input, uint vertexId : SV_VertexID )
{
	PSInput output;
	SwInstanceData instance = swLoadInstance( input.instanceSlot );
	float3 localPosition;
	float3 localNormal;
	swLoadMorphedVertex( instance.meshBatchIndex, vertexId, input.position, input.normal, localPosition, localNormal );
	float4 worldPosition = swComputeWorldPosition( localPosition, instance.world );
	const float3 worldNormal = swComputeWorldNormal( localNormal, instance.world );
	output.color  = input.color;
	output.uv     = input.uv;
	output.normal = worldNormal;

	output.position      = swComputeClipPosition( worldPosition, g_ViewProj );
	output.worldPosition = worldPosition.xyz;
	output.materialIndex = instance.materialIndex;
	return output;
}

SW_SURFACE_OUTPUT PSMain( PSInput input, bool bFrontFace : SV_IsFrontFace )
{
	const SwMaterialData material = SW_MATERIAL( input.materialIndex );
	float4 litColor = input.color * material.baseColor * swSampleMaterialTexture( material.baseColorMap, input.uv );
#if defined( MATERIAL_ALPHA_CUTOFF )
	clip( litColor.a - material.alphaCutoff );
#endif

	float3 normal = normalize( input.normal );
#if defined( MATERIAL_TWO_SIDED )
	// 양면 머티리얼의 뒷면은 노멀이 반대다 — 뒤집지 않으면 머리카락 안쪽이 바깥 빛으로 칠해진다.
	normal = bFrontFace ? normal : -normal;
#endif

#if defined( SW_PASS_GBUFFER )
	// 디퍼드의 G버퍼는 표면만 적는다 — 계단 셰이딩은 포워드 경로의 것이다(디퍼드 조명은 램버트로 칠한다).
	return swStoreSurface( float4( 0.0f, 0.0f, 0.0f, 0.0f ), litColor, normal );
#elif defined( SW_VIEWMODE_UNLIT )
	return swStoreSurface( float4( litColor.rgb, litColor.a ), litColor, normal );
#else
	const float3 shadeColor = input.color.rgb * material.shadeColor.rgb * swSampleMaterialTexture( material.shadeMap, input.uv ).rgb;
	const float  shadow     = swSampleShadowAtWorld( input.worldPosition );

	float3 directLighting;
	float3 color = shadeToonLights( litColor.rgb, shadeColor, input.worldPosition, normal, shadow, material.shadingShift, material.shadingToony,
	                                material.shadowReceive, directLighting );
	const float3 ambient = computeAmbient();
	color += litColor.rgb * ambient;

	// 림 = (프레넬 + 맷캡) × lerp( 1, 빛, rimLightingMix ) — MToon 1.0 과 같다. 맷캡은 텍스처가 있을 때만 더한다(없으면 흰색이 되어 화면이 바랜다).
	const float3 viewDirection = computeViewDirection( input.worldPosition );
	const float  fresnelBase   = saturate( 1.0f - dot( normal, viewDirection ) + material.rimLift );
	float3       rim           = pow( fresnelBase, max( material.rimFresnelPower, 1e-4f ) ) * material.rimColor.rgb;
	if ( material.matcapMap != kInvalidIndex )
		rim += material.matcapColor.rgb * swSampleMaterialTexture( material.matcapMap, computeMatcapUv( normal, viewDirection ) ).rgb;
	color += rim * lerp( float3( 1.0f, 1.0f, 1.0f ), directLighting + ambient, material.rimLightingMix );

	color += material.emissiveColor.rgb * material.emissiveStrength * swSampleMaterialTexture( material.emissiveMap, input.uv ).rgb;

#if defined( MATERIAL_BLEND_TRANSLUCENT )
	return swStoreSurface( float4( color, litColor.a ), litColor, normal );
#else
	return swStoreSurface( float4( color, 1.0f ), litColor, normal );
#endif
#endif
}
