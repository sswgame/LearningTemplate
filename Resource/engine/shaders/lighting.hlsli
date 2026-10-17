/**
 * lighting.hlsli — 씬 라이트 목록과 **조명 식 하나**.
 *
 * 포워드(`forwardlit.hlsl`)와 디퍼드(`deferredlighting.hlsl`)가 이 파일의 같은 함수를 부른다.
 * 조명 식을 두 벌 두면 둘은 반드시 갈라진다("경로마다 다른 그림"). 언리얼도 포워드와 디퍼드가 같은 `FDeferredLightingCommon` 을 나눠 쓴다.
 *
 * 라이트는 상수버퍼가 아니라 **구조버퍼**(t12, `SW_SLOT_LIGHT_SRV`)다 — 개수가 씬마다 다르고,
 * 패스당 한 번 걸면 드로우 사이에 바인딩이 바뀌지 않는다(이 엔진의 규약).
 *
 * 그림자 맵은 **아틀라스 하나**다(C++ `ShadowSetup.h`). 방향광은 캐스케이드마다 사분면 하나(하나뿐이면 아틀라스 전체)를,
 * 점광은 칸 여섯(면마다 90° 원근), 스폿은 칸 하나를 쓴다. 어느 빛이 그림자를 받는지는 원소의 `params.x` 가 말하고,
 * 점광 · 스폿은 `params.w` 가 라이트 버퍼 꼬리(g_SwLightCount 부터)의 첫 타일 원소를 가리킨다.
 */

#ifndef SW_ENGINE_LIGHTING_HLSLI
#define SW_ENGINE_LIGHTING_HLSLI

#include "binding.hlsli"

/**
 * 씬 라이트 한 개. C++ `GPULight` 와 레이아웃이 같아야 한다(float4 넷 = 64바이트).
 * float4 로만 채운 이유는 모프 정점 풀에서 물렸던 것과 같다 — std430 은 vec4 를 16바이트 경계에
 * 맞추는데 DX/Vulkan 은 DXC 가 명시 오프셋을 적어 넘어간다. 그러면 **OpenGL 만** 값이 어긋난다.
 */
struct SwLightData
{
	float4 positionRadius; // xyz 월드 위치(점광·스폿), w 반경 — 방향광은 0
	float4 colorIntensity; // rgb 빛 색, a 세기
	float4 directionType;  // xyz 빛이 나아가는 방향(방향광·스폿), w 타입 (SW_LIGHT_TYPE_*)
	float4 params;         // x 그림자를 받는가(0/1), y cos(바깥 원뿔각), z cos(안쪽 원뿔각), w 점광 · 스폿 그림자의 첫 타일 원소(g_SwLightCount 기준 상대 번호)
};

SW_DECLARE_STRUCTURED_BUFFER( SwLightData, g_SwLights, SW_SLOT_LIGHT_SRV );

/**
 * @brief 아틀라스의 한 점 둘레를 3x3 PCF 로 비교합니다 — 탭 하나가 2x2 쌍선형 비교라 가장자리의 텍셀 계단이 보이지 않는다(가장자리가 텍셀 세 개 폭으로 번진다).
 * @param texelUv  탭 간격(아틀라스 텍셀 하나의 UV 폭)
 * @param clampMin 탭을 이 사각형 안으로 자른다(칸 경계 — 옆 칸을 읽지 않게). 아틀라스 전체면 자르지 않아도 된다(bClamp = false).
 */
float swFilterShadowAtlas( float2 uv, float depth, float texelUv, bool bClamp, float2 clampMin, float2 clampMax )
{
	float lit = 0.0f;
	[unroll] for ( int offsetY = -1; offsetY <= 1; ++offsetY )
	{
		[unroll] for ( int offsetX = -1; offsetX <= 1; ++offsetX )
		{
			const float2 tapUv = uv + float2( offsetX, offsetY ) * texelUv;
			lit += swSampleShadowComparison( g_ShadowMapIndex, bClamp ? clamp( tapUv, clampMin, clampMax ) : tapUv, depth );
		}
	}
	return lit * ( 1.0f / 9.0f );
}

