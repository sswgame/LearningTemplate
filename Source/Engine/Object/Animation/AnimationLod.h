/**
 * @file AnimationLod.h
 * @brief 애니메이션 LOD — 뷰(카메라 절두체) 가시성 · 화면 크기별 갱신 주기(URO) · 본 LOD · 중요도 예산(언리얼 Animation Budget Allocator)의 데이터와 식입니다.
 * @details 판정은 `AnimationSystem::updateLod` 가 게임 스레드에서 프레임마다 한 번 합니다. 뷰는 엔진 루프가 주 시점 + 추가 뷰(화면 사각형 · 렌더 텍스처)로
 *          넣습니다(`AnimationSystem::setLodViews`). 표(단계 · 예산)는 데이터(`engine/animation/animationlod.json`)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class JsonValue;
    class SkeletonBoneLod;

    /**
     * @struct AnimationLodView
     * @brief LOD 판정에 쓰는 뷰 하나입니다(뷰-투영 · 눈 위치 · 그 행렬에서 뽑은 절두체). `make` 로 만듭니다.
     */
    struct AnimationLodView
    {
        float4x4 _viewProj{};
        float3   _position{};
        Frustum  _frustum{};

        /** @brief 뷰-투영과 눈 위치로 만듭니다(절두체를 한 번 뽑아 둡니다). */
        static AnimationLodView make( const float4x4& viewProj, const float3& position )
        {
            AnimationLodView view{};
            view._viewProj = viewProj;
            view._position = position;
            view._frustum  = Frustum::fromViewProjection( viewProj );
            return view;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationLodState
     * @brief LOD 판정의 결과 한 벌입니다. `AnimationSystem` 이 게임 스레드에서 쓰고, 클라이언트(유닛 · 스프라이트 애니메이터)가 다음 평가에서 읽습니다.
     */
    struct AnimationLodState
    {
        float32 _screenSize{ 1.0f };           ///< 보이는 뷰들 중 가장 큰 화면 크기(경계 구 지름 / 화면 높이). 안 보이면 0
        float32 _significance{ 1.0f };         ///< 예산 배분의 우선순위(지금은 화면 크기)
        uint32  _updateRateDivisor{ 1 };       ///< 포즈를 만드는 주기(프레임). 예산이 늘렸을 수 있다
        uint8   _rateLevel{ 0 };               ///< 고른 주기 단계(`AnimationLodSettings::_listRateLevel`)
        uint8   _boneLodLevel{ 0 };            ///< 본 LOD 단계(0 = 모든 본)
        uint8   _bInterpolate{ SW_FALSE };     ///< 건너뛴 프레임을 직전 두 포즈 사이 보간으로 채운다
        uint8   _bVisible{ SW_TRUE };          ///< 어느 뷰에라도 보인다
        uint8   _bVertexAnimation{ SW_FALSE }; ///< 정점 애니메이션(VAT)으로 그릴 만큼 멀다(군중 공유가 읽는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationLodRateLevel
     * @brief 화면 크기 한 구간의 갱신 주기입니다. 화면 크기가 `_minScreenSize` 이상인 첫 단계가 고릅니다.
     */
    struct AnimationLodRateLevel
    {
        float32 _minScreenSize{ 0.0f };
        uint32  _updateRateDivisor{ 1 };
        uint8   _bInterpolate{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationLodSettings
     * @brief LOD 표(데이터)입니다. 형식: `{ "rate_levels": [ { "min_screen_size", "update_rate_divisor", "interpolate" } ], "offscreen_update_rate_divisor",
     *        "budget_milliseconds", "max_update_rate_divisor", "vertex_animation_screen_size" }`. 단계는 `min_screen_size` 가 줄어드는 순이고 마지막 단계는 0 이어야 합니다.
     * @details `offscreen_update_rate_divisor` 가 0 이면 화면 밖 유닛은 포즈를 만들지 않습니다(시간 · 알림은 흐릅니다). `budget_milliseconds` 가 0 이면 예산을 보지 않습니다.
     *          `vertex_animation_screen_size` 보다 작은 군중 유닛은 정점 애니메이션(VAT)으로 그립니다(0 이면 쓰지 않음).
     */
    struct SW_API AnimationLodSettings
    {
        /** @brief 표 파일의 리소스 경로입니다. */
        static constexpr string_view kResourcePath = "engine/animation/animationlod.json";

        vector<AnimationLodRateLevel> _listRateLevel;
        uint32                        _offscreenUpdateRateDivisor{ 0 };
        uint32                        _maxUpdateRateDivisor{ 16 };
        float32                       _budgetMilliseconds{ 0.0f };
        float32                       _vertexAnimationScreenSize{ 0.0f };

        /** @brief JSON 을 읽습니다. 모르는 키 · 순서가 틀린 단계는 오류이고 false 입니다(내용은 기본값으로 돌아갑니다). */
        [[nodiscard]] bool parseJson( string_view json, string_view sourceLabel );
        /** @brief 리소스 경로의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief 화면 크기의 주기 단계입니다. 표가 비었으면 0 입니다. */
        uint32 selectRateLevel( float32 screenSize ) const;
        /** @brief 단계가 없는 기본 표(모든 유닛이 매 프레임)입니다. */
        static AnimationLodSettings makeDefault() { return AnimationLodSettings{}; }

    private:
        [[nodiscard]] bool parseRoot( const JsonValue& root, string_view sourceLabel );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationBudgetItem
     * @brief 예산 배분에 넣는 클라이언트 하나입니다.
     */
    struct AnimationBudgetItem
    {
        float32 _significance{ 0.0f };
        uint32  _updateRateDivisor{ 1 }; ///< 표가 고른 주기(입력) — 배분이 늘린 값으로 덮습니다(출력)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationLodUtil
     * @brief LOD 의 순수 식입니다(시험이 그대로 부릅니다).
     */
    struct SW_API AnimationLodUtil
    {
        /**
         * @brief 경계 구가 뷰에 차지하는 화면 크기(지름 / 화면 높이)입니다. 절두체 밖이면 0 입니다.
         * @details 행벡터 규약이라 클립 y = dot( p, 열 1 ), w = dot( p, 열 3 ) 입니다. 강체 뷰 행렬이면 열 1 의 xyz 길이가 투영의 y 배율(cot( fov / 2 ))이고,
         *          화면 크기 = 반지름 × 배율 / w 입니다(직교 투영이면 w = 1). 눈이 구 안에 있으면 1 로 봅니다.
         */
        static float32 computeScreenSize( const AnimationLodView& view, const float3& center, float32 radius );
        /**
         * @brief 중요도 순으로 예산을 맞춥니다(언리얼 Animation Budget Allocator). 덜 중요한 것부터 주기를 두 배씩(상한까지) 늘려
         *        `Σ costPerEvaluation / 주기` 가 예산 이하가 될 때까지 갑니다. 예산이 0 이하이거나 이미 맞으면 손대지 않습니다.
         * @return 예상 비용(마이크로초, 배분 뒤)입니다.
         */
        static float32 allocateBudget( AnimationBudgetItem* pItem, uint32 itemCount, float32 costPerEvaluationMicroseconds, float32 budgetMicroseconds,
                                       uint32 maxUpdateRateDivisor );
        /** @brief 주기 · 위상으로 이번 프레임에 포즈를 만드는지입니다. 같은 주기의 유닛들이 같은 프레임에 몰리지 않게 위상을 더합니다. */
        static bool isOnUpdateFrame( uint64 frameIndex, uint32 phase, uint32 divisor ) { return divisor <= 1 || ( ( frameIndex + phase ) % divisor ) == 0; }
        /** @brief 화면 크기와 표로 상태 하나를 만듭니다(가시성 · 주기 단계 · 보간 · 본 LOD · VAT). 예산은 따로입니다. */
        static AnimationLodState makeState( const AnimationLodSettings& settings, float32 screenSize, bool bVisible, const SkeletonBoneLod* pBoneLod );
    };
} // namespace sw
