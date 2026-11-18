#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Schedule/Model/ScheduleDebugModel.h"

#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"

namespace sw::editor
{
    ScheduleDebugModel::ScheduleDebugModel()
        : _listActiveSystem{}
        , _selectedNPC{}
        , _systemIndex{ 0 }
        , _previewMinute{ 0 }
        , _bFollowClock{ SW_TRUE }
        , _bExplanationShown{ SW_TRUE } // 설명 머리는 열린 채 시작한다
        , _bRawTimelineShown{ SW_FALSE }
        , _published{}
    {
    }

    void ScheduleDebugModel::gather( ScheduleDebugViewState& outState )
    {
        outState._listNPC.clear();
        outState._listRow.clear();
        outState._explanation.clear();
        outState._rawTimeline.clear();
        outState._selectedNPC.reset();
        outState._bPlayStopped = EditorPlaySession::isStopped() ? SW_TRUE : SW_FALSE;
        _published             = {};

        vector<const ScheduleSystem*> listSystem;
        ScheduleSystem::collectLiveSystems( listSystem );
        _listActiveSystem.clear();
        for ( const ScheduleSystem* pSystem : listSystem )
        {
            if ( pSystem != nullptr && pSystem->getNPCCount() > 0 )
                _listActiveSystem.push_back( pSystem );
        }
        outState._bHasSystem  = _listActiveSystem.empty() ? SW_FALSE : SW_TRUE;
        outState._systemCount = static_cast<int32>( _listActiveSystem.size() );
        if ( _listActiveSystem.empty() )
            return;

        if ( _systemIndex < 0 || _systemIndex >= static_cast<int32>( _listActiveSystem.size() ) )
            _systemIndex = 0;
        const ScheduleSystem& system = *_listActiveSystem[static_cast<size_t>( _systemIndex )];
        outState._systemIndex        = _systemIndex;
        _published._npcCount         = static_cast<uint32>( system.getNPCCount() );
        for ( int32 npcIndex = 0; npcIndex < system.getNPCCount(); ++npcIndex )
        {
            const hashed_string npcID = system.getNPCID( npcIndex );
            if ( npcID == _selectedNPC && _selectedNPC.empty() == false )
                outState._selectedNPC = npcIndex;
            outState._listNPC.emplace_back( npcID.c_str() );
        }

        if ( _bFollowClock )
            _previewMinute = system.getMinute() % kScheduleMinutesPerDay;
        outState._bFollowClock  = _bFollowClock ? SW_TRUE : SW_FALSE;
        outState._previewMinute = _previewMinute;
        outState._nowMinute     = system.getMinute() % kScheduleMinutesPerDay;
        outState._day           = system.getDay();
        if ( outState._selectedNPC.has_value() == false )
            return;

        const int32 npcIndex = *outState._selectedNPC;
        ScheduleTimelineRows::populate( system.getPlan( npcIndex ), _previewMinute, outState._listRow );
        _published._timelineRowCount = static_cast<uint32>( outState._listRow.size() );
        // explainNPC · dumpTimeline 은 지금 시각 기준이다 — 슬라이더 미리 보기와 상관없다.
        if ( _bExplanationShown )
            system.explainNPC( npcIndex, outState._explanation );
        if ( _bRawTimelineShown )
            system.dumpTimeline( npcIndex, outState._rawTimeline );
    }

    void ScheduleDebugModel::setPreviewMinute( int32 minute )
    {
        _previewMinute = minute;
        _bFollowClock  = SW_FALSE;
    }

    void ScheduleDebugModel::setTextShown( bool bExplanation, bool bRawTimeline )
    {
        _bExplanationShown = bExplanation ? SW_TRUE : SW_FALSE;
        _bRawTimelineShown = bRawTimeline ? SW_TRUE : SW_FALSE;
    }
} // namespace sw::editor
