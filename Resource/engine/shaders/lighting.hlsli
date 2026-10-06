/**
 * lighting.hlsli — 씬 라이트 목록과 **조명 식 하나**.
 *
 * 포워드(`forwardlit.hlsl`)와 디퍼드(`deferredlighting.hlsl`)가 이 파일의 같은 함수를 부른다.
 * 조명 식을 두 벌 두면 둘은 반드시 갈라진다("경로마다 다른 그림"). 언리얼도 포워드와 디퍼드가 같은 `FDeferredLightingCommon` 을 나눠 쓴다.
 *
 * 라이트는 상수버퍼가 아니라 **구조버퍼**(t12, `SW_SLOT_LIGHT_SRV`)다 — 개수가 씬마다 다르고,
 * 패스당 한 번 걸면 드로우 사이에 바인딩이 바뀌지 않는다(이 엔진의 규약).
 *
 * 그림자는 **방향광 하나**뿐이다. 그림자 맵이 하나이기 때문이고, 어느 빛이 그걸 받는지는
 * 원소의 `params.x` 가 말한다. 점광 그림자는 큐브맵이 필요한데 이 엔진에는 큐브맵 자원이 없다 —
 * 없는 것을 있는 척하지 않는다.
 */

#ifndef SW_ENGINE_LIGHTING_HLSLI
#define SW_ENGINE_LIGHTING_HLSLI

#include "binding.hlsli"

/**
 * 씬 라이트 한 개. C++ `GpuLight` 와 레이아웃이 같아야 한다(float4 넷 = 64바이트).
 * float4 로만 채운 이유는 모프 정점 풀에서 물렸던 것과 같다 — std430 은 vec4 를 16바이트 경계에
 * 맞추는데 DX/Vulkan 은 DXC 가 명시 오프셋을 적어 넘어간다. 그러면 **OpenGL 만** 값이 어긋난다.
 */
struct SwLightData
{
	float4 positionRadius; // xyz 월드 위치(점광·스폿), w 반경 — 방향광은 0
	float4 colorIntensity; // rgb 빛 색, a 세기
	float4 directionType;  // xyz 빛이 나아가는 방향(방향광·스폿), w 타입 (SW_LIGHT_TYPE_*)
	float4 params;         // x 그림자를 받는가(0/1), y cos(바깥 원뿔각), z cos(안쪽 원뿔각), w 예약
};

SW_DECLARE_STRUCTURED_BUFFER( SwLightData, g_SwLights, SW_SLOT_LIGHT_SRV );

/**
 * @brief 이 픽셀이 그림자 맵에서 얼마나 가려졌는지 — 1 이면 빛을 다 받고, 0 에 가까울수록 그늘이다.
 * @param worldNormal 받는 면의 월드 노멀(정규화) — 노멀 오프셋에 쓴다
 * @details 월드 위치를 **라이트 클립 공간으로 투영해** 샘플한다. 그림자 맵을 화면 UV 로 읽으면 그림자가 아니라
 *          "깊이 텍스처를 화면에 붙인 무늬" 라, 카메라가 움직이면 그늘이 물체를 따라오지 않고 화면에 붙는다.
 *          바이어스는 둘이다 — 노멀 쪽으로 `g_ShadowParams.z`(m) 띄운 점을 읽고(노멀 오프셋), 깊이에서 `g_ShadowParams.x`(NDC)를 뺀다.
 *          둘 다 CPU 가 텍셀 크기로 정한다(`DirectionalShadowProjection::computeShaderParams`). 깊이 바이어스만 키우면 그림자가 물체 발치에서 떨어진다.
 *          필터는 3x3 PCF 다 — 탭 하나가 2x2 쌍선형 비교라 가장자리가 텍셀 세 개 폭으로 번진다.
 * @note 맵 밖은 1(가려지지 않음)이다. 0 으로 두면 그림자 볼륨 밖이 통째로 검게 죽는다.
 */
