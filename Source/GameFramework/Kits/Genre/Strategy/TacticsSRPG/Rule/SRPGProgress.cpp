#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGProgress.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Catalog/SRPGCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGBattlefield.h"

namespace sw
{
    namespace
    {
        struct SRPGProgressInternal
        {
            static bool isCommanderLost( const SRPGBattlefield& field, SRPGTeam team, bool& outHasCommander )
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    SRPGOutcome SRPGMission::evaluate( const SRPGBattlefield& field, const SRPGMissionRule& rule )
    {
        if ( field.getTurn() <= 0 )
            return SRPGOutcome::Ongoing;
        if ( field.countAlive( SRPGTeam::Player ) == 0 )
            return SRPGOutcome::Defeat;
        bool bPlayerHasCommander = false;
        if ( rule._bLoseOnCommander == SW_TRUE && SRPGProgressInternal::isCommanderLost( field, SRPGTeam::Player, bPlayerHasCommander ) )
            return SRPGOutcome::Defeat;
        const bool bOverTurnLimit = rule._turnLimit > 0 && field.getTurn() > rule._turnLimit;
        if ( bOverTurnLimit && rule._objective != SRPGObjective::SurviveTurns )
            return SRPGOutcome::Defeat;

        switch ( rule._objective )
        {
            case SRPGObjective::DefeatAll:
            {
                return field.countAlive( SRPGTeam::Enemy ) == 0 ? SRPGOutcome::Victory : SRPGOutcome::Ongoing;
            }
            case SRPGObjective::DefeatCommander:
            {
                bool       bEnemyHasCommander = false;
                const bool bLost              = SRPGProgressInternal::isCommanderLost( field, SRPGTeam::Enemy, bEnemyHasCommander );
                if ( bEnemyHasCommander == false )
                    return field.countAlive( SRPGTeam::Enemy ) == 0 ? SRPGOutcome::Victory : SRPGOutcome::Ongoing; // 지휘관이 없으면 전멸로
                return bLost ? SRPGOutcome::Victory : SRPGOutcome::Ongoing;
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
                            return SRPGOutcome::Victory;
                    }
                }
                return SRPGOutcome::Ongoing;
            }
            case SRPGObjective::SurviveTurns:
            {
                return bOverTurnLimit ? SRPGOutcome::Victory : SRPGOutcome::Ongoing;
            }
        }
        return SRPGOutcome::Ongoing;
    }

    SRPGCampaign::SRPGCampaign()
        : _runMap{}
        , _listRoster{}
        , _seed{ 0 }
        , _bInMission{ SW_FALSE }
        , _bFailed{ SW_FALSE }
    {
    }

    void SRPGCampaign::initialize( const RunMapSettings& settings, uint32 seed )
    {
        _seed = seed;
        _runMap.generate( settings, seed );
        _listRoster.clear();
        _bInMission = SW_FALSE;
        _bFailed    = SW_FALSE;
    }

    int32 SRPGCampaign::addRosterEntry( const SRPGCatalog& catalog, const hashed_string& unitId, const hashed_string& pilotId, int32 pilotLevel )
    {
        if ( catalog.findUnit( unitId ) == nullptr || catalog.findPilot( pilotId ) == nullptr )
            return -1;
        SRPGRosterEntry entry;
        entry._unitId  = unitId;
        entry._pilotId = pilotId;
        entry._pilotLevel.setLevel( catalog.getPilotCurve(), pilotLevel );
        entry._unitLevel.setLevel( catalog.getUnitCurve(), 1 );
        _listRoster.push_back( entry );
        return static_cast<int32>( _listRoster.size() ) - 1;
    }

