#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleSystem.h"

// ScheduleSystemTest — 계획은 저장하지 않고 다시 세운다(`ScheduleSaveState` 머리말). 저장 → 새 시스템에 복원한 상태가 저장 전과 같고, 그 뒤로도 같이 가는지 본다(nogpu).

namespace
{
    constexpr const utf8* kCatalogXML = R"(<Schedules>
  <Calendar days="Mon,Tue,Wed,Thu,Fri,Sat,Sun" seasons="Spring,Summer,Fall,Winter" weathers="sunny,rain"/>
  <Place id="store" area="town" position="24 0 6" radius="1"/>
  <Place id="square" area="town" position="20 0 14" radius="4"/>
  <Place id="saloon" area="town" position="34 0 18" radius="2"/>
  <Place id="home_a" area="town" position="22 0 2"/>
  <Place id="home_b" area="town" position="32 0 8"/>
  <Appointment id="drinks" place="saloon" start="19:00" end="21:00" wait="30" priority="40"/>
  <Archetype id="villager">
    <Routine id="sleep">
      <Block start="0:00" end="7:00" activity="Sleep"/>
      <Block start="22:00" end="24:00" activity="Sleep"/>
    </Routine>
  </Archetype>
  <NPC id="a" archetype="villager" home="home_a" speed="12">
    <Routine id="work">
      <Block start="9:00" end="17:00" activity="WorkAt" place="store"/>
      <Block start="17:00" end="19:00" activity="Wander" place="square" every="30"/>
      <Block activity="Meet" appointment="drinks"/>
    </Routine>
  </NPC>
  <NPC id="b" archetype="villager" home="home_b" speed="10">
    <Routine id="idle">
      <Block start="10:00" end="15:00" activity="Wander" place="square" every="20"/>
      <Block activity="Meet" appointment="drinks"/>
    </Routine>
  </NPC>
</Schedules>)";

    constexpr int32 kHour = 60;

    void loadCatalog( sw::ScheduleCatalog& outCatalog )
    {
        SW_REQUIRE_TRUE( outCatalog.loadFromXMLText( kCatalogXML, "ScheduleSystemTest" ) );
        SW_REQUIRE_EQUAL( size_t( 2 ), outCatalog.getNPCs().size() );
    }
} // namespace

/**
 * @brief [ScheduleSystemTest] 저장한 상태를 새 시스템에 복원하면 상태 해시가 같고, 둘을 같은 시각까지 돌려도 같다
 * @details 계획은 저장하지 않으므로 복원이 계획을 다시 세운다 — 다시 세운 계획이 저장 전과 다르면 불러온 게임에서 NPC 가 다른 곳으로 간다.
 */
SW_TEST_CASE( ScheduleSystemTest, SaveRestoreKeepsTheDay )
{
    sw::ScheduleCatalog catalog;
    loadCatalog( catalog );
    const sw::ScheduleSystemSettings settings;

    sw::ScheduleSystem original;
    SW_REQUIRE_TRUE( original.initialize( &catalog, settings, 6 * kHour ) );
    original.advanceTo( 12 * kHour + 17 );

    sw::ScheduleSaveState saved;
    original.fillSaveState( saved );

    sw::ScheduleSystem restored;
    SW_REQUIRE_TRUE( restored.initialize( &catalog, settings, 0 ) );
    restored.restoreSaveState( saved );
    SW_EXPECT_EQUAL( original.getMinute(), restored.getMinute() );
    SW_EXPECT_EQUAL( original.computeStateHash(), restored.computeStateHash() );

    // 약속(19:00)을 지나 다음 날 아침까지 — 저녁의 만남 판정도 같아야 한다.
    original.advanceTo( 30 * kHour );
    restored.advanceTo( 30 * kHour );
    SW_EXPECT_EQUAL( original.computeStateHash(), restored.computeStateHash() );
    SW_EXPECT_EQUAL( original.isAppointmentMet( sw::hashed_string( "drinks" ) ), restored.isAppointmentMet( sw::hashed_string( "drinks" ) ) );
}

/**
 * @brief [ScheduleSystemTest] 같은 카탈로그 · 설정 · 시각이면 두 시스템의 상태가 같다(결정성)
 * @details 확률 조건과 돌아다니기 위치는 설정의 시드를 쓴다 — 실행마다 달라지면 네트워크 요약과 저장 복원이 어긋난다.
 */
SW_TEST_CASE( ScheduleSystemTest, SameInputsGiveTheSameState )
{
    sw::ScheduleCatalog catalog;
    loadCatalog( catalog );
    const sw::ScheduleSystemSettings settings;

    sw::ScheduleSystem first;
    sw::ScheduleSystem second;
    SW_REQUIRE_TRUE( first.initialize( &catalog, settings, 6 * kHour ) );
    SW_REQUIRE_TRUE( second.initialize( &catalog, settings, 6 * kHour ) );
    first.advanceTo( 18 * kHour );
    second.advanceTo( 18 * kHour );
    SW_EXPECT_EQUAL( first.computeStateHash(), second.computeStateHash() );
    SW_EXPECT_TRUE( first.getPlan( first.findNPCIndex( sw::hashed_string( "a" ) ) ).empty() == false );
}
