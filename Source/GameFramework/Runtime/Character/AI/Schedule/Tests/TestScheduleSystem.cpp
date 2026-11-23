#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/AI/Schedule/NPCScheduleComponent.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"

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
  <Interrupt id="talk" priority="10"/>
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

    /** @brief 예약 번호를 정해진 수부터 내고 돌려받은 번호를 적는 제공자입니다. */
    class RecordingLocator final : public sw::IScheduleActivityLocator
    {
    public:
        sw::vector<sw::ScheduleReservationSaveState> _listRestored;
        sw::vector<uint32>                           _listReleased;
        uint32                                       _nextID;

        explicit RecordingLocator( uint32 firstID )
            : _listRestored{}
            , _listReleased{}
            , _nextID{ firstID }
        {
        }

        bool reserve( const sw::ScheduleReserveRequest& request, sw::ScheduleLocation& outLocation, uint32& outReservationID ) override
        {
            outLocation      = sw::ScheduleLocation{ request._near, request._area };
            outReservationID = _nextID++;
            return true;
        }
        void release( uint32 reservationID ) override { _listReleased.push_back( reservationID ); }
        void fillState( sw::vector<sw::ScheduleReservationSaveState>& outListReservation ) const override
        {
            outListReservation.clear();
            outListReservation.push_back( sw::ScheduleReservationSaveState{} );
            outListReservation.back()._id = _nextID;
        }
        void restoreState( const sw::vector<sw::ScheduleReservationSaveState>& listReservation ) override { _listRestored = listReservation; }
    };

    void loadBenchCatalog( sw::ScheduleCatalog& outCatalog )
    {
        SW_REQUIRE_TRUE( outCatalog.loadFromXMLText( R"(<Schedules><Place id="home" position="0 0 0"/><NPC id="a" home="home" speed="10">
  <Routine id="r"><Block start="9:00" end="12:00" activity="UseObject" objectKind="Bench"/></Routine></NPC></Schedules>)",
                                                     "BenchCatalog" ) );
    }

    int32 countEvents( sw::ScheduleSystem& schedule, sw::ScheduleEvent::Kind kind )
    {
        sw::vector<sw::ScheduleEvent> listEvent;
        schedule.pollEvents( listEvent );
        int32 count = 0;
        for ( const sw::ScheduleEvent& event : listEvent )
        {
            count += event._kind == kind ? 1 : 0;
        }
        return count;
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
 * @brief [ScheduleSystemTest] 이미 모인 약속은 불러온 뒤 끼어들기에서 돌아와도 "모임" 을 다시 내지 않는다
 * @details 모인 약속 목록이 저장에 없으면 복원한 시스템은 처음 모인 것으로 보고 보상 · 대화를 두 번 부른다.
 */
SW_TEST_CASE( ScheduleSystemTest, RestoredAppointmentIsNotMetTwice )
{
    sw::ScheduleCatalog catalog;
    loadCatalog( catalog );
    const sw::ScheduleSystemSettings settings;
    const sw::hashed_string          talk( "talk" );

    sw::ScheduleSystem original;
    SW_REQUIRE_TRUE( original.initialize( &catalog, settings, 6 * kHour ) );
    const int32 guest = original.findNPCIndex( sw::hashed_string( "a" ) ).value();
    original.advanceTo( 19 * kHour + 20 );
    SW_REQUIRE_EQUAL( 1, countEvents( original, sw::ScheduleEvent::Kind::AppointmentMet ) );
    SW_REQUIRE_TRUE( original.pushInterruption( guest, talk ) );

    sw::ScheduleSaveState saved;
    original.fillSaveState( saved );
    sw::ScheduleSystem restored;
    SW_REQUIRE_TRUE( restored.initialize( &catalog, settings, 0 ) );
    restored.restoreSaveState( saved );

    SW_REQUIRE_TRUE( original.popInterruption( guest, talk ) );
    SW_EXPECT_EQUAL( 0, countEvents( original, sw::ScheduleEvent::Kind::AppointmentMet ) );
    SW_REQUIRE_TRUE( restored.popInterruption( guest, talk ) );
    SW_EXPECT_TRUE( restored.isAppointmentMet( sw::hashed_string( "drinks" ) ) );
    SW_EXPECT_EQUAL( 0, countEvents( restored, sw::ScheduleEvent::Kind::AppointmentMet ) );
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
    SW_EXPECT_TRUE( first.getPlan( first.findNPCIndex( sw::hashed_string( "a" ) ).value() ).empty() == false );
}

/**
 * @brief [ScheduleSystemTest] 엄격도 — 같은 파일의 없는 장소 · 아키타입은 로드 실패다(참조 표), 모르는 활동도 로드 실패다(코드 등록 이름). 예전에는 경고하고 그 칸 · 연결만 뺐다
 */
SW_TEST_CASE( ScheduleSystemTest, BrokenLinksFailTheLoad )
{
    {
        SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
        sw::ScheduleCatalog catalog;
        SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<Schedules><Place id="home" position="0 0 0"/><NPC id="a" home="home" archetype="ghost" speed="10">
  <Routine id="r"><Block start="9:00" end="10:00" activity="GoTo" place="nowhere"/></Routine></NPC></Schedules>)",
                                                  "BrokenPlace" ) );
    }
    {
        SW_EXPECT_LOG( "Schedule.BlockActivityUnknown" );
        sw::ScheduleCatalog catalog;
        SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<Schedules><Place id="home" position="0 0 0"/><NPC id="a" home="home" speed="10">
  <Routine id="r"><Block start="9:00" end="10:00" activity="Fly" place="home"/></Routine></NPC></Schedules>)",
                                                  "UnknownActivity" ) );
    }
    sw::ScheduleCatalog clean;
    SW_EXPECT_TRUE( clean.loadFromXMLText( R"(<Schedules><Place id="home" position="0 0 0"/><NPC id="a" home="home" speed="10">
  <Routine id="r"><Block start="9:00" end="10:00" activity="GoTo" place="home"/></Routine></NPC></Schedules>)",
                                           "Clean" ) );
}

