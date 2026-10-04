/**
 * lighting2d.hlsli — 2D 빛(점 · 스폿 · 전역)과 2D 그림자 가림막으로 빛 받는 스프라이트를 칠한다.
 *
 * 빛은 3D 와 **같은 목록**(g_SwLights, t12)에 산다 — 종류가 SW_LIGHT_TYPE_POINT2D · GLOBAL2D 인 원소이고, 그 뒤에 2D 그림자 가림막 토막
 * (SW_LIGHT_TYPE_SHADOW2D)이 붙는다. 새 슬롯 · 새 패스가 없어 네 백엔드가 이미 같은 버퍼를 같은 자리에 건다. 3D 조명 식은 2D 원소를 건너뛴다.
 *
 * 원소 칸의 뜻(C++ PointLight2DComponent · ShadowCaster2DComponent 가 쓴다):
 *   POINT2D  positionRadius = (x, y, 노멀 맵 높이, 바깥 반경), directionType = (원뿔 방향 x, y, 안 반경, 종류),
 *            params = (그림자 0/1, cos 바깥 반각, cos 안 반각, 감쇠 지수). 반각이 180 도면 cos = −1 이라 원뿔이 없다.
 *   GLOBAL2D colorIntensity 만 — 빛 받는 스프라이트의 바탕 밝기(유니티 Global Light 2D).
 *   SHADOW2D positionRadius = (시작 x, y, 끝 x, y), colorIntensity.xy = 바깥쪽 방향(가림막의 밖).
 *
 * 그림자: 픽셀에서 빛까지의 선분이 가림막 토막과 엇갈리면 가려진다. **빛을 등진 토막만** 센다(바깥쪽 방향이 빛 반대) — 그래서 가림막 안쪽 픽셀은
 * 빛 쪽 토막에 가려지지 않는다(자기 그림자 없음, 유니티 Shadow Caster 2D 의 Self Shadows 꺼짐과 같다). 원소 수 × 토막 수라 토막은 수백 개까지다.
 */

#ifndef SW_ENGINE_LIGHTING2D_HLSLI
#define SW_ENGINE_LIGHTING2D_HLSLI

#include "lighting.hlsli"

/** @brief 2D 외적(z 성분)입니다. */
float swCross2D( float2 lhs, float2 rhs )
{
	return lhs.x * rhs.y - lhs.y * rhs.x;
}

/** @brief 선분 [from, to] 와 [segmentStart, segmentEnd] 가 안쪽에서 엇갈리면 true — 끝점에 닿기만 하는 것은 엇갈림이 아니다. */
bool swSegmentsCross( float2 from, float2 to, float2 segmentStart, float2 segmentEnd )
{
	const float2 direction = to - from;
	const float2 edge      = segmentEnd - segmentStart;
	const float  denom     = swCross2D( direction, edge );
	if ( abs( denom ) < 1e-8f )
		return false;
	const float2 offset = segmentStart - from;
	const float  along  = swCross2D( offset, edge ) / denom;      // 픽셀 → 빛 선분 위의 자리(0..1)
	const float  across = swCross2D( offset, direction ) / denom; // 가림막 토막 위의 자리(0..1)
	return along > 1e-4f && along < 0.9999f && across >= 0.0f && across <= 1.0f;
}

/** @brief 픽셀 @p pixel 이 빛 @p lightPosition 에서 2D 가림막에 가려지면 true 입니다. */
bool swIsShadowed2D( float2 pixel, float2 lightPosition, uint lightCount )
{
	for ( uint casterIndex = 0u; casterIndex < lightCount; ++casterIndex )
	{
		const SwLightData caster = g_SwLights[casterIndex];
		if ( (uint)( caster.directionType.w + 0.5f ) != SW_LIGHT_TYPE_SHADOW2D )
			continue;
		const float2 segmentStart = caster.positionRadius.xy;
		const float2 segmentEnd   = caster.positionRadius.zw;
		// 빛을 등진 토막만 가린다 — 바깥쪽 방향이 빛을 보면 그 토막은 가림막의 "빛 쪽 얼굴" 이라 그 안쪽 픽셀을 가리면 안 된다.
		const float2 middle = ( segmentStart + segmentEnd ) * 0.5f;
		if ( dot( caster.colorIntensity.xy, lightPosition - middle ) > 0.0f )
			continue;
		if ( swSegmentsCross( pixel, lightPosition, segmentStart, segmentEnd ) )
			return true;
	}
	return false;
}

