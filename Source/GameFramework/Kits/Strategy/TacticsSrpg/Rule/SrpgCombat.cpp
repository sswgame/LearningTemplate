#include "pch.h"

#include "GameFramework/Kits/Strategy/TacticsSrpg/Rule/SrpgCombat.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct SrpgCombatInternal
        {
            /** @brief 기대 피해(명중률 × 피해)입니다 — 무기 · 지원자 고르기의 점수입니다. */
            static int32 computeExpected( const SrpgStrikePreview& preview ) { return preview._hit * preview._damage; }

            static SrpgPilotStat findWeaponStat( SrpgWeaponKind kind )
            {
                switch ( kind )
                {
                    case SrpgWeaponKind::Shooting:
                        return SrpgPilotStat::Shooting;
                    case SrpgWeaponKind::Melee:
                        return SrpgPilotStat::Melee;
                    case SrpgWeaponKind::Awaken:
                        return SrpgPilotStat::Awaken;
                }
                return SrpgPilotStat::Shooting;
            }

            /** @brief @p unitIndex 가 @p targetCell 을 지금 자리에서 칠 가장 좋은 무기의 타격입니다(MAP 병기 제외). */
            static SrpgStrikePreview findBestStrike( const SrpgBattlefield& field, int32 unitIndex, int32 targetIndex, int32 damagePercent )
            {
                SrpgStrikePreview best{};
                const SrpgUnit*   pUnit   = field.findUnit( unitIndex );
                const SrpgUnit*   pTarget = field.findUnit( targetIndex );
                if ( pUnit == nullptr || pTarget == nullptr )
                    return best;
                for ( int32 weaponIndex = 0; weaponIndex < static_cast<int32>( pUnit->_listWeapon.size() ); ++weaponIndex )
                {
                    const SrpgWeaponDef& weapon = *pUnit->_listWeapon[static_cast<size_t>( weaponIndex )];
                    if ( weapon.isMap() || field.computeWeaponStatus( *pUnit, weaponIndex, pUnit->_bMoved == SW_TRUE ) != SrpgWeaponStatus::Ok )
                        continue;
                    if ( SrpgBattlefield::isInWeaponRange( weapon, pUnit->_cell, pTarget->_cell ) == false )
                        continue;
                    const SrpgStrikePreview preview =
                        SrpgCombat::computeStrike( field, unitIndex, pUnit->_cell, weaponIndex, targetIndex, pTarget->_cell, damagePercent );
                    if ( best.isValid() == false || computeExpected( preview ) > computeExpected( best ) )
                        best = preview;
                }
                return best;
            }

            /** @brief 방어자와 이웃한 아군 중 대신 맞아도 쓰러지지 않는, HP 가 가장 많은 유닛입니다. */
            static int32 findSupportDefender( const SrpgBattlefield& field, int32 attackerIndex, const int2& attackerCell, int32 weaponIndex, int32 defenderIndex )
            {
                const SrpgSettings& settings  = field.getSettings();
                const SrpgUnit*     pDefender = field.findUnit( defenderIndex );
                if ( settings._bSupportDefense == SW_FALSE || pDefender == nullptr )
                    return -1;
                int32 bestIndex = -1;
                int32 bestHp    = 0;
                for ( int32 index = 0; index < static_cast<int32>( field.getUnits().size() ); ++index )
                {
                    const SrpgUnit& ally       = field.getUnits()[static_cast<size_t>( index )];
                    const bool      bCandidate = index != defenderIndex && ally._bAlive == SW_TRUE && ally._team == pDefender->_team &&
                                            ally._bSupportUsed == SW_FALSE && SrpgBattlefield::computeDistance( ally._cell, pDefender->_cell ) == 1;
                    if ( bCandidate == false )
                        continue;
                    const SrpgStrikePreview preview = SrpgCombat::computeStrike( field, attackerIndex, attackerCell, weaponIndex, index, ally._cell,
                                                                                 100 - settings._supportDefenseReduction );
                    if ( preview._critDamage >= ally._hp )
                        continue; // 대신 맞다 쓰러질 수 있으면 나서지 않는다
                    if ( bestIndex < 0 || ally._hp > bestHp )
                    {
                        bestIndex = index;
                        bestHp    = ally._hp;
                    }
                }
                return bestIndex;
            }

            /** @brief 공격자와 이웃한 아군 중 방어자에게 기대 피해가 가장 큰 지원 공격입니다. */
            static SrpgStrikePreview findSupportAttack( const SrpgBattlefield& field, int32 attackerIndex, const int2& attackerCell, int32 defenderIndex )
            {
                SrpgStrikePreview best{};
                const SrpgUnit*   pAttacker = field.findUnit( attackerIndex );
                if ( field.getSettings()._bSupportAttack == SW_FALSE || pAttacker == nullptr )
                    return best;
                for ( int32 index = 0; index < static_cast<int32>( field.getUnits().size() ); ++index )
                {
                    const SrpgUnit& ally       = field.getUnits()[static_cast<size_t>( index )];
                    const bool      bCandidate = index != attackerIndex && ally._bAlive == SW_TRUE && ally._team == pAttacker->_team &&
                                            ally._bSupportUsed == SW_FALSE && SrpgBattlefield::computeDistance( ally._cell, attackerCell ) == 1;
                    if ( bCandidate == false )
                        continue;
                    const SrpgStrikePreview preview = findBestStrike( field, index, defenderIndex, 100 );
                    if ( preview.isValid() && ( best.isValid() == false || computeExpected( preview ) > computeExpected( best ) ) )
                        best = preview;
                }
                return best;
            }

            /** @brief 같은 적을 사거리에 둔 아군마다 동기 공격 하나입니다(공격자 · 지원 공격자 제외). */
            static void collectSyncStrikes( const SrpgBattlefield& field, int32 attackerIndex, int32 supportIndex, int32 defenderIndex, vector<SrpgStrikePreview>& outListStrike )
            {
                outListStrike.clear();
                const SrpgUnit* pAttacker = field.findUnit( attackerIndex );
                if ( field.getSettings()._bSyncAttack == SW_FALSE || pAttacker == nullptr )
                    return;
                for ( int32 index = 0; index < static_cast<int32>( field.getUnits().size() ); ++index )
                {
                    const SrpgUnit& ally = field.getUnits()[static_cast<size_t>( index )];
                    if ( index == attackerIndex || index == supportIndex || ally._bAlive == SW_FALSE || ally._team != pAttacker->_team )
                        continue;
                    const SrpgStrikePreview preview = findBestStrike( field, index, defenderIndex, field.getSettings()._syncDamagePercent );
                    if ( preview.isValid() )
                        outListStrike.push_back( preview );
                }
            }

            static int64 computeXp( int32 base, int32 levelStep, int32 attackerLevel, int32 defenderLevel )
            {
                return MathUtil::max<int64>( 1, static_cast<int64>( base ) + static_cast<int64>( defenderLevel - attackerLevel ) * levelStep );
            }

            /** @brief 타격 하나를 굴립니다 — 명중 · 크리티컬 판정, 피해, 기력, 경험치, 알림. */
            static void resolveStrike( SrpgBattlefield& field, const SrpgStrikePreview& preview, SrpgStrikeRole role, SrpgCombatResult& outResult )
            {
                const SrpgUnit* pAttacker = field.findUnit( preview._attacker );
                const SrpgUnit* pDefender = field.findUnit( preview._defender );
                if ( pAttacker == nullptr || pDefender == nullptr || pAttacker->_bAlive == SW_FALSE || pDefender->_bAlive == SW_FALSE )
                    return;
                const SrpgSettings& settings      = field.getSettings();
                const int32         attackerLevel = pAttacker->_pilotLevel.getLevel();
                const int32         defenderLevel = pDefender->_pilotLevel.getLevel();
                SrpgStrikeResult    result;
                result._preview = preview;
                result._role    = role;
                result._bHit    = field.getRandom().nextInt( 0, 99 ) < preview._hit ? SW_TRUE : SW_FALSE;
                if ( result._bHit == SW_TRUE )
                {
                    result._bCrit         = field.getRandom().nextInt( 0, 99 ) < preview._crit ? SW_TRUE : SW_FALSE;
                    result._damage        = result._bCrit == SW_TRUE ? preview._critDamage : preview._damage;
                    const bool bDestroyed = field.applyDamage( preview._defender, result._damage, preview._attacker );
                    result._bDestroyed    = bDestroyed ? SW_TRUE : SW_FALSE;
                    field.pushEvent( SrpgEvent{ preview._attacker, preview._defender, result._damage, SrpgEvent::Kind::Hit, pAttacker->_team } );
                    field.addMorale( preview._attacker, settings._moraleOnHit + ( bDestroyed ? settings._moraleOnKill : 0 ) );
                    field.addMorale( preview._defender, settings._moraleOnDamaged );
                    int64 xp = computeXp( settings._xpHit, settings._xpLevelStep, attackerLevel, defenderLevel );
                    if ( bDestroyed )
                        xp += computeXp( settings._xpKill, settings._xpLevelStep, attackerLevel, defenderLevel );
                    field.grantXp( preview._attacker, xp );
                }
                else
                {
                    field.pushEvent( SrpgEvent{ preview._attacker, preview._defender, 0, SrpgEvent::Kind::Missed, pAttacker->_team } );
                    field.addMorale( preview._defender, settings._moraleOnEvade );
                }
                outResult._listStrike.push_back( result );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SrpgStrikePreview SrpgCombat::computeStrike( const SrpgBattlefield& field, int32 attackerIndex, const int2& attackerCell, int32 weaponIndex, int32 defenderIndex,
                                                 const int2& defenderCell, int32 damagePercent )
    {
        SrpgStrikePreview    preview{};
        const SrpgUnit*      pAttacker = field.findUnit( attackerIndex );
        const SrpgUnit*      pDefender = field.findUnit( defenderIndex );
        const SrpgWeaponDef* pWeapon   = pAttacker != nullptr ? field.findWeapon( *pAttacker, weaponIndex ) : nullptr;
        if ( pAttacker == nullptr || pDefender == nullptr || pWeapon == nullptr )
            return preview;
        const SrpgSettings&   settings = field.getSettings();
        const SrpgUnit&       attacker = *pAttacker;
        const SrpgUnit&       defender = *pDefender;
        const SrpgTerrainDef* pTerrain = field.findTerrainAt( defenderCell );
        const int32           aptitude = field.computeAptitude( attacker, attackerCell );

        const int32 stat        = attacker.computeStat( SrpgCombatInternal::findWeaponStat( pWeapon->_kind ) );
        int32       attackPower = pWeapon->_power * ( 100 + stat ) / 100;
        attackPower             = attackPower * aptitude / 100;
        attackPower             = attackPower * attacker._morale / 100;
        const int32 defensePower =
            ( defender._pDef->_armor + defender.computeStat( SrpgPilotStat::Defense ) ) * ( 100 + ( pTerrain != nullptr ? pTerrain->_defenseBonus : 0 ) ) / 100;
        const int32 damage = MathUtil::max( settings._minimumDamage, attackPower - defensePower ) * MathUtil::max( 0, damagePercent ) / 100;

        int32 hit = settings._baseHit + pWeapon->_hitBonus + attacker.computeStat( SrpgPilotStat::Reaction ) - defender.computeStat( SrpgPilotStat::Reaction ) +
                    ( defender._pDef->_size - attacker._pDef->_size ) * settings._sizeHitStep - ( pTerrain != nullptr ? pTerrain->_evasionBonus : 0 ) -
                    defender._pDef->_mobility - defender._dodge;
        hit = hit * aptitude / 100;
        const int32 crit =
            settings._baseCrit + pWeapon->_critBonus + MathUtil::max( 0, attacker.computeStat( SrpgPilotStat::Awaken ) - defender.computeStat( SrpgPilotStat::Awaken ) ) / 2;

        preview._attacker   = attackerIndex;
        preview._weapon     = weaponIndex;
        preview._defender   = defenderIndex;
        preview._hit        = MathUtil::clamp( hit, 0, 100 );
        preview._crit       = MathUtil::clamp( crit, 0, 100 );
        preview._damage     = damage;
        preview._critDamage = damage * settings._critPercent / 100;
        return preview;
    }

    SrpgWeaponStatus SrpgCombat::computeForecast( const SrpgBattlefield& field, int32 attackerIndex, int32 weaponIndex, int32 defenderIndex, SrpgForecast& outForecast )
    {
        const SrpgUnit* pAttacker = field.findUnit( attackerIndex );
        if ( pAttacker == nullptr )
        {
            outForecast = SrpgForecast{};
            return outForecast._status;
        }
        return computeForecastFrom( field, attackerIndex, pAttacker->_cell, weaponIndex, defenderIndex, pAttacker->_bMoved == SW_TRUE, outForecast );
    }

    SrpgWeaponStatus SrpgCombat::computeForecastFrom( const SrpgBattlefield& field, int32 attackerIndex, const int2& attackerCell, int32 weaponIndex, int32 defenderIndex,
                                                      bool bAfterMove, SrpgForecast& outForecast )
    {
        outForecast               = SrpgForecast{};
        outForecast._defender     = defenderIndex;
        const SrpgUnit* pAttacker = field.findUnit( attackerIndex );
        const SrpgUnit* pDefender = field.findUnit( defenderIndex );
        const bool      bTargetOk = pAttacker != nullptr && pDefender != nullptr && pAttacker->_bAlive == SW_TRUE && pDefender->_bAlive == SW_TRUE &&
                               SrpgBattlefield::isHostile( pAttacker->_team, pDefender->_team );
        if ( bTargetOk == false )
            return outForecast._status = SrpgWeaponStatus::InvalidTarget;
        outForecast._status = field.computeWeaponStatus( *pAttacker, weaponIndex, bAfterMove );
        if ( outForecast._status != SrpgWeaponStatus::Ok )
            return outForecast._status;
        const SrpgWeaponDef& weapon = *field.findWeapon( *pAttacker, weaponIndex );
        if ( weapon.isMap() )
            return outForecast._status = SrpgWeaponStatus::InvalidWeapon;
        if ( SrpgBattlefield::isInWeaponRange( weapon, attackerCell, pDefender->_cell ) == false )
            return outForecast._status = SrpgWeaponStatus::OutOfRange;

        const int32 supportDefender  = SrpgCombatInternal::findSupportDefender( field, attackerIndex, attackerCell, weaponIndex, defenderIndex );
        outForecast._supportDefender = supportDefender;
        if ( supportDefender >= 0 )
        {
            outForecast._attack = computeStrike( field, attackerIndex, attackerCell, weaponIndex, supportDefender, field.findUnit( supportDefender )->_cell,
                                                 100 - field.getSettings()._supportDefenseReduction );
        }
        else
        {
            outForecast._attack = computeStrike( field, attackerIndex, attackerCell, weaponIndex, defenderIndex, pDefender->_cell );
        }
        outForecast._supportAttack = SrpgCombatInternal::findSupportAttack( field, attackerIndex, attackerCell, defenderIndex );
        SrpgCombatInternal::collectSyncStrikes( field, attackerIndex, outForecast._supportAttack._attacker, defenderIndex, outForecast._listSync );
        const int32 counterWeapon = findCounterWeapon( field, defenderIndex, attackerIndex, attackerCell );
        if ( counterWeapon >= 0 )
            outForecast._counter = computeStrike( field, defenderIndex, pDefender->_cell, counterWeapon, attackerIndex, attackerCell );
        return outForecast._status;
    }

    int32 SrpgCombat::findCounterWeapon( const SrpgBattlefield& field, int32 defenderIndex, int32 attackerIndex, const int2& attackerCell )
    {
        const SrpgUnit* pDefender = field.findUnit( defenderIndex );
        if ( pDefender == nullptr || pDefender->_bAlive == SW_FALSE )
            return -1;
        int32 bestWeapon   = -1;
        int32 bestExpected = -1;
        for ( int32 weaponIndex = 0; weaponIndex < static_cast<int32>( pDefender->_listWeapon.size() ); ++weaponIndex )
        {
            const SrpgWeaponDef& weapon  = *pDefender->_listWeapon[static_cast<size_t>( weaponIndex )];
            const bool           bUsable = weapon._bCounter == SW_TRUE && weapon.isMap() == false &&
                                 field.computeWeaponStatus( *pDefender, weaponIndex, false ) == SrpgWeaponStatus::Ok;
            if ( bUsable == false || SrpgBattlefield::isInWeaponRange( weapon, pDefender->_cell, attackerCell ) == false )
                continue;
            const int32 expected =
                SrpgCombatInternal::computeExpected( computeStrike( field, defenderIndex, pDefender->_cell, weaponIndex, attackerIndex, attackerCell ) );
            if ( expected > bestExpected )
            {
                bestExpected = expected;
                bestWeapon   = weaponIndex;
            }
        }
        return bestWeapon;
    }

    SrpgWeaponStatus SrpgCombat::executeAttack( SrpgBattlefield& field, int32 attackerIndex, int32 weaponIndex, int32 defenderIndex, SrpgCombatResult& outResult )
    {
        outResult                 = SrpgCombatResult{};
        const SrpgUnit* pAttacker = field.findUnit( attackerIndex );
        if ( pAttacker == nullptr || field.canAct( attackerIndex ) == false || pAttacker->_bAttacked == SW_TRUE )
            return outResult._status = SrpgWeaponStatus::CannotAct;
        SrpgForecast forecast;
        outResult._status = computeForecast( field, attackerIndex, weaponIndex, defenderIndex, forecast );
        if ( outResult._status != SrpgWeaponStatus::Ok )
            return outResult._status;

        field.consumeWeapon( attackerIndex, weaponIndex );
        SrpgUnit* pMutableAttacker   = field.findUnit( attackerIndex );
        pMutableAttacker->_bAttacked = SW_TRUE;
        pMutableAttacker->_bMoved    = SW_TRUE; // 공격한 뒤에는 움직이지 않는다
        if ( forecast._supportDefender >= 0 )
            field.findUnit( forecast._supportDefender )->_bSupportUsed = SW_TRUE;

        SrpgCombatInternal::resolveStrike( field, forecast._attack, SrpgStrikeRole::Main, outResult );
        for ( const SrpgStrikePreview& sync : forecast._listSync )
        {
            SrpgCombatInternal::resolveStrike( field, sync, SrpgStrikeRole::Sync, outResult ); // 동기 공격은 EN · 탄을 쓰지 않는다
        }
        if ( forecast._supportAttack.isValid() && field.findUnit( defenderIndex )->_bAlive == SW_TRUE )
        {
            field.consumeWeapon( forecast._supportAttack._attacker, forecast._supportAttack._weapon );
            field.findUnit( forecast._supportAttack._attacker )->_bSupportUsed = SW_TRUE;
            SrpgCombatInternal::resolveStrike( field, forecast._supportAttack, SrpgStrikeRole::Support, outResult );
        }
        if ( forecast._counter.isValid() && field.findUnit( defenderIndex )->_bAlive == SW_TRUE && field.findUnit( attackerIndex )->_bAlive == SW_TRUE )
        {
            field.consumeWeapon( defenderIndex, forecast._counter._weapon );
            SrpgCombatInternal::resolveStrike( field, forecast._counter, SrpgStrikeRole::Counter, outResult );
        }
        return outResult._status;
    }

    SrpgWeaponStatus SrpgCombat::executeMapAttack( SrpgBattlefield& field, int32 attackerIndex, int32 weaponIndex, const int2& aimCell, SrpgCombatResult& outResult )
    {
        outResult                 = SrpgCombatResult{};
        const SrpgUnit* pAttacker = field.findUnit( attackerIndex );
        if ( pAttacker == nullptr || field.canAct( attackerIndex ) == false || pAttacker->_bAttacked == SW_TRUE )
            return outResult._status = SrpgWeaponStatus::CannotAct;
        outResult._status = field.computeWeaponStatus( *pAttacker, weaponIndex, pAttacker->_bMoved == SW_TRUE );
        if ( outResult._status != SrpgWeaponStatus::Ok )
            return outResult._status;
        vector<int2> listCell;
        if ( field.collectMapCells( attackerIndex, weaponIndex, aimCell, listCell ) == false )
            return outResult._status = SrpgWeaponStatus::InvalidWeapon;

        const bool                bFriendlyFire = field.getSettings()._bMapFriendlyFire == SW_TRUE;
        vector<SrpgStrikePreview> listStrike;
        for ( const int2& cell : listCell )
        {
            const int32 targetIndex = field.findUnitAt( cell );
            if ( targetIndex < 0 || targetIndex == attackerIndex )
                continue;
            if ( bFriendlyFire == false && SrpgBattlefield::isHostile( pAttacker->_team, field.findUnit( targetIndex )->_team ) == false )
                continue;
            listStrike.push_back( computeStrike( field, attackerIndex, pAttacker->_cell, weaponIndex, targetIndex, cell ) );
        }
        if ( listStrike.empty() )
            return outResult._status = SrpgWeaponStatus::InvalidTarget;

        field.consumeWeapon( attackerIndex, weaponIndex );
        SrpgUnit* pMutableAttacker   = field.findUnit( attackerIndex );
        pMutableAttacker->_bAttacked = SW_TRUE;
        pMutableAttacker->_bMoved    = SW_TRUE;
        for ( const SrpgStrikePreview& strike : listStrike )
        {
            SrpgCombatInternal::resolveStrike( field, strike, SrpgStrikeRole::Map, outResult );
        }
        return outResult._status;
    }
} // namespace sw