/** @brief 캐스케이드 c 의 상수(배율, 이동 x, 이동 y, 깊이 끝)입니다. */
float4 swLoadShadowCascade( uint cascadeIndex )
{
	return ( cascadeIndex == 0u ) ? g_ShadowCascade0 : ( cascadeIndex == 1u ) ? g_ShadowCascade1 : ( cascadeIndex == 2u ) ? g_ShadowCascade2 : g_ShadowCascade3;
}

/**
 * @brief 방향광 캐스케이드 하나에서 가림을 잽니다(1 = 빛). 그 캐스케이드 볼륨 밖이면 @p outInside 가 false 이고 값은 1 이다.
 * @details 캐스케이드는 기준 볼륨(g_LightViewProj)과 빛 회전 · 깊이 구간이 같아 xy 만 배율 · 이동으로 옮긴다. 텍셀이 배율만큼 작으므로
 *          노멀 오프셋 · 깊이 바이어스(g_ShadowParams.z · x 는 기준 볼륨의 값)도 배율로 나눈다. 칸 원점은 사분면 c 다(칸 배율 0.5 일 때).
 */
float swSampleShadowCascade( uint cascadeIndex, float3 worldPosition, float3 worldNormal, out bool outInside )
{
	const float4 cascade        = swLoadShadowCascade( cascadeIndex );
	const float  scale          = cascade.x;
	const float3 samplePosition = worldPosition + worldNormal * ( g_ShadowParams.z / scale );
	const float4 lightClip      = mul( float4( samplePosition, 1.0f ), g_LightViewProj );
	const float3 ndc            = lightClip.xyz / lightClip.w;
	const float2 tileUv         = ( ndc.xy * scale + cascade.yz ) * float2( 0.5f, -0.5f ) + 0.5f;
	const float  tileScale      = g_ShadowCascadeLayout.y;
	const float2 tileOrigin     = float2( cascadeIndex & 1u, cascadeIndex >> 1u ) * tileScale;
	const float2 uv             = tileOrigin + tileUv * tileScale;
	outInside = lightClip.w > 0.0f && tileUv.x >= 0.0f && tileUv.x <= 1.0f && tileUv.y >= 0.0f && tileUv.y <= 1.0f && ndc.z >= 0.0f && ndc.z <= 1.0f;

	// 바이어스(g_ShadowParams.x · z)가 없으면 자기 자신에 그림자가 져 표면이 줄무늬가 된다(shadow acne). 간격은 아틀라스 텍셀 하나의 UV 폭이다.
	// 칸이 아틀라스 전체가 아니면 탭을 칸 안(반 텍셀 안쪽)으로 자른다 — 옆 사분면은 다른 캐스케이드 · 점광의 깊이다.
	const float  texelUv   = g_ShadowParams.w * tileScale;
	const bool   bClamp    = tileScale < 1.0f;
	const float2 clampMin  = tileOrigin + 0.5f * texelUv;
	const float2 clampMax  = tileOrigin + tileScale - 0.5f * texelUv;
	const float  lit       = outInside ? swFilterShadowAtlas( uv, ndc.z - g_ShadowParams.x / scale, texelUv, bClamp, clampMin, clampMax ) : 1.0f;
	// 세기는 g_ShadowParams.y — 완전한 검정이 아니라 "얼마나 어두워지는가" 다.
	return lerp( 1.0f - g_ShadowParams.y, 1.0f, saturate( lit ) );
}

