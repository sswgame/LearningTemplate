/**
 * @file FogSettings.h
 * @brief 높이 · 거리 지수 안개 한 벌(색, 밀도, 높이 감쇠, 기준 높이, 시작 거리, 최대 불투명도, 방향광 쪽 산란)과 시선을 따라 쌓인 불투명도 식입니다.
 * @details 값은 씬의 `FogComponent`(와 물속 안개)가 정하고 렌더 패킷(`RenderFramePacket::_fog`)이 렌더 스레드로 나른다. 렌더러가 컴포넌트 헤더를 보지 않도록 값 타입만 따로 둔다.
 *          안개 패스(`RenderPassType::Fog`, fog.hlsl)가 같은 식을 셰이더로 센다 — 식은 언리얼 ExponentialHeightFog 와 같은 모양이다:
 *          밀도(h) = `_density` · exp( -`_heightFalloff` · ( h - `_baseHeight` ) ), 시선 위 광학 두께 = 밀도의 선적분, 불투명도 = min( `_maxOpacity`, 1 - exp( -광학 두께 ) ).
 *          `_startDistance` 안쪽 구간은 적분에서 뺀다. 셰이더 상수는 `g_FogColorDensity`(rgb 색, a 밀도) · `g_FogHeightParams`(x 높이 감쇠, y 기준 높이, z 시작 거리, w 최대 불투명도) ·
 *          `g_FogInscatter`(rgb 방향광 산란 색 × 세기, w 지수) · `g_FogVolumeParams`(x 볼륨 안개를 켠 1 · 끈 0, y 볼륨 거리, z 산란 배율, w 이방성)로 싣는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
    /**
     * @brief 안개 한 벌입니다. 물속 안개(`WaterBodyComponent::findUnderwaterFog`)도 같은 형식으로 덮는다 — 높이 감쇠 0 의 균일한 안개다.
     */
    struct SW_API FogSettings
    {
        float3  _color{ 0.50f, 0.60f, 0.70f };       ///< 안개 색(선형 RGB)
        float32 _density{ 0.02f };                   ///< 기준 높이의 밀도(1/m)
        float32 _heightFalloff{ 0.2f };              ///< 높이 감쇠(1/m, 0 이면 높이와 무관한 균일 안개)
        float32 _baseHeight{ 0.0f };                 ///< 밀도가 `_density` 인 월드 높이(m)
        float32 _startDistance{ 0.0f };              ///< 이 거리 안쪽은 안개가 없다(m)
        float32 _maxOpacity{ 1.0f };                 ///< 불투명도 상한(0..1)
        float3  _inscatterColor{ 1.0f, 0.9f, 0.7f }; ///< 방향광 쪽 시선에 더하는 산란 색(선형 RGB)
        float32 _inscatterIntensity{ 0.0f };         ///< 방향광 산란 세기(0 이면 끈다)
        float32 _inscatterExponent{ 8.0f };          ///< 방향광 쪽으로 좁히는 지수(클수록 해 둘레만)
        float32 _volumeDistance{ 64.0f };            ///< 볼륨 안개가 덮는 거리(m, 볼륨 안개를 켠 때만)
        float32 _scattering{ 1.0f };                 ///< 볼륨 안개의 산란 배율(라이트가 안개에 비치는 세기)
        float32 _anisotropy{ 0.3f };                 ///< 볼륨 안개 위상 함수의 이방성(-1 뒤로 산란 … 1 앞으로 산란)
        uint8   _bVolumetric{ SW_FALSE };            ///< 볼륨 안개(froxel)를 켠다 — 라이트 산란을 넣는다(`VolumetricFog`)
        uint8   _bUnderwater{ SW_FALSE };            ///< 물속 안개로 덮은 값이다(진단용)

        /**
         * @brief 눈 @p eye 에서 점 @p point 까지 시선 위의 광학 두께(밀도의 선적분)입니다. fog.hlsl `computeOpticalDepth` 와 같은 식입니다.
         * @details 시작 거리 안쪽은 뺀다. 높이 감쇠가 0 이거나 시선이 수평에 가까우면(지수 인자가 0 에 가까우면) 길이 × 밀도다.
         */
        [[nodiscard]] float32 computeOpticalDepth( const float3& eye, const float3& point ) const;
        /** @brief 그 시선의 안개 불투명도입니다: min( `_maxOpacity`, 1 - exp( -광학 두께 ) ). */
        [[nodiscard]] float32 computeOpacity( const float3& eye, const float3& point ) const;
        /** @brief 방향광 산란 항의 가중치입니다: pow( saturate( dot( 시선 방향, 해 쪽 방향 ) ), 지수 ). 방향은 단위 벡터여야 한다. */
        [[nodiscard]] float32 computeInscatterWeight( const float3& viewDirection, const float3& toSunDirection ) const;
    };
} // namespace sw