float swSampleShadowAtWorld( float3 worldPosition, float3 worldNormal )
{
	if ( g_ShadowMapIndex == kInvalidIndex )
		return 1.0f;

	const float3 samplePosition = worldPosition + worldNormal * g_ShadowParams.z;
	const float4 lightClip      = mul( float4( samplePosition, 1.0f ), g_LightViewProj );
	if ( lightClip.w <= 0.0f )
		return 1.0f;

	const float3 ndc = lightClip.xyz / lightClip.w;
	const float2 uv  = ndc.xy * float2( 0.5f, -0.5f ) + 0.5f;
	if ( uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || ndc.z < 0.0f || ndc.z > 1.0f )
		return 1.0f;

	// 바이어스(g_ShadowParams.x · z)가 없으면 자기 자신에 그림자가 져 표면이 줄무늬가 된다(shadow acne).
	// 3x3 PCF — 탭 하나가 2x2 쌍선형 비교라 가장자리의 텍셀 계단이 보이지 않는다. 간격 g_ShadowParams.w = 텍셀 하나의 UV 폭.
	const float depth = ndc.z - g_ShadowParams.x;
	float       lit   = 0.0f;
	[unroll] for ( int offsetY = -1; offsetY <= 1; ++offsetY )
	{
		[unroll] for ( int offsetX = -1; offsetX <= 1; ++offsetX )
			lit += swSampleShadowComparison( g_ShadowMapIndex, uv + float2( offsetX, offsetY ) * g_ShadowParams.w, depth );
	}
	lit *= 1.0f / 9.0f;

	// 세기는 g_ShadowParams.y — 완전한 검정이 아니라 "얼마나 어두워지는가" 다.
	return lerp( 1.0f - g_ShadowParams.y, 1.0f, saturate( lit ) );
}

/**
 * @brief 화면 UV와 깊이에서 월드 위치를 복원합니다 (디퍼드 전용).
 * @details G버퍼에 위치를 저장하지 않는다 — 첨부 하나를 통째로 아끼고, 복원은 역행렬 곱 하나다.
 *          이 엔진은 행벡터 규약이라 `mul( 벡터, 행렬 )` 이다(`mul( worldPosition, g_ViewProj )` 와 같은 순서).
 */
float3 swComputeWorldPositionFromDepth( float2 uv, float deviceDepth )
{
	const float2 ndcXy = uv * float2( 2.0f, -2.0f ) + float2( -1.0f, 1.0f );
	const float4 world = mul( float4( ndcXy, deviceDepth, 1.0f ), g_InvViewProj );
	return world.xyz / world.w;
}

/**
 * @brief 3D 빛 하나가 이 위치에 닿는 방향과 감쇠를 구합니다. 2D 빛 · 2D 그림자 토막이면 false 입니다.
 * @param outToLight 표면에서 빛으로 가는 단위 방향
 * @param outAttenuation 거리 · 원뿔 감쇠(방향광은 1). 세기와 색은 곱하지 않는다
 * @details 조명 식이 둘(이 파일의 `swShadeLights` · 셀 셰이딩 `toon.hlsl`)이어도 "빛이 어디서 얼마나 오는가" 는 한 벌이다 —
 *          감쇠를 두 곳에 적으면 같은 점광이 두 머티리얼에 다른 반경으로 닿는다.
 * @note early-return 이 없다 — GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다(binding.hlsli swComputeMorphElement).
 */
