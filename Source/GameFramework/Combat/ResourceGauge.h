/**
 * @file ResourceGauge.h
 * @brief 쓰고 다시 차는 게이지 — 스태미나 · 부스트 · 데드아이(쓰면 준다), 총 · 부스터 과열(쓰면 오른다)입니다.
 * @details 젤다 스태미나(0 까지 쓰면 탈진 — 일정량 다시 찰 때까지 못 쓴다 · 스태미나 그릇), 소울라이크 스태미나(쓴 뒤 잠깐 멈췄다 찬다),
 *          캡슐파이터 부스트 · 위쳐 · 리썰 컴퍼니 달리기, 과열형 무기가 같은 계산입니다. 시간은 `update` 로만 흐릅니다(결정적).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 게이지 설정입니다. 시간은 초입니다. */
    struct ResourceGaugeSettings
    {
        float32 _max{ 100.0f };
        float32 _regenRate{ 20.0f };           ///< 초당 회복(과열형이면 초당 식는 양)
        float32 _regenDelay{ 1.0f };           ///< 마지막으로 쓴 뒤 이만큼 지나야 차기(식기) 시작한다
        float32 _drainPerSecond{ 10.0f };      ///< 계속 쓰기(`drain( deltaTime )`)의 기본 초당 소비
        float32 _exhaustThreshold{ 0.0f };     ///< 0 보다 크면 — 0 까지 쓰면 탈진, 이 값까지 다시 차야 다시 쓸 수 있다
        float32 _overheatCooldown{ 0.0f };     ///< 과열형 — 과열되면 이만큼 더 기다린 뒤 식기 시작한다(벌칙)
        float32 _overheatRecoverLevel{ 0.0f }; ///< 과열형 — 열이 이 값까지 내려가야 잠금이 풀린다
        uint8   _bOverheatMode{ SW_FALSE };    ///< 쓰면 오르고 최대에 닿으면 과열(캡슐파이터 부스트 · 총 과열)
    };

    /**
     * @class ResourceGauge
     * @brief 값 하나와 잠금 하나입니다. 보통형은 값 = 남은 양(가득 = 최대), 과열형은 값 = 열(0 = 식음)입니다.
     * @details 보통형: `trySpend` 는 남은 양이 모자라면 쓰지 않습니다. `drain` 은 남은 만큼만 쓰고 0 에 닿으면 false 입니다(그때
     *          `_exhaustThreshold` 가 있으면 탈진 — 그 값까지 다시 찰 때까지 `trySpend` · `drain` 이 실패한다).
     *          과열형: 쓰면 열이 오르고 최대를 넘는 `trySpend` 는 실패, 최대에 닿으면 과열 — `_overheatRecoverLevel` 까지 식을 때까지 잠긴다.
     *          쓰면 회복 지연이 처음부터 다시 셉니다 — 바닥에 닿은 채 계속 쓰려 해도(`drain` 이 false 여도) 지연은 다시 셉니다. 같은 프레임에 쓰고 `update` 하면 지연이 0 일 때만 그 프레임에 찬다.
     */
    class SW_GF_API ResourceGauge
    {
    public:
        ResourceGauge();
        explicit ResourceGauge( const ResourceGaugeSettings& settings );

        /** @brief 설정을 두고 가득 찬(과열형은 식은) 상태로 시작합니다. 최대치 보너스는 0, 회복 배율은 1 로. */
        void initialize( const ResourceGaugeSettings& settings );

        /** @brief 한 번에 @p amount 를 씁니다(구르기 · 강공격 · 부스트 대시 · 한 발). 잠겼거나 모자라면 쓰지 않고 false 입니다. */
        [[nodiscard]] bool trySpend( float32 amount );
        /**
         * @brief 계속 씁니다(달리기 · 활공 · 부스트 유지). 이번 프레임 몫 @p rate × @p deltaTime 을 씁니다.
         * @return 다 쓸 수 있었으면 true. 잠겼거나 이번에 바닥(과열)에 닿았으면 false — 게임은 그 행동을 멈춘다.
         */
        bool drain( float32 rate, float32 deltaTime );
        /** @brief 설정의 `_drainPerSecond` 로 계속 씁니다. */
        bool drain( float32 deltaTime ) { return drain( _settings._drainPerSecond, deltaTime ); }
        /**
         * @brief 잠금과 상관없이 바로 깎습니다(정신력 충격 · 맞아서 줄어드는 스태미나). 과열형이면 그만큼 열이 오릅니다.
         * @details 회복 지연이 처음부터 다시 셉니다. 보통형이 0 에 닿으면 `_exhaustThreshold` 에 따라 탈진, 과열형이 최대에 닿으면 과열입니다.
         * @return 실제로 깎인(과열형은 오른) 양.
         */
        float32 reduce( float32 amount );
        /** @brief 시간을 흘립니다 — 회복 지연 · 회복(식기) · 잠금 풀기. */
        void update( float32 deltaTime );
        /** @brief 바로 채웁니다(물약 · 보급). 과열형이면 그만큼 식힙니다. 잠금 풀기 조건도 다시 봅니다. */
        void restore( float32 amount );
        /** @brief 가득 채우고(과열형은 다 식히고) 잠금을 풉니다. */
        void refill();
        /**
         * @brief 회복(식기) 속도 배율입니다(코어가 줄어 회복이 느려진다 · 스킬 버프). 회복 지연에는 듣지 않습니다. 음수는 0 으로 칩니다.
         */
        void    setRegenScale( float32 scale ) { _regenScale = scale > 0.0f ? scale : 0.0f; }
        float32 getRegenScale() const { return _regenScale; }
        /** @brief 최대치 보너스(스태미나 그릇 · 업그레이드)를 둡니다. 보통형은 늘어난 만큼 함께 찹니다. 줄면 값을 새 최대로 자릅니다. */
        void setMaxBonus( float32 bonus );

        const ResourceGaugeSettings& getSettings() const { return _settings; }
        float32                      getValue() const { return _value; }
        float32                      getMax() const { return _settings._max + _maxBonus; }
        float32                      getMaxBonus() const { return _maxBonus; }
        /** @brief 값 / 최대 입니다(과열형은 열의 비율). */
        float32 getRatio() const;
        /** @brief 보통형에서 바닥나 잠겼는가입니다. */
        bool isExhausted() const { return _bLocked == SW_TRUE && _settings._bOverheatMode == SW_FALSE; }
        /** @brief 과열형에서 최대에 닿아 잠겼는가입니다. */
        bool isOverheated() const { return _bLocked == SW_TRUE && _settings._bOverheatMode == SW_TRUE; }
        /** @brief 지금 쓸 수 있는가(잠기지 않았고 보통형이면 남은 양이 있다)입니다. */
        bool canUse() const;

    private:
        void markUsed();
        void releaseLockIfRecovered();

        ResourceGaugeSettings _settings;
        float32               _value;
        float32               _maxBonus;
        float32               _regenScale;
        float32               _sinceUse; ///< 마지막으로 쓴 뒤 지난 시간
        float32               _overheatPenaltyRemaining;
        uint8                 _bLocked; ///< 탈진 · 과열 잠금
    };
} // namespace sw
