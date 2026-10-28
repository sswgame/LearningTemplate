/**
 * @file AnimationLOD.h
 * @brief 애니메이션 LOD — 뷰(카메라 절두체) 가시성 · 화면 크기별 갱신 주기(URO) · 본 LOD · 중요도 예산(언리얼 Animation Budget Allocator)의 데이터와 식입니다.
 * @details 판정은 `AnimationSystem::updateLOD` 가 게임 스레드에서 프레임마다 한 번 합니다. 뷰는 엔진 루프가 주 시점 + 추가 뷰(화면 사각형 · 렌더 텍스처)로
 *          넣습니다(`AnimationSystem::setLODViews`). 표(단계 · 예산)는 데이터(`engine/animation/animationlod.xml`)입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Graphics/LOD/LODView.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class SkeletonBoneLOD;
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationLODState
     * @brief LOD 판정의 결과 한 벌입니다. `AnimationSystem` 이 게임 스레드에서 쓰고, 클라이언트(유닛 · 스프라이트 애니메이터)가 다음 평가에서 읽습니다.
     */
    struct AnimationLODState
    {
        float32 _screenSize{ 1.0f };           ///< 보이는 뷰들 중 가장 큰 화면 크기(경계 구 지름 / 화면 높이). 안 보이면 0
        float32 _significance{ 1.0f };         ///< 예산 배분의 우선순위(지금은 화면 크기)
        uint32  _updateRateDivisor{ 1 };       ///< 포즈를 만드는 주기(프레임). 예산이 늘렸을 수 있다
        uint8   _rateLevel{ 0 };               ///< 고른 주기 단계(`AnimationLODSettings::_listRateLevel`)
        uint8   _boneLODLevel{ 0 };            ///< 본 LOD 단계(0 = 모든 본)
        uint8   _bInterpolate{ SW_FALSE };     ///< 건너뛴 프레임을 직전 두 포즈 사이 보간으로 채운다
        uint8   _bVisible{ SW_TRUE };          ///< 어느 뷰에라도 보인다
        uint8   _bVertexAnimation{ SW_FALSE }; ///< 정점 애니메이션(VAT)으로 그릴 만큼 멀다(군중 공유가 읽는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationLODRateLevel
     * @brief 화면 크기 한 구간의 갱신 주기입니다. 화면 크기가 `_minScreenSize` 이상인 첫 단계가 고릅니다.
     */
    REFLECT()
    struct SW_API AnimationLODRateLevel
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Smallest on-screen size (bounding sphere diameter / screen height) this level still covers" )
        float32 _minScreenSize{ 0.0f };
        PROPERTY( Min = 1, Tooltip = "Build the pose every N frames" )
        uint32 _updateRateDivisor{ 1 };
        PROPERTY( Tooltip = "Fill skipped frames by blending the last two poses" )
        bool _bInterpolate{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationLODSettings
     * @brief LOD 표(데이터)입니다(`engine/animation/animationlod.xml`). 단계는 `_minScreenSize` 가 줄어드는 순이고 마지막 단계는 0 이어야 합니다(`validate`).
     * @details `_offscreenUpdateRateDivisor` 가 0 이면 화면 밖 유닛은 포즈를 만들지 않습니다(시간 · 알림은 흐릅니다). `_budgetMilliseconds` 가 0 이면 예산을 보지 않습니다.
     *          예산 배분의 입력은 결정적 비용 모형(유닛 수 × `_unitCostMicroseconds`)이다 — 벽시계 측정값으로 배분하면 같은 입력이 기계 · 부하마다 다른 갱신 주기
     *          (그래서 다른 포즈 프레임)가 된다. 예산을 켜면 단가가 필수다(`validate`).
     *          `_vertexAnimationScreenSize` 보다 작은 군중 유닛은 정점 애니메이션(VAT)으로 그립니다(0 이면 쓰지 않음).
     *          파일은 리플렉션 구조체로 읽는다(`ConfigManager::readConfigXMLElement` — 모르는 속성 · 자식 · 읽지 못한 값 · 범위 밖 숫자는 오류). 칸 표는 PROPERTY 가 기준이다.
     */
    REFLECT()
    struct SW_API AnimationLODSettings
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Update-rate levels from the largest on-screen size to 0 (at least one; the last must start at 0)" )
        vector<AnimationLODRateLevel> _listRateLevel;
        PROPERTY( Min = 0, Tooltip = "Update divisor for units that are not visible (0: no pose at all)" )
        uint32 _offscreenUpdateRateDivisor{ 0 };
        PROPERTY( Min = 1, Tooltip = "Upper bound the budget allocator may raise a divisor to" )
        uint32 _maxUpdateRateDivisor{ 16 };
        PROPERTY( Min = 0.0, Units = ms, Tooltip = "Animation budget per frame (0: unlimited)" )
        float32 _budgetMilliseconds{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Modelled cost of one pose evaluation in microseconds - the budget divides by this, never by measured time (required when the budget is on)" )
        float32 _unitCostMicroseconds{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Crowd units smaller than this on screen are drawn with vertex animation (0: never)" )
        float32 _vertexAnimationScreenSize{ 0.0f };

        /** @brief XML 을 읽고 검사합니다. 모르는 속성 · 자식 · 순서가 틀린 단계는 오류이고 false 입니다(내용은 기본값으로 돌아갑니다). */
        [[nodiscard]] bool parseXML( string_view xml, string_view sourceLabel );
        /** @brief 리소스 경로의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 단계가 비지 않았고, `_minScreenSize` 가 줄어드는 순이며, 마지막이 0 인지 봅니다. 어긋나면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validate( string_view sourceLabel ) const;
        /** @brief 화면 크기의 주기 단계입니다. 표가 비었으면 0 입니다. */
        uint32 selectRateLevel( float32 screenSize ) const;
        /** @brief 단계가 없는 기본 표(모든 유닛이 매 프레임)입니다. */
        static AnimationLODSettings makeDefault() { return AnimationLODSettings{}; }
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
     * @struct AnimationLODUtil
     * @brief LOD 의 순수 식입니다(테스트가 그대로 부릅니다).
     */
    struct SW_API AnimationLODUtil
    {
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
        static AnimationLODState makeState( const AnimationLODSettings& settings, float32 screenSize, bool bVisible, const SkeletonBoneLOD* pBoneLOD );
    };
} // namespace sw
