/**
 * @file SkyGradient.h
 * @brief 하늘 한 벌(그러데이션 또는 대기 산란, 구름 층)과 방향마다의 하늘빛 식입니다.
 * @details 값은 씬의 `SkyComponent` 가 정하고, 렌더 패킷(`RenderFramePacket::_sky`)이 렌더 스레드로 나른다. 렌더러가 컴포넌트 헤더를 보지 않도록 값 타입만 따로 둔다.
 *          모드가 둘이다 — `Gradient`(천정 · 지평선 · 땅 3색)와 `Atmosphere`(레일리 · 미 단일 산란, `SkyAtmosphere`). 구름(`SkyClouds`)은 모드와 무관하게 하늘 위에 얹는다(평면 노이즈 층 — 셰이더만 그리고
 *          프로브 베이크는 구름을 보지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 하늘을 어떻게 그리는가입니다. */
    ENUM()
    enum class SkyMode : uint8
    {
        Gradient = 0, ///< 천정 · 지평선 · 땅 3색 그러데이션(`sky.hlsl` 의 evaluateSky)
        Atmosphere,   ///< 레일리 · 미 단일 산란(`SkyAtmosphere::evaluateScattering`) — 시간대는 방향광 방향이 정한다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 대기 산란의 값과 식입니다(Nishita 1993 / 스크래치어픽셀 대기 튜토리얼의 단일 산란). 거리 단위는 km 입니다.
     * @details 눈은 행성 중심에서 `_planetRadiusKm + _observerAltitudeKm` 위(+y)에 있다. 시선을 대기 끝(또는 행성 표면)까지 `kViewStepCount` 구간으로 걸으며 구간마다
     *          해 쪽으로 `kLightStepCount` 구간의 광학 두께를 구한다. 레일리(산란 계수 `computeRayleighBeta`, 규모 높이 `kRayleighScaleHeightKm`)와 미(`computeMieBeta`, `kMieScaleHeightKm`,
     *          소멸 = 산란 × `kMieExtinctionRatio`, 위상 함수 이방성 `_mieAnisotropy`)의 합이다. 시선이 행성 표면을 만나면 땅의 반사(알베도 × N·L × 해 투과율 / π)를 더한다.
     *          행성이 해를 가리는 구간은 해 빛이 없다(행성 그림자). 결과는 **해 복사휘도 1 당** 값이고 부르는 쪽이 해 색 · 세기를 곱한다.
     *          셰이더 `sky.hlsl` 의 `computeAtmosphere` 가 같은 식 · 같은 구간 수 · 같은 상수다 — 식을 고치면 두 곳을 함께 고친다.
     */
    struct SW_API SkyAtmosphere
    {
        static constexpr uint32  kViewStepCount          = 16;      ///< sky.hlsl kAtmosphereViewSteps 와 같다
        static constexpr uint32  kLightStepCount         = 8;       ///< sky.hlsl kAtmosphereLightSteps 와 같다
        static constexpr float32 kRayleighScaleHeightKm  = 8.0f;    ///< 레일리 밀도가 1/e 로 줄어드는 높이
        static constexpr float32 kMieScaleHeightKm       = 1.2f;    ///< 미 밀도가 1/e 로 줄어드는 높이
        static constexpr float32 kMieExtinctionRatio     = 1.1f;    ///< 미 소멸 계수 / 산란 계수
        static constexpr float32 kRayleighBetaRedPerKm   = 0.0058f; ///< 레일리 산란 계수(1/km) 빨강 — 해수면, `_rayleighScale` 1
        static constexpr float32 kRayleighBetaGreenPerKm = 0.0135f; ///< 같은 계수의 초록
        static constexpr float32 kRayleighBetaBluePerKm  = 0.0331f; ///< 같은 계수의 파랑
        static constexpr float32 kMieBetaPerKm           = 0.021f;  ///< 미 산란 계수(1/km), `_mieScale` 1

        float32 _rayleighScale{ 1.0f };       ///< 레일리 산란 계수의 배율(대기 맑기)
        float32 _mieScale{ 1.0f };            ///< 미 산란 계수의 배율(먼지 · 안개 낀 정도)
        float32 _mieAnisotropy{ 0.76f };      ///< 미 위상 함수의 이방성(0..1, 클수록 해 둘레만 밝다)
        float32 _sunIntensity{ 20.0f };       ///< 해 복사휘도 배율(해 색 × 이 값)
        float32 _planetRadiusKm{ 6360.0f };   ///< 행성 반지름
        float32 _atmosphereHeightKm{ 60.0f }; ///< 대기 두께
        float32 _observerAltitudeKm{ 0.0f };  ///< 눈의 해발 고도(km — 0.1 이 100 m)
        float32 _sunDiscAngle{ 0.03f };       ///< 해 원반의 각 반지름(라디안, 0 이면 원반 없음). 셰이더만 그린다 — 프로브 베이크는 원반을 넣지 않는다(방향광이 따로 비춘다)

        /** @brief 레일리 산란 계수(1/km)입니다 — 기본 계수 × `_rayleighScale`. */
        [[nodiscard]] float3 computeRayleighBeta() const;
        /** @brief 미 산란 계수(1/km)입니다 — 기본 계수 × `_mieScale`. */
        [[nodiscard]] float32 computeMieBeta() const;
        /**
         * @brief 방향 @p direction(단위 벡터)에서 오는 단일 산란 복사휘도입니다. 해 복사휘도 1 당이다.
         * @param toSun 해 쪽 단위 방향(눈 기준 월드)
         * @param groundAlbedo 시선이 행성 표면을 만났을 때의 땅 반사율(선형 RGB)
         */
        [[nodiscard]] float3 evaluateScattering( const float3& direction, const float3& toSun, const float3& groundAlbedo ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 구름 층입니다 — 눈 위 고정 고도의 평면에 노이즈를 놓고 바람으로 흘립니다(레이마치 없음). 셰이더(sky.hlsl)가 하늘 위에 얹는다.
     * @details 구름 밀도 = 값 노이즈 fbm 을 `_coverage` 로 문턱 처리한 것이다. 시선이 평면에 닿는 점(`_altitudeKm` / 시선 y)의 xz 를 `_scale`(1/km)로 곱해 노이즈를 읽고, 시간은
     *          패스 상수의 머티리얼 시계(`g_SwMaterialTime`)에 `_windSpeedKmPerSecond` 를 곱해 흘린다. 지평선 근처는 평면이 너무 멀어 서서히 지운다.
     */
    struct SW_API SkyClouds
    {
        float32 _coverage{ 0.5f };              ///< 구름이 덮는 비율(0..1)
        float32 _altitudeKm{ 2.0f };            ///< 구름 층의 눈 위 고도(km)
        float32 _scale{ 0.4f };                 ///< 노이즈 배율(1/km) — 클수록 구름 덩이가 작다
        float32 _opacity{ 0.9f };               ///< 구름의 최대 불투명도
        float32 _windDirection{ 0.0f };         ///< 바람이 부는 방향(라디안, +x 에서 +z 쪽으로)
        float32 _windSpeedKmPerSecond{ 0.01f }; ///< 바람 속도(km/s)
        float32 _softness{ 0.15f };             ///< 문턱 경계의 부드러움(0..0.5)
        float3  _color{ 1.0f, 1.0f, 1.0f };     ///< 구름 색(선형 RGB) — 빛을 받은 흰색에 곱한다
        uint8   _bEnabled{ SW_FALSE };          ///< 구름을 그린다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 하늘 한 벌입니다. 셰이더(`sky.hlsl`)와 같은 식을 CPU 에서 셉니다.
     * @details `Gradient` 모드: 위쪽은 천정 색에서 지평선 색으로, 아래쪽은 땅 색에서 지평선 색으로 간다. 지평선 쪽 비율은 `(1 - |y|)^지수` 이고 마지막에 세기를 곱한다.
     *          지수가 클수록 지평선 띠가 좁다. 셰이더 상수로는 `g_SkyZenith`(rgb, w = 지수) · `g_SkyHorizon`(rgb) · `g_SkyGround`(rgb, w = 세기)로 싣는다.
     *          `Atmosphere` 모드: 산란(`SkyAtmosphere`) × 해 색 × `_atmosphere._sunIntensity` × 세기. 해는 `setSun` 으로 받는다 — 해(방향광)가 없으면 해 색이 0 이라 하늘은 0(땅도 0)이다(폴백 없음).
     *          셰이더는 해 방향 · 색을 키 라이트 상수(`g_KeyLightDirIntensity` · `g_KeyLightColor`)에서 읽는다 — 같은 값이다. 같은 식을 간접 조명 베이크가 프로브마다 SH 로 투영한다(계획 `IndirectLighting` P2).
     */
    struct SW_API SkyGradient
    {
        float3        _zenithColor{};                      ///< 바로 위(+y)의 색(선형 RGB)
        float3        _horizonColor{};                     ///< 지평선의 색(선형 RGB)
        float3        _groundColor{};                      ///< 바로 아래(-y)의 색(선형 RGB). 대기 모드에서는 땅의 반사율이다
        float32       _intensity{ 0.0f };                  ///< 세 색(또는 산란)에 곱하는 세기
        float32       _horizonExponent{ 1.0f };            ///< 지평선 쪽 비율 `(1 - |y|)` 의 지수(0 보다 크다)
        SkyMode       _mode{ SkyMode::Gradient };          ///< 하늘 모드
        SkyAtmosphere _atmosphere{};                       ///< 대기 모드의 값
        SkyClouds     _clouds{};                           ///< 구름 층(모드와 무관)
        float3        _toSunDirection{ 0.0f, 1.0f, 0.0f }; ///< 해 쪽 단위 방향(대기 모드). `setSun` 이 정한다
        float3        _sunColor{};                         ///< 해 색(선형 RGB, 세기 곱하기 전). 0 이면 해가 없다

        /**
         * @brief 해(방향광)를 정합니다. @p lightTravelDirection 은 빛이 나아가는 방향(방향광의 `getLightDirection`)이고 해 쪽은 그 반대다. 길이가 0 이면 해가 없다(색 0).
         */
        void setSun( const float3& lightTravelDirection, const float3& color );

        /** @brief 방향 @p direction(길이는 상관없다, 0 벡터는 지평선)에서 오는 하늘빛입니다. 구름과 해 원반은 넣지 않는다. */
        float3 evaluateRadiance( const float3& direction ) const;
    };
} // namespace sw
