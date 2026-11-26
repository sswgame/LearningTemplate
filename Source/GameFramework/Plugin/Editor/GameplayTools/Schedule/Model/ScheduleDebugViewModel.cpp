#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Schedule/Model/ScheduleDebugViewModel.h"

#include "Core/String/StringBuilder.h"

#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"

namespace sw::editor
{
    ScheduleDebugViewModel::ScheduleDebugViewModel()
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

    void ScheduleDebugViewModel::gather( ScheduleDebugViewState& outState )
    {
        outState._listNPC.clear();
        outState._listRow.clear();
        outState._explanation.clear();
        outState._rawTimeline.clear();
        outState._selectedNPC.reset();
        outState._bPlayStopped = EditorPlaySession::getUIState().isStopped() ? SW_TRUE : SW_FALSE;
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

    void ScheduleDebugViewModel::setPreviewMinute( int32 minute )
    {
        _previewMinute = minute;
        _bFollowClock  = SW_FALSE;
    }

    void ScheduleDebugViewModel::setTextShown( bool bExplanation, bool bRawTimeline )
    {
        _bExplanationShown = bExplanation ? SW_TRUE : SW_FALSE;
        _bRawTimelineShown = bRawTimeline ? SW_TRUE : SW_FALSE;
    }
} // namespace sw::editor

namespace sw::editor
{
    string ScheduleTimelineRows::formatClock( int32 minuteOfDay )
    {
        int32 minute = minuteOfDay % kScheduleMinutesPerDay;
        if ( minute < 0 )
            minute += kScheduleMinutesPerDay;
        const int32                             hours   = minute / 60;
        const int32                             minutes = minute % 60;
        StringBuilder<constant::buffer::kMax64> text;
        text.appendFormat( "%#%#:%#%#", hours < 10 ? "0" : "", hours, minutes < 10 ? "0" : "", minutes );
        return string( text.c_str() );
    }

    optional<int32> ScheduleTimelineRows::findSegmentAt( const vector<ScheduleSegment>& listSegment, int32 minuteOfDay )
    {
        for ( int32 segmentIndex = 0; segmentIndex < static_cast<int32>( listSegment.size() ); ++segmentIndex )
        {
            const ScheduleSegment& segment = listSegment[static_cast<size_t>( segmentIndex )];
            if ( segment._startMinute <= minuteOfDay && minuteOfDay < segment._endMinute )
                return segmentIndex;
        }
        return nullopt;
    }

    void ScheduleTimelineRows::populate( const vector<ScheduleSegment>& listSegment, int32 previewMinute, vector<ScheduleTimelineRow>& outListRow )
    {
        outListRow.clear();
        outListRow.reserve( listSegment.size() );
        const optional<int32> activeIndex = findSegmentAt( listSegment, previewMinute );
        for ( int32 segmentIndex = 0; segmentIndex < static_cast<int32>( listSegment.size() ); ++segmentIndex )
        {
            const ScheduleSegment&                  segment = listSegment[static_cast<size_t>( segmentIndex )];
            ScheduleTimelineRow                     row;
            StringBuilder<constant::buffer::kMax32> time;
            time.appendFormat( "%#-%#", formatClock( segment._startMinute ), formatClock( segment._endMinute ) );
            row._time = string( time.view() );
            if ( segment._travelMinutes > 0 )
            {
                StringBuilder<constant::buffer::kMax32> travel;
                travel.appendFormat( "%# -> %#", formatClock( segment._departMinute ), formatClock( segment.getArriveMinute() ) );
                row._travel = string( travel.view() );
            }
            if ( segment._target._area.empty() == false )
            {
                row._place = string( segment._target._area.c_str() );
            }
            else
            {
                StringBuilder<constant::buffer::kMax128> place;
                place.appendFormat( "(%#, %#, %#)", segment._target._position._x, segment._target._position._y, segment._target._position._z );
                row._place = string( place.c_str() );
            }
            row._activity = string( segment._activity.c_str() );
            StringBuilder<constant::buffer::kMax256> source;
            if ( segment._sourceID.empty() )
                source.append( toString( segment._source ) );
            else
                source.appendFormat( "%# '%#'", toString( segment._source ), segment._sourceID.c_str() );
            row._source       = string( source.c_str() );
            row._segmentIndex = segmentIndex;
            row._bActive      = activeIndex == segmentIndex;
            row._bPast        = segment._bPast != SW_FALSE;
            outListRow.push_back( std::move( row ) );
        }
    }
} // namespace sw::editor
