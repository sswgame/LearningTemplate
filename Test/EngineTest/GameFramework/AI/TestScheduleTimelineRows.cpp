// NPC 일정 디버그 창의 표 행 변환 — 시각 글, 이동 글, 자리(지역 · 좌표), 출처, 미리 보는 시각을 덮는 칸 표시.
#include "pch.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/PlugIn/Editor/Base/Schedule/ScheduleTimelineRows.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( ScheduleTimelineRowsTest, BuildsRowsAndMarksPreviewSegment )
{
    vector<ScheduleSegment> listSegment;
    ScheduleSegment         home;
    home._startMinute  = 0;
    home._endMinute    = 480;
    home._departMinute = 0;
    home._activity     = hashed_string( "sleep" );
    home._target._area = hashed_string( "home" );
    home._bPast        = SW_TRUE;
    listSegment.push_back( home );

    ScheduleSegment work;
    work._startMinute   = 480;
    work._endMinute     = 720;
    work._departMinute  = 460;
    work._travelMinutes = 15;
    work._activity      = hashed_string( "work" );
    work._sourceID      = hashed_string( "weekday" );
    work._source        = ScheduleSegmentSource::Routine;
    listSegment.push_back( work );

    SW_EXPECT_EQUAL( ScheduleTimelineRows::formatClock( 485 ), string( "08:05" ) );
    SW_EXPECT_EQUAL( ScheduleTimelineRows::formatClock( kScheduleMinutesPerDay + 61 ), string( "01:01" ) );
    SW_EXPECT_EQUAL( ScheduleTimelineRows::findSegmentAt( listSegment, 480 ), 1 );
    SW_EXPECT_EQUAL( ScheduleTimelineRows::findSegmentAt( listSegment, 720 ), -1 );

    vector<ScheduleTimelineRow> listRow;
    ScheduleTimelineRows::populate( listSegment, 500, listRow );
    SW_EXPECT_EQUAL( listRow.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( listRow[0]._time, string( "00:00-08:00" ) );
    SW_EXPECT_TRUE( listRow[0]._travel.empty() );
    SW_EXPECT_EQUAL( listRow[0]._place, string( "home" ) );
    SW_EXPECT_TRUE( listRow[0]._bPast );
    SW_EXPECT_TRUE( listRow[0]._bActive == false );
    SW_EXPECT_EQUAL( listRow[1]._travel, string( "07:40 -> 07:55" ) );
    SW_EXPECT_EQUAL( listRow[1]._activity, string( "work" ) );
    SW_EXPECT_EQUAL( listRow[1]._source, string( "routine 'weekday'" ) );
    SW_EXPECT_TRUE( listRow[1]._place.empty() == false ); // 지역이 없으면 좌표 글
    SW_EXPECT_TRUE( listRow[1]._bActive );
}
