/**
 * @file TerrainLayerRule.h
 * @brief 지형 레이어의 자동 규칙(경사 · 고도)입니다. 칠한 스플랫 위에 "가파르면 바위, 높으면 눈, 물가 아래는 모래" 를 데이터로 덮습니다.
 * @details 언리얼 랜드스케이프 오토 머티리얼(Slope · Height 마스크로 레이어를 고르는 머티리얼 그래프)과 같은 자리지만, 규칙을 머티리얼 그래프가 아니라
 *          레이어 데이터(씬의 `TerrainLayer::_rule`)에 둔다 — CPU 질의(`TerrainHeightfield::computeLayerWeightsAt` → 발소리 · 배치 필터)가 같은 규칙을 읽어
 *          그린 땅과 게임플레이가 보는 땅이 같다(언리얼은 오토 머티리얼이 셰이더 안이라 물리 재질이 칠한 가중치만 본다).
 *          셰이더(`terrain.hlsl` 의 `TerrainLayerRule::computeCoverage`)와 같은 식이다 — 바꾸면 둘을 함께 바꾼다(단위 테스트 `TerrainLayerRuleTest` 가 값을 박는다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct TerrainLayerRule
     * @brief 레이어 하나의 경사 · 고도 띠입니다. 덮는 정도 = 세기 × 경사 띠 × 고도 띠(각 띠는 양 끝에 페이드 폭을 둔 사다리꼴)입니다.
     * @details 레이어 순서대로 덮는다(`SurfaceLayerWeights::applyCoverage`) — 뒤 레이어의 규칙이 앞 레이어의 규칙을 덮으므로 눈처럼 마지막에 이겨야 하는 레이어를 뒤에 둔다.
     *          세기 0 이면 규칙이 꺼져 칠한 가중치 그대로다(기본값).
     */
    REFLECT()
    struct SW_API TerrainLayerRule
    {
        REFLECT_BODY();

        /// @brief 페이드 폭의 아래 한계입니다 — 0 으로 나누지 않게 하고, 이 폭은 계단과 구별되지 않는다(셰이더와 같은 값).
        static constexpr float32 kMinFade = 1.0e-4f;

        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "How strongly the rule overrides the painted weights (0 = off)" )
        float32 _strength{ 0.0f };
        PROPERTY( Units = deg, Tooltip = "Slope band in degrees from horizontal (min, max)" )
        float2 _slopeRange{ 0.0f, 90.0f };
        PROPERTY( Units = deg, Min = 0.0, Tooltip = "Width of the soft edge at both ends of the slope band" )
        float32 _slopeFade{ 6.0f };
        PROPERTY( Units = m, Tooltip = "World height band (min, max)" )
        float2 _altitudeRange{ -100000.0f, 100000.0f };
        PROPERTY( Units = m, Min = 0.0, Tooltip = "Width of the soft edge at both ends of the height band" )
        float32 _altitudeFade{ 2.0f };

        /** @brief 규칙이 켜졌으면(세기 > 0) true 입니다. */
        bool isActive() const { return _strength > 0.0f; }
        /**
         * @brief 경사 @p slopeDegrees(수평에서 잰 도) · 높이 @p altitude(월드 m)에서 이 레이어가 덮는 정도(0..1)입니다.
         */
        float32 computeCoverage( float32 slopeDegrees, float32 altitude ) const;
        /**
         * @brief 값 @p value 가 띠 [@p low, @p high] 안이면 1, 양 끝에서 폭 @p fade 로 0 까지 선형으로 내려가는 사다리꼴입니다(끝값에서 0.5).
         */
        static float32 computeBand( float32 value, float32 low, float32 high, float32 fade );
        /** @brief 단위 노멀의 y 로 수평에서 잰 경사(도, 0..90)입니다. */
        static float32 computeSlopeDegrees( float32 normalY );
    };
} // namespace sw
