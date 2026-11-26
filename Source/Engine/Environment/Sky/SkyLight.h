/**
 * @file SkyLight.h
 * @brief 스카이 라이트입니다 — 씬의 하늘(`SkyGradient`)을 거칠기 단계마다 걸러 반사 프로브 · 프로브 볼륨 밖의 환경 반사와 확산 환경광으로 씁니다.
 * @details 언리얼 Sky Light · 유니티 Environment Lighting / Reflections(Source = Skybox) · Godot Sky(Radiance) 와 같은 자리다. 환경광의 우선순위는 셰이더 `SurfaceBRDF::shadeLights` 가 정한다:
 *          반사는 반사 프로브 → 스카이 라이트 → 앰비언트 색, 확산은 프로브 볼륨 → 스카이 라이트 → 앰비언트 색. 앰비언트 색은 폴백이 아니라 "하늘 없는 장면" 의 정의다.
 *
 *          데이터는 반사 프로브와 같은 모양(옥타헤드럴, 단계마다 같은 크기 — `ReflectionProbeAsset`)이고 프로브 버퍼(t13)에 같이 올라간다. 큐브맵 · 새 패스 · 새 슬롯이 없어
 *          네 백엔드가 같은 길로 읽는다. 거르기는 CPU 가 원본(`OctahedralRadianceMap`)을 만들어 반사 프로브 베이크와 같은 함수(`ReflectionProbeAsset::filterRoughnessLevels`
 *          — 원본 밉 사슬을 쓰는 filtered importance sampling)로 하므로 GPU 캡처 · 리드백이 없다. 원본은 그러데이션 · 대기 모드면 하늘 식(`SkyGradient::evaluateRadiance`
 *          — sky.hlsl 과 같은 식)을 텍셀마다 센 것이라 구름 층과 해 원반은 들지 않는다(해는 방향광이 따로 비춘다). HDRI 모드면 원본 그림을 옥타헤드럴로 접은
 *          `.radiancemap`(`SkyGradient::_hdriRadianceMap`, 회전을 돌려 읽는다)이다 — CPU 는 BC6H 텍스처를 읽지 않는다. 언리얼 Sky Light 가 HDRI 큐브맵을 걸러 밉에 담는 자리다.
 *
 *          다시 거르는 때(`SkyLightCache::update`): 처음 보는 하늘(스카이 라이트가 없었다 · 다른 `SkyComponent` · 거르기 설정이 바뀜)은 그 프레임에, 같은 하늘의 값이 바뀌면
 *          (시간대 · 날씨) `SkyLightConfig::_captureIntervalFrames` 에 한 번이다(언리얼 Real Time Capture 의 주기 자리). 하늘이 그대로인 프레임의 비용은 값 견주기 한 번이다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/SphericalHarmonics.h"

#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Graphics/Lighting/ReflectionProbeAsset.h"

namespace sw
{
    struct SkyLightConfig;

    /**
     * @struct SkyLightData
     * @brief 거른 하늘 한 벌입니다 — 반사는 거칠기 단계마다의 옥타헤드럴 텍셀, 확산은 SH9 입니다.
     */
    struct SW_API SkyLightData
    {
        ReflectionProbeAsset _reflection; ///< 거칠기 단계마다 거른 하늘(단계 0 은 거르지 않은 하늘). 하늘은 무한히 멀어 찍은 위치는 쓰지 않는다(0)
        SH9Color             _ambient;    ///< 하늘의 SH9(복사휘도) — 셰이더는 조도 / π 로 접은 원소(`SH9Color::makeGPUElement`)를 읽는다
        /// @brief 캐시가 거를 때마다 올리는 번호입니다(`make` 는 건드리지 않는다). 프로브 버퍼가 다시 올릴지 정하는 열쇠다(`LightProbeElementBuilder::appendSkyLight`).
        uint32 _revision{ 0 };

        /**
         * @brief @p sky 를 @p config 의 크기 · 단계 수 · 표본 수 · 원본 밉 수로 걸러 @p outData 를 만듭니다.
         * @details 원본은 그러데이션 · 대기 모드면 `config._resolution` 한 변에서 센 하늘 식, HDRI 모드면 @p pHdriRadiance 의 한 변에서 회전해 읽은 원본 × 세기다.
         *          단계 0 은 텍셀 중심 방향의 원본, 단계 1 부터는 GGX 거르기(`ReflectionProbeAsset::filterRoughnessLevels` — 반사 프로브 베이크와 같은 함수)다.
         *          SH9 는 원본 밉 0 텍셀을 입체각 가중치로 투영한다(옥타헤드럴 텍셀의 입체각 ∝ 방향의 L1 길이³ — 난수 없이 결정적이다).
         * @param pHdriRadiance HDRI 모드의 원본(`SkyGradient::_hdriRadianceMap` 을 읽은 것, 밉 0 만 쓴다). 다른 모드는 보지 않는다
         * @return 크기 · 단계 수 · 밉 수가 범위 밖이거나 표본이 0 이거나 HDRI 모드에 원본이 없으면 그 까닭이고 @p outData 는 빈다.
         */
        [[nodiscard]] static expected<void> make( const SkyGradient& sky, const OctahedralRadianceMap* pHdriRadiance, const SkyLightConfig& config, SkyLightData& outData );
    };

    /**
     * @class SkyLightCache
     * @brief 씬 수집기(게임 스레드)가 프레임을 넘어 드는 스카이 라이트 하나입니다. 하늘이 바뀔 때만 다시 거릅니다.
     */
    class SW_API SkyLightCache
    {
    public:
        /**
         * @brief 이번 프레임의 하늘로 스카이 라이트를 맞춥니다. 프레임에 한 번 부릅니다(간격을 이 호출 수로 센다).
         * @param pSky 씬의 하늘. 없으면 nullptr — 스카이 라이트도 없다(지난 것을 놓는다. 하늘이 다시 생기면 그 프레임에 거른다)
         * @return 이번 프레임에 쓸 스카이 라이트. 하늘이 없거나 거르지 못했으면(설정이 범위 밖 — 오류를 한 번 남긴다) nullptr 입니다.
         */
        [[nodiscard]] const SkyLightData* update( const SkyGradient* pSky, const SkyLightConfig& config );
        /** @brief 거른 횟수입니다(`SkyLightData::_revision`). 내용이 바뀔 때마다 오른다. */
        uint32 getRevision() const { return _data._revision; }

    private:
        /**
         * @brief HDRI 하늘의 원본을 돌려줍니다. 경로가 바뀐 때만 읽는다(동기, 밉 0 하나, 한 변 128 이면 약 200 KB). HDRI 모드가 아니거나 못 읽었으면 nullptr 이다(오류는 한 번 남긴다).
         * @details 반사 프로브 데이터(`ReflectionProbeComponent`)도 경로가 정해질 때 동기로 읽는다 — 같은 크기의 일이다.
         */
        const OctahedralRadianceMap* findHdriRadiance( const SkyGradient& sky );

    private:
        SkyLightData          _data;                    ///< 지금 쓰는 스카이 라이트(`_bCaptured` 일 때만 뜻이 있다)
        SkyGradient           _source;                  ///< `_data` 를 만든 하늘
        OctahedralRadianceMap _hdriRadiance;            ///< `_hdriRadiancePath` 를 읽은 원본(못 읽었으면 빈다)
        hashed_string         _hdriRadiancePath{};      ///< `_hdriRadiance` 를 읽으려 한 경로
        uint32                _filterSampleCount{ 0 };  ///< `_data` 를 만든 표본 수(크기 · 단계 수는 `_data._reflection` 이 든다)
        uint32                _filterMipCount{ 0 };     ///< `_data` 를 만든 원본 밉 수 상한
        uint32                _framesSinceCapture{ 0 }; ///< 마지막으로 거른 뒤 지난 `update` 수
        uint8                 _bCaptured{ SW_FALSE };   ///< `_data` 가 있다
    };
} // namespace sw