/**
 * @brief 이 픽셀이 방향광 그림자 맵에서 얼마나 가려졌는지 — 1 이면 빛을 다 받고, 0 에 가까울수록 그늘이다.
 * @param worldNormal 받는 면의 월드 노멀(정규화) — 노멀 오프셋에 쓴다
 * @details 월드 위치를 **라이트 클립 공간으로 투영해** 샘플한다. 그림자 맵을 화면 UV 로 읽으면 그림자가 아니라
 *          "깊이 텍스처를 화면에 붙인 무늬" 라, 카메라가 움직이면 그늘이 물체를 따라오지 않고 화면에 붙는다.
 *          바이어스는 둘이다 — 노멀 쪽으로 `g_ShadowParams.z`(m) 띄운 점을 읽고(노멀 오프셋), 깊이에서 `g_ShadowParams.x`(NDC)를 뺀다.
 *          둘 다 CPU 가 텍셀 크기로 정한다(`DirectionalShadowProjection::computeShaderParams`). 깊이 바이어스만 키우면 그림자가 물체 발치에서 떨어진다.
 *
 *          캐스케이드는 카메라 뷰 깊이(`g_ViewProj` 의 클립 w — 원근에서 뷰 z, 직교에서 1)로 고른다. 끝에서 `g_ShadowCascadeLayout.z` 만큼은
 *          다음 캐스케이드와 섞어 경계가 줄로 보이지 않게 한다(유니티 URP Cascade Blend). 고른 캐스케이드 볼륨 밖이면 다음 캐스케이드를 읽는다.
 * @note 맵 밖은 1(가려지지 않음)이다. 0 으로 두면 그림자 볼륨 밖이 통째로 검게 죽는다. early-return 이 없다(GL 드라이버 함정, binding.hlsli swComputeMorphElement).
 */
float swSampleShadowAtWorld( float3 worldPosition, float3 worldNormal )
{
	float result = 1.0f;
	if ( g_ShadowMapIndex != kInvalidIndex )
	{
		const uint  cascadeCount = (uint)( g_ShadowCascadeLayout.x + 0.5f );
		const float viewDepth    = mul( float4( worldPosition, 1.0f ), g_ViewProj ).w;
		uint        cascadeIndex = cascadeCount;
		[unroll] for ( uint index = 0u; index < SW_SHADOW_CASCADE_MAX; ++index )
		{
			if ( cascadeIndex == cascadeCount && index < cascadeCount && viewDepth <= swLoadShadowCascade( index ).w )
				cascadeIndex = index;
		}
		if ( cascadeIndex < cascadeCount )
		{
			bool        bInside = false;
			const float first   = swSampleShadowCascade( cascadeIndex, worldPosition, worldNormal, bInside );
			result              = first;
			const uint  nextIndex  = cascadeIndex + 1u;
			const float splitEnd   = swLoadShadowCascade( cascadeIndex ).w;
			const float splitStart = ( cascadeIndex == 0u ) ? 0.0f : swLoadShadowCascade( cascadeIndex - 1u ).w;
			const float blendWidth = max( ( splitEnd - splitStart ) * g_ShadowCascadeLayout.z, 1e-4f );
			const float blend      = saturate( ( viewDepth - ( splitEnd - blendWidth ) ) / blendWidth );
			const bool  bUseNext   = nextIndex < cascadeCount && ( bInside == false || blend > 0.0f );
			if ( bUseNext )
			{
				bool        bNextInside = false;
				const float second      = swSampleShadowCascade( nextIndex, worldPosition, worldNormal, bNextInside );
				result                  = ( bInside == false ) ? second : lerp( first, second, bNextInside ? blend : 0.0f );
			}
		}
	}
	return result;
}

/**
 * @brief 점광 · 스폿 하나의 그림자 — 1 이면 그 빛을 다 받는다. 그림자 칸이 없는 빛(`params.x` 가 0)이면 1 이다.
 * @details 타일 원소 둘을 읽는다 — [0] 아틀라스 자리를 접은 뷰-투영(행 넷), [1] 칸 uv 사각형(xy 최소, zw 최대) · (노멀 오프셋 / m, 깊이 바이어스 / m, 텍셀 UV 폭, 세기).
 *          원근 칸은 텍셀 크기가 거리에 비례하므로 바이어스에 빛까지의 거리를 곱한다. 깊이 바이어스는 NDC 가 아니라 **빛 쪽으로 옮긴 월드 점**으로 준다
 *          (원근 깊이는 비선형이라 NDC 상수가 거리마다 다른 월드 길이가 된다). 점광은 빛에서 본 방향의 가장 큰 축으로 면을 고른다(+X −X +Y −Y +Z −Z).
 */
