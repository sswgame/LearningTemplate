/**
 * @file SrpgAiController.h
 * @brief 택틱스 SRPG 의 적 AI — 갈 수 있는 칸 × 무기 × 표적마다 "기대 피해 − 반격 위험" 점수를 매겨 가장 좋은 것을 고릅니다(결정적).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgBattlefield.h"

namespace sw
{
    struct SrpgCombatResult;

    /** @brief 점수의 무게입니다. */
    struct SrpgAiSettings
    {
        int32 _counterRiskPercent{ 100 }; ///< 반격으로 받을 기대 피해를 얼마나 무겁게 볼까(%)
        int32 _killBonus{ 1000 };         ///< 격파할 수 있으면 더하는 점수(× 명중률)
        int32 _commanderBonus{ 500 };     ///< 표적이 지휘관이면 더하는 점수(× 명중률)
    };

    /** @brief 한 유닛의 계획입니다. */
    struct SrpgAiPlan
    {
        int2  _moveCell{};
        int32 _weapon{ -1 };
        int32 _target{ -1 };
        int32 _score{ 0 }; ///< 기대 피해 단위(명중률 % 를 곱한 뒤 100 으로 나눈 값)
        uint8 _bAttack{ SW_FALSE };
    };

    /**
     * @class SrpgAiController
     * @brief 표적이 없으면 가장 가까운 적 쪽으로 다가갑니다. 같은 점수면 덜 걷는 칸, 그다음 칸 번호 · 무기 · 표적 순서가 앞선 것 — 늘 같은 답입니다.
     * @details 점수 = 명중 × min( 피해, 표적 HP ) / 100 + 격파 가능이면 명중 × 격파 보너스 / 100 − 반격 명중 × min( 반격 피해, 내 HP ) / 100 × 위험 / 100.
     *          피해는 크리티컬을 넣지 않은 보통 피해입니다.
     */
    class SW_GF_API SrpgAiController
    {
    public:
        SrpgAiController();

        void                  setSettings( const SrpgAiSettings& settings ) { _settings = settings; }
        const SrpgAiSettings& getSettings() const { return _settings; }

        /** @brief 계획을 세웁니다. 움직일 수 없는 유닛이면 false 입니다. */
        [[nodiscard]] bool makePlan( const SrpgBattlefield& field, int32 unitIndex, SrpgAiPlan& outPlan ) const;
        /** @brief 계획대로 움직이고 치고 행동을 끝냅니다. @p pOutResult 가 있으면 전투 결과를 담습니다. */
        [[nodiscard]] bool runUnit( SrpgBattlefield& field, int32 unitIndex, SrpgCombatResult* pOutResult = nullptr ) const;
        /** @brief 지금 페이즈 팀(@p team 일 때만)의 행동할 수 있는 유닛을 번호 순서로 모두 움직입니다. 움직인 수입니다. */
        int32 runPhase( SrpgBattlefield& field, SrpgTeam team ) const;

    private:
        SrpgAiSettings _settings;
    };
} // namespace sw
