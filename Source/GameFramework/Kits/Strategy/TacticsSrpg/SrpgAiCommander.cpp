#include "pch.h"

#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgAiCommander.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Navigation/GridReachability.h"
#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgCombat.h"

namespace sw
{
    namespace
    {
        struct SrpgAiCommanderInternal
        {
            static constexpr int32 kNoScore = -2147483647;

            /** @brief @p cell 에서 가장 가까운 적까지의 거리입니다. 적이 없으면 −1 입니다. */
            static int32 computeNearestHostileDistance( const SrpgBattlefield& field, const SrpgUnit& unit, const int2& cell )
            {
                int32 nearest = -1;
                for ( const SrpgUnit& other : field.getUnits() )
                {
                    if ( other._bAlive == SW_FALSE || SrpgBattlefield::isHostile( unit._team, other._team ) == false )
                        continue;
                    const int32 distance = SrpgBattlefield::computeDistance( other._cell, cell );
                    if ( nearest < 0 || distance < nearest )
                        nearest = distance;
                }
                return nearest;
            }

            static int32 scoreForecast( const SrpgBattlefield& field, const SrpgAiSettings& settings, const SrpgUnit& unit, const SrpgForecast& forecast )
            {
                const SrpgStrikePreview& attack    = forecast._attack;
                const SrpgUnit*          pReceiver = field.findUnit( attack._defender );
                if ( pReceiver == nullptr )
                    return kNoScore;
                const bool bKill = attack._damage >= pReceiver->_hp;
                int32      score = attack._hit * MathUtil::min( attack._damage, pReceiver->_hp ) / 100;
                if ( bKill )
                    score += attack._hit * settings._killBonus / 100;
                if ( pReceiver->_bCommander == SW_TRUE )
                    score += attack._hit * settings._commanderBonus / 100;
                if ( forecast._counter.isValid() )
                {
                    int32 risk = forecast._counter._hit * MathUtil::min( forecast._counter._damage, unit._hp ) / 100 * settings._counterRiskPercent / 100;
                    if ( bKill )
                        risk = risk * ( 100 - attack._hit ) / 100; // 격파하면 반격은 오지 않는다
                    score -= risk;
                }
                return score;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SrpgAiCommander::SrpgAiCommander()
        : _settings{}
    {
    }

    bool SrpgAiCommander::makePlan( const SrpgBattlefield& field, int32 unitIndex, SrpgAiPlan& outPlan ) const
    {
        outPlan = SrpgAiPlan{};
        if ( field.canAct( unitIndex ) == false )
            return false;
        const SrpgUnit& unit = *field.findUnit( unitIndex );
        outPlan._moveCell    = unit._cell;

        GridReachability reach;
        vector<int2>     listStand;
        if ( unit._bMoved == SW_FALSE )
        {
            field.computeMoveRange( unitIndex, reach );
            reach.collectReachable( listStand );
        }
        else
        {
            listStand.push_back( unit._cell );
        }
        if ( unit._bAttacked == SW_TRUE )
            return true; // 이미 쳤다 — 제자리에서 끝낸다

        int32        bestScore = SrpgAiCommanderInternal::kNoScore;
        int32        bestCost  = 0;
        SrpgForecast forecast;
        for ( const int2& stand : listStand )
        {
            const int32 cost       = unit._bMoved == SW_FALSE ? reach.getCost( stand ) : 0;
            const bool  bAfterMove = unit._bMoved == SW_TRUE || stand != unit._cell;
            for ( int32 weaponIndex = 0; weaponIndex < static_cast<int32>( unit._listWeapon.size() ); ++weaponIndex )
            {
                if ( unit._listWeapon[static_cast<size_t>( weaponIndex )]->isMap() )
                    continue;
                for ( int32 targetIndex = 0; targetIndex < static_cast<int32>( field.getUnits().size() ); ++targetIndex )
                {
                    const SrpgWeaponStatus status = SrpgCombat::computeForecastFrom( field, unitIndex, stand, weaponIndex, targetIndex, bAfterMove, forecast );
                    if ( status != SrpgWeaponStatus::Ok )
                        continue;
                    const int32 score   = SrpgAiCommanderInternal::scoreForecast( field, _settings, unit, forecast );
                    const bool  bBetter = score > bestScore || ( score == bestScore && cost < bestCost );
                    if ( bBetter == false )
                        continue;
                    bestScore         = score;
                    bestCost          = cost;
                    outPlan._moveCell = stand;
                    outPlan._weapon   = weaponIndex;
                    outPlan._target   = targetIndex;
                    outPlan._score    = score;
                    outPlan._bAttack  = SW_TRUE;
                }
            }
        }
        if ( outPlan._bAttack == SW_TRUE )
            return true;

        // 칠 것이 없다 — 가장 가까운 적에게 다가간다(같으면 덜 걷는 칸)
        int32 bestDistance = SrpgAiCommanderInternal::computeNearestHostileDistance( field, unit, unit._cell );
        bestCost           = 0;
        for ( const int2& stand : listStand )
        {
            const int32 distance = SrpgAiCommanderInternal::computeNearestHostileDistance( field, unit, stand );
            const int32 cost     = unit._bMoved == SW_FALSE ? reach.getCost( stand ) : 0;
            const bool  bCloser  = distance >= 0 && ( distance < bestDistance || ( distance == bestDistance && cost < bestCost ) );
            if ( bCloser == false )
                continue;
            bestDistance      = distance;
            bestCost          = cost;
            outPlan._moveCell = stand;
        }
        return true;
    }

    bool SrpgAiCommander::runUnit( SrpgBattlefield& field, int32 unitIndex, SrpgCombatResult* pOutResult ) const
    {
        SrpgAiPlan plan;
        if ( makePlan( field, unitIndex, plan ) == false )
            return false;
        if ( field.moveUnit( unitIndex, plan._moveCell ) && plan._bAttack == SW_TRUE )
        {
            SrpgCombatResult result;
            (void)SrpgCombat::executeAttack( field, unitIndex, plan._weapon, plan._target, result );
            if ( pOutResult != nullptr )
                *pOutResult = result;
        }
        field.endUnitAction( unitIndex );
        return true;
    }

    int32 SrpgAiCommander::runPhase( SrpgBattlefield& field, SrpgTeam team ) const
    {
        int32       movedCount = 0;
        const int32 limit      = field.countAlive( team );
        while ( movedCount < limit && field.getPhaseTeam() == team )
        {
            int32 unitIndex = field.getActiveUnit();
            if ( unitIndex < 0 )
            {
                for ( int32 index = 0; index < static_cast<int32>( field.getUnits().size() ); ++index )
                {
                    if ( field.findUnit( index )->_team == team && field.canAct( index ) )
                    {
                        unitIndex = index;
                        break;
                    }
                }
            }
            if ( unitIndex < 0 || field.canAct( unitIndex ) == false || runUnit( field, unitIndex ) == false )
                break;
            ++movedCount;
        }
        return movedCount;
    }
} // namespace sw
