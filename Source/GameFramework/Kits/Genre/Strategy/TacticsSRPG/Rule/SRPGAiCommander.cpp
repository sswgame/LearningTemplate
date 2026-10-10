#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGAiCommander.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Actor/Navigation/GridReachability.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGCombat.h"

namespace sw
{
    namespace
    {
        struct SRPGAiCommanderInternal
        {
            static constexpr int32 kNoScore = -2147483647;

            /** @brief @p cell 에서 가장 가까운 적까지의 거리입니다. 적이 없으면 −1 입니다. */
            static int32 computeNearestHostileDistance( const SRPGBattlefield& field, const SRPGUnit& unit, const int2& cell )
            {
                int32 nearest = -1;
                for ( const SRPGUnit& other : field.getUnits() )
                {
                    if ( other._bAlive == SW_FALSE || SRPGBattlefield::isHostile( unit._team, other._team ) == false )
                        continue;
                    const int32 distance = SRPGBattlefield::computeDistance( other._cell, cell );
                    if ( nearest < 0 || distance < nearest )
                        nearest = distance;
                }
                return nearest;
            }

            static int32 scoreForecast( const SRPGBattlefield& field, const SRPGAiSettings& settings, const SRPGUnit& unit, const SRPGForecast& forecast )
            {
                const SRPGStrikePreview& attack    = forecast._attack;
                const SRPGUnit*          pReceiver = field.findUnit( attack._defender );
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
    SRPGAiCommander::SRPGAiCommander()
        : _settings{}
    {
    }

    bool SRPGAiCommander::makePlan( const SRPGBattlefield& field, int32 unitIndex, SRPGAiPlan& outPlan ) const
    {
        outPlan = SRPGAiPlan{};
        if ( field.canAct( unitIndex ) == false )
            return false;
        const SRPGUnit& unit = *field.findUnit( unitIndex );
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

        int32        bestScore = SRPGAiCommanderInternal::kNoScore;
        int32        bestCost  = 0;
        SRPGForecast forecast;
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
                    const SRPGWeaponStatus status = SRPGCombat::computeForecastFrom( field, unitIndex, stand, weaponIndex, targetIndex, bAfterMove, forecast );
                    if ( status != SRPGWeaponStatus::Ok )
                        continue;
                    const int32 score   = SRPGAiCommanderInternal::scoreForecast( field, _settings, unit, forecast );
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
        int32 bestDistance = SRPGAiCommanderInternal::computeNearestHostileDistance( field, unit, unit._cell );
        bestCost           = 0;
        for ( const int2& stand : listStand )
        {
            const int32 distance = SRPGAiCommanderInternal::computeNearestHostileDistance( field, unit, stand );
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

    bool SRPGAiCommander::runUnit( SRPGBattlefield& field, int32 unitIndex, SRPGCombatResult* pOutResult ) const
    {
        SRPGAiPlan plan;
        if ( makePlan( field, unitIndex, plan ) == false )
            return false;
        if ( field.moveUnit( unitIndex, plan._moveCell ) && plan._bAttack == SW_TRUE )
        {
            SRPGCombatResult result;
            (void)SRPGCombat::executeAttack( field, unitIndex, plan._weapon, plan._target, result );
            if ( pOutResult != nullptr )
                *pOutResult = result;
        }
        field.endUnitAction( unitIndex );
        return true;
    }

    int32 SRPGAiCommander::runPhase( SRPGBattlefield& field, SRPGTeam team ) const
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