/**
 * @brief 점 · 스폿 2D 빛 하나의 감쇠 — 안 반경 안은 1, 바깥 반경에서 0, 그 사이는 ((바깥 − 거리) / (바깥 − 안))^지수, 원뿔이면 그 각도 감쇠를 곱한다.
 * @details C++ `PointLight2DComponent::computeAttenuation` 이 같은 식이다(시험이 GPU 되읽기와 견준다).
 */
float swComputeLight2dAttenuation( SwLightData light, float2 pixel )
{
	const float2 delta    = pixel - light.positionRadius.xy;
	const float  distance = length( delta );
	const float  outer    = max( light.positionRadius.w, 1e-4f );
	const float  inner    = clamp( light.directionType.z, 0.0f, outer );
	if ( distance >= outer )
		return 0.0f;
	const float ramp        = saturate( ( outer - distance ) / max( outer - inner, 1e-4f ) );
	float       attenuation = pow( ramp, max( light.params.w, 1e-3f ) );
	const float cosOuter    = light.params.y;
	if ( cosOuter > -0.9999f && distance > 1e-4f )
	{
		const float cosAngle = dot( delta / distance, light.directionType.xy );
		const float cone     = saturate( ( cosAngle - cosOuter ) / max( light.params.z - cosOuter, 1e-4f ) );
		attenuation *= cone;
	}
	return attenuation;
}

/**
 * @brief 빛 받는 스프라이트 픽셀을 2D 빛으로 칠합니다.
 * @param albedo     스프라이트 색(텍스처 × 머티리얼 × 인스턴스 색)
 * @param pixel      월드 X · Y
 * @param normal     월드 노멀(노멀 맵이 있을 때). 스프라이트 평면은 -Z(카메라 쪽)를 본다
 * @param bHasNormal 노멀 맵이 있는가 — 없으면 N·L 을 곱하지 않는다(유니티 Sprite-Lit 과 같다)
 * @details 2D 빛이 하나도 없으면 칠하지 않고 그대로 돌려준다(빛 받는 머티리얼을 쓴 씬에 빛이 없어도 까맣게 죽지 않게).
 */
float3 swShadeLights2d( float3 albedo, float2 pixel, float3 normal, bool bHasNormal )
{
	const uint lightCount = ( g_SwLightsIndex == kInvalidIndex ) ? 0u : g_SwLightCount;
	float3     light      = float3( 0.0f, 0.0f, 0.0f );
	bool       bAny2D     = false;
	for ( uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex )
	{
		const SwLightData record = g_SwLights[lightIndex];
		const uint        type   = (uint)( record.directionType.w + 0.5f );
		if ( type == SW_LIGHT_TYPE_GLOBAL2D )
		{
			bAny2D = true;
			light += record.colorIntensity.rgb * record.colorIntensity.a;
			continue;
		}
		if ( type != SW_LIGHT_TYPE_POINT2D )
			continue;
		bAny2D                  = true;
		float attenuation       = swComputeLight2dAttenuation( record, pixel );
		if ( attenuation <= 0.0f )
			continue;
		if ( bHasNormal )
		{
			// 빛은 스프라이트 평면에서 카메라 쪽(-Z)으로 높이만큼 떠 있다.
			const float3 toLight = normalize( float3( record.positionRadius.xy - pixel, -max( record.positionRadius.z, 1e-3f ) ) );
			attenuation *= saturate( dot( normal, toLight ) );
		}
		if ( record.params.x > 0.5f && swIsShadowed2D( pixel, record.positionRadius.xy, lightCount ) )
			continue;
		light += record.colorIntensity.rgb * record.colorIntensity.a * attenuation;
	}
	if ( bAny2D == false )
		return albedo;
	return albedo * light;
}

#endif // SW_ENGINE_LIGHTING2D_HLSLI
