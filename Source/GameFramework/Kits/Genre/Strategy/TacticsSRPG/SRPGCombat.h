/**
 * @file SRPGCombat.h
 * @brief 택틱스 SRPG 전투 계산(`SRPGBattlefield::compute*` · `execute*`)의 결과 형식입니다. 명중률, 피해, 크리티컬 예측(`computeForecast`), 반격, 지원 공격, 지원 방어, 동기 공격, 결정적 난수를 쓰는 실제 전투, MAP 병기를 다룹니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/SRPGBattlefield.h"

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
        int32 _attacker{ constant::invalid_index::kInt32 };
        int32 _weapon{ constant::invalid_index::kInt32 };
        int32 _defender{ constant::invalid_index::kInt32 }; ///< 실제로 맞는 쪽이다(지원 방어이면 대신 맞는 아군)
        int32 _hit{ 0 };                                    ///< 명중률(0..100)
        int32 _crit{ 0 };                                   ///< 크리티컬 확률(0..100)
        int32 _damage{ 0 };                                 ///< 크리티컬이 아닐 때의 피해
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
        int32                     _defender{ constant::invalid_index::kInt32 };
        optional<int32>           _supportDefender{}; ///< 대신 맞는 아군이다(없으면 비어 있다)
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
