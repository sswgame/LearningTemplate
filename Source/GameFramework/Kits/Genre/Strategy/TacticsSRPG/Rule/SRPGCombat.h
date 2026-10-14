/**
 * @file SRPGCombat.h
 * @brief 택틱스 SRPG 의 전투 계산을 담당합니다. 명중률, 피해, 크리티컬 예측(`computeForecast`), 반격, 지원 공격, 지원 방어, 동기 공격, 결정적 난수를 쓰는 실제 전투, MAP 병기를 다룹니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGBattlefield.h"

namespace sw
{
    /** @brief 한 번의 타격이 전투에서 맡은 역할입니다. */
    enum class SRPGStrikeRole : uint8
    {
        Main = 0,
        Counter,
        Support, ///< 지원 공격
        Sync,    ///< 동기 공격
        Map
    };

    /** @brief 한 번의 타격 예측입니다. 쏘는 쪽이 -1 이면 없는 타격입니다. */
    struct SRPGStrikePreview
    {
        int32 _attacker{ -1 };
        int32 _weapon{ -1 };
        int32 _defender{ -1 }; ///< 실제로 맞는 쪽이다(지원 방어이면 대신 맞는 아군)
        int32 _hit{ 0 };       ///< 명중률(0..100)
        int32 _crit{ 0 };      ///< 크리티컬 확률(0..100)
        int32 _damage{ 0 };    ///< 크리티컬이 아닐 때의 피해
        int32 _critDamage{ 0 };

        bool isValid() const { return _attacker >= 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 공격 전에 보여 주는 예측입니다. */
    struct SRPGForecast
    {
        vector<SRPGStrikePreview> _listSync{}; ///< 동기 공격이다(켜져 있을 때만 채운다)
        SRPGStrikePreview         _attack{};
        SRPGStrikePreview         _supportAttack{};
        SRPGStrikePreview         _counter{}; ///< 방어자가 살아남을 때의 반격이다
        int32                     _defender{ -1 };
        int32                     _supportDefender{ -1 }; ///< 대신 맞는 아군이다(없으면 -1)
        SRPGWeaponStatus          _status{ SRPGWeaponStatus::InvalidTarget };
    };
} // namespace sw

namespace sw
{
    /** @brief 실제로 일어난 타격 하나입니다. */
    struct SRPGStrikeResult
    {
        SRPGStrikePreview _preview{};
        int32             _damage{ 0 }; ///< 들어간 피해이다(빗나가면 0)
        SRPGStrikeRole    _role{ SRPGStrikeRole::Main };
        uint8             _bHit{ SW_FALSE };
        uint8             _bCrit{ SW_FALSE };
        uint8             _bDestroyed{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 공격 한 번의 결과입니다. 타격 순서대로 담습니다. */
    struct SRPGCombatResult
    {
        vector<SRPGStrikeResult> _listStrike{};
        SRPGWeaponStatus         _status{ SRPGWeaponStatus::InvalidTarget };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SRPGCombat
     * @brief 전투 공식을 담습니다. 모두 정수 계산이며 나눗셈은 버림입니다.
     * @details - 공격력 = 위력 x (100 + 능력치) / 100 x 적성 / 100 x 기력 / 100. 능력치는 무기에 따라 사격, 격투, 각성 중 하나이다.
     *          - 방어력 = (장갑 + 수비) x (100 + 지형 방어) / 100
     *          - 피해 = max( 최소 피해, 공격력 - 방어력 ) x 비율 / 100. 비율은 지원 방어와 동기 공격에서 100 미만이고, 크리티컬이면 크리티컬 배율 / 100 을 곱한다.
     *          - 명중 = ( 기본 + 무기 명중 + 반응 차 + 크기 차 x 크기 단계 - 지형 회피 - 운동성 - 이동 회피 ) x 적성 / 100 이며 0..100 으로 자른다.
     *          - 크리티컬 = 기본 + 무기 크리티컬 + max( 0, 각성 차 ) / 2 이며 0..100 으로 자른다.
     *          실제 전투 순서는 주 공격, 동기 공격, 지원 공격, 반격(방어자가 살아 있을 때)이다. 판정 순서가 항상 같으므로 같은 시드이면 같은 결과가 나온다.
     */
    struct SW_KIT_API SRPGCombat
    {
        /** @brief 한 번의 타격을 예측합니다(사거리와 차례는 보지 않습니다). @p damagePercent 는 피해 비율입니다. */
        static SRPGStrikePreview computeStrike( const SRPGBattlefield& field, int32 attackerIndex, const int2& attackerCell, int32 weaponIndex, int32 defenderIndex,
                                                const int2& defenderCell, int32 damagePercent = 100 );
        /** @brief 지금 위치에서의 예측을 계산합니다. */
        static SRPGWeaponStatus computeForecast( const SRPGBattlefield& field, int32 attackerIndex, int32 weaponIndex, int32 defenderIndex, SRPGForecast& outForecast );
        /** @brief @p attackerCell 로 옮겼다고 가정하고 예측합니다(AI 와 이동 전 미리 보기용). */
        static SRPGWeaponStatus computeForecastFrom( const SRPGBattlefield& field, int32 attackerIndex, const int2& attackerCell, int32 weaponIndex, int32 defenderIndex,
                                                     bool bAfterMove, SRPGForecast& outForecast );
        /** @brief 방어자가 @p attackerCell 의 공격자에게 반격할 무기(기대 피해가 가장 큰 것)를 반환합니다. 없으면 -1 입니다. */
        static int32 findCounterWeapon( const SRPGBattlefield& field, int32 defenderIndex, int32 attackerIndex, const int2& attackerCell );

        /** @brief 공격합니다. 실패하면 상태만 담고 아무것도 바꾸지 않습니다. 행동 끝내기(`endUnitAction`)는 호출하는 쪽이 합니다. */
        static SRPGWeaponStatus executeAttack( SRPGBattlefield& field, int32 attackerIndex, int32 weaponIndex, int32 defenderIndex, SRPGCombatResult& outResult );
        /** @brief MAP 병기를 씁니다. 범위 안의 적마다 한 번씩 타격하며 반격과 지원은 없습니다. 맞을 유닛이 없으면 쏘지 않습니다(`InvalidTarget`). */
        static SRPGWeaponStatus executeMapAttack( SRPGBattlefield& field, int32 attackerIndex, int32 weaponIndex, const int2& aimCell, SRPGCombatResult& outResult );
    };
} // namespace sw
