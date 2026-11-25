/**
 * @file SkyGradient.h
 * @brief 하늘 한 벌(그러데이션 또는 대기 산란, 구름 층)과 방향마다의 하늘빛 식입니다.
 * @details 값은 씬의 `SkyComponent` 가 정하고, 렌더 패킷(`RenderFramePacket::_sky`)이 렌더 스레드로 나른다. 렌더러가 컴포넌트 헤더를 보지 않도록 값 타입만 따로 둔다.
 *          모드가 둘이다. `Gradient`(천정 · 지평선 · 땅 3색)와 `Atmosphere`(레일리 · 미 단일 산란, `SkyAtmosphere`). 구름(`SkyClouds`)은 모드와 무관하게 하늘 위에 얹는다(평면 노이즈 층 또는 볼륨 — 셰이더만 그리고
 *          프로브 베이크는 구름을 보지 않는다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/SphericalHarmonics.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct SkyCloudConfig;
} // namespace sw

namespace sw
{
    /** @brief 하늘을 어떻게 그리는가입니다. */
    ENUM()
    enum class SkyMode : uint8
    {
        Gradient = 0, ///< 천정 · 지평선 · 땅 3색 그러데이션(`sky.hlsl` 의 evaluateSky)
        Atmosphere,   ///< 레일리 · 미 단일 산란(`SkyAtmosphere::evaluateScattering`) — 시간대는 방향광 방향이 정한다
        Hdri,         ///< 등장방형 HDR 텍스처(`sky.hlsl` 의 evaluateHdri, 언리얼 HDRI Backdrop · 유니티 Skybox/Panoramic). 간접 조명은 원본을 투영한 SH9 계수다
    };
} // namespace sw

namespace sw
{
    /** @brief 구름을 어떻게 그리는가입니다. 값은 셰이더 상수 `g_SkyCloudColor.w` 다. */
    ENUM()
    enum class SkyCloudMode : uint8
    {
        None = 0,   ///< 구름이 없다
        Layer,      ///< 평면 노이즈 층(sky.hlsl — 유니티 HDRP Cloud Layer 자리). 레이마치가 없다
        Volumetric, ///< 두께 있는 층의 레이마치(volumetric_clouds.hlsl — 언리얼 Volumetric Cloud · 유니티 HDRP Volumetric Clouds 자리). 품질(`SkyCloudConfig`)이 끄면 Layer 로 그린다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 대기 산란의 값과 식입니다(Nishita 1993 / 스크래치어픽셀 대기 튜토리얼의 단일 산란). 거리 단위는 km 입니다.
     * @details 눈은 행성 중심에서 `_planetRadiusKm + _observerAltitudeKm` 위(+y)에 있다. 시선을 대기 끝(또는 행성 표면)까지 `kViewStepCount` 구간으로 걸으며 구간마다
     *          해 쪽으로 `kLightStepCount` 구간의 광학 두께를 구한다. 레일리(산란 계수 `computeRayleighBeta`, 규모 높이 `kRayleighScaleHeightKm`)와 미(`computeMieBeta`, `kMieScaleHeightKm`,
     *          소멸 = 산란 × `kMieExtinctionRatio`, 위상 함수 이방성 `_mieAnisotropy`)의 합이다. 시선이 행성 표면을 만나면 땅의 반사(알베도 × N·L × 해 투과율 / π)를 더한다.
     *          행성이 해를 가리는 구간은 해 빛이 없다(행성 그림자). 결과는 해 복사휘도 1 당 값이고 부르는 쪽이 해 색 · 세기를 곱한다.
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
        /** @brief 눈에서 해까지의 투과율(rgb)입니다. 행성이 해를 가리면 0 입니다. sky.hlsl 이 평면 구름의 해 빛에 곱하는 값과 같다. */
        [[nodiscard]] float3 computeSunTransmittance( const float3& toSun ) const;
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
     * @brief 구름 층입니다 — 눈 위 고정 고도에 노이즈를 놓고 바람으로 흘립니다. 평면이면 sky.hlsl 이, 볼륨이면 volumetric_clouds.hlsl 이 그린다.
     * @details 볼륨은 `_altitudeKm` 부터 `_thicknessKm` 만큼 두께 있는 층을 걷고 소멸 계수 `_extinctionPerKm` 로 빛을 줄인다. 나머지 값은 두 종류가 함께 쓴다.
     *          평면의 구름 밀도 = 값 노이즈 fbm 을 `_coverage` 로 문턱 처리한 것이다. 시선이 평면에 닿는 점(`_altitudeKm` / 시선 y)의 xz 를 `_scale`(1/km)로 곱해 노이즈를 읽고, 시간은
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
        float32 _thicknessKm{ 1.5f };           ///< 볼륨 층의 두께(km)
        float32 _extinctionPerKm{ 18.0f };      ///< 볼륨 층의 소멸 계수(1/km, 밀도 1 일 때)
        float32 _multiScattering{ 0.5f };       ///< 볼륨의 여러 번 산란 — 옥타브마다 이어지는 몫(0..1, 0 이면 한 번 산란뿐)
        float32 _bottomOcclusion{ 0.5f };       ///< 볼륨 층 바닥에서 가리는 하늘빛의 비율(0..1)
        float32 _groundLight{ 1.0f };           ///< 땅에서 반사해 볼륨 바닥을 밝히는 빛의 배율
        // 품질 값 — 렌더 패킷이 `applyQuality` 로 채운다(저장하지 않는다). 볼륨이 아니면 쓰지 않는다.
        uint32       _viewStepCount{ 0 };         ///< 시선마다 걷는 걸음 수
        uint32       _scatterOctaveCount{ 0 };    ///< 여러 번 산란을 어림하는 옥타브 수
        float32      _historyWeight{ 0.0f };      ///< 시간 누적의 기록 비율
        SkyCloudMode _mode{ SkyCloudMode::None }; ///< 구름의 종류