bool swComputeLightIncidence( SwLightData light, float3 worldPosition, out float3 outToLight, out float outAttenuation )
{
	const uint lightType = (uint)( light.directionType.w + 0.5f );
	// 2D 빛 · 그림자 토막은 빛 받는 스프라이트(lighting2d.hlsli)의 것이다.
	const bool bLight3d    = ( lightType <= SW_LIGHT_TYPE_SPOT );
	float3     toLight     = -light.directionType.xyz;
	float      attenuation = 1.0f;
	if ( bLight3d && lightType != SW_LIGHT_TYPE_DIRECTIONAL )
	{
		// 점광과 스폿은 거리 감쇠가 같다 — 스폿은 거기에 원뿔을 곱할 뿐이다.
		const float3 delta    = light.positionRadius.xyz - worldPosition;
		const float  distance = length( delta );
		// 반경이 0 이면 나눗셈이 터진다. 반경 밖은 0 이 되어 그 빛이 계산에서 빠진다.
		const float radius = max( light.positionRadius.w, 1e-4f );
		toLight            = delta / max( distance, 1e-4f );
		// 역제곱을 반경에서 자른 형태 — 언리얼의 `InverseSquaredFalloff` 와 같은 모양이다.
		// 물리적으로 정확한 역제곱만 쓰면 빛이 영원히 닿아 타일 컬링이 의미를 잃는다.
		const float normalized = saturate( 1.0f - ( distance / radius ) );
		attenuation            = normalized * normalized;

		if ( lightType == SW_LIGHT_TYPE_SPOT )
		{
			// 원뿔 감쇠. `dot(빛이 나아가는 방향, 빛에서 표면으로 가는 방향)` 이 1 에 가까울수록
			// 원뿔 중심이다. 안쪽 각 안은 1, 바깥 각 밖은 0, 사이는 부드럽게 떨어진다.
			const float cosAngle = dot( light.directionType.xyz, -toLight );
			const float cosOuter = light.params.y;
			const float cosInner = light.params.z;
			// 안쪽과 바깥쪽이 같으면 0 으로 나눈다 — 그 경우 경계가 칼같이 끊긴다.
			const float cone = saturate( ( cosAngle - cosOuter ) / max( cosInner - cosOuter, 1e-4f ) );
			attenuation *= cone * cone;
		}
	}
	outToLight     = toLight;
	outAttenuation = attenuation;
	return bLight3d;
}

/** @brief 이번 패스에 걸린 라이트 수입니다. 라이트 버퍼가 안 걸렸으면 0 이다(키라이트 폴백). */
uint swComputeLightCount()
{
	return ( g_SwLightsIndex == kInvalidIndex ) ? 0u : g_SwLightCount;
}

/**
 * @brief 이 표면을 씬의 **모든 라이트**로 셰이딩합니다.
 * @param albedo   표면 색 (앰비언트에도 곱해진다)
 * @param worldPosition 월드 위치 — 점광의 거리 감쇠에 쓴다
 * @param normal   월드 노멀 (정규화되어 있어야 한다)
 * @param shadow   `swSampleShadowAtWorld` 가 준 값 — `params.x` 가 켜진 빛에만 곱한다
 * @details 라이트 버퍼가 안 걸렸으면(`kInvalidIndex`) PassCB 의 키라이트 하나로 폴백한다 —
 *          라이트 컴포넌트가 없는 씬도 키라이트 하나로 그려진다.
 */
float3 swShadeLights( float3 albedo, float3 worldPosition, float3 normal, float shadow )
{
	// 앰비언트는 빛 목록과 무관하게 **한 번만** 더한다 — 라이트마다 더하면 빛을 늘릴수록 화면이 바랜다.
	float3 lit = albedo * g_KeyLightColor.rgb * g_KeyLightColor.a;

	const uint lightCount = swComputeLightCount();
	// 3D 빛이 하나도 없으면(빛 목록이 비었거나 2D 빛 · 그림자 토막뿐) 키라이트 하나로 폴백한다 — 2D 빛을 둔 씬의 3D 물체가 까맣게 죽지 않게.
	bool bAnyLight3d = false;

	for ( uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex )
	{
		const SwLightData light = g_SwLights[lightIndex];

		float3 toLight;
		float  attenuation;
		if ( swComputeLightIncidence( light, worldPosition, toLight, attenuation ) == false )
			continue;
		bAnyLight3d = true;

		const float normalDotLight = saturate( dot( normal, toLight ) );
		if ( normalDotLight * attenuation <= 0.0f )
			continue;

		const float shadowTerm = ( light.params.x > 0.5f ) ? shadow : 1.0f;
		lit += albedo * normalDotLight * attenuation * light.colorIntensity.a * light.colorIntensity.rgb * shadowTerm;
	}

	if ( bAnyLight3d == false )
	{
		const float3 keyDirection      = normalize( -g_KeyLightDirIntensity.xyz );
		const float  keyNormalDotLight = saturate( dot( normal, keyDirection ) );
		lit += albedo * keyNormalDotLight * g_KeyLightDirIntensity.w * g_KeyLightColor.rgb * shadow;
	}
	return lit;
}

#endif // SW_ENGINE_LIGHTING_HLSLI
