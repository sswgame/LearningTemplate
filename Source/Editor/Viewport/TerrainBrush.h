/**
 * @file TerrainBrush.h
 * @brief 지형 칠하기 도구의 브러시 계산입니다(언리얼 Landscape Mode 의 Sculpt/Paint · 유니티 Terrain Tools 대응). ImGui 와 씬을 모르는 순수 로직입니다.
 * @details 브러시 중심(월드 x, z)과 지형 격자(원점 · 셀 크기 · 해상도)에서 바뀔 샘플과 가중치를 고르고, 높이 배열 또는 레이어 가중치 배열에 적용한다.
 *          높이와 가중치는 샘플 단위 배열(N × N, 행 우선이며 z 가 바깥)로 받는다 — `TerrainHeightfield::getHeightSamples` 와 같은 배치다.
 *          지형 쪽에 런타임 쓰기 API 가 생기면 뷰포트 도구가 이 함수들로 스트로크를 적용한다(지금은 미리보기 원만 그린다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    /** @brief 브러시가 하는 일입니다. */
    enum class TerrainBrushMode : uint8
    {
        Raise,      ///< 높이를 올린다
        Lower,      ///< 높이를 내린다
        Flatten,    ///< 목표 높이로 당긴다
        PaintLayer, ///< 레이어 하나의 가중치를 올리고 합을 1 로 맞춘다
    };

    /** @brief 브러시 설정입니다. */
    struct TerrainBrushSettings
    {
        TerrainBrushMode _mode{ TerrainBrushMode::Raise };
        float32          _radius{ 4.0f };   ///< 월드 반경(m)
        float32          _strength{ 1.0f }; ///< 초당 변화량(높이는 m/s, 가중치는 1/s)
        float32          _falloff{ 0.5f };  ///< 0 = 가장자리까지 가득, 1 = 중심에서부터 줄어든다
        uint32           _layerIndex{ 0 };  ///< `PaintLayer` 가 올리는 레이어(0..3)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 브러시가 닿는 샘플 하나입니다. */
    struct TerrainBrushSample
    {
        uint32  _sampleX{ 0 };
        uint32  _sampleZ{ 0 };
        float32 _weight{ 0.0f }; ///< (0, 1]
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 지형 격자의 배치입니다. 샘플 (0, 0) 이 `_origin` 이다. */
    struct TerrainBrushGrid
    {
        float2 _origin{};   ///< 샘플 (0, 0) 의 월드 (x, z)
        float2 _cellSize{}; ///< 셀 한 변(x, z)
        uint32 _resolution{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct TerrainBrush
     * @brief 브러시 감쇠, 닿는 샘플 고르기, 높이 · 가중치 적용입니다.
     */
    struct TerrainBrush
    {
        /**
         * @brief 중심에서 @p distance 떨어진 곳의 가중치입니다.
         * @details 안쪽 반경 = 반경 × (1 − 감쇠) 안은 1, 반경 밖은 0, 그 사이는 smoothstep 으로 줄어든다.
         */
        static float32 computeFalloffWeight( float32 distance, const TerrainBrushSettings& settings );

        /** @brief 중심 (x, z) 의 브러시가 닿는 샘플을 @p outListSample 에 채웁니다(지우고 채운다). 격자 밖은 잘라 냅니다. */
        static void collectSamples( const float2& center, const TerrainBrushGrid& grid, const TerrainBrushSettings& settings,
                                    vector<TerrainBrushSample>& outListSample );

        /**
         * @brief 높이 배열에 한 프레임을 적용합니다. 평탄화는 @p flattenHeight 로 당깁니다(한 프레임에 목표를 넘지 않습니다).
         * @return 바뀐 샘플이 있으면 true 입니다.
         */
        [[nodiscard]] static bool applyHeight( vector<float32>& inOutListHeight, uint32 resolution, const vector<TerrainBrushSample>& listSample,
                                               const TerrainBrushSettings& settings, float32 flattenHeight, float32 deltaTime );

        /**
         * @brief 샘플별 레이어 가중치(합 1) 배열에 `PaintLayer` 한 프레임을 적용합니다. 칠한 샘플은 합이 1 로 다시 맞춰집니다.
         * @return 바뀐 샘플이 있으면 true 입니다.
         */
        [[nodiscard]] static bool applyLayerWeight( vector<float4>& inOutListWeight, uint32 resolution, const vector<TerrainBrushSample>& listSample,
                                                    const TerrainBrushSettings& settings, float32 deltaTime );
    };
} // namespace sw::editor
