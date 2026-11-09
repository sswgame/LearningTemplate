#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGCommanderAI.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGCombat.h"
#include "GameFramework/Runtime/Character/Navigation/GridReachability.h"

namespace sw
{
    namespace
    {
        struct SRPGCommanderAIInternal
        {
            static constexpr int32 kNoScore = -2147483647;

            /** @brief @p cell 에서 가장 가까운 적까지의 거리를 반환합니다. 적이 없으면 -1 입니다. */
            static int32 scoreForecast( const SRPGBattlefield& field, const SRPGCommanderAISettings& settings, const SRPGUnit& unit, const SRPGForecast& forecast )
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
    SRPGCommanderAI::SRPGCommanderAI()
        : _settings{}
    {
    }

    bool SRPGCommanderAI::makePlan( const SRPGBattlefield& field, int32 unitIndex, SRPGCommanderAIPlan& outPlan ) const
    {
        outPlan = SRPGCommanderAIPlan{};
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
            return true; // 이미 공격했으므로 제자리에서 끝낸다

        int32        bestScore = SRPGCommanderAIInternal::kNoScore;
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
                    const int32 score   = SRPGCommanderAIInternal::scoreForecast( field, _settings, unit, forecast );
                    const bool  bBetter = score > bestScore || ( score == bestScore && cost < bestCost );
                    if ( bBetter == false )
                        continue;
                    bestScore         = score;
                    bestCost          = cost;
                    outPlan._moveCell = stand;
                    outPlan._attack   = SRPGCommanderAITarget{ weaponIndex, targetIndex };
                    outPlan._score    = score;
                }
            }
        }
        if ( outPlan._attack.has_value() )
            return true;

        // 공격할 대상이 없으면 가장 가까운 적에게 다가간다(거리가 같으면 덜 걷는 셀을 고른다).
        int32 bestDistance = field.computeNearestHostileDistance( unit, unit._cell );
        bestCost           = 0;
        for ( const int2& stand : listStand )
        {
            const int32 distance = field.computeNearestHostileDistance( unit, stand );
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

    bool SRPGCommanderAI::runUnit( SRPGBattlefield& field, int32 unitIndex, SRPGCombatResult* pOutResult ) const
    {
        SRPGCommanderAIPlan plan;
        if ( makePlan( field, unitIndex, plan ) == false )
            return false;
        // 이미 움직인 유닛의 계획은 제자리다. `moveUnit` 은 그 유닛을 거절하므로 제자리면 부르지 않는다(거절을 공격 포기로 읽었다).
        const bool bInPlace = plan._moveCell == field.findUnit( unitIndex )->_cell;
        if ( ( bInPlace || field.moveUnit( unitIndex, plan._moveCell ) ) && plan._attack.has_value() )
        {
            SRPGCombatResult result;
            (void)SRPGCombat::executeAttack( field, unitIndex, plan._attack->_weapon, plan._attack->_target, result ); // 결과는 result 로 넘긴다
            if ( pOutResult != nullptr )
                *pOutResult = result;
        }
        field.endUnitAction( unitIndex );
        return true;
    }

    int32 SRPGCommanderAI::runPhase( SRPGBattlefield& field, SRPGTeam team ) const
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
            const bool bStopActing = unitIndex < 0 || field.canAct( unitIndex ) == false || runUnit( field, unitIndex ) == false;
            if ( bStopActing )
                break;
            ++movedCount;
        }
        return movedCount;
    }
} // namespace sw
