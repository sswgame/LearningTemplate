/**
 * lighting2d.hlsli — 2D 빛(점 · 스폿 · 전역)과 2D 그림자 가림막으로 빛 받는 스프라이트를 칠한다.
 *
 * 빛은 3D 와 **같은 목록**(g_SwLights, t8)에 산다 — 종류가 SW_LIGHT_TYPE_POINT2D · GLOBAL2D 인 원소이고, 그 뒤에 2D 그림자 가림막 토막
 * (SW_LIGHT_TYPE_SHADOW2D)이 붙는다. 새 슬롯 · 새 패스가 없어 네 백엔드가 이미 같은 버퍼를 같은 자리에 건다. 3D 조명 식은 2D 원소를 건너뛴다.
 *
 * 원소 칸의 뜻(C++ PointLight2DComponent · ShadowCaster2DComponent 가 쓴다):
 *   POINT2D  positionRadius = (x, y, 노멀 맵 높이, 바깥 반경), directionType = (원뿔 방향 x, y, 안 반경, 종류),
 *            params = (그림자 0/1, cos 바깥 반각, cos 안 반각, 감쇠 지수). 반각이 180 도면 cos = −1 이라 원뿔이 없다.
 *   GLOBAL2D colorIntensity 만 — 빛 받는 스프라이트의 바탕 밝기(유니티 Global Light 2D).
 *   FREEFORM2D positionRadius = (원점 x, y, 노멀 맵 높이, 닿는 반경), directionType = (0, 0, 감쇠 거리, 종류), params = (그림자 0/1, 0, 0, 감쇠 지수).
 *            변은 바로 뒤 extra.z 개의 FREEFORM2D_EDGE 원소(positionRadius = 시작 x, y, 끝 x, y)다. 노멀 맵 · 그림자의 빛 위치는 원점이다.
 *   POINT2D · FREEFORM2D 공통 extra = (그림자 원천 반경 m — 0 이면 딱딱한 그림자, 섞는 방식 SW_LIGHT2D_BLEND_*, 뒤 원소 수, 쿠키 아틀라스 칸 — 점 · 스폿만, SW_LIGHT2D_NO_COOKIE_CELL 이면 없음).
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
 * @details C++ `PointLight2DComponent::computeAttenuation` 이 같은 식이다(테스트가 GPU 되읽기와 견준다).
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
 * @brief 자유 모양 2D 빛 하나의 감쇠 — 다각형 안은 1, 밖은 (1 − 변까지 거리 / 감쇠 거리)^지수. 변은 빛 원소 @p lightIndex 바로 뒤의 extra.z 개다.
 * @details C++ `FreeformLight2DComponent::computeAttenuation` 이 같은 식이다(안 · 밖은 짝홀 규칙). 목록이 상한에서 잘렸으면 남은 변만 본다 — 버퍼 밖을 읽지 않는다.
 */
float swComputeFreeformLight2dAttenuation( SwLightData light, uint lightIndex, uint lightCount, float2 pixel )
{
	const uint edgeCount = min( (uint)( light.extra.z + 0.5f ), SW_LIGHT2D_MAX_FREEFORM_EDGES );
	bool       bInside   = false;
	float      distance  = 3.402823466e+38f;
	for ( uint edgeIndex = 0u; edgeIndex < edgeCount; ++edgeIndex )
	{
		const uint element = lightIndex + 1u + edgeIndex;
		if ( element >= lightCount )
			break;
		const SwLightData edge  = g_SwLights[element];
		const float2      start = edge.positionRadius.xy;
		const float2      end   = edge.positionRadius.zw;
		const float2      side  = end - start;
		const float       lengthSq = dot( side, side );
		const float       along    = ( lengthSq > 1e-12f ) ? saturate( dot( pixel - start, side ) / lengthSq ) : 0.0f;
		distance                   = min( distance, length( pixel - ( start + side * along ) ) );
		const bool bStraddles      = ( start.y > pixel.y ) != ( end.y > pixel.y );
		if ( bStraddles && pixel.x < ( end.x - start.x ) * ( pixel.y - start.y ) / ( end.y - start.y ) + start.x )
			bInside = !bInside;
	}
	if ( edgeCount < 3u )
		return 0.0f;
	if ( bInside )
		return 1.0f;
	const float ramp = saturate( 1.0f - distance / max( light.directionType.z, 1e-4f ) );
	return pow( ramp, max( light.params.w, 1e-3f ) );
}