        /** @brief 품질 값 @p config 를 싣습니다 — 볼륨을 끈 품질이면 볼륨 구름을 평면 구름으로 바꿉니다. 볼륨인데 걸음 수 · 산란 옥타브 수가 없으면 멈춥니다(폴백 없음). */
        void applyQuality( const SkyCloudConfig& config );
    };
} // namespace sw

namespace sw
{
    /** @brief 볼륨 구름이 해 말고 받는 빛입니다(선형 RGB). 반구 하나의 등방 산란 몫 1/2 을 곱한 값이다(`SkyGradient::computeCloudLighting`). */
    struct SkyCloudLighting
    {
        float3 _sky{};    ///< 위 반구의 하늘빛
        float3 _ground{}; ///< 땅에서 반사해 올라오는 빛
    };

    /**
     * @brief 하늘 한 벌입니다. 셰이더(`sky.hlsl`)와 같은 식을 CPU 에서 셉니다.
     * @details `Gradient` 모드: 위쪽은 천정 색에서 지평선 색으로, 아래쪽은 땅 색에서 지평선 색으로 간다. 지평선 쪽 비율은 `(1 - |y|)^지수` 이고 마지막에 세기를 곱한다.
     *          지수가 클수록 지평선 띠가 좁다. 셰이더 상수로는 `g_SkyZenith`(rgb, w = 지수) · `g_SkyHorizon`(rgb) · `g_SkyGround`(rgb, w = 세기)로 싣는다.
     *          `Atmosphere` 모드: 산란(`SkyAtmosphere`) × 해 색 × `_atmosphere._sunIntensity` × 세기. 해는 `setSun` 으로 받는다 — 해(방향광)가 없으면 해 색이 0 이라 하늘은 0(땅도 0)이다(폴백 없음).
     *          셰이더는 해 방향 · 색을 키 라이트 상수(`g_KeyLightDirIntensity` · `g_KeyLightColor`)에서 읽는다 — 같은 값이다. 같은 식을 간접 조명 베이크가 프로브마다 SH 로 투영한다.
     *          `Hdri` 모드: 셰이더는 텍스처를 읽고, 이 식은 원본을 투영한 SH9 계수(`_hdriAmbient`)를 평가해 간접 조명에 저주파 빛을 준다(언리얼 Sky Light 의 저차 SH 와 같은 자리). 음수 링잉은 0 으로 자른다.
     *          스카이 라이트의 반사는 이 식이 아니라 옥타헤드럴 원본(`_hdriRadianceMap`)을 거칠기 단계마다 거른 것이다(`SkyLightData::make`).
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
        hashed_string _hdriTexture{};                      ///< HDRI 모드의 등장방형 텍스처 경로
        float32       _hdriRotation{ 0.0f };               ///< HDRI 모드에서 하늘을 Y 축으로 돌리는 각(라디안)
        SH9Color      _hdriAmbient{};                      ///< HDRI 모드의 저주파 빛(원본의 SH9 투영, `Scripts/dev/make_sky_hdri_prefabs.py`). CPU 는 텍스처를 읽지 않는다
        hashed_string _hdriRadianceMap{};                  ///< HDRI 모드의 옥타헤드럴 원본(`.radiancemap` — `OctahedralRadianceMap`). 스카이 라이트가 읽어 반사를 거른다(같은 스크립트가 쓴다)
        uint64        _sourceID{ 0 };                      ///< 이 값을 낸 `SkyComponent` 의 번호(컴포넌트가 아니면 0). 스카이 라이트가 "다른 하늘" 과 "같은 하늘의 값이 바뀜" 을 가린다

        /**
         * @brief 해(방향광)를 정합니다. @p lightTravelDirection 은 빛이 나아가는 방향(방향광의 `getLightDirection`)이고 해 쪽은 그 반대다. 길이가 0 이면 해가 없다(색 0).
         */
        void setSun( const float3& lightTravelDirection, const float3& color );

