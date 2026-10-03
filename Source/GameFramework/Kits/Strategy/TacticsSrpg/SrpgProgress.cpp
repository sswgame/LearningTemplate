#include "pch.h"

#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgProgress.h"

#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgBattlefield.h"
#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgCatalog.h"
#include "GameFramework/Utility/GameRandom.h"

namespace sw
{
    namespace
    {
        struct SrpgProgressInternal
        {
            static bool isCommanderLost( const SrpgBattlefield& field, SrpgTeam team, bool& outHasCommander )
            {
                outHasCommander = false;
                bool bLost      = false;
                for ( const SrpgUnit& unit : field.getUnits() )
                {
                    if ( unit._team != team || unit._bCommander == SW_FALSE )
                        continue;
                    outHasCommander = true;
                    bLost           = bLost || unit._bAlive == SW_FALSE;
                }
                return bLost;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SrpgOutcome SrpgMission::evaluate( const SrpgBattlefield& field, const SrpgMissionRule& rule )
    {
        if ( field.getTurn() <= 0 )
            return SrpgOutcome::Ongoing;
        if ( field.countAlive( SrpgTeam::Player ) == 0 )
            return SrpgOutcome::Defeat;
        bool bPlayerHasCommander = false;
        if ( rule._bLoseOnCommander == SW_TRUE && SrpgProgressInternal::isCommanderLost( field, SrpgTeam::Player, bPlayerHasCommander ) )
            return SrpgOutcome::Defeat;
        const bool bOverTurnLimit = rule._turnLimit > 0 && field.getTurn() > rule._turnLimit;
        if ( bOverTurnLimit && rule._objective != SrpgObjective::SurviveTurns )
            return SrpgOutcome::Defeat;

        switch ( rule._objective )
        {
            case SrpgObjective::DefeatAll:
                return field.countAlive( SrpgTeam::Enemy ) == 0 ? SrpgOutcome::Victory : SrpgOutcome::Ongoing;
            case SrpgObjective::DefeatCommander:
            {
                bool       bEnemyHasCommander = false;
                const bool bLost              = SrpgProgressInternal::isCommanderLost( field, SrpgTeam::Enemy, bEnemyHasCommander );
                if ( bEnemyHasCommander == false )
                    return field.countAlive( SrpgTeam::Enemy ) == 0 ? SrpgOutcome::Victory : SrpgOutcome::Ongoing; // 지휘관이 없으면 전멸로
                return bLost ? SrpgOutcome::Victory : SrpgOutcome::Ongoing;
            }
            case SrpgObjective::ReachCell:
            {
                for ( const SrpgUnit& unit : field.getUnits() )
                {
                    if ( unit._team != SrpgTeam::Player || unit._bAlive == SW_FALSE )
                        continue;
                    for ( const int2& goal : rule._listGoalCell )
                    {
                        if ( unit._cell == goal )
                            return SrpgOutcome::Victory;
                    }
                }
                return SrpgOutcome::Ongoing;
            }
            case SrpgObjective::SurviveTurns:
                return bOverTurnLimit ? SrpgOutcome::Victory : SrpgOutcome::Ongoing;
        }
        return SrpgOutcome::Ongoing;
    }

    SrpgCampaign::SrpgCampaign()
        : _runMap{}
        , _listRoster{}
        , _seed{ 0 }
        , _bInMission{ SW_FALSE }
        , _bFailed{ SW_FALSE }
    {
    }

    void SrpgCampaign::initialize( const RunMapSettings& settings, uint32 seed )
    {
        _seed = seed;
        _runMap.generate( settings, seed );
        _listRoster.clear();
        _bInMission = SW_FALSE;
        _bFailed    = SW_FALSE;
    }

    int32 SrpgCampaign::addRosterEntry( const SrpgCatalog& catalog, const hashed_string& unitId, const hashed_string& pilotId, int32 pilotLevel )
    {
        if ( catalog.findUnit( unitId ) == nullptr || catalog.findPilot( pilotId ) == nullptr )
            return -1;
        SrpgRosterEntry entry;
        entry._unitId  = unitId;
        entry._pilotId = pilotId;
        entry._pilotLevel.setLevel( catalog.getPilotCurve(), pilotLevel );
        entry._unitLevel.setLevel( catalog.getUnitCurve(), 1 );
        _listRoster.push_back( entry );
        return static_cast<int32>( _listRoster.size() ) - 1;
    }

    bool SrpgCampaign::beginMission( int32 nodeIndex )
    {
        if ( _bFailed == SW_TRUE || _bInMission == SW_TRUE || _runMap.moveTo( nodeIndex ) == false )
            return false;
        _bInMission = SW_TRUE;
        return true;
    }

    int32 SrpgCampaign::deployRoster( SrpgBattlefield& field, const vector<int2>& listCell ) const
    {
        int32  placedCount = 0;
        size_t cellIndex   = 0;
        for ( size_t rosterIndex = 0; rosterIndex < _listRoster.size() && cellIndex < listCell.size(); ++rosterIndex )
        {
            const SrpgRosterEntry& entry = _listRoster[rosterIndex];
            if ( entry._bLost == SW_TRUE )
                continue;
            const int32 unitIndex = field.addUnit( entry._unitId, entry._pilotId, SrpgTeam::Player, listCell[cellIndex], entry._pilotLevel.getLevel() );
            ++cellIndex;
            SrpgUnit* pUnit = field.findUnit( unitIndex );
            if ( pUnit == nullptr )
                continue;
            pUnit->_pilotLevel  = entry._pilotLevel;
            pUnit->_unitLevel   = entry._unitLevel;
            pUnit->_rosterIndex = static_cast<int32>( rosterIndex );
            ++placedCount;
        }
        return placedCount;
    }

    void SrpgCampaign::completeMission( const SrpgBattlefield& field, SrpgOutcome outcome )
    {
        if ( _bInMission == SW_FALSE || outcome == SrpgOutcome::Ongoing )
            return;
        for ( const SrpgUnit& unit : field.getUnits() )
        {
            if ( unit._rosterIndex < 0 || unit._rosterIndex >= static_cast<int32>( _listRoster.size() ) )
                continue;
            SrpgRosterEntry& entry = _listRoster[static_cast<size_t>( unit._rosterIndex )];
            entry._pilotLevel      = unit._pilotLevel;
            entry._unitLevel       = unit._unitLevel;
            entry._unitId          = unit._pDef->_id; // 작전 중 개발했으면 새 기체로
            if ( unit._bAlive == SW_FALSE )
                entry._bLost = SW_TRUE;
        }
        bool bAnyLeft = false;
        for ( const SrpgRosterEntry& entry : _listRoster )
            bAnyLeft = bAnyLeft || entry._bLost == SW_FALSE;
        _bInMission = SW_FALSE;
        if ( outcome == SrpgOutcome::Defeat || bAnyLeft == false )
            _bFailed = SW_TRUE;
    }

    hashed_string SrpgCampaign::getMissionKind() const
    {
        const RunNode* pNode = _runMap.findNode( _runMap.getCurrent() );
        return pNode != nullptr ? pNode->_kind : hashed_string{};
    }

    uint32 SrpgCampaign::getMissionSeed() const { return GameHash::mix32( _seed ^ GameHash::mix32( static_cast<uint32>( _runMap.getCurrent() + 1 ) * 0x9e3779b1u ) ); }
} // namespace sw
