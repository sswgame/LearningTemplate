/**
 * lighting.hlsli — 씬 라이트 목록과 **조명 식 하나**.
 *
 * 포워드(`forwardlit.hlsl`)와 디퍼드(`deferredlighting.hlsl`)가 이 파일의 같은 함수를 부른다.
 * 조명 식을 두 벌 두면 둘은 반드시 갈라진다("경로마다 다른 그림"). 언리얼도 포워드와 디퍼드가 같은 `FDeferredLightingCommon` 을 나눠 쓴다.
 *
 * 라이트는 상수버퍼가 아니라 **구조버퍼**(t8, `SW_SLOT_LIGHT_SRV`)다 — 개수가 씬마다 다르고,
 * 패스당 한 번 걸면 드로우 사이에 바인딩이 바뀌지 않는다(이 엔진의 규약).
 *
 * 그림자 맵은 **아틀라스 하나**다(C++ `ShadowSetup.h`). 방향광은 캐스케이드마다 사분면 하나(하나뿐이면 아틀라스 전체)를,
 * 점광은 칸 여섯(면마다 90° 원근), 스폿은 칸 하나를 쓴다. 어느 빛이 그림자를 받는지는 원소의 `params.x` 가 말하고,
 * 점광 · 스폿은 `params.w` 가 라이트 버퍼 꼬리(g_SwLightCount 부터)의 첫 타일 원소를 가리킨다.
 */

#ifndef SW_ENGINE_LIGHTING_HLSLI
#define SW_ENGINE_LIGHTING_HLSLI

#include "binding.hlsli"

// 씬 라이트 한 개(LightData)의 정의는 lightdata.hlsli 하나다 — 컴퓨트(lightgrid.hlsl)도 같은 파일을 쓴다. float4 로만 채운 이유는 그 파일 머리 주석.
#include "lightdata.hlsli"

SW_DECLARE_STRUCTURED_BUFFER( LightData, g_SwLights, SW_SLOT_LIGHT_SRV );
// 라이트 프로브의 SH9(프로브 하나가 float4 일곱 개). 볼륨 원소(SW_LIGHT_TYPE_PROBE_VOLUME)가 첫 프로브 번호를 싣는다 — 읽는 함수는 LightProbe::load 하나다.
SW_DECLARE_STRUCTURED_BUFFER( float4, g_SwLightProbes, SW_SLOT_LIGHT_PROBE_SRV );

// 이 헤더가 내놓는 함수의 묶음 — 선언만 둔다(정의는 아래, binding.hlsli 0-1 절과 같은 방식). 빛 원소 하나의 함수는 LightData(lightdata.hlsli) 멤버다.
/** @brief 그림자 아틀라스 읽기 — 방향광 캐스케이드와 그 필터. 점광 · 스폿은 LightData::sampleLocalShadow 다. */
struct Shadow
{
    static float filterAtlas( float2 uv, float depth, float texelUv, bool bClamp, float2 clampMin, float2 clampMax );
    static float4 loadCascade( uint cascadeIndex );
    static float sampleCascade( uint cascadeIndex, float3 worldPosition, float3 worldNormal, out bool outInside );
    static float sampleAtWorld( float3 worldPosition, float3 worldNormal );
};

/** @brief 라이트 프로브 SH9(g_SwLightProbes). */
struct LightProbe
{
    static float4 loadElement( uint elementIndex );
    static float4 load( uint probeIndex, uint element );
    static float3 evaluateSphericalHarmonics( uint probeIndex, float3 normal );
};

/** @brief 반사 프로브 텍셀 읽기. 원소 하나의 반사는 LightData::sampleReflectionProbe 다. */
struct ReflectionProbe
{
    static float2 encodeOctahedral( float3 direction );
    static float3 sampleLevel( uint firstTexel, uint resolution, uint level, float2 uv );
};

/** @brief 조명 식 — 포워드와 디퍼드가 같은 함수를 부른다. */
struct Lighting
{
    static float3 computeRimLight( float3 normal );
    static float3 shadeLights( float3 albedo, float3 worldPosition, float3 normal, float shadow, float roughness, float metallic );
};

/**
 * @brief 아틀라스의 한 점 둘레를 3x3 PCF 로 비교합니다 — 탭 하나가 2x2 쌍선형 비교라 가장자리의 텍셀 계단이 보이지 않는다(가장자리가 텍셀 세 개 폭으로 번진다).
 * @param texelUv  탭 간격(아틀라스 텍셀 하나의 UV 폭)
 * @param clampMin 탭을 이 사각형 안으로 자른다(칸 경계 — 옆 칸을 읽지 않게). 아틀라스 전체면 자르지 않아도 된다(bClamp = false).
 */
static float Shadow::filterAtlas( float2 uv, float depth, float texelUv, bool bClamp, float2 clampMin, float2 clampMax )
{
    float lit = 0.0f;
    SW_UNROLL for ( int offsetY = -1; offsetY <= 1; ++offsetY )
    {
        SW_UNROLL for ( int offsetX = -1; offsetX <= 1; ++offsetX )
        {
            const float2 tapUv = uv + float2( offsetX, offsetY ) * texelUv;
            lit += Bindless::sampleShadowComparison( g_ShadowMapIndex, bClamp ? clamp( tapUv, clampMin, clampMax ) : tapUv, depth );
        }
    }
    return lit * ( 1.0f / 9.0f );
}

/** @brief 캐스케이드 c 의 상수(배율, 이동 x, 이동 y, 깊이 끝)입니다. */
static float4 Shadow::loadCascade( uint cascadeIndex )
{
    return ( cascadeIndex == 0u ) ? g_ShadowCascade0 : ( cascadeIndex == 1u ) ? g_ShadowCascade1
                                                 : ( cascadeIndex == 2u )     ? g_ShadowCascade2
                                                                              : g_ShadowCascade3;
}