/**
 * @brief 빛 @p lightPosition 에서 본 2D 그림자 — 1 이면 빛 받음, 0 이면 가려짐.
 * @details 그림자 원천 반경(@p softness)이 0 이면 딱딱한 그림자(가림막 검사 한 번)다. 크면 원천을 픽셀 → 빛 방향에 수직으로 가로지르는
 *          SW_LIGHT2D_SOFT_SHADOW_SAMPLES 점에서 각각 보고 받은 비율을 낸다 — 반그림자 폭이 원천 반경 × 거리 비로 넓어진다(유니티 Shadow Softness 자리).
 */
float swComputeShadow2D( float2 pixel, float2 lightPosition, float softness, uint lightCount )
{
	if ( softness <= 1e-4f )
		return swIsShadowed2D( pixel, lightPosition, lightCount ) ? 0.0f : 1.0f;
	const float2 toLight = lightPosition - pixel;
	const float  len     = length( toLight );
	const float2 across  = ( len > 1e-6f ) ? float2( -toLight.y, toLight.x ) / len : float2( 1.0f, 0.0f );
	float        lit     = 0.0f;
	[unroll]
	for ( uint sampleIndex = 0u; sampleIndex < SW_LIGHT2D_SOFT_SHADOW_SAMPLES; ++sampleIndex )
	{
		const float offset = ( (float)sampleIndex / (float)( SW_LIGHT2D_SOFT_SHADOW_SAMPLES - 1u ) ) * 2.0f - 1.0f;
		lit += swIsShadowed2D( pixel, lightPosition + across * ( offset * softness ), lightCount ) ? 0.0f : 1.0f;
	}
	return lit / (float)SW_LIGHT2D_SOFT_SHADOW_SAMPLES;
}

/**
 * @brief 점 · 스폿 2D 빛의 쿠키(빛 텍스처) 색입니다. 칸이 없거나 아틀라스가 안 걸렸으면 흰색입니다.
 * @details 쿠키는 바깥 반경의 정사각형을 덮고 원뿔 방향(directionType.xy)이 +U, 그 왼쪽(반시계 90 도)이 -V 다(텍스처는 위가 v = 0).
 *          칸 가장자리는 반 텍셀 안쪽으로 잘라 이웃 칸이 번지지 않게 한다. C++ Light2DCookieAtlas 가 칸을 나눈다.
 */
float3 swSampleLight2dCookie( SwLightData light, float2 pixel )
{
	const int cell = (int)floor( light.extra.w + 0.5f );
	if ( cell < 0 || g_Light2DCookieAtlasIndex == kInvalidIndex )
		return float3( 1.0f, 1.0f, 1.0f );
	const float2 delta  = pixel - light.positionRadius.xy;
	const float  outer  = max( light.positionRadius.w, 1e-4f );
	const float2 along  = light.directionType.xy;
	const float2 across = float2( -along.y, along.x );
	const float2 local  = float2( dot( delta, along ), dot( delta, across ) ) / outer; // [-1, 1] 이 바깥 반경
	const float2 cellUv = saturate( float2( local.x * 0.5f + 0.5f, 0.5f - local.y * 0.5f ) );
	const float  cells  = (float)SW_LIGHT2D_COOKIE_CELLS_PER_ROW;
	const float  inset  = 0.5f * cells / (float)SW_LIGHT2D_COOKIE_ATLAS_SIZE;
	const float2 origin = float2( (float)( (uint)cell % SW_LIGHT2D_COOKIE_CELLS_PER_ROW ), (float)( (uint)cell / SW_LIGHT2D_COOKIE_CELLS_PER_ROW ) );
	const float2 atlasUv = ( origin + clamp( cellUv, inset, 1.0f - inset ) ) / cells;
	return swSampleLight2dCookieAtlas( atlasUv ).rgb;
}

