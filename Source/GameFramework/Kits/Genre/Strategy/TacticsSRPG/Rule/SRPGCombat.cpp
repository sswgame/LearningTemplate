#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGCombat.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct SRPGCombatInternal
        {
            /** @brief 기대 피해(명중률 × 피해)입니다 — 무기 · 지원자 고르기의 점수입니다. */
            static int32 computeExpected( const SRPGStrikePreview& preview ) { return preview._hit * preview._damage; }

            static SRPGPilotStat findWeaponStat( SRPGWeaponKind kind )
            {
                switch ( kind )
                {
                    case SRPGWeaponKind::Shooting:
                        return SRPGPilotStat::Shooting;
                    case SRPGWeaponKind::Melee:
                        return SRPGPilotStat::Melee;
                    case SRPGWeaponKind::Awaken:
                        return SRPGPilotStat::Awaken;
                }
                return SRPGPilotStat::Shooting;
            }

            /** @brief @p unitIndex 가 @p targetCell 을 지금 자리에서 칠 가장 좋은 무기의 타격입니다(MAP 병기 제외). */
            static SRPGStrikePreview findBestStrike( const SRPGBattlefield& field, int32 unitIndex, int32 targetIndex, int32 damagePercent )
            {
                SRPGStrikePreview best{};
                const SRPGUnit*   pUnit   = field.findUnit( unitIndex );
                const SRPGUnit*   pTarget = field.findUnit( targetIndex );
                if ( pUnit == nullptr || pTarget == nullptr )
                    return best;
                for ( int32 weaponIndex = 0; weaponIndex < static_cast<int32>( pUnit->_listWeapon.size() ); ++weaponIndex )
                {
                    const SRPGWeaponDef& weapon = *pUnit->_listWeapon[static_cast<size_t>( weaponIndex )];
                    if ( weapon.isMap() || field.computeWeaponStatus( *pUnit, weaponIndex, pUnit->_bMoved == SW_TRUE ) != SRPGWeaponStatus::Ok )
                        continue;
                    if ( SRPGBattlefield::isInWeaponRange( weapon, pUnit->_cell, pTarget->_cell ) == false )
                        continue;
                    const SRPGStrikePreview preview =
                        SRPGCombat::computeStrike( field, unitIndex, pUnit->_cell, weaponIndex, targetIndex, pTarget->_cell, damagePercent );
                    if ( best.isValid() == false || computeExpected( preview ) > computeExpected( best ) )
                        best = preview;
                }
                return best;
            }

            /** @brief 방어자와 이웃한 아군 중 대신 맞아도 쓰러지지 않는, HP 가 가장 많은 유닛입니다. */
            static int32 findSupportDefender( const SRPGBattlefield& field, int32 attackerIndex, const int2& attackerCell, int32 weaponIndex, int32 defenderIndex )
            {
                const SRPGSettings& settings  = field.getSettings();
                const SRPGUnit*     pDefender = field.findUnit( defenderIndex );
                if ( settings._bSupportDefense == SW_FALSE || pDefender == nullptr )
                    return -1;
                int32 bestIndex = -1;
                int32 bestHp    = 0;
                for ( int32 index = 0; index < static_cast<int32>( field.getUnits().size() ); ++index )
                {
                    const SRPGUnit& ally       = field.getUnits()[static_cast<size_t>( index )];
                    const bool      bCandidate = index != defenderIndex && ally._bAlive == SW_TRUE && ally._team == pDefender->_team &&
                                            ally._bSupportUsed == SW_FALSE && SRPGBattlefield::computeDistance( ally._cell, pDefender->_cell ) == 1;
                    if ( bCandidate == false )
                        continue;
                    const SRPGStrikePreview preview = SRPGCombat::computeStrike( field, attackerIndex, attackerCell, weaponIndex, index, ally._cell,
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
            static SRPGStrikePreview findSupportAttack( const SRPGBattlefield& field, int32 attackerIndex, const int2& attackerCell, int32 defenderIndex )
            {
                SRPGStrikePreview best{};
                const SRPGUnit*   pAttacker = field.findUnit( attackerIndex );
                if ( field.getSettings()._bSupportAttack == SW_FALSE || pAttacker == nullptr )
                    return best;
                for ( int32 index = 0; index < static_cast<int32>( field.getUnits().size() ); ++index )
                {
                    const SRPGUnit& ally       = field.getUnits()[static_cast<size_t>( index )];
                    const bool      bCandidate = index != attackerIndex && ally._bAlive == SW_TRUE && ally._team == pAttacker->_team &&
                                            ally._bSupportUsed == SW_FALSE && SRPGBattlefield::computeDistance( ally._cell, attackerCell ) == 1;
                    if ( bCandidate == false )
                        continue;
                    const SRPGStrikePreview preview = findBestStrike( field, index, defenderIndex, 100 );
                    if ( preview.isValid() && ( best.isValid() == false || computeExpected( preview ) > computeExpected( best ) ) )
                        best = preview;
                }
                return best;
            }

            /** @brief 같은 적을 사거리에 둔 아군마다 동기 공격 하나입니다(공격자 · 지원 공격자 제외). */
            static void collectSyncStrikes( const SRPGBattlefield& field, int32 attackerIndex, int32 supportIndex, int32 defenderIndex, vector<SRPGStrikePreview>& outListStrike )
            {
                outListStrike.clear();
                const SRPGUnit* pAttacker = field.findUnit( attackerIndex );
                if ( field.getSettings()._bSyncAttack == SW_FALSE || pAttacker == nullptr )
                    return;
                for ( int32 index = 0; index < static_cast<int32>( field.getUnits().size() ); ++index )
                {
                    const SRPGUnit& ally = field.getUnits()[static_cast<size_t>( index )];
                    if ( index == attackerIndex || index == supportIndex || ally._bAlive == SW_FALSE || ally._team != pAttacker->_team )
                        continue;
                    const SRPGStrikePreview preview = findBestStrike( field, index, defenderIndex, field.getSettings()._syncDamagePercent );
                    if ( preview.isValid() )
                        outListStrike.push_back( preview );
                }
            }

            static int64 computeXp( int32 base, int32 levelStep, int32 attackerLevel, int32 defenderLevel )
            {
                return MathUtil::max<int64>( 1, static_cast<int64>( base ) + static_cast<int64>( defenderLevel - attackerLevel ) * levelStep );
            }

            /** @brief 타격 하나를 굴립니다 — 명중 · 크리티컬 판정, 피해, 기력, 경험치, 알림. */
            static void resolveStrike( SRPGBattlefield& field, const SRPGStrikePreview& preview, SRPGStrikeRole role, SRPGCombatResult& outResult )
            {
                const SRPGUnit* pAttacker = field.findUnit( preview._attacker );
                const SRPGUnit* pDefender = field.findUnit( preview._defender );
                if ( pAttacker == nullptr || pDefender == nullptr || pAttacker->_bAlive == SW_FALSE || pDefender->_bAlive == SW_FALSE )
                    return;
                const SRPGSettings& settings      = field.getSettings();
                const int32         attackerLevel = pAttacker->_pilotLevel.getLevel();
                const int32         defenderLevel = pDefender->_pilotLevel.getLevel();
                SRPGStrikeResult    result;
                result._preview = preview;
                result._role    = role;
                result._bHit    = field.getRandom().nextInt( 0, 99 ) < preview._hit ? SW_TRUE : SW_FALSE;
                if ( result._bHit == SW_TRUE )
                {
                    result._bCrit         = field.getRandom().nextInt( 0, 99 ) < preview._crit ? SW_TRUE : SW_FALSE;
                    result._damage        = result._bCrit == SW_TRUE ? preview._critDamage : preview._damage;
                    const bool bDestroyed = field.applyDamage( preview._defender, result._damage, preview._attacker );
                    result._bDestroyed    = bDestroyed ? SW_TRUE : SW_FALSE;
                    field.pushEvent( SRPGEvent{ preview._attacker, preview._defender, result._damage, SRPGEvent::Kind::Hit, pAttacker->_team } );
                    field.addMorale( preview._attacker, settings._moraleOnHit + ( bDestroyed ? settings._moraleOnKill : 0 ) );
                    field.addMorale( preview._defender, settings._moraleOnDamaged );
                    int64 xp = computeXp( settings._xpHit, settings._xpLevelStep, attackerLevel, defenderLevel );
                    if ( bDestroyed )
                        xp += computeXp( settings._xpKill, settings._xpLevelStep, attackerLevel, defenderLevel );
                    field.grantXp( preview._attacker, xp );
                }
                else
                {
                    field.pushEvent( SRPGEvent{ preview._attacker, preview._defender, 0, SRPGEvent::Kind::Missed, pAttacker->_team } );
                    field.addMorale( preview._defender, settings._moraleOnEvade );
                }
                outResult._listStrike.push_back( result );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SRPGStrikePreview SRPGCombat::computeStrike( const SRPGBattlefield& field, int32 attackerIndex, const int2& attackerCell, int32 weaponIndex, int32 defenderIndex,
                                                 const int2& defenderCell, int32 damagePercent )
    {
        SRPGStrikePreview    preview{};
        const SRPGUnit*      pAttacker = field.findUnit( attackerIndex );
        const SRPGUnit*      pDefender = field.findUnit( defenderIndex );
        const SRPGWeaponDef* pWeapon   = pAttacker != nullptr ? field.findWeapon( *pAttacker, weaponIndex ) : nullptr;
        if ( pAttacker == nullptr || pDefender == nullptr || pWeapon == nullptr )
            return preview;
        const SRPGSettings&   settings = field.getSettings();
        const SRPGUnit&       attacker = *pAttacker;
        const SRPGUnit&       defender = *pDefender;
        const SRPGTerrainDef* pTerrain = field.findTerrainAt( defenderCell );
        const int32           aptitude = field.computeAptitude( attacker, attackerCell );

        const int32 stat        = attacker.computeStat( SRPGCombatInternal::findWeaponStat( pWeapon->_kind ) );
        int32       attackPower = pWeapon->_power * ( 100 + stat ) / 100;
        attackPower             = attackPower * aptitude / 100;
        attackPower             = attackPower * attacker._morale / 100;
        const int32 defensePower =
            ( defender._pDef->_armor + defender.computeStat( SRPGPilotStat::Defense ) ) * ( 100 + ( pTerrain != nullptr ? pTerrain->_defenseBonus : 0 ) ) / 100;
        const int32 damage = MathUtil::max( settings._minimumDamage, attackPower - defensePower ) * MathUtil::max( 0, damagePercent ) / 100;

        int32 hit = settings._baseHit + pWeapon->_hitBonus + attacker.computeStat( SRPGPilotStat::Reaction ) - defender.computeStat( SRPGPilotStat::Reaction ) +
                    ( defender._pDef->_size - attacker._pDef->_size ) * settings._sizeHitStep - ( pTerrain != nullptr ? pTerrain->_evasionBonus : 0 ) -
                    defender._pDef->_mobility - defender._dodge;
        hit = hit * aptitude / 100;
        const int32 crit =
            settings._baseCrit + pWeapon->_critBonus + MathUtil::max( 0, attacker.computeStat( SRPGPilotStat::Awaken ) - defender.computeStat( SRPGPilotStat::Awaken ) ) / 2;

        preview._attacker   = attackerIndex;
        preview._weapon     = weaponIndex;
        preview._defender   = defenderIndex;
        preview._hit        = MathUtil::clamp( hit, 0, 100 );
        preview._crit       = MathUtil::clamp( crit, 0, 100 );
        preview._damage     = damage;
        preview._critDamage = damage * settings._critPercent / 100;
        return preview;
    }

    SRPGWeaponStatus SRPGCombat::computeForecast( const SRPGBattlefield& field, int32 attackerIndex, int32 weaponIndex, int32 defenderIndex, SRPGForecast& outForecast )
    {
        const SRPGUnit* pAttacker = field.findUnit( attackerIndex );
        if ( pAttacker == nullptr )
        {
            outForecast = SRPGForecast{};
            return outForecast._status;
        }
        return computeForecastFrom( field, attackerIndex, pAttacker->_cell, weaponIndex, defenderIndex, pAttacker->_bMoved == SW_TRUE, outForecast );
    }

    SRPGWeaponStatus SRPGCombat::computeForecastFrom( const SRPGBattlefield& field, int32 attackerIndex, const int2& attackerCell, int32 weaponIndex, int32 defenderIndex,
                                                      bool bAfterMove, SRPGForecast& outForecast )
    {
        outForecast               = SRPGForecast{};
        outForecast._defender     = defenderIndex;
        const SRPGUnit* pAttacker = field.findUnit( attackerIndex );
        const SRPGUnit* pDefender = field.findUnit( defenderIndex );
        const bool      bTargetOk = pAttacker != nullptr && pDefender != nullptr && pAttacker->_bAlive == SW_TRUE && pDefender->_bAlive == SW_TRUE &&
                               SRPGBattlefield::isHostile( pAttacker->_team, pDefender->_team );
        if ( bTargetOk == false )
            return outForecast._status = SRPGWeaponStatus::InvalidTarget;
        outForecast._status = field.computeWeaponStatus( *pAttacker, weaponIndex, bAfterMove );
        if ( outForecast._status != SRPGWeaponStatus::Ok )
            return outForecast._status;
        const SRPGWeaponDef& weapon = *field.findWeapon( *pAttacker, weaponIndex );
        if ( weapon.isMap() )
            return outForecast._status = SRPGWeaponStatus::InvalidWeapon;
        if ( SRPGBattlefield::isInWeaponRange( weapon, attackerCell, pDefender->_cell ) == false )
            return outForecast._status = SRPGWeaponStatus::OutOfRange;

        const int32 supportDefender  = SRPGCombatInternal::findSupportDefender( field, attackerIndex, attackerCell, weaponIndex, defenderIndex );
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
        outForecast._supportAttack = SRPGCombatInternal::findSupportAttack( field, attackerIndex, attackerCell, defenderIndex );
        SRPGCombatInternal::collectSyncStrikes( field, attackerIndex, outForecast._supportAttack._attacker, defenderIndex, outForecast._listSync );
        const int32 counterWeapon = findCounterWeapon( field, defenderIndex, attackerIndex, attackerCell );
        if ( counterWeapon >= 0 )
            outForecast._counter = computeStrike( field, defenderIndex, pDefender->_cell, counterWeapon, attackerIndex, attackerCell );
        return outForecast._status;
    }

    int32 SRPGCombat::findCounterWeapon( const SRPGBattlefield& field, int32 defenderIndex, int32 attackerIndex, const int2& attackerCell )
    {
        const SRPGUnit* pDefender = field.findUnit( defenderIndex );
        if ( pDefender == nullptr || pDefender->_bAlive == SW_FALSE )
            return -1;
        int32 bestWeapon   = -1;
        int32 bestExpected = -1;
        for ( int32 weaponIndex = 0; weaponIndex < static_cast<int32>( pDefender->_listWeapon.size() ); ++weaponIndex )
        {
            const SRPGWeaponDef& weapon  = *pDefender->_listWeapon[static_cast<size_t>( weaponIndex )];
            const bool           bUsable = weapon._bCounter == SW_TRUE && weapon.isMap() == false &&
                                 field.computeWeaponStatus( *pDefender, weaponIndex, false ) == SRPGWeaponStatus::Ok;
            if ( bUsable == false || SRPGBattlefield::isInWeaponRange( weapon, pDefender->_cell, attackerCell ) == false )
                continue;
            const int32 expected =
                SRPGCombatInternal::computeExpected( computeStrike( field, defenderIndex, pDefender->_cell, weaponIndex, attackerIndex, attackerCell ) );
            if ( expected > bestExpected )
            {
                bestExpected = expected;
                bestWeapon   = weaponIndex;
            }
        }
        return bestWeapon;
    }

    SRPGWeaponStatus SRPGCombat::executeAttack( SRPGBattlefield& field, int32 attackerIndex, int32 weaponIndex, int32 defenderIndex, SRPGCombatResult& outResult )
    {
        outResult                 = SRPGCombatResult{};
        const SRPGUnit* pAttacker = field.findUnit( attackerIndex );
        if ( pAttacker == nullptr || field.canAct( attackerIndex ) == false || pAttacker->_bAttacked == SW_TRUE )
            return outResult._status = SRPGWeaponStatus::CannotAct;
        SRPGForecast forecast;
        outResult._status = computeForecast( field, attackerIndex, weaponIndex, defenderIndex, forecast );
        if ( outResult._status != SRPGWeaponStatus::Ok )
            return outResult._status;

        field.consumeWeapon( attackerIndex, weaponIndex );
        SRPGUnit* pMutableAttacker   = field.findUnit( attackerIndex );
        pMutableAttacker->_bAttacked = SW_TRUE;
        pMutableAttacker->_bMoved    = SW_TRUE; // 공격한 뒤에는 움직이지 않는다
        if ( forecast._supportDefender >= 0 )
            field.findUnit( forecast._supportDefender )->_bSupportUsed = SW_TRUE;

        SRPGCombatInternal::resolveStrike( field, forecast._attack, SRPGStrikeRole::Main, outResult );
        for ( const SRPGStrikePreview& sync : forecast._listSync )
        {
            SRPGCombatInternal::resolveStrike( field, sync, SRPGStrikeRole::Sync, outResult ); // 동기 공격은 EN · 탄을 쓰지 않는다
        }
        if ( forecast._supportAttack.isValid() && field.findUnit( defenderIndex )->_bAlive == SW_TRUE )
        {
            field.consumeWeapon( forecast._supportAttack._attacker, forecast._supportAttack._weapon );
            field.findUnit( forecast._supportAttack._attacker )->_bSupportUsed = SW_TRUE;
            SRPGCombatInternal::resolveStrike( field, forecast._supportAttack, SRPGStrikeRole::Support, outResult );
        }
        if ( forecast._counter.isValid() && field.findUnit( defenderIndex )->_bAlive == SW_TRUE && field.findUnit( attackerIndex )->_bAlive == SW_TRUE )
        {
            field.consumeWeapon( defenderIndex, forecast._counter._weapon );
            SRPGCombatInternal::resolveStrike( field, forecast._counter, SRPGStrikeRole::Counter, outResult );
        }
        return outResult._status;
    }

    SRPGWeaponStatus SRPGCombat::executeMapAttack( SRPGBattlefield& field, int32 attackerIndex, int32 weaponIndex, const int2& aimCell, SRPGCombatResult& outResult )
    {
        outResult                 = SRPGCombatResult{};
        const SRPGUnit* pAttacker = field.findUnit( attackerIndex );
        if ( pAttacker == nullptr || field.canAct( attackerIndex ) == false || pAttacker->_bAttacked == SW_TRUE )
            return outResult._status = SRPGWeaponStatus::CannotAct;
        outResult._status = field.computeWeaponStatus( *pAttacker, weaponIndex, pAttacker->_bMoved == SW_TRUE );
        if ( outResult._status != SRPGWeaponStatus::Ok )
            return outResult._status;
        vector<int2> listCell;
        if ( field.collectMapCells( attackerIndex, weaponIndex, aimCell, listCell ) == false )
            return outResult._status = SRPGWeaponStatus::InvalidWeapon;

        const bool                bFriendlyFire = field.getSettings()._bMapFriendlyFire == SW_TRUE;
        vector<SRPGStrikePreview> listStrike;
        for ( const int2& cell : listCell )
        {
            const int32 targetIndex = field.findUnitAt( cell );
            if ( targetIndex < 0 || targetIndex == attackerIndex )
                continue;
            if ( bFriendlyFire == false && SRPGBattlefield::isHostile( pAttacker->_team, field.findUnit( targetIndex )->_team ) == false )
                continue;
            listStrike.push_back( computeStrike( field, attackerIndex, pAttacker->_cell, weaponIndex, targetIndex, cell ) );
        }
        if ( listStrike.empty() )
            return outResult._status = SRPGWeaponStatus::InvalidTarget;

        field.consumeWeapon( attackerIndex, weaponIndex );
        SRPGUnit* pMutableAttacker   = field.findUnit( attackerIndex );
        pMutableAttacker->_bAttacked = SW_TRUE;
        pMutableAttacker->_bMoved    = SW_TRUE;
        for ( const SRPGStrikePreview& strike : listStrike )
        {
            SRPGCombatInternal::resolveStrike( field, strike, SRPGStrikeRole::Map, outResult );
        }
        return outResult._status;
    }
} // namespace sw