/**
 * @brief [ScheduleSystemTest] 오브젝트가 어느 일정 NPC 인지는 `NPCScheduleComponent` 의 id 로 찾는다 — 이름이 NPC id 와 같아도 칸이 없으면 NPC 가 아니다
 * @details 예전에는 디버그 HUD 가 오브젝트 이름(`Villager.<id>` 의 끝)으로, 상호작용 끼어들기가 이름 그대로 맞췄다. 변이: `findNPCIndex` 가 이름을 보면 둘째 단언이 진다.
 */
SW_TEST_CASE( ScheduleSystemTest, NPCObjectIsFoundByItsComponentNotItsName )
{
    sw::ScheduleCatalog catalog;
    loadCatalog( catalog );
    sw::ScheduleSystem schedule;
    SW_REQUIRE_TRUE( schedule.initialize( &catalog, sw::ScheduleSystemSettings{}, 6 * kHour ) );

    sw::GameObjectManager manager;
    sw::GameObject*       pVillager = manager.createGameObject( sw::hashed_string( "Villager.b" ) );
    pVillager->addComponent<sw::NPCScheduleComponent>()->setNPCID( sw::hashed_string( "b" ) );
    sw::GameObject* pNamedOnly = manager.createGameObject( sw::hashed_string( "a" ) );

    const sw::optional<int32> villagerIndex = sw::NPCScheduleComponent::findNPCIndex( schedule, *pVillager );
    SW_REQUIRE_TRUE( villagerIndex.has_value() );
    SW_EXPECT_TRUE( schedule.getNPCID( *villagerIndex ) == sw::hashed_string( "b" ) );
    SW_EXPECT_FALSE( sw::NPCScheduleComponent::findNPCIndex( schedule, *pNamedOnly ).has_value() );
}

/**
 * @brief [ScheduleSystemTest] 같은 NPC 의 예약 창이 넓어지면 넓어진 시간까지 자리를 쥔다 — 나머지 시간을 남이 겹쳐 잡지 못한다
 * @details 약속이 깨져 나뉘었던 칸이 다시 하나로 합쳐지면 더 넓은 창으로 다시 예약한다. 옛 창만 들고 있으면 정원 1 인 벤치에 둘이 선다.
 */