/**
 * @brief 빛 받는 스프라이트 픽셀을 2D 빛으로 칠합니다.
 * @param albedo     스프라이트 색(텍스처 × 머티리얼 × 인스턴스 색)
 * @param pixel      월드 X · Y
 * @param normal     월드 노멀(노멀 맵이 있을 때). 스프라이트 평면은 -Z(카메라 쪽)를 본다
 * @param bHasNormal 노멀 맵이 있는가 — 없으면 N·L 을 곱하지 않는다(유니티 Sprite-Lit 과 같다)
 * @details 2D 빛이 하나도 없으면 칠하지 않고 그대로 돌려준다(빛 받는 머티리얼을 쓴 씬에 빛이 없어도 까맣게 죽지 않게).
 *          섞는 방식(extra.y): 더하기 · 빼기를 모두 더하고(0 아래는 0) 곱하기 빛을 곱한다 — 빛 순서와 무관하다. 전역 빛은 늘 더하기다.
 */
float3 swShadeLights2d( float3 albedo, float2 pixel, float3 normal, bool bHasNormal )
{
	const uint lightCount = ( g_SwLightsIndex == kInvalidIndex ) ? 0u : g_SwLightCount;
	float3     added      = float3( 0.0f, 0.0f, 0.0f );
	float3     multiplied = float3( 1.0f, 1.0f, 1.0f );
	bool       bAny2D     = false;
	bool       bAnyAdded  = false; // 더하기 · 빼기 · 전역 빛이 하나라도 있는가 — 곱하기 빛만 있으면 바탕을 1 로 둔다(빛 없는 곳이 까맣지 않게)
	for ( uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex )
	{
		const SwLightData record = g_SwLights[lightIndex];
		const uint        type   = (uint)( record.directionType.w + 0.5f );
		if ( type == SW_LIGHT_TYPE_GLOBAL2D )
		{
			bAny2D    = true;
			bAnyAdded = true;
			added += record.colorIntensity.rgb * record.colorIntensity.a;
			continue;
		}
		const bool bPoint    = type == SW_LIGHT_TYPE_POINT2D;
		const bool bFreeform = type == SW_LIGHT_TYPE_FREEFORM2D;
		if ( bPoint == false && bFreeform == false )
			continue;
		bAny2D            = true;
		bAnyAdded         = bAnyAdded || ( (uint)( record.extra.y + 0.5f ) != SW_LIGHT2D_BLEND_MULTIPLY );
		float attenuation = bPoint ? swComputeLight2dAttenuation( record, pixel ) : swComputeFreeformLight2dAttenuation( record, lightIndex, lightCount, pixel );
		if ( attenuation <= 0.0f )
			continue;
		if ( bHasNormal )
		{
			// 빛은 스프라이트 평면에서 카메라 쪽(-Z)으로 높이만큼 떠 있다(자유 모양은 컴포넌트 원점).
			const float3 toLight = normalize( float3( record.positionRadius.xy - pixel, -max( record.positionRadius.z, 1e-3f ) ) );
			attenuation *= saturate( dot( normal, toLight ) );
		}
		if ( record.params.x > 0.5f )
			attenuation *= swComputeShadow2D( pixel, record.positionRadius.xy, record.extra.x, lightCount );
		const float3 cookie     = bPoint ? swSampleLight2dCookie( record, pixel ) : float3( 1.0f, 1.0f, 1.0f );
		const float3 radiance   = record.colorIntensity.rgb * record.colorIntensity.a * cookie;
		const uint   blendStyle = (uint)( record.extra.y + 0.5f );
		if ( blendStyle == SW_LIGHT2D_BLEND_MULTIPLY )
			multiplied *= lerp( float3( 1.0f, 1.0f, 1.0f ), radiance, attenuation );
		else
		{
			bAnyAdded = true;
			added += ( blendStyle == SW_LIGHT2D_BLEND_SUBTRACTIVE ) ? -radiance * attenuation : radiance * attenuation;
		}
	}
	if ( bAny2D == false )
		return albedo;
	const float3 base = bAnyAdded ? max( added, float3( 0.0f, 0.0f, 0.0f ) ) : float3( 1.0f, 1.0f, 1.0f );
	return albedo * base * multiplied;
}

#endif // SW_ENGINE_LIGHTING2D_HLSLI