/**
 * @brief 방향광 캐스케이드 하나에서 가림을 잽니다(1 = 빛). 그 캐스케이드 볼륨 밖이면 @p outInside 가 false 이고 값은 1 이다.
 * @details 캐스케이드는 기준 볼륨(g_LightViewProj)과 빛 회전 · 깊이 구간이 같아 xy 만 배율 · 이동으로 옮긴다. 텍셀이 배율만큼 작으므로
 *          노멀 오프셋 · 깊이 바이어스(g_ShadowParams.z · x 는 기준 볼륨의 값)도 배율로 나눈다. 칸 원점은 사분면 c 다(칸 배율 0.5 일 때).
 */
static float Shadow::sampleCascade( uint cascadeIndex, float3 worldPosition, float3 worldNormal, out bool outInside )
{
    const float4 cascade        = Shadow::loadCascade( cascadeIndex );
    const float  scale          = cascade.x;
    const float3 samplePosition = worldPosition + worldNormal * ( g_ShadowParams.z / scale );
    const float4 lightClip      = mul( float4( samplePosition, 1.0f ), g_LightViewProj );
    const float3 ndc            = lightClip.xyz / lightClip.w;
    const float2 tileUv         = ( ndc.xy * scale + cascade.yz ) * float2( 0.5f, -0.5f ) + 0.5f;
    const float  tileScale      = g_ShadowCascadeLayout.y;
    const float2 tileOrigin     = float2( cascadeIndex & 1u, cascadeIndex >> 1u ) * tileScale;
    const float2 uv             = tileOrigin + tileUv * tileScale;
    outInside                   = lightClip.w > 0.0f && tileUv.x >= 0.0f && tileUv.x <= 1.0f && tileUv.y >= 0.0f && tileUv.y <= 1.0f && ndc.z >= 0.0f && ndc.z <= 1.0f;

    // 바이어스(g_ShadowParams.x · z)가 없으면 자기 자신에 그림자가 져 표면이 줄무늬가 된다(shadow acne). 간격은 아틀라스 텍셀 하나의 UV 폭이다.
    // 칸이 아틀라스 전체가 아니면 탭을 칸 안(반 텍셀 안쪽)으로 자른다 — 옆 사분면은 다른 캐스케이드 · 점광의 깊이다.
    const float  texelUv  = g_ShadowParams.w * tileScale;
    const bool   bClamp   = tileScale < 1.0f;
    const float2 clampMin = tileOrigin + 0.5f * texelUv;
    const float2 clampMax = tileOrigin + tileScale - 0.5f * texelUv;
    const float  lit      = outInside ? Shadow::filterAtlas( uv, ndc.z - g_ShadowParams.x / scale, texelUv, bClamp, clampMin, clampMax ) : 1.0f;
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
 * @note 맵 밖은 1(가려지지 않음)이다. 0 으로 두면 그림자 볼륨 밖이 통째로 검게 죽는다. early-return 이 없다(GL 드라이버 함정, binding.hlsli MorphPool::computeElement).
 */
static float Shadow::sampleAtWorld( float3 worldPosition, float3 worldNormal )
{
    float result = 1.0f;
    if ( g_ShadowMapIndex != kInvalidIndex )
    {
        const uint  cascadeCount = (uint)( g_ShadowCascadeLayout.x + 0.5f );
        const float viewDepth    = mul( float4( worldPosition, 1.0f ), g_ViewProj ).w;
        uint        cascadeIndex = cascadeCount;
        SW_UNROLL for ( uint index = 0u; index < SW_SHADOW_CASCADE_MAX; ++index )
        {
            if ( cascadeIndex == cascadeCount && index < cascadeCount && viewDepth <= Shadow::loadCascade( index ).w )
                cascadeIndex = index;
        }
        if ( cascadeIndex < cascadeCount )
        {
            bool        bInside    = false;
            const float first      = Shadow::sampleCascade( cascadeIndex, worldPosition, worldNormal, bInside );
            result                 = first;
            const uint  nextIndex  = cascadeIndex + 1u;
            const float splitEnd   = Shadow::loadCascade( cascadeIndex ).w;
            const float splitStart = ( cascadeIndex == 0u ) ? 0.0f : Shadow::loadCascade( cascadeIndex - 1u ).w;
            const float blendWidth = max( ( splitEnd - splitStart ) * g_ShadowCascadeLayout.z, 1e-4f );
            const float blend      = saturate( ( viewDepth - ( splitEnd - blendWidth ) ) / blendWidth );
            const bool  bUseNext   = nextIndex < cascadeCount && ( bInside == false || blend > 0.0f );
            if ( bUseNext )
            {
                bool        bNextInside = false;
                const float second      = Shadow::sampleCascade( nextIndex, worldPosition, worldNormal, bNextInside );
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
float LightData::sampleLocalShadow( float3 worldPosition, float3 worldNormal )
{
    const uint   lightType = (uint)( _directionType.w + 0.5f );
    const float3 fromLight = worldPosition - _positionRadius.xyz;
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
    const uint      element    = g_SwLightCount + (uint)( _params.w + 0.5f ) + faceIndex * SW_SHADOW_TILE_ELEMENT_COUNT;
    const LightData matrixRows = g_SwLights[element];
    const LightData info       = g_SwLights[element + 1u];
    const float4x4  viewProj   = float4x4( matrixRows._positionRadius, matrixRows._colorIntensity, matrixRows._directionType, matrixRows._params );

    const float  distance       = length( fromLight );
    const float3 toLight        = -fromLight / max( distance, 1e-4f );
    const float3 samplePosition = worldPosition + worldNormal * ( info._colorIntensity.x * distance ) + toLight * ( info._colorIntensity.y * distance );
    const float4 lightClip      = mul( float4( samplePosition, 1.0f ), viewProj );
    const float3 ndc            = lightClip.xyz / lightClip.w;
    const float2 uv             = ndc.xy * float2( 0.5f, -0.5f ) + 0.5f;
    const bool   bInside        = lightClip.w > 0.0f && ndc.z >= 0.0f && ndc.z <= 1.0f && uv.x >= info._positionRadius.x && uv.x <= info._positionRadius.z &&
                         uv.y >= info._positionRadius.y && uv.y <= info._positionRadius.w;
    const float lit = bInside ? Shadow::filterAtlas( uv, ndc.z, info._colorIntensity.z, true, info._positionRadius.xy, info._positionRadius.zw ) : 1.0f;
    return lerp( 1.0f - info._colorIntensity.w, 1.0f, saturate( lit ) );
}

/**
 * @brief 빛 하나가 이 점에 드리우는 그림자 항입니다(1 = 그림자 없음). 방향광은 미리 잰 @p directionalShadow 를, 점광 · 스폿은 자기 타일을 읽는다.
 * @details 조명 식이 둘(`Lighting::shadeLights` · 셀 셰이딩 `toon.hlsl`)이어도 그림자 항은 이 한 벌이다.
 */
float LightData::computeShadow( float3 worldPosition, float3 worldNormal, float directionalShadow )
{
    const uint lightType = (uint)( _directionType.w + 0.5f );
    float      shadow    = 1.0f;
    if ( _params.x > 0.5f )
        shadow = ( lightType == SW_LIGHT_TYPE_DIRECTIONAL ) ? directionalShadow : sampleLocalShadow( worldPosition, worldNormal );
    return shadow;
}

/**
 * @brief 화면 UV와 깊이에서 월드 위치를 복원합니다 (디퍼드 전용).
 * @details G버퍼에 위치를 저장하지 않는다 — 첨부 하나를 통째로 아끼고, 복원은 역행렬 곱 하나다.
 *          이 엔진은 행벡터 규약이라 `mul( 벡터, 행렬 )` 이다(`mul( worldPosition, g_ViewProj )` 와 같은 순서).
 */
static float3 Transform::computeWorldPositionFromDepth( float2 uv, float deviceDepth )
{
    const float2 ndcXy = uv * float2( 2.0f, -2.0f ) + float2( -1.0f, 1.0f );
    const float4 world = mul( float4( ndcXy, deviceDepth, 1.0f ), g_InvViewProj );
    return world.xyz / world.w;
}

/**
 * @brief 3D 빛 하나가 이 위치에 닿는 방향과 감쇠를 구합니다. 2D 빛 · 2D 그림자 토막이면 false 입니다.
 * @param outToLight 표면에서 빛으로 가는 단위 방향
 * @param outAttenuation 거리 · 원뿔 감쇠(방향광은 1). 세기와 색은 곱하지 않는다
 * @details 조명 식이 둘(이 파일의 `Lighting::shadeLights` · 셀 셰이딩 `toon.hlsl`)이어도 "빛이 어디서 얼마나 오는가" 는 한 벌이다 —
 *          감쇠를 두 곳에 적으면 같은 점광이 두 머티리얼에 다른 반경으로 닿는다.
 * @note early-return 이 없다 — GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다(binding.hlsli MorphPool::computeElement).
 */
bool LightData::computeIncidence( float3 worldPosition, out float3 outToLight, out float outAttenuation )
{
    const uint lightType = (uint)( _directionType.w + 0.5f );
    // 2D 빛 · 그림자 토막은 빛 받는 스프라이트(lighting2d.hlsli)의 것이다.
    const bool bLight3d    = ( lightType <= SW_LIGHT_TYPE_SPOT );
    float3     toLight     = -_directionType.xyz;
    float      attenuation = 1.0f;
    if ( bLight3d && lightType != SW_LIGHT_TYPE_DIRECTIONAL )
    {
        // 점광과 스폿은 거리 감쇠가 같다 — 스폿은 거기에 원뿔을 곱할 뿐이다.
        const float3 delta    = _positionRadius.xyz - worldPosition;
        const float  distance = length( delta );
        // 반경이 0 이면 나눗셈이 터진다. 반경 밖은 0 이 되어 그 빛이 계산에서 빠진다.
        const float radius = max( _positionRadius.w, 1e-4f );
        toLight            = delta / max( distance, 1e-4f );
        // 역제곱을 반경에서 자른 형태 — 언리얼의 `InverseSquaredFalloff` 와 같은 모양이다.
        // 물리적으로 정확한 역제곱만 쓰면 빛이 영원히 닿아 타일 컬링이 의미를 잃는다.
        const float normalized = saturate( 1.0f - ( distance / radius ) );
        attenuation            = normalized * normalized;

        if ( lightType == SW_LIGHT_TYPE_SPOT )
        {
            // 원뿔 감쇠. `dot(빛이 나아가는 방향, 빛에서 표면으로 가는 방향)` 이 1 에 가까울수록
            // 원뿔 중심이다. 안쪽 각 안은 1, 바깥 각 밖은 0, 사이는 부드럽게 떨어진다.
            const float cosAngle = dot( _directionType.xyz, -toLight );
            const float cosOuter = _params.y;
            const float cosInner = _params.z;
            // 안쪽과 바깥쪽이 같으면 0 으로 나눈다 — 그 경우 경계가 칼같이 끊긴다.
            const float cone = saturate( ( cosAngle - cosOuter ) / max( cosInner - cosOuter, 1e-4f ) );
            attenuation *= cone * cone;
        }
    }
    outToLight     = toLight;
    outAttenuation = attenuation;
    return bLight3d;
}

/** @brief 라이트 프로브 버퍼(g_SwLightProbes, t13)의 원소 하나입니다 — 이 함수만 그 버퍼를 읽는다(SH9 · 반사 프로브 텍셀이 같은 버퍼다). */
static float4 LightProbe::loadElement( uint elementIndex )
{
    return g_SwLightProbes[elementIndex];
}

/**
 * @brief 라이트 프로브 하나의 SH9 원소 @p element(0..SW_LIGHT_PROBE_FLOAT4_COUNT-1)를 읽습니다. 이 함수만 프로브의 위치를 안다(g_SwLightProbes, t13).
 */
static float4 LightProbe::load( uint probeIndex, uint element )
{
    return LightProbe::loadElement( probeIndex * SW_LIGHT_PROBE_FLOAT4_COUNT + element );
}

/**
 * @brief 프로브 하나의 SH9 를 노멀 @p normal 로 평가합니다. 결과는 조도 / π 다 — 알베도에 곱하면 램버트 반사광이다(앰비언트와 같은 단위).
 * @details C++ `SH9Color::makeGPUElement` 가 상수를 미리 곱해 둔 배치(유니티 ShadeSH9)다: A · (n, 1) + B · (xy, yz, z², xz) + C · (x² − y²).
 *          SphericalHarmonicsTest.GPUElementEvaluatesToIrradianceOverPi 가 같은 식을 C++ 로 확인한다.
 */
static float3 LightProbe::evaluateSphericalHarmonics( uint probeIndex, float3 normal )
{
    const float4 linearInput    = float4( normal, 1.0f );
    const float4 quadraticInput = normal.xyzz * normal.yzzx;
    const float  squareInput    = normal.x * normal.x - normal.y * normal.y;
    const float3 linearTerm     = float3( dot( LightProbe::load( probeIndex, 0u ), linearInput ), dot( LightProbe::load( probeIndex, 1u ), linearInput ),
                                          dot( LightProbe::load( probeIndex, 2u ), linearInput ) );
    const float3 quadraticTerm  = float3( dot( LightProbe::load( probeIndex, 3u ), quadraticInput ), dot( LightProbe::load( probeIndex, 4u ), quadraticInput ),
                                          dot( LightProbe::load( probeIndex, 5u ), quadraticInput ) );
    return max( linearTerm + quadraticTerm + LightProbe::load( probeIndex, 6u ).rgb * squareInput, 0.0f );
}

/**
 * @brief 프로브 볼륨 원소 하나(bindingslots.hlsli 8 절의 SW_LIGHT_TYPE_PROBE_VOLUME 배치)에서 이 위치의 간접광을 구합니다.
 * @return rgb = 조도 / π(이웃 프로브 여덟의 삼선형 보간), a = 가중치 — 상자 안이면 1, 밖이면 0 이다. 상자 밖을 가장 가까운 프로브로 자르지 않는다
 *         (IndirectLighting.md P3 계약 — 볼륨이 없는 곳은 볼륨의 빛을 받지 않는다). 프로브 버퍼가 안 걸렸으면 0 이다.
 */
float4 LightData::sampleProbeVolume( float3 worldPosition, float3 normal )
{
    const float3 origin    = _colorIntensity.xyz;
    const float3 spacing   = max( _directionType.xyz, 1e-4f );
    const uint3  count     = uint3( _params.xyz + 0.5f );
    const uint   probeBase = (uint)( _params.w + 0.5f );
    const float3 lastCell  = float3( max( count, 1u ) - 1u );
    const float3 local     = ( worldPosition - origin ) / spacing;
    // 프로브가 하나뿐인 축은 그 축의 두께가 0 이다 — 간격 반 안을 안으로 본다.
    const float3 multiAxis  = float3( count > 1u ); // 1 이면 그 축에 프로브가 둘 이상
    const float3 lowBound   = multiAxis * 0.5f - 0.5f;
    const float3 highBound  = lerp( float3( 0.5f, 0.5f, 0.5f ), lastCell, multiAxis );
    const bool   bInside    = all( local >= lowBound ) && all( local <= highBound ) && g_SwLightProbesIndex != kInvalidIndex;
    const float3 clamped    = clamp( local, 0.0f, lastCell );
    const uint3  cell       = min( uint3( clamped ), uint3( max( lastCell - 1.0f, 0.0f ) ) );
    const float3 blend      = saturate( clamped - float3( cell ) ) * multiAxis;
    const uint3  nextOffset = uint3( count > 1u );
    float3       result     = float3( 0.0f, 0.0f, 0.0f );
    if ( bInside )
    {
        SW_UNROLL for ( uint corner = 0u; corner < 8u; ++corner )
        {
            const uint3  pick       = uint3( corner & 1u, ( corner >> 1u ) & 1u, ( corner >> 2u ) & 1u );
            const uint3  probeCell  = cell + pick * nextOffset;
            const float3 axisWeight = lerp( 1.0f - blend, blend, float3( pick ) );
            const uint   probeIndex = probeBase + ( probeCell.z * count.y + probeCell.y ) * count.x + probeCell.x;
            result += LightProbe::evaluateSphericalHarmonics( probeIndex, normal ) * ( axisWeight.x * axisWeight.y * axisWeight.z );
        }
    }
    return float4( result, bInside ? 1.0f : 0.0f );
}

/**
 * @brief 길이 1 의 방향을 옥타헤드럴 사각형의 UV([0, 1]²)로 접습니다(+Y 가 가운데). C++ `ReflectionProbeData::encodeOctahedral` 과 같은 식이다.
 * @details 부호는 0 을 양수로 친다 — HLSL `sign` 은 0 에서 0 이라 쓰지 않는다(C++ 와 갈린다).
 */
static float2 ReflectionProbe::encodeOctahedral( float3 direction )
{
    const float3 folded     = direction / max( abs( direction.x ) + abs( direction.y ) + abs( direction.z ), 1e-8f );
    const float2 signXz     = float2( folded.x >= 0.0f ? 1.0f : -1.0f, folded.z >= 0.0f ? 1.0f : -1.0f );
    const float2 lower      = ( 1.0f - abs( folded.zx ) ) * signXz;
    const float2 octahedral = ( folded.y >= 0.0f ) ? folded.xz : lower;
    return octahedral * 0.5f + 0.5f;
}

/** @brief 반사 프로브의 한 거칠기 단계를 방향 @p direction 으로 쌍선형 샘플합니다. 사각형 가장자리는 자른다(C++ `ReflectionProbeData::sampleLevel` 과 같다). */
static float3 ReflectionProbe::sampleLevel( uint firstTexel, uint resolution, uint level, float2 uv )
{
    const float  lastTexel = (float)( resolution - 1u );
    const float2 texel     = clamp( uv * (float)resolution - 0.5f, 0.0f, lastTexel );
    const uint2  low       = uint2( floor( texel ) );
    const uint2  high      = min( low + 1u, uint2( resolution - 1u, resolution - 1u ) );
    const float2 blend     = texel - float2( low );
    const uint   levelBase = firstTexel + level * resolution * resolution;
    const float3 topLeft   = LightProbe::loadElement( levelBase + low.y * resolution + low.x ).rgb;
    const float3 topRight  = LightProbe::loadElement( levelBase + low.y * resolution + high.x ).rgb;
    const float3 lowLeft   = LightProbe::loadElement( levelBase + high.y * resolution + low.x ).rgb;
    const float3 lowRight  = LightProbe::loadElement( levelBase + high.y * resolution + high.x ).rgb;
    return lerp( lerp( topLeft, topRight, blend.x ), lerp( lowLeft, lowRight, blend.x ), blend.y );
}

/**
 * @brief 반사 프로브 원소 하나(bindingslots.hlsli 8 절의 SW_LIGHT_TYPE_REFLECTION_PROBE 배치)에서 이 위치 · 반사 방향의 빛을 구합니다.
 * @return rgb = 거칠기에 맞게 거른 빛(두 단계 사이를 섞는다), a = 가중치 — 상자 안 깊이 들어가면 1, 경계 안쪽 섞는 폭에서 0 으로 떨어지고 상자 밖은 0.
 *         프로브 버퍼가 안 걸렸으면 0 이다. 텍셀은 단계마다 같은 크기라 거칠기 단계를 고르는 것은 번호 식 하나다.
 */
float4 LightData::sampleReflectionProbe( float3 worldPosition, float3 reflectDirection, float roughness )
{
    const float3 halfExtent = _colorIntensity.xyz;
    const float  blendWidth = max( _colorIntensity.w, 1e-3f );
    const float3 inside     = halfExtent - abs( worldPosition - _positionRadius.xyz ); // 축마다 경계까지 남은 거리(음수면 밖)
    const float  weight     = saturate( min( inside.x, min( inside.y, inside.z ) ) / blendWidth );
    const uint   resolution = (uint)( _params.x + 0.5f );
    const uint   levelCount = (uint)( _params.y + 0.5f );
    const uint   firstTexel = (uint)( _params.z + 0.5f );
    const bool   bUsable    = weight > 0.0f && resolution > 0u && levelCount > 1u && g_SwLightProbesIndex != kInvalidIndex;
    float3       radiance   = float3( 0.0f, 0.0f, 0.0f );
    if ( bUsable )
    {
        const float2 uv        = ReflectionProbe::encodeOctahedral( reflectDirection );
        const float  levelPick = saturate( roughness ) * (float)( levelCount - 1u );
        const uint   lowLevel  = min( (uint)levelPick, levelCount - 1u );
        const uint   highLevel = min( lowLevel + 1u, levelCount - 1u );
        radiance               = lerp( ReflectionProbe::sampleLevel( firstTexel, resolution, lowLevel, uv ), ReflectionProbe::sampleLevel( firstTexel, resolution, highLevel, uv ),
                                       levelPick - (float)lowLevel );
    }
    return float4( radiance, bUsable ? weight : 0.0f );
}

/**
 * 라이트 순회 커서. 조명 식(`Lighting::shadeLights`, 셀 셰이딩 `toon.hlsl`)은 라이트를 이 커서로만 돕니다.
 * 조명 식은 버퍼 번호를 직접 세지 않고, `LightCursor::begin` 로 커서를 연 뒤 `LightCursor::next` 가 거짓을 낼 때까지 돕니다.
 * 어느 라이트를 도는지(목록 전체 또는 클러스터의 라이트 목록)는 이 두 함수만 압니다. 그래서 컬링 방식을 바꿔도 조명 식은 그대로입니다.
 * 언리얼 `ForwardLightData` 의 그리드 순회, 유니티 Forward+ 의 `LIGHT_LOOP_BEGIN` 에 해당합니다.
 *
 *   LightCursor cursor = LightCursor::begin( worldPosition );
 *   LightData   light;
 *   while ( cursor.next( light ) ) { ... }
 */
struct LightCursor
{
    uint _position;        // 첫 구간의 다음 항목. 그리드가 없으면 라이트 번호, 있으면 전역 구간(방향광)의 그리드 단어 번호다
    uint _end;             // 첫 구간의 끝
    uint _clusterPosition; // 둘째 구간(이 위치의 클러스터 목록)의 다음 그리드 단어 번호. 그리드가 없으면 빈 구간이다
    uint _clusterEnd;      // 둘째 구간의 끝
    uint _bGrid;           // 1 이면 두 구간의 항목이 그리드 단어이고 라이트 번호는 그 단어에 있다

    static LightCursor begin( float3 worldPosition );
    bool next( out LightData outLight ); // 커서를 한 칸 옮긴다(멤버를 고친다)
};

/**
 * @brief 이 월드 위치를 비추는 라이트의 순회를 엽니다. 라이트 버퍼가 바인딩되지 않았으면 빈 커서입니다.
 * @details 지금 뷰에 라이트 그리드가 있으면(`LightGrid::hasGrid`) 전역 구간(방향광)과 이 위치의 클러스터 목록을 돈다. 없으면(`gv_lightCulling` 0) 목록 전체를 돈다.
 */
static LightCursor LightCursor::begin( float3 worldPosition )
{
    LightCursor cursor;
    cursor._position        = 0u;
    cursor._end             = LightGrid::computeLightCount();
    cursor._clusterPosition = 0u;
    cursor._clusterEnd      = 0u;
    cursor._bGrid           = 0u;
    if ( cursor._end > 0u && LightGrid::hasGrid() )
    {
        const uint cluster      = LightGrid::computeCluster( worldPosition );
        const uint indexBase    = LightGrid::loadWord( SW_LIGHT_GRID_WORD_INDEX_BASE );
        const uint clusterWord  = LightGrid::loadWord( SW_LIGHT_GRID_WORD_CLUSTER_BASE ) + cluster * SW_LIGHT_GRID_WORDS_PER_CLUSTER;
        cursor._position        = indexBase;
        cursor._end             = indexBase + LightGrid::loadWord( SW_LIGHT_GRID_WORD_GLOBAL_COUNT );
        cursor._clusterPosition = indexBase + LightGrid::loadWord( clusterWord );
        cursor._clusterEnd      = cursor._clusterPosition + LightGrid::loadWord( clusterWord + 1u );
        cursor._bGrid           = 1u;
    }
    return cursor;
}

/**
 * @brief 커서의 다음 라이트를 @p outLight 에 꺼냅니다. 남은 라이트가 없으면 거짓이고, @p outLight 는 어느 조명 식도 받지 않는 원소(2D 그림자 세그먼트)입니다.
 * @note early-return 이 없습니다. GL 드라이버가 early-return 형태를 잘못 컴파일한 적이 있습니다(binding.hlsli MorphPool::computeElement).
 *       남은 라이트가 없으면 버퍼를 읽지 않습니다. 바인딩되지 않은 버퍼를 읽으면 DX12 와 Vulkan 은 디바이스를 잃습니다.
 *       그리드의 라이트 번호가 라이트 수를 넘으면(그리드가 깨졌다) 그 항목은 조명 식이 받지 않는 원소로 건너뛴다 — 버퍼 밖을 읽지 않는다.
 */
bool LightCursor::next( out LightData outLight )
{
    const bool bFirst     = _position < _end;
    const bool bSecond    = ( bFirst == false ) && ( _clusterPosition < _clusterEnd );
    const bool bHasLight  = bFirst || bSecond;
    const uint entry      = bFirst ? _position : _clusterPosition;
    uint       lightIndex = entry;
    if ( bHasLight && _bGrid != 0u )
        lightIndex = LightGrid::loadWord( entry );
    outLight._positionRadius = float4( 0.0f, 0.0f, 0.0f, 0.0f );
    outLight._colorIntensity = float4( 0.0f, 0.0f, 0.0f, 0.0f );
    outLight._directionType  = float4( 0.0f, 0.0f, 0.0f, (float)SW_LIGHT_TYPE_SHADOW2D );
    outLight._params         = float4( 0.0f, 0.0f, 0.0f, 0.0f );
    outLight._extra          = float4( 0.0f, 0.0f, 0.0f, 0.0f );
    if ( bHasLight && lightIndex < g_SwLightCount )
        outLight = g_SwLights[lightIndex];
    _position += bFirst ? 1u : 0u;
    _clusterPosition += bSecond ? 1u : 0u;
    return bHasLight;
}

// ------------------------------------------------------------------------------
// 금속도 · 거칠기 PBR (IndirectLighting.md P5) — 언리얼 기본 셰이딩 모델과 같은 모양이다: 확산은 램버트, 반사는 GGX 분포 · 스미스 결합 가시성(근사) · 슐릭 프레넬.
// 단위: 이 엔진의 확산 항은 π 로 나누지 않는다(빛 세기가 π 를 품는다). 반사 항도 같은 단위가 되게 BRDF 에 π 를 곱한다 — 두 항의 비는 물리식과 같다.
// 툰(toon.hlsl)은 이 식을 쓰지 않는다(결정 기록 5-12 D2-12 — 툰은 별도 경로).
// ------------------------------------------------------------------------------
static const float kDielectricSpecular = 0.04f;  // 유전체의 수직 반사율 F0(언리얼 Specular 0.5)
static const float kMinRoughness       = 0.045f; // 거칠기 0 의 GGX 는 점 하나라 반짝임이 사라지고 나눗셈이 터진다(필라멘트의 하한)

/** @brief PBR 표면 — 금속도가 기본 색을 확산색과 F0 로 나눈다(언리얼 DiffuseColor · SpecularColor). */
struct SurfacePBR
{
    float3 _diffuseColor;
    float3 _specularColor; // F0
    float  _roughness;     // 지각 거칠기(하한을 건 값)
    float  _alpha;         // GGX α = 거칠기²

    static SurfacePBR make( float3 baseColor, float roughness, float metallic );
    float3 evaluateSpecular( float3 normal, float3 toView, float3 toLight, float normalDotLight );
    float3 computeEnvironmentSpecular( float normalDotView );
};

static SurfacePBR SurfacePBR::make( float3 baseColor, float roughness, float metallic )
{
    const float clampedMetallic = saturate( metallic );
    SurfacePBR  surface;
    surface._diffuseColor  = baseColor * ( 1.0f - clampedMetallic );
    surface._specularColor = lerp( float3( kDielectricSpecular, kDielectricSpecular, kDielectricSpecular ), baseColor, clampedMetallic );
    surface._roughness     = max( saturate( roughness ), kMinRoughness );
    surface._alpha         = surface._roughness * surface._roughness;
    return surface;
}

/**
 * @brief 이 월드 위치에서 카메라로 가는 단위 방향입니다. 원근 · 직교 둘 다 맞다 — 이 위치를 지나는 시선의 가까운 면 점과 먼 면 점을 잇는다.
 * @details PassCB 에 카메라 위치 칸을 두지 않고 뷰-투영(g_ViewProj · g_InvViewProj)에서 구한다 — 직교 카메라는 위치가 시선 방향을 정하지 않는다.
 */
static float3 View::computeToView( float3 worldPosition )
{
    const float4 clip      = mul( float4( worldPosition, 1.0f ), g_ViewProj );
    const float2 ndcXy     = clip.xy / max( abs( clip.w ), 1e-6f ) * sign( clip.w ); // 카메라 뒤의 점(w < 0)도 시선이 맞는다(툰 외곽선 · 물의 정점)
    const float4 nearPoint = mul( float4( ndcXy, 0.0f, 1.0f ), g_InvViewProj );
    const float4 farPoint  = mul( float4( ndcXy, 1.0f, 1.0f ), g_InvViewProj );
    return normalize( nearPoint.xyz / nearPoint.w - farPoint.xyz / farPoint.w );
}

/** @brief 빛 하나의 반사 BRDF × π(엔진 단위) × N·L 입니다. 언리얼 `SpecularGGX` 의 D_GGX · Vis_SmithJointApprox · F_Schlick 과 같다. */
float3 SurfacePBR::evaluateSpecular( float3 normal, float3 toView, float3 toLight, float normalDotLight )
{
    const float3 halfway       = normalize( toView + toLight );
    const float  normalDotHalf = saturate( dot( normal, halfway ) );
    const float  normalDotView = max( saturate( dot( normal, toView ) ), 1e-4f );
    const float  viewDotHalf   = saturate( dot( toView, halfway ) );
    const float  alphaSquared  = _alpha * _alpha;
    const float  denominator   = normalDotHalf * normalDotHalf * ( alphaSquared - 1.0f ) + 1.0f;
    // D · π — 분포의 1/π 와 엔진 단위의 π 가 지워진다.
    const float  distributionTimesPi = alphaSquared / max( denominator * denominator, 1e-8f );
    const float  visibility          = 0.5f / max( normalDotLight * ( normalDotView * ( 1.0f - _alpha ) + _alpha ) +
                                                       normalDotView * ( normalDotLight * ( 1.0f - _alpha ) + _alpha ),
                                                   1e-5f );
    const float  fresnelWeight       = pow( 1.0f - viewDotHalf, 5.0f );
    const float3 fresnel             = _specularColor + ( 1.0f - _specularColor ) * fresnelWeight;
    return distributionTimesPi * visibility * fresnel * normalDotLight;
}

/**
 * @brief 고른 환경광 아래의 반사 비율(반구 적분)의 근사입니다 — 언리얼 모바일 `EnvBRDFApprox`(Karis 2014).
 * @details 환경 반사의 빛은 반사 프로브(IndirectLighting.md P4)가 덮는 곳은 그 프로브의 빛, 아니면 앰비언트 색 하나다. 이 비율을 곱해야 금속이 그늘에서 검게 죽지 않는다.
 */
float3 SurfacePBR::computeEnvironmentSpecular( float normalDotView )
{
    const float4 c0        = float4( -1.0f, -0.0275f, -0.572f, 0.022f );
    const float4 c1        = float4( 1.0f, 0.0425f, 1.04f, -0.04f );
    const float4 fitted    = _roughness * c0 + c1;
    const float  a004      = min( fitted.x * fitted.x, exp2( -9.28f * normalDotView ) ) * fitted.x + fitted.y;
    const float2 scaleBias = float2( -1.04f, 1.04f ) * a004 + fitted.zw;
    return _specularColor * scaleBias.x + scaleBias.y;
}

/**
 * @brief 시선 쪽 가장자리를 키 라이트 색으로 살짝 밝히는 림 항입니다. 포워드 · 디퍼드가 **같은 함수**를 부릅니다.
 * @param normal 월드 노멀
 * @details 상수(지수 3 · 세기 0.12)는 이 함수 한 곳에만 있다. 예전에는 포워드(2 · 0.15)와 디퍼드(3 · 0.12)가 각자 들고 있어 같은 표면이 경로마다 달랐다.
 */
static float3 Lighting::computeRimLight( float3 normal )
{
    const float rim = pow( 1.0f - saturate( dot( normal, float3( 0.0f, 0.0f, 1.0f ) ) ), 3.0f ) * 0.12f;
    return rim * g_KeyLightColor.rgb;
}

/**
 * @brief 이 표면을 이 위치를 비추는 **모든 라이트**로 셰이딩합니다(금속도 · 거칠기 PBR).
 * @param albedo   기본 색(언리얼 BaseColor) — 금속도가 확산색과 F0 로 나눈다
 * @param worldPosition 월드 위치 — 점광의 거리 감쇠와 시선 방향에 쓴다
 * @param normal   월드 노멀 (정규화되어 있어야 한다)
 * @param shadow   `Shadow::sampleAtWorld` 가 준 방향광 값 — `params.x` 가 켜진 방향광에만 곱한다(점광 · 스폿은 `LightData::computeShadow` 가 자기 타일을 읽는다)
 * @param roughness · metallic 머티리얼 칸(포워드) 또는 G버퍼 머티리얼 칸(디퍼드)의 값. 기본값을 두지 않는다 — 셰이더마다 자기 값을 넘긴다
 * @details 3D 라이트가 하나도 없으면 앰비언트만 남습니다. 키 라이트로 대신 비추지 않습니다. 3D 씬은 방향광이나 스카이를 데이터로 가집니다(결정 기록 5-12 D2-13).
 *          이 위치를 덮는 라이트 프로브 볼륨(커서가 도는 SW_LIGHT_TYPE_PROBE_VOLUME 원소)이 있으면 확산 앰비언트 대신 그 볼륨들의 SH 를 가중 평균해 쓴다
 *          (IndirectLighting.md P3). 볼륨이 없는 곳은 지금까지의 앰비언트 그대로다. 이 위치를 덮는 반사 프로브(SW_LIGHT_TYPE_REFLECTION_PROBE 원소)가 있으면
 *          반사 앰비언트를 그 프로브가 찍어 거른 빛으로 바꾼다(P4 — 거칠기로 단계를 고르고 반사 방향으로 읽는다). 프로브가 없는 곳은 앰비언트 색 그대로다.
 */
static float3 Lighting::shadeLights( float3 albedo, float3 worldPosition, float3 normal, float shadow, float roughness, float metallic )
{
    // 보기 모드 LightingOnly 는 포워드 셰이더의 표면 색을 회색으로 바꾼다(ViewMode::overrideLightingAlbedo — binding.hlsli).
    albedo                         = ViewMode::overrideLightingAlbedo( albedo );
    const SurfacePBR surface       = SurfacePBR::make( albedo, roughness, metallic );
    const float3     toView        = View::computeToView( worldPosition );
    const float      normalDotView = max( saturate( dot( normal, toView ) ), 1e-4f );
    // 앰비언트는 빛 목록과 무관하게 **한 번만** 더한다 — 라이트마다 더하면 빛을 늘릴수록 화면이 바랜다.
    const float3 ambient             = g_KeyLightColor.rgb * g_KeyLightColor.a;
    const float3 environmentSpecular = surface.computeEnvironmentSpecular( normalDotView );
    const float3 reflectDirection    = reflect( -toView, normal );
    float3       lit                 = ( surface._diffuseColor + environmentSpecular ) * ambient;
    float3       probeSum            = float3( 0.0f, 0.0f, 0.0f );
    float        probeWeight         = 0.0f;
    float3       reflectionSum       = float3( 0.0f, 0.0f, 0.0f );
    float        reflectionWeight    = 0.0f;

    LightCursor cursor = LightCursor::begin( worldPosition );
    LightData   light;
    while ( cursor.next( light ) )
    {
        const uint lightType = (uint)( light._directionType.w + 0.5f );
        if ( lightType == SW_LIGHT_TYPE_PROBE_VOLUME )
        {
            const float4 probe = light.sampleProbeVolume( worldPosition, normal );
            probeSum += probe.rgb * probe.a;
            probeWeight += probe.a;
            continue;
        }
        if ( lightType == SW_LIGHT_TYPE_REFLECTION_PROBE )
        {
            const float4 reflection = light.sampleReflectionProbe( worldPosition, reflectDirection, surface._roughness );
            reflectionSum += reflection.rgb * reflection.a;
            reflectionWeight += reflection.a;
            continue;
        }
        float3 toLight;
        float  attenuation;
        if ( light.computeIncidence( worldPosition, toLight, attenuation ) == false )
            continue;

        const float normalDotLight = saturate( dot( normal, toLight ) );
        if ( normalDotLight * attenuation <= 0.0f )
            continue;

        const float  shadowTerm = light.computeShadow( worldPosition, normal, shadow );
        const float3 response   = surface._diffuseColor * normalDotLight + surface.evaluateSpecular( normal, toView, toLight, normalDotLight );
        lit += response * attenuation * light._colorIntensity.a * light._colorIntensity.rgb * shadowTerm;
    }
    // 볼륨이 덮는 곳은 확산 앰비언트를 프로브의 간접광으로 바꾼다(겹친 볼륨은 가중 평균). 덮지 않으면 아무 연산도 더하지 않는다.
    if ( probeWeight > 0.0f )
        lit += surface._diffuseColor * ( probeSum / probeWeight - ambient );
    // 반사 프로브가 덮는 곳은 반사 앰비언트를 프로브의 빛으로 바꾼다(IndirectLighting.md P4). 겹친 프로브는 가중 평균이고, 가중치 합이 1 보다 작은 경계는
    // 모자란 몫을 앰비언트로 채워 상자 밖과 이어진다. 덮지 않으면 아무 연산도 더하지 않는다.
    if ( reflectionWeight > 0.0f )
    {
        const float3 environment = ( reflectionSum + ambient * max( 1.0f - reflectionWeight, 0.0f ) ) / max( reflectionWeight, 1.0f );
        lit += environmentSpecular * ( environment - ambient );
    }
    return lit;
}

#endif // SW_ENGINE_LIGHTING_HLSLI
