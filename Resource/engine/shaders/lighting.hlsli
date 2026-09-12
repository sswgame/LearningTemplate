/**
 * lighting.hlsli — 씬 라이트 목록과 **조명 식 하나**.
 *
 * 포워드(`forwardlit.hlsl`)와 디퍼드(`deferredlighting.hlsl`)가 이 파일의 같은 함수를 부른다.
 * 조명 식을 두 벌 두면 둘은 반드시 갈라진다 — 이 저장소에서 "백엔드/경로마다 다른 그림"이 가장
 * 비싼 버그였다. 언리얼도 포워드와 디퍼드가 같은 `FDeferredLightingCommon` 을 나눠 쓴다.
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
 * @details 월드 위치를 **라이트 클립 공간으로 투영해** 샘플한다. 예전에는 그림자 맵을 화면 UV로
 *          (포워드는 심지어 로컬 좌표에서 만든 UV로) 읽었다 — 그건 그림자가 아니라 "깊이 텍스처를
 *          화면에 붙인 무늬" 다. 카메라가 움직이면 그늘이 물체를 따라오지 않고 화면에 붙어 있었다.
 * @note 맵 밖은 1(가려지지 않음)이다. 0 으로 두면 그림자 볼륨 밖이 통째로 검게 죽는다.
 */
float SwSampleShadowAtWorld( float3 worldPos )
{
	if ( g_ShadowMapIndex == SW_INVALID_INDEX )
		return 1.0f;

	const float4 lightClip = mul( float4( worldPos, 1.0f ), g_LightViewProj );
	if ( lightClip.w <= 0.0f )
		return 1.0f;

	const float3 ndc = lightClip.xyz / lightClip.w;
	const float2 uv  = ndc.xy * float2( 0.5f, -0.5f ) + 0.5f;
	if ( uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || ndc.z < 0.0f || ndc.z > 1.0f )
		return 1.0f;

	// 바이어스는 g_ShadowParams.x. 없으면 자기 자신에 그림자가 져 표면이 줄무늬가 된다(shadow acne).
	const float lit = SW_SampleShadowCmp( g_ShadowMapIndex, uv, ndc.z - g_ShadowParams.x );

	// 세기는 g_ShadowParams.y — 완전한 검정이 아니라 "얼마나 어두워지는가" 다.
	return lerp( 1.0f - g_ShadowParams.y, 1.0f, saturate( lit ) );
}

/**
 * @brief 화면 UV와 깊이에서 월드 위치를 복원합니다 (디퍼드 전용).
 * @details G버퍼에 위치를 굽지 않는다 — 첨부 하나를 통째로 아끼고, 복원은 역행렬 곱 하나다.
 *          이 엔진은 행벡터 규약이라 `mul( 벡터, 행렬 )` 이다(`mul( worldPos, g_ViewProj )` 와 같은 순서).
 */
float3 SwWorldPositionFromDepth( float2 uv, float deviceDepth )
{
	const float2 ndcXy = uv * float2( 2.0f, -2.0f ) + float2( -1.0f, 1.0f );
	const float4 world = mul( float4( ndcXy, deviceDepth, 1.0f ), g_InvViewProj );
	return world.xyz / world.w;
}

/**
 * @brief 이 표면을 씬의 **모든 라이트**로 셰이딩합니다.
 * @param albedo   표면 색 (앰비언트에도 곱해진다)
 * @param worldPos 월드 위치 — 점광의 거리 감쇠에 쓴다
 * @param normal   월드 노멀 (정규화되어 있어야 한다)
 * @param shadow   `SwSampleShadowAtWorld` 가 준 값 — `params.x` 가 켜진 빛에만 곱한다
 * @details 라이트 버퍼가 안 걸렸으면(`SW_INVALID_INDEX`) PassCB 의 키라이트 하나로 폴백한다 —
 *          라이트 컴포넌트가 없는 씬도 예전과 같은 그림이 나온다.
 */
float3 SwShadeLights( float3 albedo, float3 worldPos, float3 normal, float shadow )
{
	// 앰비언트는 빛 목록과 무관하게 **한 번만** 더한다 — 라이트마다 더하면 빛을 늘릴수록 화면이 바랜다.
	float3 lit = albedo * g_KeyLightColor.rgb * g_KeyLightColor.a;

	const uint lightCount = ( g_SwLightsIndex == SW_INVALID_INDEX ) ? 0u : g_SwLightCount;
	if ( lightCount == 0u )
	{
		const float3 keyDir   = normalize( -g_KeyLightDirIntensity.xyz );
		const float  keyNdotl = saturate( dot( normal, keyDir ) );
		lit += albedo * keyNdotl * g_KeyLightDirIntensity.w * g_KeyLightColor.rgb * shadow;
		return lit;
	}

	for ( uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex )
	{
		const SwLightData light = g_SwLights[lightIndex];

		const uint lightType = (uint)( light.directionType.w + 0.5f );

		float3 toLight     = -light.directionType.xyz;
		float  attenuation = 1.0f;
		if ( lightType != SW_LIGHT_TYPE_DIRECTIONAL )
		{
			// 점광과 스폿은 거리 감쇠가 같다 — 스폿은 거기에 원뿔을 곱할 뿐이다.
			const float3 delta    = light.positionRadius.xyz - worldPos;
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

		const float ndotl = saturate( dot( normal, toLight ) );
		if ( ndotl * attenuation <= 0.0f )
			continue;

		const float shadowTerm = ( light.params.x > 0.5f ) ? shadow : 1.0f;
		lit += albedo * ndotl * attenuation * light.colorIntensity.a * light.colorIntensity.rgb * shadowTerm;
	}

	return lit;
}

#endif // SW_ENGINE_LIGHTING_HLSLI