    bool SRPGCampaign::beginMission( int32 nodeIndex )
    {
        if ( _bFailed == SW_TRUE || _bInMission == SW_TRUE || _runMap.moveTo( nodeIndex ) == false )
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
            const int32 unitIndex = field.addUnit( entry._unitId, entry._pilotId, SRPGTeam::Player, listCell[cellIndex], entry._pilotLevel.getLevel() );
            ++cellIndex;
            SRPGUnit* pUnit = field.findUnit( unitIndex );
            if ( pUnit == nullptr )
                continue;
            pUnit->_pilotLevel  = entry._pilotLevel;
            pUnit->_unitLevel   = entry._unitLevel;
            pUnit->_rosterIndex = static_cast<int32>( rosterIndex );
            ++placedCount;
        }
        return placedCount;
    }

    void SRPGCampaign::completeMission( const SRPGBattlefield& field, SRPGOutcome outcome )
    {
        if ( _bInMission == SW_FALSE || outcome == SRPGOutcome::Ongoing )
            return;
        for ( const SRPGUnit& unit : field.getUnits() )
        {
            if ( unit._rosterIndex < 0 || unit._rosterIndex >= static_cast<int32>( _listRoster.size() ) )
                continue;
            SRPGRosterEntry& entry = _listRoster[static_cast<size_t>( unit._rosterIndex )];
            entry._pilotLevel      = unit._pilotLevel;
            entry._unitLevel       = unit._unitLevel;
            entry._unitId          = unit._pDef->_id; // 작전 중 개발했으면 새 기체로
            if ( unit._bAlive == SW_FALSE )
                entry._bLost = SW_TRUE;
        }
        bool bAnyLeft = false;
        for ( const SRPGRosterEntry& entry : _listRoster )
        {
            bAnyLeft = bAnyLeft || entry._bLost == SW_FALSE;
        }
        _bInMission = SW_FALSE;
        if ( outcome == SRPGOutcome::Defeat || bAnyLeft == false )
            _bFailed = SW_TRUE;
    }

    hashed_string SRPGCampaign::getMissionKind() const
    {
        const RunNode* pNode = _runMap.findNode( _runMap.getCurrent() );
        return pNode != nullptr ? pNode->_kind : hashed_string{};
    }

    uint32 SRPGCampaign::getMissionSeed() const { return GameHash::mix32( _seed ^ GameHash::mix32( static_cast<uint32>( _runMap.getCurrent() + 1 ) * 0x9e3779b1u ) ); }

    void SRPGCampaign::writeState( Archive& outArchive ) const
    {
        _runMap.writeState( outArchive );
        outArchive << static_cast<uint32>( _listRoster.size() );
        for ( const SRPGRosterEntry& entry : _listRoster )
        {
            entry._pilotLevel.writeState( outArchive );
            entry._unitLevel.writeState( outArchive );
            StateArchiveUtil::writeName( outArchive, entry._unitId );
            StateArchiveUtil::writeName( outArchive, entry._pilotId );
            outArchive << entry._bLost;
        }
        outArchive << _seed;
        outArchive << _bInMission;
        outArchive << _bFailed;
    }

    bool SRPGCampaign::readState( Archive& archive )
    {
        RunMap runMap;
        uint32 count = 0;
        // 명단마다 레벨 둘(40) + 이름 둘(8) + 잃음(1)
        if ( runMap.readState( archive ) == false || StateArchiveUtil::readCount( archive, 49, count ) == false )
            return false;
        vector<SRPGRosterEntry> listRoster( count );
        for ( SRPGRosterEntry& entry : listRoster )
        {
            const bool bEntryRead = entry._pilotLevel.readState( archive ) && entry._unitLevel.readState( archive ) &&
                                    StateArchiveUtil::readName( archive, entry._unitId ) && StateArchiveUtil::readName( archive, entry._pilotId );
            if ( bEntryRead == false )
                return false;
            archive >> entry._bLost;
            if ( archive.isError() || entry._bLost > SW_TRUE )
                return false;
        }
        uint32 seed       = 0;
        uint8  bInMission = SW_FALSE;
        uint8  bFailed    = SW_FALSE;
        archive >> seed;
        archive >> bInMission;
        archive >> bFailed;
        if ( archive.isError() || bInMission > SW_TRUE || bFailed > SW_TRUE )
            return false;
        _runMap     = std::move( runMap );
        _listRoster = std::move( listRoster );
        _seed       = seed;
        _bInMission = bInMission;
        _bFailed    = bFailed;
        return true;
    }
} // namespace sw