float swSampleLocalShadow( SwLightData light, float3 worldPosition, float3 worldNormal )
{
	const uint   lightType = (uint)( light.directionType.w + 0.5f );
	const float3 fromLight = worldPosition - light.positionRadius.xyz;
	const float3 axis      = abs( fromLight );
	uint         faceIndex = 0u;
	if ( lightType == SW_LIGHT_TYPE_POINT )
	{
		if ( axis.x >= axis.y && axis.x >= axis.z )
			faceIndex = ( fromLight.x >= 0.0f ) ? 0u : 1u;
		else if ( axis.y >= axis.z )
			faceIndex = ( fromLight.y >= 0.0f ) ? 2u : 3u;
		else
			faceIndex = ( fromLight.z >= 0.0f ) ? 4u : 5u;
	}
	const uint        element    = g_SwLightCount + (uint)( light.params.w + 0.5f ) + faceIndex * SW_SHADOW_TILE_ELEMENT_COUNT;
	const SwLightData matrixRows = g_SwLights[element];
	const SwLightData info       = g_SwLights[element + 1u];
	const float4x4    viewProj   = float4x4( matrixRows.positionRadius, matrixRows.colorIntensity, matrixRows.directionType, matrixRows.params );

	const float  distance       = length( fromLight );
	const float3 toLight        = -fromLight / max( distance, 1e-4f );
	const float3 samplePosition = worldPosition + worldNormal * ( info.colorIntensity.x * distance ) + toLight * ( info.colorIntensity.y * distance );
	const float4 lightClip      = mul( float4( samplePosition, 1.0f ), viewProj );
	const float3 ndc            = lightClip.xyz / lightClip.w;
	const float2 uv             = ndc.xy * float2( 0.5f, -0.5f ) + 0.5f;
	const bool   bInside        = lightClip.w > 0.0f && ndc.z >= 0.0f && ndc.z <= 1.0f && uv.x >= info.positionRadius.x && uv.x <= info.positionRadius.z &&
	                     uv.y >= info.positionRadius.y && uv.y <= info.positionRadius.w;
	const float lit = bInside ? swFilterShadowAtlas( uv, ndc.z, info.colorIntensity.z, true, info.positionRadius.xy, info.positionRadius.zw ) : 1.0f;
	return lerp( 1.0f - info.colorIntensity.w, 1.0f, saturate( lit ) );
}

/**
 * @brief 빛 하나가 이 점에 드리우는 그림자 항입니다(1 = 그림자 없음). 방향광은 미리 잰 @p directionalShadow 를, 점광 · 스폿은 자기 타일을 읽는다.
 * @details 조명 식이 둘(`swShadeLights` · 셀 셰이딩 `toon.hlsl`)이어도 그림자 항은 이 한 벌이다.
 */
