/**
 * @file SrpgCombat.h
 * @brief 택틱스 SRPG 의 전투 계산 — 명중률 · 피해 · 크리티컬 예측(`computeForecast`), 반격 · 지원 공격 · 지원 방어 · 동기 공격, 결정적 난수로 실제 전투, MAP 병기입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSrpg/Rule/SrpgBattlefield.h"

namespace sw
{
    /** @brief 한 번의 타격이 전투에서 맡은 몫입니다. */
    enum class SrpgStrikeRole : uint8
    {
        Main = 0,
        Counter,
        Support, ///< 지원 공격
        Sync,    ///< 동기 공격
        Map
    };

    /** @brief 한 번의 타격 예측입니다. 쏘는 쪽이 −1 이면 없는 타격입니다. */
    struct SrpgStrikePreview
    {
        int32 _attacker{ -1 };
        int32 _weapon{ -1 };
        int32 _defender{ -1 }; ///< 실제로 맞는 쪽(지원 방어면 대신 맞는 아군)
        int32 _hit{ 0 };       ///< 명중률(0..100)
        int32 _crit{ 0 };      ///< 크리티컬 확률(0..100)
        int32 _damage{ 0 };    ///< 보통 피해
        int32 _critDamage{ 0 };

        bool isValid() const { return _attacker >= 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 공격 전에 보이는 예측 창입니다. */
    struct SrpgForecast
    {
        vector<SrpgStrikePreview> _listSync{}; ///< 동기 공격(켰을 때)
        SrpgStrikePreview         _attack{};
        SrpgStrikePreview         _supportAttack{};
        SrpgStrikePreview         _counter{}; ///< 방어자가 살아남으면
        int32                     _defender{ -1 };
        int32                     _supportDefender{ -1 }; ///< 대신 맞는 아군(없으면 −1)
        SrpgWeaponStatus          _status{ SrpgWeaponStatus::InvalidTarget };
    };
} // namespace sw

namespace sw
{
    /** @brief 실제로 일어난 타격 하나입니다. */
    struct SrpgStrikeResult
    {
        SrpgStrikePreview _preview{};
        int32             _damage{ 0 }; ///< 들어간 피해(빗나가면 0)
        SrpgStrikeRole    _role{ SrpgStrikeRole::Main };
        uint8             _bHit{ SW_FALSE };
        uint8             _bCrit{ SW_FALSE };
        uint8             _bDestroyed{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 공격 한 번의 결과(타격 순서대로)입니다. */
    struct SrpgCombatResult
    {
        vector<SrpgStrikeResult> _listStrike{};
        SrpgWeaponStatus         _status{ SrpgWeaponStatus::InvalidTarget };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SrpgCombat
     * @brief 공식(모두 정수, 나눗셈은 버림)입니다.
     * @details - 공격력 = 위력 × (100 + 능력치) / 100 × 적성 / 100 × 기력 / 100 — 능력치는 무기에 따라 사격 · 격투 · 각성
     *          - 방어력 = (장갑 + 수비) × (100 + 지형 방어) / 100
     *          - 피해 = max( 최소 피해, 공격력 − 방어력 ) × 몫 / 100 — 몫은 지원 방어 · 동기 공격에서 100 미만, 크리티컬은 × 크리티컬 배율 / 100
     *          - 명중 = ( 기본 + 무기 명중 + 반응 차 + 크기 차 × 크기 걸음 − 지형 회피 − 운동성 − 이동 회피 ) × 적성 / 100, 0..100 으로 자른다
     *          - 크리티컬 = 기본 + 무기 크리티컬 + max( 0, 각성 차 ) / 2, 0..100
     *          실제 전투 순서는 주 공격 → 동기 공격 → 지원 공격 → 반격(방어자가 살아 있으면)입니다. 판정 순서가 늘 같아 같은 씨앗이면 같은 결과입니다.
     */
    struct SW_GF_API SrpgCombat
    {
        /** @brief 한 번의 타격을 예측합니다(사거리 · 차례는 보지 않는다). @p damagePercent 는 피해 몫입니다. */
        static SrpgStrikePreview computeStrike( const SrpgBattlefield& field, int32 attackerIndex, const int2& attackerCell, int32 weaponIndex, int32 defenderIndex,
                                                const int2& defenderCell, int32 damagePercent = 100 );
        /** @brief 지금 자리에서의 예측입니다. */
        static SrpgWeaponStatus computeForecast( const SrpgBattlefield& field, int32 attackerIndex, int32 weaponIndex, int32 defenderIndex, SrpgForecast& outForecast );
        /** @brief @p attackerCell 로 옮겼다고 치고 예측합니다(AI · 이동 전 미리 보기). */
        static SrpgWeaponStatus computeForecastFrom( const SrpgBattlefield& field, int32 attackerIndex, const int2& attackerCell, int32 weaponIndex, int32 defenderIndex,
                                                     bool bAfterMove, SrpgForecast& outForecast );
        /** @brief 방어자가 @p attackerCell 의 공격자에게 반격할 무기(기대 피해가 가장 큰 것)입니다. 없으면 −1 입니다. */
        static int32 findCounterWeapon( const SrpgBattlefield& field, int32 defenderIndex, int32 attackerIndex, const int2& attackerCell );

        /** @brief 공격합니다. 실패하면 상태만 담고 아무것도 바꾸지 않습니다. 행동 끝내기(`endUnitAction`)는 부르는 쪽이 합니다. */
        static SrpgWeaponStatus executeAttack( SrpgBattlefield& field, int32 attackerIndex, int32 weaponIndex, int32 defenderIndex, SrpgCombatResult& outResult );
        /** @brief MAP 병기를 씁니다 — 범위 안의 적마다 한 번씩, 반격 · 지원 없음. 맞을 유닛이 없으면 쏘지 않습니다(`InvalidTarget`). */
        static SrpgWeaponStatus executeMapAttack( SrpgBattlefield& field, int32 attackerIndex, int32 weaponIndex, const int2& aimCell, SrpgCombatResult& outResult );
    };
} // namespace sw
