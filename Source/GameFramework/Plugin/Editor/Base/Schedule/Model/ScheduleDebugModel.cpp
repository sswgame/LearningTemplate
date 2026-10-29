#include "pch.h"

#include "GameFramework/Plugin/Editor/Base/Schedule/Model/ScheduleDebugModel.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleSystem.h"

namespace sw::editor
{
    ScheduleDebugModel::ScheduleDebugModel()
        : _listActiveSystem{}
        , _listRow{}
        , _text{}
        , _selectedNPC{}
        , _systemIndex{ 0 }
        , _previewMinute{ 0 }
        , _bFollowClock{ true }
        , _published{}
    {
    }

    const ScheduleSystem* ScheduleDebugModel::refreshSystems()
    {
        vector<const ScheduleSystem*> listSystem;
        ScheduleSystem::collectLiveSystems( listSystem );
        _listActiveSystem.clear();
        for ( const ScheduleSystem* pSystem : listSystem )
        {
            if ( pSystem != nullptr && pSystem->getNPCCount() > 0 )
                _listActiveSystem.push_back( pSystem );
        }
        _published = {};
        if ( _listActiveSystem.empty() )
            return nullptr;
        return selectSystem( _systemIndex );
    }

    const ScheduleSystem* ScheduleDebugModel::selectSystem( int32 systemIndex )
    {
        if ( _listActiveSystem.empty() )
            return nullptr;
        _systemIndex                  = ( systemIndex < 0 || systemIndex >= static_cast<int32>( _listActiveSystem.size() ) ) ? 0 : systemIndex;
        const ScheduleSystem* pSystem = _listActiveSystem[static_cast<size_t>( _systemIndex )];
        _published._npcCount          = static_cast<uint32>( pSystem->getNPCCount() );
        return pSystem;
    }

    int32 ScheduleDebugModel::findSelectedNPCIndex( const ScheduleSystem& system ) const
    {
        return _selectedNPC.empty() ? -1 : system.findNPCIndex( _selectedNPC );
    }

    void ScheduleDebugModel::followClock( const ScheduleSystem& system )
    {
        if ( _bFollowClock )
            _previewMinute = system.getMinute() % kScheduleMinutesPerDay;
    }

    void ScheduleDebugModel::setPreviewMinute( int32 minute )
    {
        _previewMinute = minute;
        _bFollowClock  = false;
    }

    const vector<ScheduleTimelineRow>& ScheduleDebugModel::refreshTimeline( const ScheduleSystem& system, int32 npcIndex )
    {
        ScheduleTimelineRows::populate( system.getPlan( npcIndex ), _previewMinute, _listRow );
        _published._timelineRowCount = static_cast<uint32>( _listRow.size() );
        return _listRow;
    }

    const string& ScheduleDebugModel::explain( const ScheduleSystem& system, int32 npcIndex )
    {
        system.explainNPC( npcIndex, _text );
        return _text;
    }

    const string& ScheduleDebugModel::dumpTimeline( const ScheduleSystem& system, int32 npcIndex )
    {
        system.dumpTimeline( npcIndex, _text );
        return _text;
    }
} // namespace sw::editor
