// ScheduleDebugViewModelTest: 살아 있는 일정 시스템에서 모으는 값: NPC 목록 · 고른 NPC 의 타임라인 · 설명 글, 미리 볼 시각의 시계 따라가기(지역 일정 시스템 — nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Schedule/Model/ScheduleDebugViewModel.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    constexpr const utf8* kDebugCatalogXML = R"(<Schedules>
  <Place id="home" position="0 0 0"/>
  <Place id="yard" position="4 0 0" radius="1"/>
  <NPC id="schedule_debug_test_npc" home="home" speed="10">
    <Routine id="work"><Block start="9:00" end="17:00" activity="WorkAt" place="yard"/></Routine>
  </NPC>
  <NPC id="schedule_debug_test_other" home="home" speed="10">
    <Routine id="rest"><Block start="10:00" end="12:00" activity="Wander" place="yard" every="20"/></Routine>
  </NPC>
</Schedules>)";

    /** @brief 이 케이스의 일정 시스템을 고릅니다 — 다른 시스템(앞선 실행 · 게임)이 함께 살아 있어도 된다. 찾지 못하면 false 입니다. */
    bool selectTestSystem( ScheduleDebugViewModel& model, ScheduleDebugViewState& state )
    {
        model.gather( state );
        for ( int32 index = 0; index < state._systemCount; ++index )
        {
            model.selectSystem( index );
            model.gather( state );
            for ( const string& npc : state._listNPC )
            {
                if ( npc == "schedule_debug_test_npc" )
                    return true;
            }
        }
        return false;
    }
} // namespace

/** @brief [ScheduleDebugViewModelTest] 고른 시스템의 NPC 와 시각을 모으고, NPC 를 고르면 타임라인과 설명이 차며, 미리 볼 시각을 정하면 시계를 따라가지 않는다 */
SW_TEST_CASE( ScheduleDebugViewModelTest, GathersTheSelectedNPC )
{
    ScheduleCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kDebugCatalogXML, "ScheduleDebugViewModelTest" ) );
    ScheduleSystem system;
    SW_REQUIRE_TRUE( system.initialize( &catalog, ScheduleSystemSettings{}, 8 * 60 ) );

    ScheduleDebugViewModel model;
    ScheduleDebugViewState state;
    SW_REQUIRE_TRUE( selectTestSystem( model, state ) );
    SW_EXPECT_TRUE( state._bHasSystem == SW_TRUE );
    SW_EXPECT_EQUAL( size_t{ 2 }, state._listNPC.size() );
    SW_EXPECT_FALSE( state._selectedNPC.has_value() );
    SW_EXPECT_TRUE( state._listRow.empty() );
    SW_EXPECT_EQUAL( 8 * 60, state._nowMinute );
    SW_EXPECT_EQUAL( state._nowMinute, state._previewMinute ); // 시계를 따라간다

    model.selectNPC( "schedule_debug_test_npc" );
    model.setTextShown( true, true );
    model.setPreviewMinute( 10 * 60 );
    model.gather( state );
    SW_REQUIRE_TRUE( state._selectedNPC.has_value() );
    SW_EXPECT_EQUAL( string( "schedule_debug_test_npc" ), state._listNPC[static_cast<size_t>( *state._selectedNPC )] );
    SW_EXPECT_FALSE( state._listRow.empty() );
    SW_EXPECT_FALSE( state._explanation.empty() );
    SW_EXPECT_FALSE( state._rawTimeline.empty() );
    SW_EXPECT_EQUAL( 10 * 60, state._previewMinute );
    SW_EXPECT_TRUE( state._bFollowClock == SW_FALSE );
    SW_EXPECT_EQUAL( static_cast<uint32>( state._listRow.size() ), model.getPublished()._timelineRowCount );

    model.setTextShown( false, false );
    model.setFollowClock( true );
    model.gather( state );
    SW_EXPECT_TRUE( state._explanation.empty() );
    SW_EXPECT_TRUE( state._rawTimeline.empty() );
    SW_EXPECT_EQUAL( state._nowMinute, state._previewMinute );
}
