#include "pch.h"

#include "GameFramework/PlugIn/Editor/Base/Schedule/ScheduleTimelineRows.h"

#include "Core/String/StringBuilder.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"

namespace sw::editor
{
    string ScheduleTimelineRows::formatClock( int32 minuteOfDay )
    {
        int32 minute = minuteOfDay % kScheduleMinutesPerDay;
        if ( minute < 0 )
            minute += kScheduleMinutesPerDay;
        const int32                           hours   = minute / 60;
        const int32                           minutes = minute % 60;
        StringBuilder<constant::kMaxBuffer64> text;
        text.appendFormat( "%#%#:%#%#", hours < 10 ? "0" : "", hours, minutes < 10 ? "0" : "", minutes );
        return string( text.c_str() );
    }

    int32 ScheduleTimelineRows::findSegmentAt( const vector<ScheduleSegment>& listSegment, int32 minuteOfDay )
    {
        for ( int32 segmentIndex = 0; segmentIndex < static_cast<int32>( listSegment.size() ); ++segmentIndex )
        {
            const ScheduleSegment& segment = listSegment[static_cast<size_t>( segmentIndex )];
            if ( segment._startMinute <= minuteOfDay && minuteOfDay < segment._endMinute )
                return segmentIndex;
        }
        return -1;
    }

    void ScheduleTimelineRows::populate( const vector<ScheduleSegment>& listSegment, int32 previewMinute, vector<ScheduleTimelineRow>& outListRow )
    {
        outListRow.clear();
        outListRow.reserve( listSegment.size() );
        const int32 activeIndex = findSegmentAt( listSegment, previewMinute );
        for ( int32 segmentIndex = 0; segmentIndex < static_cast<int32>( listSegment.size() ); ++segmentIndex )
        {
            const ScheduleSegment& segment = listSegment[static_cast<size_t>( segmentIndex )];
            ScheduleTimelineRow    row;
            row._time = formatClock( segment._startMinute );
            row._time += "-";
            row._time += formatClock( segment._endMinute );
            if ( segment._travelMinutes > 0 )
            {
                row._travel = formatClock( segment._departMinute );
                row._travel += " -> ";
                row._travel += formatClock( segment.getArriveMinute() );
            }
            if ( segment._target._area.empty() == false )
            {
                row._place = string( segment._target._area.c_str() );
            }
            else
            {
                StringBuilder<constant::kMaxBuffer128> place;
                place.appendFormat( "(%#, %#, %#)", segment._target._position._x, segment._target._position._y, segment._target._position._z );
                row._place = string( place.c_str() );
            }
            row._activity = string( segment._activity.c_str() );
            StringBuilder<constant::kMaxBuffer256> source;
            if ( segment._sourceID.empty() )
                source.append( toString( segment._source ) );
            else
                source.appendFormat( "%# '%#'", toString( segment._source ), segment._sourceID.c_str() );
            row._source       = string( source.c_str() );
            row._segmentIndex = segmentIndex;
            row._bActive      = segmentIndex == activeIndex;
            row._bPast        = segment._bPast != SW_FALSE;
            outListRow.push_back( std::move( row ) );
        }
    }
} // namespace sw::editor