float swComputeLightShadow( SwLightData light, float3 worldPosition, float3 worldNormal, float directionalShadow )
{
	const uint lightType = (uint)( light.directionType.w + 0.5f );
	float      shadow    = 1.0f;
	if ( light.params.x > 0.5f )
		shadow = ( lightType == SW_LIGHT_TYPE_DIRECTIONAL ) ? directionalShadow : swSampleLocalShadow( light, worldPosition, worldNormal );
	return shadow;
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

/** @brief 이번 패스에 바인딩된 라이트 수입니다. 라이트 버퍼가 바인딩되지 않았으면 0 이고, 그 씬은 앰비언트만 받습니다(키 라이트로 대신하지 않습니다). */
uint swComputeLightCount()
{
	return ( g_SwLightsIndex == kInvalidIndex ) ? 0u : g_SwLightCount;
}

/**
 * 라이트 순회 커서. 조명 식(`swShadeLights`, 셀 셰이딩 `toon.hlsl`)은 라이트를 이 커서로만 돕니다.
 * 조명 식은 버퍼 번호를 직접 세지 않고, `swBeginLights` 로 커서를 연 뒤 `swNextLight` 가 거짓을 낼 때까지 돕니다.
 * 어느 라이트를 도는지(목록 전체 또는 클러스터의 라이트 목록)는 이 두 함수만 압니다. 그래서 컬링 방식을 바꿔도 조명 식은 그대로입니다.
 * 언리얼 `ForwardLightData` 의 그리드 순회, 유니티 Forward+ 의 `LIGHT_LOOP_BEGIN` 에 해당합니다.
 *
 *   SwLightCursor cursor = swBeginLights( worldPosition );
 *   SwLightData   light;
 *   while ( swNextLight( cursor, light ) ) { ... }
 */
struct SwLightCursor
{
	uint position; // 다음에 읽을 라이트 번호
	uint end;      // 이 번호에 닿으면 끝난다
};

/** @brief 이 월드 위치를 비추는 라이트의 순회를 엽니다. 라이트 버퍼가 바인딩되지 않았으면 빈 커서입니다. */
SwLightCursor swBeginLights( float3 worldPosition )
{
	SwLightCursor cursor;
	cursor.position = 0u;
	cursor.end      = swComputeLightCount();
	return cursor;
}

/**
 * @brief 커서의 다음 라이트를 @p outLight 에 꺼냅니다. 남은 라이트가 없으면 거짓이고, @p outLight 는 어느 조명 식도 받지 않는 원소(2D 그림자 세그먼트)입니다.
 * @note early-return 이 없습니다. GL 드라이버가 early-return 형태를 잘못 컴파일한 적이 있습니다(binding.hlsli swComputeMorphElement).
 *       남은 라이트가 없으면 버퍼를 읽지 않습니다. 바인딩되지 않은 버퍼를 읽으면 DX12 와 Vulkan 은 디바이스를 잃습니다.
 */
bool swNextLight( inout SwLightCursor inoutCursor, out SwLightData outLight )
{
	const bool bHasLight = inoutCursor.position < inoutCursor.end;
	outLight.positionRadius = float4( 0.0f, 0.0f, 0.0f, 0.0f );
	outLight.colorIntensity = float4( 0.0f, 0.0f, 0.0f, 0.0f );
	outLight.directionType  = float4( 0.0f, 0.0f, 0.0f, (float)SW_LIGHT_TYPE_SHADOW2D );
	outLight.params         = float4( 0.0f, 0.0f, 0.0f, 0.0f );
	if ( bHasLight )
		outLight = g_SwLights[inoutCursor.position];
	inoutCursor.position += bHasLight ? 1u : 0u;
	return bHasLight;
}

/**
 * @brief 이 표면을 이 위치를 비추는 **모든 라이트**로 셰이딩합니다.
 * @param albedo   표면 색 (앰비언트에도 곱해진다)
 * @param worldPosition 월드 위치 — 점광의 거리 감쇠에 쓴다
 * @param normal   월드 노멀 (정규화되어 있어야 한다)
 * @param shadow   `swSampleShadowAtWorld` 가 준 방향광 값 — `params.x` 가 켜진 방향광에만 곱한다(점광 · 스폿은 `swComputeLightShadow` 가 자기 타일을 읽는다)
 * @details 3D 라이트가 하나도 없으면 앰비언트만 남습니다. 키 라이트로 대신 비추지 않습니다. 3D 씬은 방향광이나 스카이를 데이터로 가집니다(결정 기록 5-12 D2-13).
 */
float3 swShadeLights( float3 albedo, float3 worldPosition, float3 normal, float shadow )
{
#if defined( SW_VIEWMODE_LIGHTINGONLY )
	// 보기 모드 LightingOnly — 포워드 셰이더의 표면 색을 회색으로 바꾼다(binding.hlsli 의 SW_VIEWMODE_LIGHTINGONLY_ALBEDO).
	albedo = SW_VIEWMODE_LIGHTINGONLY_ALBEDO;
#endif
	// 앰비언트는 빛 목록과 무관하게 **한 번만** 더한다 — 라이트마다 더하면 빛을 늘릴수록 화면이 바랜다.
	float3 lit = albedo * g_KeyLightColor.rgb * g_KeyLightColor.a;

	SwLightCursor cursor = swBeginLights( worldPosition );
	SwLightData   light;
	while ( swNextLight( cursor, light ) )
	{
		float3 toLight;
		float  attenuation;
		if ( swComputeLightIncidence( light, worldPosition, toLight, attenuation ) == false )
			continue;

		const float normalDotLight = saturate( dot( normal, toLight ) );
		if ( normalDotLight * attenuation <= 0.0f )
			continue;

		const float shadowTerm = swComputeLightShadow( light, worldPosition, normal, shadow );
		lit += albedo * normalDotLight * attenuation * light.colorIntensity.a * light.colorIntensity.rgb * shadowTerm;
	}
	return lit;
}

#endif // SW_ENGINE_LIGHTING_HLSLI
