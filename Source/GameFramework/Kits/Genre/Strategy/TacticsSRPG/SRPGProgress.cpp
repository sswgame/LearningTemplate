#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/SRPGProgress.h"

#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"

#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/SRPGBattlefield.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/SRPGCatalog.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    namespace
    {
        bool isCommanderLost( const SRPGBattlefield& field, SRPGTeam team, bool& outHasCommander )
        {
            outHasCommander = false;
            bool bLost      = false;
            for ( const SRPGUnit& unit : field.getUnits() )
            {
                if ( unit._team != team || unit._bCommander == SW_FALSE )
                    continue;
                outHasCommander = true;
                bLost           = bLost || unit._bAlive == SW_FALSE;
            }
            return bLost;
        }
    } // namespace
} // namespace sw

namespace sw
{
    SRPGResult SRPGMissionRule::evaluate( const SRPGBattlefield& field, const SRPGMissionRule& rule )
    {
        if ( field.getTurn() <= 0 )
            return SRPGResult::Ongoing;
        if ( field.countAlive( SRPGTeam::Player ) == 0 )
            return SRPGResult::Defeat;
        bool bPlayerHasCommander = false;
        if ( rule._bLoseOnCommander == SW_TRUE && isCommanderLost( field, SRPGTeam::Player, bPlayerHasCommander ) )
            return SRPGResult::Defeat;
        const bool bOverTurnLimit = rule._turnLimit > 0 && field.getTurn() > rule._turnLimit;
        if ( bOverTurnLimit && rule._objective != SRPGObjective::SurviveTurns )
            return SRPGResult::Defeat;

        switch ( rule._objective )
        {
            case SRPGObjective::DefeatAll:
            {
                return field.countAlive( SRPGTeam::Enemy ) == 0 ? SRPGResult::Victory : SRPGResult::Ongoing;
            }
            case SRPGObjective::DefeatCommander:
            {
                bool       bEnemyHasCommander = false;
                const bool bLost              = isCommanderLost( field, SRPGTeam::Enemy, bEnemyHasCommander );
                if ( bEnemyHasCommander == false )
                    return field.countAlive( SRPGTeam::Enemy ) == 0 ? SRPGResult::Victory : SRPGResult::Ongoing; // 지휘관이 없으면 전멸로 판정한다
                return bLost ? SRPGResult::Victory : SRPGResult::Ongoing;
            }
            case SRPGObjective::ReachCell:
            {
                for ( const SRPGUnit& unit : field.getUnits() )
                {
                    if ( unit._team != SRPGTeam::Player || unit._bAlive == SW_FALSE )
                        continue;
                    for ( const int2& goal : rule._listGoalCell )
                    {
                        if ( unit._cell == goal )
                            return SRPGResult::Victory;
                    }
                }
                return SRPGResult::Ongoing;
            }
            case SRPGObjective::SurviveTurns:
            {
                return bOverTurnLimit ? SRPGResult::Victory : SRPGResult::Ongoing;
            }
        }
        return SRPGResult::Ongoing;
    }

    SRPGCampaign::SRPGCampaign()
        : _runMap{}
        , _listRoster{}
        , _seed{ 0 }
        , _bInMission{ SW_FALSE }
        , _bFailed{ SW_FALSE }
    {
    }

    bool SRPGCampaign::initialize( const RunMapSettings& settings, uint32 seed )
    {
        if ( _runMap.generate( settings, seed ) == false )
            return false;
        _seed = seed;
        _listRoster.clear();
        _bInMission = SW_FALSE;
        _bFailed    = SW_FALSE;
        return true;
    }