SW_TEST_CASE( ScheduleSystemTest, WidenedReservationHoldsTheWholeWindow )
{
    sw::ScheduleCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<Schedules><Place id="home" position="0 0 0"/><Spot id="bench" kind="Bench" position="1 0 0"/><Spot id="stool" kind="Bench" position="9 0 0"/>
  <NPC id="a" home="home" speed="10"/><NPC id="b" home="home" speed="10"/></Schedules>)",
                                              "WidenedReservation" ) );
    sw::ScheduleSpotLocator locator;
    locator.initialize( &catalog );

    sw::ScheduleReserveRequest request;
    request._npc         = sw::hashed_string( "a" );
    request._objectKind  = sw::hashed_string( "Bench" );
    request._startMinute = 9 * kHour;
    request._endMinute   = 10 * kHour;
    sw::ScheduleLocation location;
    uint32               narrowID = 0;
    SW_REQUIRE_TRUE( locator.reserve( request, location, narrowID ) );
    uint32 wideID      = 0;
    request._endMinute = 12 * kHour;
    SW_REQUIRE_TRUE( locator.reserve( request, location, wideID ) );
    SW_EXPECT_EQUAL( narrowID, wideID );

    // 남은 11 시 ~ 12 시도 a 의 것이다 — b 는 먼 의자로 간다.
    sw::ScheduleReserveRequest other = request;
    other._npc                       = sw::hashed_string( "b" );
    other._startMinute               = 11 * kHour;
    uint32 otherID                   = 0;
    SW_REQUIRE_TRUE( locator.reserve( other, location, otherID ) );
    SW_EXPECT_NEAR( 9.0f, location._position._x, 1.0e-4f );

    // 남이 쥔 시간까지는 넓히지 못한다 — 의자의 아침을 잡은 c 는 b 의 11 시를 빼앗지 않는다.
    sw::ScheduleReserveRequest third = request;
    third._npc                       = sw::hashed_string( "c" );
    third._startMinute               = 9 * kHour;
    third._endMinute                 = 10 * kHour;
    uint32 thirdID                   = 0;
    SW_REQUIRE_TRUE( locator.reserve( third, location, thirdID ) );
    SW_EXPECT_NEAR( 9.0f, location._position._x, 1.0e-4f );
    third._endMinute = 12 * kHour;
    SW_EXPECT_FALSE( locator.reserve( third, location, thirdID ) );
}

/**
 * @brief [ScheduleSystemTest] 슬롯 제공자를 바꾸면 예약을 준 제공자에 돌려주고, 새 제공자의 번호를 지우지 않는다
 */
SW_TEST_CASE( ScheduleSystemTest, SwappingTheLocatorReturnsReservationsToTheirOwner )
{
    sw::ScheduleCatalog catalog;
    loadBenchCatalog( catalog );
    RecordingLocator   first( 1 );
    RecordingLocator   second( 100 );
    sw::ScheduleSystem schedule;
    schedule.setActivityLocator( &first );
    SW_REQUIRE_TRUE( schedule.initialize( &catalog, sw::ScheduleSystemSettings{}, 6 * kHour ) );
    const uint32 heldID = first._nextID - 1;

    schedule.setActivityLocator( &second );
    SW_EXPECT_TRUE( sw::VectorUtil::contains( first._listReleased, heldID ) );
    schedule.advanceTo( 6 * kHour + 1 );
    SW_EXPECT_FALSE( sw::VectorUtil::contains( second._listReleased, heldID ) );
    schedule.shutdown();
}

/**
 * @brief [ScheduleSystemTest] 바깥 슬롯 제공자의 예약도 일정 저장에 실리고 복원된다
 * @details 기본 제공자만 저장하면 복원이 바깥 제공자의 예약을 지운 뒤 다시 골라 저장 전과 다른 자리로 간다.
 */
SW_TEST_CASE( ScheduleSystemTest, CustomLocatorReservationsRoundTrip )
{
    sw::ScheduleCatalog catalog;
    loadBenchCatalog( catalog );
    RecordingLocator   locator( 1 );
    sw::ScheduleSystem schedule;
    schedule.setActivityLocator( &locator );
    SW_REQUIRE_TRUE( schedule.initialize( &catalog, sw::ScheduleSystemSettings{}, 6 * kHour ) );

    sw::ScheduleSaveState saved;
    schedule.fillSaveState( saved );
    SW_REQUIRE_EQUAL( size_t( 1 ), saved._listReservation.size() );
    const uint32 savedID = saved._listReservation.front()._id;
    schedule.restoreSaveState( saved );
    SW_REQUIRE_EQUAL( size_t( 1 ), locator._listRestored.size() );
    SW_EXPECT_EQUAL( savedID, locator._listRestored.front()._id );
    schedule.shutdown();
}