        /** @brief HDRI 를 읽을 방향입니다 — 하늘을 Y 축으로 `_hdriRotation` 만큼 돌린 것은 방향 @p unit(길이 1)을 반대로 돌려 읽는 것이다(sky.hlsl evaluateHdri 와 같은 회전). */
        [[nodiscard]] float3 computeHdriLookupDirection( const float3& unit ) const;
        /** @brief 방향 @p direction(길이는 상관없다, 0 벡터는 지평선)에서 오는 하늘빛입니다. 구름과 해 원반은 넣지 않는다. */
        float3 evaluateRadiance( const float3& direction ) const;
        /**
         * @brief 볼륨 구름이 해 말고 받는 빛입니다 — 위 반구의 평균 하늘빛과 바로 아래의 땅 빛(× `_groundLight`).
         * @details 땅은 구름이 덮은 몫(덮는 비율 × 불투명도)만큼 해를 못 받는다. 언리얼 Volumetric Cloud 의 Sky Light · Ground Contribution 자리이고 프레임에 한 번 구한다.
         */
        [[nodiscard]] SkyCloudLighting computeCloudLighting() const;
        /**
         * @brief @p other 와 `evaluateRadiance` 가 모든 방향에서 같으면 true 입니다 — 구름(`_clouds`)과 출처(`_sourceID`)만 빼고 모든 값을 견준다.
         * @details 스카이 라이트가 다시 거를지 정한다(`SkyLightCache`). 색 · 방향은 `float3::operator==` 의 허용 오차로 견준다 — 그 안의 흔들림은 같은 하늘이다.
         *          하늘빛을 바꾸는 필드를 더하면 이 함수에도 더한다(SkyTest.SameRadianceComparesEveryRadianceField 가 필드마다 본다).
         */
        [[nodiscard]] bool hasSameRadiance( const SkyGradient& other ) const;
    };
} // namespace sw