    expected<int32> SRPGCampaign::addRosterEntry( const SRPGCatalog& catalog, const hashed_string& unitID, const hashed_string& pilotID, int32 pilotLevel )
    {
        if ( catalog.findUnit( unitID ) == nullptr || catalog.findPilot( pilotID ) == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "unknown unit or pilot id", unitID.c_str() } );
        SRPGRosterEntry entry;
        entry._unitID  = unitID;
        entry._pilotID = pilotID;
        entry._pilotLevel.setLevel( catalog.getPilotCurve(), pilotLevel );
        entry._unitLevel.setLevel( catalog.getUnitCurve(), 1 );
        _listRoster.push_back( entry );
        return static_cast<int32>( _listRoster.size() ) - 1;
    }

    bool SRPGCampaign::beginMission( int32 nodeIndex )
    {
        const bool bCannotBeginMission = _bFailed == SW_TRUE || _bInMission == SW_TRUE || _runMap.moveTo( nodeIndex ) == false;
        if ( bCannotBeginMission )
            return false;
        _bInMission = SW_TRUE;
        return true;
    }

    int32 SRPGCampaign::deployRoster( SRPGBattlefield& field, const vector<int2>& listCell ) const
    {
        int32  placedCount = 0;
        size_t cellIndex   = 0;
        for ( size_t rosterIndex = 0; rosterIndex < _listRoster.size() && cellIndex < listCell.size(); ++rosterIndex )
        {
            const SRPGRosterEntry& entry = _listRoster[rosterIndex];
            if ( entry._bLost == SW_TRUE )
                continue;
            const expected<int32> unitIndex = field.addUnit( entry._unitID, entry._pilotID, SRPGTeam::Player, listCell[cellIndex], entry._pilotLevel.getLevel() );
            ++cellIndex;
            if ( unitIndex.has_value() == false )
                continue;
            SRPGUnit* pUnit = field.findUnit( *unitIndex );
            if ( pUnit == nullptr )
                continue;
            pUnit->_pilotLevel  = entry._pilotLevel;
            pUnit->_unitLevel   = entry._unitLevel;
            pUnit->_rosterIndex = static_cast<int32>( rosterIndex );
            ++placedCount;
        }
        return placedCount;
    }

    void SRPGCampaign::completeMission( const SRPGBattlefield& field, SRPGResult result )
    {
        if ( _bInMission == SW_FALSE || result == SRPGResult::Ongoing )
            return;
        for ( const SRPGUnit& unit : field.getUnits() )
        {
            if ( unit._rosterIndex < 0 || unit._rosterIndex >= static_cast<int32>( _listRoster.size() ) )
                continue;
            SRPGRosterEntry& entry = _listRoster[static_cast<size_t>( unit._rosterIndex )];
            entry._pilotLevel      = unit._pilotLevel;
            entry._unitLevel       = unit._unitLevel;
            entry._unitID          = unit._pDef->_id; // 작전 중에 개발했으면 새 기체로 갱신한다
            if ( unit._bAlive == SW_FALSE )
                entry._bLost = SW_TRUE;
        }
        bool bAnyLeft = false;
        for ( const SRPGRosterEntry& entry : _listRoster )
        {
            bAnyLeft = bAnyLeft || entry._bLost == SW_FALSE;
        }
        _bInMission = SW_FALSE;
        if ( result == SRPGResult::Defeat || bAnyLeft == false )
            _bFailed = SW_TRUE;
    }

    hashed_string SRPGCampaign::getMissionKind() const
    {
        const RunNode* pNode = _runMap.findNode( _runMap.getCurrent() );
        return pNode != nullptr ? pNode->_kind : hashed_string{};
    }

    uint32 SRPGCampaign::getMissionSeed() const
    {
        return GameRandom::deriveSeed( _seed, HashUtil::mix32( static_cast<uint32>( _runMap.getCurrent() + 1 ) * 0x9e3779b1u ) );
    }

    void SRPGCampaign::serializeState( StateIO& io )
    {
        io.value( _runMap ); // 지도는 제 본문(리플렉션 칸 + 칸 사이 검사)으로 싣는다
        io.reflected( *this );
        if ( io.isWriting() || io.isOk() == false )
            return;
        // 레벨은 안쪽 구조체 칸이라 그 형식의 칸 사이 검사(xp ≤ 총 xp)를 여기서 한다
        for ( const SRPGRosterEntry& entry : _listRoster )
        {
            io.check( entry._pilotLevel.getXP() <= entry._pilotLevel.getTotalXP() && entry._unitLevel.getXP() <= entry._unitLevel.getTotalXP() );
        }
    }

} // namespace sw
