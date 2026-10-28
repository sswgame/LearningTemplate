/**
 * @file EditorGridUtil.h
 * @brief 뷰포트 바닥 격자의 판정을 담당합니다. 간격 단계, 굵은 선, 가장자리 흐림을 계산하며, ImGui 없이 계산만 하므로 `Test/EditorTest` 가 테스트합니다.
 * @details 격자 선은 월드에 고정된다. 선 하나는 "간격의 몇 배인가"(월드 인덱스)로 정해지고, 굵은 선인지도 그 인덱스로만 정한다.
 *          카메라 위치를 판정에 넣으면 카메라가 움직일 때 굵은 선이 다른 월드 선으로 미끄러진다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    /** @brief 이번 프레임의 격자 간격 단계입니다. */
    struct EditorGridLevel
    {
        float32 _step{ 1.0f };        ///< 가는 선 간격(월드 m). 1, 10, 100 중 하나다
        float32 _coarseBlend{ 0.0f }; ///< 0..1 범위다. 1 에 가까울수록 가는 선이 사라지고 굵은 선 판정이 다음 단계(간격 × 10)로 넘어간다
        float32 _radius{ 20.0f };     ///< 격자를 그리는 반지름(월드 m). 가장자리로 갈수록 흐려진다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 선 하나의 표시 속성입니다. */
    struct EditorGridLineStyle
    {
        float32 _visibility{ 1.0f };  ///< 0..1 범위다. 단계가 바뀌는 동안 사라지는 가는 선은 0 이 된다
        float32 _majorWeight{ 0.0f }; ///< 0..1 범위다. 1 이면 굵은 선의 색과 두께를 쓴다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 격자가 마지막으로 그린 결과입니다. 에디터 시나리오 프로브(`Editor.Grid*`)가 읽습니다. 값은 격자를 그리는 오버레이(`EditorViewportCanvasOverlay`)가 듭니다.
     * @details 굵은 선 판정이 카메라를 따라 미끄러지면 `_misplacedMajorCount` 가 0 이 아니다. 그린 선의 월드 좌표로 다시 센다.
     */
    struct EditorGridStats
    {
        float32 _step{ 0.0f };
        float3  _cameraPos{};
        uint32  _majorLineCount{ 0 };
        uint32  _misplacedMajorCount{ 0 };
        int32   _frame{ -1 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorGridUtil
     * @brief 격자 판정 함수 모음입니다. 언리얼과 유니티의 에디터 격자처럼 카메라 높이에 따라 간격을 1m, 10m, 100m 로 바꾸고, 바뀌는 구간에서 두 간격을 섞습니다.
     */
    struct EditorGridUtil
    {
        /** @brief 가는 선 몇 개마다 굵은 선이 하나인지 정합니다. 월드 인덱스가 이 수의 배수이면 굵은 선입니다. */
        static constexpr int64 kMajorEvery = 5;
        /** @brief 다음 단계의 간격이 이번 간격의 몇 배인지 정합니다. */
        static constexpr int64 kLevelRatio = 10;
        /** @brief 단계 수입니다. 0 은 1m, 1 은 10m, 2 는 100m 입니다. */
        static constexpr int32 kMaxLevel = 2;
        /** @brief 이 높이까지는 1 m 간격입니다. 높이가 10 배가 될 때마다 한 단계 올라갑니다. */
        static constexpr float32 kLevelReferenceHeight = 10.0f;
        /** @brief 단계 안에서 섞기를 시작하는 지점(0..1)입니다. 앞부분에서는 한 간격만 또렷하게 보입니다. */
        static constexpr float32 kBlendStart = 0.5f;
        /** @brief 1m 단계의 격자 반지름(월드 m)입니다. 높이를 따라 연속으로 커집니다. */
        static constexpr float32 kBaseRadius = 20.0f;
        /** @brief 반지름의 이 비율부터 가장자리 흐림이 시작됩니다. */
        static constexpr float32 kEdgeFadeStart = 0.55f;

        /**
         * @brief 카메라에서 격자 평면까지의 거리로 간격 단계를 고릅니다. 직교 뷰는 보이는 반 높이의 2배를 거리로 씁니다.
         * @details 높이가 `kLevelReferenceHeight × 10^k` 를 넘을 때마다 간격이 10배가 되고, 그 사이 뒤쪽 절반에서 가는 선이 서서히 사라진다.
         *          반지름은 높이를 따라 연속이므로 단계가 바뀌어도 가장자리가 튀지 않는다.
         */
        [[nodiscard]] static EditorGridLevel selectLevel( float32 viewHeight );

        /** @brief 이번 간격의 월드 인덱스 @p worldIndex 인 선이 굵은 선이면 true 입니다. 좌표는 인덱스에 간격을 곱한 값이며, 카메라와 무관합니다. */
        [[nodiscard]] static constexpr bool isMajorLine( int64 worldIndex ) { return worldIndex % kMajorEvery == 0; }

        /**
         * @brief 월드 인덱스 @p worldIndex 인 선의 표시 속성을 구합니다.
         * @details 다음 간격의 배수가 아닌 선은 `_coarseBlend` 만큼 흐려지고, 굵은 선의 가중치는 이번 단계(`kMajorEvery` 배수)와 다음 단계
         *          (`kMajorEvery × kLevelRatio` 배수) 사이를 섞는다. 그래서 단계 경계 양쪽에서 같은 월드 선이 같은 모양이다.
         */
        [[nodiscard]] static EditorGridLineStyle evaluateLine( int64 worldIndex, const EditorGridLevel& level );

        /** @brief 격자 중심에서 @p distance 떨어진 점의 가장자리 흐림을 구합니다. 1 이 또렷하고 0 이 안 보임입니다. */
        [[nodiscard]] static float32 computeEdgeFade( float32 distance, float32 radius );
    };
} // namespace sw::editor
