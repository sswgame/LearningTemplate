#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleActivity.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Actor/AI/Schedule/SchedulePathing.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Base/Actor/Navigation/GridPathfinder.h"
#include "GameFramework/Base/Actor/Navigation/NavGrid.h"
#include "GameFramework/Base/World/Query/GameFlags.h"

#include "TestFramework/TestFramework.h"

// NPC 하루 일정 — 칸 고르기(시각 · 조건 · 우선순위) · 축제 · 일찍 나서기 · 끼어들기와 돌아가기 · 약속 · 화면 밖 따라잡기 · 잠 · 저장 · 결정성 · 2D.
// 시계는 가짜다 — 시간은 정수 분(`advanceTo`)으로만 흐른다. 0 일은 봄 1 일 월요일이다.

using namespace sw;

namespace
{
    struct ScheduleTestInternal
    {
        static constexpr const utf8* kTownXml = R"(
<Schedules>
  <Calendar days="Mon,Tue,Wed,Thu,Fri,Sat,Sun" seasons="Spring,Summer" weathers="sunny,rain"/>
  <Place id="pierre_home" area="home_p" position="0 0 0"/>
  <Place id="abigail_home" area="home_a" position="100 0 0"/>
  <Place id="store" area="town" position="30 0 0"/>
  <Place id="square" area="town" position="50 0 0" radius="5"/>
  <Place id="saloon" area="town" position="60 0 0"/>
  <Spot id="bench_near" kind="Bench" area="town" position="52 0 0" capacity="1"/>
  <Spot id="bench_far" kind="Bench" area="town" position="58 0 0" capacity="1"/>
  <Interrupt id="Talk" priority="10" timeout="30"/>
  <Interrupt id="Alarm" priority="50"/>
  <Appointment id="lunch" place="saloon" start="12:00" end="13:00" wait="30" priority="50" days="Wed"/>
  <Archetype id="villager">
    <Routine id="night"><Block start="0:00" end="8:00" activity="Sleep"/><Block start="22:00" end="24:00" activity="Sleep"/></Routine>
  </Archetype>
  <Npc id="pierre" archetype="villager" home="pierre_home" speed="1">
    <Routine id="weekday" days="Mon,Tue,Wed,Thu,Fri">
      <Block start="9:00" end="17:00" activity="WorkAt" place="store" animation="Sweep"/>
      <Block activity="Meet" appointment="lunch"/>
      <Block start="17:00" end="19:00" activity="GoTo" place="square"/>
    </Routine>
    <Routine id="weekend" days="Sat,Sun">
      <Block start="10:00" end="16:00" activity="Wander" place="square" every="30"/>
    </Routine>
    <Routine id="rainy" priority="10" weathers="rain">
      <Block start="17:00" end="22:00" activity="StayHome"/>
    </Routine>
    <Routine id="friend" priority="20" tags="Relationship.Player.Friend">
      <Block start="19:00" end="20:00" activity="GoTo" place="saloon"/>
    </Routine>
    <Routine id="closed" priority="30" flags="shopClosed">
      <Block start="9:00" end="17:00" activity="StayHome"/>
    </Routine>
  </Npc>
  <Npc id="abigail" archetype="villager" home="abigail_home" speed="2">
    <Routine id="daily">
      <Block start="9:00" end="12:00" activity="GoTo" place="square"/>
      <Block activity="Meet" appointment="lunch"/>
      <Block start="18:00" end="20:00" activity="UseObject" objectKind="Bench" place="square"/>
    </Routine>
  </Npc>
  <Npc id="emily" archetype="villager" home="pierre_home" speed="1">
    <Routine id="daily"><Block start="18:00" end="20:00" activity="UseObject" objectKind="Bench" place="square"/></Routine>
  </Npc>
  <Event id="egg_festival" priority="100" seasons="Spring" daysOfSeason="13" archetypes="villager">
    <Block start="9:00" end="14:00" activity="Attend" place="square"/>
  </Event>
</Schedules>
)";

        static constexpr int32 kMonday    = 0;
        static constexpr int32 kWednesday = 2;
        static constexpr int32 kSaturday  = 5;

        static int32 makeMinute( int32 day, int32 hour, int32 minute = 0 ) { return day * kScheduleMinutesPerDay + hour * 60 + minute; }

        static ScheduleSystemSettings makeSettings( uint32 seed = 7u )
        {
            ScheduleSystemSettings settings;
            settings._clock._listSeason    = { hashed_string( "Spring" ), hashed_string( "Summer" ) };
            settings._clock._daysPerSeason = 28;
            settings._seed                 = seed;
            return settings;
        }

        static bool isNear( const float3& expected, const float3& actual, float32 tolerance = 1.0e-3f ) { return float3::getDistance( expected, actual ) <= tolerance; }

        static int32 countEvents( const vector<ScheduleEvent>& listEvent, ScheduleEvent::Kind kind, int32 npcIndex = -1 )
        {
            int32 count = 0;
            for ( const ScheduleEvent& event : listEvent )
            {
                const bool bNpc = npcIndex < 0 || event._npcIndex == npcIndex;
                count += event._kind == kind && bNpc ? 1 : 0;
            }
            return count;
        }

        /** @brief x = 40 에 벽(z 가 8 이상인 곳만 열림)이 있는 XZ 격자 — 고운 경로가 곧은 선과 다르게 돈다. */
        static void makeWalledGrid( NavGrid& outGrid )
        {
            outGrid.initialize( 120, 21, 1.0f, float3{ -10.0f, 0.0f, -10.0f } );
            for ( int32 cellY = 0; cellY <= 17; ++cellY )
            {
                outGrid.setBlocked( 50, cellY, true );
            }
        }

        /** @brief 활동 시작 · 끝을 받아 적는 애니메이션 훅입니다. */
        class RecordingAnimator final : public IScheduleActivityAnimator
        {
        public:
            vector<ScheduleActivityCue> _listStarted{};
            vector<ScheduleActivityCue> _listEnded{};

            void onActivityStarted( const ScheduleActivityCue& cue ) override { _listStarted.push_back( cue ); }
            void onActivityEnded( const ScheduleActivityCue& cue ) override { _listEnded.push_back( cue ); }
        };
    };
} // namespace

SW_TEST_CASE( ScheduleTest, ClockTextParsesHoursAndMinutes )
{
    int32 minutes = -1;
    SW_EXPECT_TRUE( ScheduleCatalog::parseClockMinutes( "9:30", minutes ) );
    SW_EXPECT_EQUAL( 570, minutes );
    SW_EXPECT_TRUE( ScheduleCatalog::parseClockMinutes( "24:00", minutes ) );
    SW_EXPECT_EQUAL( 1440, minutes );
    SW_EXPECT_TRUE( ScheduleCatalog::parseClockMinutes( "7", minutes ) );
    SW_EXPECT_EQUAL( 420, minutes );
    SW_EXPECT_FALSE( ScheduleCatalog::parseClockMinutes( "24:01", minutes ) );
    SW_EXPECT_FALSE( ScheduleCatalog::parseClockMinutes( "9:5", minutes ) );
    SW_EXPECT_FALSE( ScheduleCatalog::parseClockMinutes( "9:60", minutes ) );
    SW_EXPECT_FALSE( ScheduleCatalog::parseClockMinutes( "noon", minutes ) );
}

SW_TEST_CASE( ScheduleTest, CatalogResolvesArchetypesAndAppointmentAttendees )
{
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( ScheduleTestInternal::kTownXml, "town" ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( catalog.getNpcs().size() ) );
    const ScheduleNpcDef* pPierre = catalog.findNpc( "pierre" );
    SW_ASSERT_NOT_NULL( pPierre );
    SW_EXPECT_EQUAL( 6, static_cast<int32>( pPierre->_listRoutine.size() ) ); // 자기 다섯 + 묶음의 night
    SW_EXPECT_TRUE( pPierre->_listRoutine.back()._id == hashed_string( "night" ) );
    const ScheduleAppointmentDef* pLunch = catalog.findAppointment( "lunch" );
    SW_ASSERT_NOT_NULL( pLunch );
    SW_ASSERT_EQUAL( 2, static_cast<int32>( pLunch->_listAttendee.size() ) );
    SW_EXPECT_TRUE( pLunch->_listAttendee[0] == hashed_string( "pierre" ) );
    SW_EXPECT_TRUE( pLunch->_listAttendee[1] == hashed_string( "abigail" ) );
}

/**
 * @brief [ScheduleTest] 틀린 데이터는 이름을 들어 경고하고 그 항목을 뺀다 — 모르는 원소 · 속성 · 활동 · 장소 · 요일 · 계절 · 날씨, 틀린 시각 · 조건식,
 *        시작 ≥ 끝, 겹치는 칸, 장소 · 오브젝트 종류가 빠진 칸, 묶음 순환
 */
SW_TEST_CASE( ScheduleTest, CatalogWarnsAboutEveryBadName )
{
    constexpr const utf8*    kBadXml = R"(
<Schedules>
  <Calendar days="Mon,Tue" seasons="Spring" weathers="sunny"/>
  <Place id="home" position="0 0 0"/>
  <Bogus/>
  <Archetype id="a" parent="b"/>
  <Archetype id="b" parent="a"/>
  <Npc id="x" home="home" color="red">
    <Routine id="r" seasons="Winter" days="Funday">
      <Block start="9:00" end="8:00" activity="GoTo" place="home"/>
      <Block start="9:00" end="10:00" activity="Dance" place="home"/>
      <Block start="9:00" end="10:00" activity="GoTo" place="nowhere"/>
      <Block start="25:00" end="26:00" activity="StayHome"/>
      <Block start="10:00" end="12:00" activity="StayHome"/>
      <Block start="11:00" end="13:00" activity="GoTo" place="home"/>
      <Block start="13:00" end="14:00" activity="GoTo"/>
      <Block start="14:00" end="15:00" activity="UseObject"/>
      <Block start="15:00" end="16:00" activity="StayHome" weathers="snow" flags="a &amp;&amp;"/>
    </Routine>
  </Npc>
</Schedules>
)";
    ScheduleCatalog          catalog;
    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "schedule data with unknown names" );
        SW_ASSERT_TRUE( catalog.loadFromXmlText( kBadXml, "bad" ) );
    }
    const utf8* const arrExpected[] = { "unknown element <Bogus>",
                                        "unknown attribute 'color'",
                                        "unknown season 'Winter'",
                                        "unknown day 'Funday'",
                                        "needs start < end",
                                        "unknown activity 'Dance'",
                                        "unknown place 'nowhere'",
                                        "invalid start time '25:00'",
                                        "overlapping blocks",
                                        "without a place",
                                        "without an objectKind",
                                        "unknown weather 'snow'",
                                        "invalid flags expression",
                                        "parent cycle" };
    for ( const utf8* pExpected : arrExpected )
    {
        SW_EXPECT_TRUE_MSG( logs.countContaining( pExpected ) >= 1, ( string( "missing warning: " ) + pExpected + logs.joined() ).c_str() );
    }
    const ScheduleNpcDef* pNpc = catalog.findNpc( "x" );
    SW_ASSERT_NOT_NULL( pNpc );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( pNpc->_listRoutine.size() ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( pNpc->_listRoutine[0]._listBlock.size() ) ); // 쓸 수 있는 칸만 남는다
}

SW_TEST_CASE( ScheduleTest, ResourceSampleScheduleLoadsWithoutWarnings )
{
    ScheduleCatalog          catalog;
    test::ScopedLogCollector logs;
    SW_ASSERT_TRUE( catalog.loadFromResource( "game/harvestvalley/data/villagers.schedules.xml" ) );
    SW_EXPECT_TRUE_MSG( logs.joined().empty(), logs.joined().c_str() );
    SW_EXPECT_TRUE( catalog.getNpcs().size() >= 2u );
}

/**
 * @brief [ScheduleTest] 칸은 시각 · 요일 · 날씨 · 플래그 · 태그 조건과 우선순위로 고른다
 */
SW_TEST_CASE( ScheduleTest, BlockSelectionFollowsTimeConditionsAndPriority )
{
    using Internal = ScheduleTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( Internal::kTownXml, "town" ) );
    GameFlags      flags;
    ScheduleSystem system;
    system.setFlags( &flags );
    system.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( Internal::kMonday, 6 ) );
    const int32 pierre = system.findNpcIndex( "pierre" );
    SW_ASSERT_TRUE( pierre >= 0 );

    SW_EXPECT_TRUE( system.getNpcView( pierre )._activity == hashed_string( "Sleep" ) ); // 묶음의 루틴
    system.advanceTo( Internal::makeMinute( Internal::kMonday, 10 ) );
    ScheduleNpcView view = system.getNpcView( pierre );
    SW_EXPECT_TRUE( view._activity == hashed_string( "WorkAt" ) );
    SW_EXPECT_TRUE( view._phase == ScheduleNpcPhase::Performing );
    SW_EXPECT_TRUE( Internal::isNear( float3{ 30.0f, 0.0f, 0.0f }, view._location._position ) );
    SW_EXPECT_TRUE( view._animation == hashed_string( "Sweep" ) ); // 칸이 활동의 기본 애니메이션을 덮는다

    // 비 — 우선순위 10 루틴이 저녁을 덮는다(맑으면 광장).
    system.advanceTo( Internal::makeMinute( Internal::kMonday, 18 ) );
    SW_EXPECT_TRUE( system.getNpcView( pierre )._activity == hashed_string( "GoTo" ) );
    system.setWeather( "rain" );
    system.advanceTo( Internal::makeMinute( Internal::kMonday, 18, 1 ) );
    SW_EXPECT_TRUE( system.getNpcView( pierre )._activity == hashed_string( "StayHome" ) );
    system.setWeather( "sunny" );

    // 태그 — 친구가 되면 19 시에 술집(우선순위 20).
    system.addNpcTag( pierre, TagID::request( "Relationship.Player.Friend" ) );
    system.advanceTo( Internal::makeMinute( Internal::kMonday, 19, 30 ) );
    view = system.getNpcView( pierre );
    SW_EXPECT_TRUE( view._activity == hashed_string( "GoTo" ) );
    SW_EXPECT_TRUE( view._target._position._x == 60.0f );

    // 요일 — 토요일은 주말 루틴(광장을 돌아다닌다).
    system.advanceTo( Internal::makeMinute( Internal::kSaturday, 11 ) );
    view = system.getNpcView( pierre );
    SW_EXPECT_TRUE( view._activity == hashed_string( "Wander" ) );
    SW_EXPECT_TRUE( float3::getDistance( float3{ 50.0f, 0.0f, 0.0f }, view._target._position ) <= 5.0f + 1.0e-3f );

    // 플래그 — 가게가 닫히면 낮에 집(우선순위 30).
    flags.setFlag( "shopClosed", 1 );
    system.advanceTo( Internal::makeMinute( Internal::kSaturday + 2, 10 ) ); // 월요일
    SW_EXPECT_TRUE( system.getNpcView( pierre )._activity == hashed_string( "StayHome" ) );
    flags.setFlag( "shopClosed", 0 );
    system.advanceTo( Internal::makeMinute( Internal::kSaturday + 2, 10, 1 ) );
    SW_EXPECT_TRUE( system.getNpcView( pierre )._activity == hashed_string( "WorkAt" ) );
}

/**
 * @brief [ScheduleTest] 축제(우선순위 높은 행사)는 그날 그 시간만 덮고, 나머지 시간과 다른 날은 평소 루틴이다
 */
SW_TEST_CASE( ScheduleTest, FestivalOverridesOnlyItsHoursOnItsDay )
{
    using Internal = ScheduleTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( Internal::kTownXml, "town" ) );
    ScheduleSystem system;
    const int32    kFestivalDay = 12; // 봄 13 일 토요일
    system.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( kFestivalDay, 6 ) );
    const int32 pierre  = system.findNpcIndex( "pierre" );
    const int32 abigail = system.findNpcIndex( "abigail" );

    system.advanceTo( Internal::makeMinute( kFestivalDay, 11 ) );
    SW_EXPECT_TRUE( system.getNpcView( pierre )._activity == hashed_string( "Attend" ) );
    SW_EXPECT_TRUE( system.getNpcView( abigail )._activity == hashed_string( "Attend" ) ); // 묶음으로 모두 참가
    const vector<ScheduleSegment>& listSegment = system.getPlan( pierre );
    bool                           bFound      = false;
    for ( const ScheduleSegment& segment : listSegment )
    {
        if ( segment._source == ScheduleSegmentSource::Event )
        {
            bFound = true;
            SW_EXPECT_EQUAL( 9 * 60, segment._startMinute );
            SW_EXPECT_EQUAL( 14 * 60, segment._endMinute );
        }
    }
    SW_EXPECT_TRUE( bFound );
    system.advanceTo( Internal::makeMinute( kFestivalDay, 15 ) );
    SW_EXPECT_TRUE( system.getNpcView( pierre )._activity == hashed_string( "Wander" ) ); // 축제가 끝나면 주말 루틴

    // 여름 13 일(같은 토요일)에는 축제가 없다.
    const int32 kSummerDay = 40;
    system.advanceTo( Internal::makeMinute( kSummerDay, 11 ) );
    SW_EXPECT_TRUE( system.getNpcView( pierre )._activity == hashed_string( "Wander" ) );
    string explanation;
    system.explainNpc( pierre, explanation );
    SW_EXPECT_TRUE_MSG( explanation.find( "event 'egg_festival' p100 [seasons failed]" ) != string::npos, explanation.c_str() );
}

/**
 * @brief [ScheduleTest] 칸 시작에 닿도록 이동 시간만큼 일찍 나서고, 가는 동안은 경로 위 비율만큼의 자리다
 */
SW_TEST_CASE( ScheduleTest, LeavesEarlyByEstimatedTravelTime )
{
    using Internal = ScheduleTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( Internal::kTownXml, "town" ) );
    ScheduleSystem system;
    system.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( Internal::kMonday, 6 ) );
    const int32 pierre = system.findNpcIndex( "pierre" );

    const ScheduleSegment* pWork = nullptr;
    for ( const ScheduleSegment& segment : system.getPlan( pierre ) )
    {
        if ( segment._activity == hashed_string( "WorkAt" ) )
            pWork = &segment;
    }
    SW_ASSERT_NOT_NULL( pWork );
    SW_EXPECT_EQUAL( 30, pWork->_travelMinutes ); // 집(0) → 가게(30), 분당 1
    SW_EXPECT_EQUAL( 8 * 60 + 30, pWork->_departMinute );

    system.advanceTo( Internal::makeMinute( Internal::kMonday, 8, 29 ) );
    SW_EXPECT_TRUE( system.getNpcView( pierre )._phase == ScheduleNpcPhase::Performing ); // 아직 집
    system.advanceTo( Internal::makeMinute( Internal::kMonday, 8, 45 ) );
    ScheduleNpcView view = system.getNpcView( pierre );
    SW_EXPECT_TRUE( view._phase == ScheduleNpcPhase::Traveling );
    SW_EXPECT_NEAR_EQUAL( 0.5f, view._travelFraction, 1.0e-4f );
    SW_EXPECT_TRUE( Internal::isNear( float3{ 15.0f, 0.0f, 0.0f }, view._location._position ) );
    system.advanceTo( Internal::makeMinute( Internal::kMonday, 9 ) );
    view = system.getNpcView( pierre );
    SW_EXPECT_TRUE( view._phase == ScheduleNpcPhase::Performing );
    SW_EXPECT_TRUE( Internal::isNear( float3{ 30.0f, 0.0f, 0.0f }, view._location._position ) );

    string timeline;
    system.dumpTimeline( pierre, timeline );
    SW_EXPECT_TRUE_MSG( timeline.find( "09:00-17:00 WorkAt @ 'town'" ) != string::npos && timeline.find( "leave 08:30 (+30 min)" ) != string::npos, timeline.c_str() );
}

/**
 * @brief [ScheduleTest] 끼어들기는 우선순위로 쌓이고 만료되며, 모두 끝나면 끼어든 자리에서 "지금 시각의" 칸으로 돌아간다(끊긴 칸이 아니라)
 */
SW_TEST_CASE( ScheduleTest, InterruptionStackResumesToTheCurrentBlock )
{
    using Internal = ScheduleTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( Internal::kTownXml, "town" ) );
    ScheduleSystem system;
    system.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( Internal::kMonday, 6 ) );
    const int32 pierre = system.findNpcIndex( "pierre" );

    system.advanceTo( Internal::makeMinute( Internal::kMonday, 9, 30 ) );
    SW_EXPECT_TRUE( system.pushInterruption( pierre, "Talk" ) );
    SW_EXPECT_TRUE( system.getNpcView( pierre )._phase == ScheduleNpcPhase::Interrupted );
    system.advanceTo( Internal::makeMinute( Internal::kMonday, 9, 50 ) );
    SW_EXPECT_TRUE( system.pushInterruption( pierre, "Alarm" ) );
    SW_EXPECT_TRUE( system.getNpcView( pierre )._interruption == hashed_string( "Alarm" ) );
    {
        test::ScopedDefensiveTestLog expected( "unknown interruption id" );
        SW_EXPECT_FALSE( system.pushInterruption( pierre, "Dance" ) );
    }

    system.advanceTo( Internal::makeMinute( Internal::kMonday, 10, 5 ) ); // Talk 만료(10:00) — 경보는 남는다
    ScheduleNpcView view = system.getNpcView( pierre );
    SW_EXPECT_TRUE( view._phase == ScheduleNpcPhase::Interrupted );
    SW_EXPECT_TRUE( view._interruption == hashed_string( "Alarm" ) );
    SW_EXPECT_TRUE( Internal::isNear( float3{ 30.0f, 0.0f, 0.0f }, view._location._position ) );
    system.reportInterruptedLocation( pierre, ScheduleLocation{
                                                  float3{ 20.0f, 0.0f, 0.0f },
                                                  hashed_string( "town" )
    } ); // 도망쳤다

    system.advanceTo( Internal::makeMinute( Internal::kMonday, 17, 30 ) );
    SW_EXPECT_TRUE( system.getNpcView( pierre )._phase == ScheduleNpcPhase::Interrupted );
    SW_EXPECT_TRUE( system.popInterruption( pierre, "Alarm" ) );
    SW_EXPECT_FALSE( system.popInterruption( pierre, "Alarm" ) );
    // 17:30 의 칸은 광장(17–19)이다 — 도망친 자리(20)에서 광장(50)까지 30 분.
    system.advanceTo( Internal::makeMinute( Internal::kMonday, 17, 45 ) );
    view = system.getNpcView( pierre );
    SW_EXPECT_TRUE( view._activity == hashed_string( "GoTo" ) );
    SW_EXPECT_TRUE( view._phase == ScheduleNpcPhase::Traveling );
    SW_EXPECT_TRUE_MSG( Internal::isNear( float3{ 35.0f, 0.0f, 0.0f }, view._location._position ), "resume starts from the reported location" );
    system.advanceTo( Internal::makeMinute( Internal::kMonday, 18 ) );
    view = system.getNpcView( pierre );
    SW_EXPECT_TRUE( view._phase == ScheduleNpcPhase::Performing );
    SW_EXPECT_TRUE( Internal::isNear( float3{ 50.0f, 0.0f, 0.0f }, view._location._position ) );

    vector<ScheduleEvent> listEvent;
    system.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 2, Internal::countEvents( listEvent, ScheduleEvent::Kind::Interrupted, pierre ) );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, ScheduleEvent::Kind::Resumed, pierre ) );
}

/**
 * @brief [ScheduleTest] 두 NPC 의 일정이 같은 약속을 가리키면 각자 이동 시간만큼 일찍 나서 같은 시각에 모이고, 한 사람이 못 오면 기다림 뒤 약속이 깨져
 *        다른 사람은 아래 칸으로 돌아간다
 */
SW_TEST_CASE( ScheduleTest, AppointmentBringsTwoNpcsTogetherOrBreaks )
{
    using Internal = ScheduleTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( Internal::kTownXml, "town" ) );
    {
        ScheduleSystem system;
        system.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( Internal::kWednesday, 6 ) );
        const int32 pierre  = system.findNpcIndex( "pierre" );
        const int32 abigail = system.findNpcIndex( "abigail" );
        system.advanceTo( Internal::makeMinute( Internal::kWednesday, 11, 59 ) );
        SW_EXPECT_FALSE( system.isAppointmentMet( "lunch" ) );
        system.advanceTo( Internal::makeMinute( Internal::kWednesday, 12, 5 ) );
        SW_EXPECT_TRUE( system.isAppointmentMet( "lunch" ) );
        SW_EXPECT_TRUE( Internal::isNear( float3{ 60.0f, 0.0f, 0.0f }, system.getNpcView( pierre )._location._position ) );
        SW_EXPECT_TRUE( Internal::isNear( float3{ 60.0f, 0.0f, 0.0f }, system.getNpcView( abigail )._location._position ) );
        // 가게(30)에서 30 분, 광장(50)에서 분당 2 로 5 분.
        for ( const ScheduleSegment& segment : system.getPlan( pierre ) )
        {
            if ( segment._source == ScheduleSegmentSource::Appointment )
                SW_EXPECT_EQUAL( 11 * 60 + 30, segment._departMinute );
        }
        for ( const ScheduleSegment& segment : system.getPlan( abigail ) )
        {
            if ( segment._source == ScheduleSegmentSource::Appointment )
                SW_EXPECT_EQUAL( 11 * 60 + 55, segment._departMinute );
        }
        vector<ScheduleEvent> listEvent;
        system.drainEvents( listEvent );
        SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, ScheduleEvent::Kind::AppointmentMet ) );
        system.advanceTo( Internal::makeMinute( Internal::kWednesday, 13, 10 ) );
        SW_EXPECT_TRUE( system.getNpcView( pierre )._activity == hashed_string( "WorkAt" ) );
    }
    {
        ScheduleSystem system;
        system.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( Internal::kWednesday, 6 ) );
        const int32 pierre  = system.findNpcIndex( "pierre" );
        const int32 abigail = system.findNpcIndex( "abigail" );
        system.advanceTo( Internal::makeMinute( Internal::kWednesday, 11 ) );
        SW_EXPECT_TRUE( system.pushInterruption( abigail, "Alarm" ) );
        system.advanceTo( Internal::makeMinute( Internal::kWednesday, 12, 29 ) );
        SW_EXPECT_FALSE( system.isAppointmentBroken( "lunch" ) );
        SW_EXPECT_TRUE( system.getNpcView( pierre )._activity == hashed_string( "Meet" ) ); // 기다린다
        system.advanceTo( Internal::makeMinute( Internal::kWednesday, 12, 45 ) );
        SW_EXPECT_TRUE( system.isAppointmentBroken( "lunch" ) );
        const ScheduleNpcView view = system.getNpcView( pierre );
        SW_EXPECT_TRUE( view._activity == hashed_string( "WorkAt" ) );
        SW_EXPECT_TRUE( view._phase == ScheduleNpcPhase::Traveling );
        SW_EXPECT_TRUE( Internal::isNear( float3{ 45.0f, 0.0f, 0.0f }, view._location._position ) ); // 12:30 술집(60)에서 나서 15 분
        vector<ScheduleEvent> listEvent;
        system.drainEvents( listEvent );
        SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, ScheduleEvent::Kind::AppointmentBroken, pierre ) );
        SW_EXPECT_EQUAL( 0, Internal::countEvents( listEvent, ScheduleEvent::Kind::AppointmentMet ) );
    }
}

/**
 * @brief [ScheduleTest] 화면 밖(Far)으로 몇 시간씩 건너뛴 NPC 는 매 분 화면 안(Near)으로 돌린 NPC 와 그날 끝에 같은 자리 · 같은 상태(해시)이고,
 *        중간에 화면 안으로 들어오면 그 시각의 고운 경로 위 자리로 선다
 */
SW_TEST_CASE( ScheduleTest, OffScreenCatchUpMatchesOnScreenSimulation )
{
    using Internal = ScheduleTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( Internal::kTownXml, "town" ) );
    NavGrid grid;
    Internal::makeWalledGrid( grid );
    GridPathfinder          pathfinder;
    NavGridSchedulePathing  finePathing( &grid, &pathfinder, SchedulePlane::XZ );
    StraightSchedulePathing planningPathing( SchedulePlane::XZ );

    ScheduleSystem  onScreen;
    ScheduleSystem  offScreen;
    ScheduleSystem  snapping;
    ScheduleSystem* arrSystem[] = { &onScreen, &offScreen, &snapping };
    for ( ScheduleSystem* pSystem : arrSystem )
    {
        pSystem->setPathing( &planningPathing, &finePathing );
        pSystem->initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( Internal::kWednesday, 6 ) );
    }
    for ( int32 npcIndex = 0; npcIndex < onScreen.getNpcCount(); ++npcIndex )
    {
        onScreen.setNpcLod( npcIndex, ScheduleLod::Near );
    }
    const int32 pierre = onScreen.findNpcIndex( "pierre" );

    // 같은 입력을 같은 시각에 — 10:07 말 걸기(30 분 — 만료 10:37 은 화면 밖 사건 간격 15 분의 칸 위가 아니다), 15:00 비.
    // 화면 안은 매 분, 화면 밖은 그 시각까지 한 번에.
    const int32 talkMinute = Internal::makeMinute( Internal::kWednesday, 10, 7 );
    const int32 snapMinute = Internal::makeMinute( Internal::kWednesday, 13, 10 ); // 피에르가 술집 → 가게(벽을 돌아) 가는 중
    const int32 rainMinute = Internal::makeMinute( Internal::kWednesday, 15 );
    const int32 endMinute  = Internal::makeMinute( Internal::kWednesday, 23, 30 );
    for ( int32 minute = onScreen.getMinute() + 1; minute <= endMinute; ++minute )
    {
        onScreen.advanceTo( minute );
        if ( minute == talkMinute )
            SW_EXPECT_TRUE( onScreen.pushInterruption( pierre, "Talk" ) );
        if ( minute == rainMinute )
            onScreen.setWeather( "rain" );
        if ( minute == snapMinute )
        {
            offScreen.advanceTo( talkMinute );
            SW_EXPECT_TRUE( offScreen.pushInterruption( pierre, "Talk" ) );
            offScreen.advanceTo( snapMinute );
            snapping.advanceTo( talkMinute );
            SW_EXPECT_TRUE( snapping.pushInterruption( pierre, "Talk" ) );
            snapping.advanceTo( snapMinute );

            const ScheduleNpcView nearView = onScreen.getNpcView( pierre );
            const ScheduleNpcView farView  = offScreen.getNpcView( pierre );
            SW_EXPECT_TRUE( nearView._phase == ScheduleNpcPhase::Traveling );
            SW_EXPECT_TRUE( farView._phase == ScheduleNpcPhase::Traveling );
            SW_EXPECT_NEAR_EQUAL( nearView._travelFraction, farView._travelFraction, 1.0e-5f );
            SW_EXPECT_TRUE_MSG( float3::getDistance( nearView._location._position, farView._location._position ) > 1.0f, "the fine route detours around the wall" );
            snapping.setNpcLod( pierre, ScheduleLod::Near );
            SW_EXPECT_TRUE( Internal::isNear( nearView._location._position, snapping.getNpcView( pierre )._location._position ) );
            vector<ScheduleEvent> listEvent;
            snapping.drainEvents( listEvent );
            SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, ScheduleEvent::Kind::Snapped, pierre ) );
        }
    }
    offScreen.advanceTo( rainMinute );
    offScreen.setWeather( "rain" );
    offScreen.advanceTo( endMinute );

    SW_EXPECT_EQUAL( onScreen.computeStateHash(), offScreen.computeStateHash() );
    for ( int32 npcIndex = 0; npcIndex < onScreen.getNpcCount(); ++npcIndex )
    {
        const ScheduleNpcView nearView = onScreen.getNpcView( npcIndex );
        const ScheduleNpcView farView  = offScreen.getNpcView( npcIndex );
        SW_EXPECT_TRUE( nearView._activity == farView._activity );
        SW_EXPECT_TRUE( nearView._location._area == farView._location._area );
        SW_EXPECT_TRUE( Internal::isNear( nearView._location._position, farView._location._position ) );
    }
}

/**
 * @brief [ScheduleTest] 잠(시간 건너뛰기)은 끼어들기를 지우고 모두를 일정이 말하는 자리로 옮긴다 — 건너뛰지 않고 흘린 것과 같은 상태, 사이 사건 없이 `Snapped`
 */
SW_TEST_CASE( ScheduleTest, TimeSkipJumpsEveryNpcToTheirScheduledPlace )
{
    using Internal = ScheduleTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( Internal::kTownXml, "town" ) );
    ScheduleSystem flowing;
    ScheduleSystem sleeping;
    flowing.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( Internal::kWednesday, 6 ) );
    sleeping.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( Internal::kWednesday, 6 ) );
    const int32 pierre = sleeping.findNpcIndex( "pierre" );

    const int32 morning = Internal::makeMinute( Internal::kWednesday + 1, 9, 10 );
    flowing.advanceTo( morning );
    sleeping.advanceTo( Internal::makeMinute( Internal::kWednesday, 21 ) );
    SW_EXPECT_TRUE( sleeping.pushInterruption( pierre, "Alarm" ) );
    vector<ScheduleEvent> listEvent;
    sleeping.drainEvents( listEvent );
    listEvent.clear();
    sleeping.skipTo( morning );

    SW_EXPECT_EQUAL( flowing.computeStateHash(), sleeping.computeStateHash() );
    const ScheduleNpcView view = sleeping.getNpcView( pierre );
    SW_EXPECT_TRUE( view._activity == hashed_string( "WorkAt" ) );
    SW_EXPECT_TRUE( view._interruption.empty() );
    SW_EXPECT_TRUE( Internal::isNear( float3{ 30.0f, 0.0f, 0.0f }, view._location._position ) );
    sleeping.drainEvents( listEvent );
    SW_EXPECT_EQUAL( sleeping.getNpcCount(), Internal::countEvents( listEvent, ScheduleEvent::Kind::Snapped ) );
    SW_EXPECT_EQUAL( 0, Internal::countEvents( listEvent, ScheduleEvent::Kind::Departed ) );
}

/**
 * @brief [ScheduleTest] 저장 · 복원 — 리플렉션 아카이브로 쓰고 읽은 상태(끼어들기 · 깨진 약속 · 자리 예약 · 다시 세운 계획)가 같은 해시이고, 그 뒤로도 같이 흐른다
 */
SW_TEST_CASE( ScheduleTest, SaveStateRoundTripsThroughTheArchive )
{
    using Internal = ScheduleTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( Internal::kTownXml, "town" ) );
    ScheduleSystem original;
    original.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( Internal::kWednesday, 6 ) );
    const int32 pierre  = original.findNpcIndex( "pierre" );
    const int32 abigail = original.findNpcIndex( "abigail" );
    original.advanceTo( Internal::makeMinute( Internal::kWednesday, 11 ) );
    SW_EXPECT_TRUE( original.pushInterruption( abigail, "Alarm" ) );
    original.advanceTo( Internal::makeMinute( Internal::kWednesday, 12, 40 ) ); // 약속이 깨지고 피에르는 가게로
    SW_EXPECT_TRUE( original.pushInterruption( pierre, "Talk" ) );
    original.addNpcTag( pierre, TagID::request( "Relationship.Player.Friend" ) );
    original.advanceTo( Internal::makeMinute( Internal::kWednesday, 12, 45 ) );

    ScheduleSaveState saved;
    original.fillSaveState( saved );
    SW_EXPECT_TRUE( saved._listReservation.empty() == false ); // 저녁 벤치 예약
    Archive writer;
    SW_ASSERT_TRUE( writer.serializeObject( saved ) );
    Archive           reader( writer.getData(), writer.getSize() );
    ScheduleSaveState loaded;
    SW_ASSERT_TRUE( reader.deserializeObject( loaded ) );

    ScheduleSystem restored;
    restored.initialize( &catalog, Internal::makeSettings( 99u ), Internal::makeMinute( 0, 6 ) );
    restored.restoreSaveState( loaded );
    SW_EXPECT_EQUAL( original.getMinute(), restored.getMinute() );
    SW_EXPECT_TRUE( restored.isAppointmentBroken( "lunch" ) );
    SW_EXPECT_TRUE( restored.getNpcView( pierre )._interruption == hashed_string( "Talk" ) );
    SW_EXPECT_TRUE( restored.getNpcView( abigail )._interruption == hashed_string( "Alarm" ) );
    SW_EXPECT_EQUAL( original.computeStateHash(), restored.computeStateHash() );

    original.popInterruption( abigail, "Alarm" );
    restored.popInterruption( abigail, "Alarm" );
    original.advanceTo( Internal::makeMinute( Internal::kWednesday, 23 ) );
    restored.advanceTo( Internal::makeMinute( Internal::kWednesday, 23 ) );
    SW_EXPECT_EQUAL( original.computeStateHash(), restored.computeStateHash() );
    SW_EXPECT_TRUE( Internal::isNear( original.getNpcView( abigail )._location._position, restored.getNpcView( abigail )._location._position ) );
}

/**
 * @brief [ScheduleTest] 결정성 — 같은 씨앗이면 같은 해시 · 같은 네트워크 요약 바이트, 다른 씨앗이면 돌아다니는 자리가 달라 해시가 다르다. 요약은 바이트로 왕복한다
 */
SW_TEST_CASE( ScheduleTest, DeterminismHashAndNetSummary )
{
    using Internal = ScheduleTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( Internal::kTownXml, "town" ) );
    ScheduleSystem first;
    ScheduleSystem second;
    ScheduleSystem otherSeed;
    first.initialize( &catalog, Internal::makeSettings( 1u ), Internal::makeMinute( Internal::kSaturday, 6 ) );
    second.initialize( &catalog, Internal::makeSettings( 1u ), Internal::makeMinute( Internal::kSaturday, 6 ) );
    otherSeed.initialize( &catalog, Internal::makeSettings( 2u ), Internal::makeMinute( Internal::kSaturday, 6 ) );
    const int32 noon = Internal::makeMinute( Internal::kSaturday, 12, 10 );
    first.advanceTo( noon );
    for ( int32 minute = second.getMinute() + 7; minute <= noon; minute += 7 )
    {
        second.advanceTo( minute ); // 다른 간격으로 흘려도
    }
    second.advanceTo( noon );
    otherSeed.advanceTo( noon );
    SW_EXPECT_EQUAL( first.computeStateHash(), second.computeStateHash() );
    SW_EXPECT_NOT_EQUAL( first.computeStateHash(), otherSeed.computeStateHash() );

    vector<ScheduleNetSummary> listSummary;
    first.fillNetSummary( listSummary );
    SW_ASSERT_EQUAL( first.getNpcCount(), static_cast<int32>( listSummary.size() ) );
    vector<uint8> bytes;
    ScheduleSystem::encodeNetSummary( first.getMinute(), listSummary, bytes );
    SW_EXPECT_EQUAL( 6 + 26 * static_cast<int32>( listSummary.size() ), static_cast<int32>( bytes.size() ) );
    int32                      minute = 0;
    vector<ScheduleNetSummary> listDecoded;
    SW_ASSERT_TRUE( ScheduleSystem::decodeNetSummary( bytes, minute, listDecoded ) );
    SW_EXPECT_EQUAL( noon, minute );
    SW_ASSERT_EQUAL( listSummary.size(), listDecoded.size() );
    for ( size_t entryIndex = 0; entryIndex < listSummary.size(); ++entryIndex )
    {
        SW_EXPECT_TRUE( Internal::isNear( listSummary[entryIndex]._position, listDecoded[entryIndex]._position, 0.0f ) );
        SW_EXPECT_EQUAL( listSummary[entryIndex]._areaHash, listDecoded[entryIndex]._areaHash );
        SW_EXPECT_EQUAL( listSummary[entryIndex]._activityHash, listDecoded[entryIndex]._activityHash );
        SW_EXPECT_EQUAL( listSummary[entryIndex]._segmentIndex, listDecoded[entryIndex]._segmentIndex );
        SW_EXPECT_EQUAL( listSummary[entryIndex]._phase, listDecoded[entryIndex]._phase );
    }
    bytes.pop_back();
    SW_EXPECT_FALSE( ScheduleSystem::decodeNetSummary( bytes, minute, listDecoded ) );
}

/**
 * @brief [ScheduleTest] 2D — 같은 일정 코드가 XY 평면의 타일 격자(2D 농장)에서 돈다: 벽을 돌아가는 길로 이동 시간을 재고, 모든 자리는 z = 0 이다
 */
SW_TEST_CASE( ScheduleTest, RunsOnA2DTileGridInTheXyPlane )
{
    using Internal                 = ScheduleTestInternal;
    constexpr const utf8* kFarmXml = R"(
<Schedules>
  <Place id="house" area="farm" position="2.5 6.5 0"/>
  <Place id="field" area="farm" position="15.5 6.5 0" radius="2"/>
  <Npc id="farmer" home="house" speed="1">
    <Routine id="work">
      <Block start="8:00" end="12:00" activity="WorkAt" place="field"/>
      <Block start="12:00" end="14:00" activity="Wander" place="field" every="30"/>
    </Routine>
  </Npc>
</Schedules>
)";
    ScheduleCatalog       catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kFarmXml, "farm" ) );
    NavGrid grid;
    grid.initialize( 20, 10, 1.0f, float3{} );
    for ( int32 cellY = 1; cellY <= 9; ++cellY )
    {
        grid.setBlocked( 10, cellY, true ); // y = 0 줄만 열린 울타리 — 아래로 돌아간다
    }
    GridPathfinder         pathfinder;
    NavGridSchedulePathing pathing( &grid, &pathfinder, SchedulePlane::XY );

    ScheduleSystem system;
    system.setPathing( &pathing, &pathing );
    system.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( 0, 6 ) );
    system.setNpcLod( 0, ScheduleLod::Near );
    const ScheduleSegment* pWork = nullptr;
    for ( const ScheduleSegment& segment : system.getPlan( 0 ) )
    {
        if ( segment._activity == hashed_string( "WorkAt" ) )
            pWork = &segment;
    }
    SW_ASSERT_NOT_NULL( pWork );
    SW_EXPECT_TRUE_MSG( pWork->_travelMinutes > 14, "the route goes around the fence, longer than the straight 13" );
    SW_EXPECT_EQUAL( 8 * 60 - pWork->_travelMinutes, pWork->_departMinute );

    system.advanceTo( pWork->_departMinute + pWork->_travelMinutes / 2 );
    ScheduleNpcView view = system.getNpcView( 0 );
    SW_EXPECT_TRUE( view._phase == ScheduleNpcPhase::Traveling );
    SW_EXPECT_EQUAL( 0.0f, view._location._position._z );
    SW_EXPECT_TRUE_MSG( view._location._position._y < 3.0f, "half way the farmer is down at the gap in the fence" );
    system.advanceTo( Internal::makeMinute( 0, 8 ) );
    SW_EXPECT_TRUE( Internal::isNear( float3{ 15.5f, 6.5f, 0.0f }, system.getNpcView( 0 )._location._position ) );
    // 13:30 까지 — 그 뒤로는 14:00 에 집에 닿도록 일찍 나선다.
    for ( int32 minute = Internal::makeMinute( 0, 12 ); minute < Internal::makeMinute( 0, 13, 30 ); minute += 10 )
    {
        system.advanceTo( minute );
        view = system.getNpcView( 0 );
        SW_EXPECT_EQUAL( 0.0f, view._location._position._z );
        SW_EXPECT_EQUAL( 0.0f, view._target._position._z );
        SW_EXPECT_TRUE( float3::getDistance( float3{ 15.5f, 6.5f, 0.0f }, view._target._position ) <= 2.0f + 1.0e-3f );
    }
}

/**
 * @brief [ScheduleTest] 활동 자리(벤치) — 앞 NPC 가 가까운 벤치, 다음 NPC 가 다른 벤치를 예약하고, 다시 세워도 같은 자리이며(멱등) 날이 바뀌면 지난 예약은 반납된다
 */
SW_TEST_CASE( ScheduleTest, SpotLocatorReservesDistinctBenchesIdempotently )
{
    using Internal = ScheduleTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( Internal::kTownXml, "town" ) );
    ScheduleSystem system;
    system.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( Internal::kMonday, 6 ) );
    const int32 abigail = system.findNpcIndex( "abigail" );
    const int32 emily   = system.findNpcIndex( "emily" );
    system.advanceTo( Internal::makeMinute( Internal::kMonday, 19 ) );
    SW_EXPECT_TRUE( Internal::isNear( float3{ 52.0f, 0.0f, 0.0f }, system.getNpcView( abigail )._target._position ) );
    SW_EXPECT_TRUE( Internal::isNear( float3{ 58.0f, 0.0f, 0.0f }, system.getNpcView( emily )._target._position ) );

    system.setWeather( "rain" ); // 모두 다시 세운다
    system.advanceTo( Internal::makeMinute( Internal::kMonday, 19, 1 ) );
    SW_EXPECT_TRUE( Internal::isNear( float3{ 52.0f, 0.0f, 0.0f }, system.getNpcView( abigail )._target._position ) );
    SW_EXPECT_TRUE( Internal::isNear( float3{ 58.0f, 0.0f, 0.0f }, system.getNpcView( emily )._target._position ) );
    ScheduleSaveState saved;
    system.fillSaveState( saved );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( saved._listReservation.size() ) );

    system.advanceTo( Internal::makeMinute( Internal::kMonday + 1, 19 ) );
    system.fillSaveState( saved );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( saved._listReservation.size() ) ); // 어제 것은 반납했다
}

/**
 * @brief [ScheduleTest] 애니메이션 훅은 화면 안 NPC 의 활동 시작 · 끝에만 불린다
 */
SW_TEST_CASE( ScheduleTest, AnimatorHookPlaysOnlyForNearNpcs )
{
    using Internal = ScheduleTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( Internal::kTownXml, "town" ) );
    Internal::RecordingAnimator animator;
    ScheduleSystem              system;
    system.setActivityAnimator( &animator );
    system.initialize( &catalog, Internal::makeSettings(), Internal::makeMinute( Internal::kMonday, 8 ) );
    const int32 pierre = system.findNpcIndex( "pierre" );
    system.setNpcLod( pierre, ScheduleLod::Near );
    animator._listStarted.clear();
    system.advanceTo( Internal::makeMinute( Internal::kMonday, 9, 5 ) );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( animator._listStarted.size() ) );
    SW_EXPECT_TRUE( animator._listStarted[0]._npc == hashed_string( "pierre" ) );
    SW_EXPECT_TRUE( animator._listStarted[0]._animation == hashed_string( "Sweep" ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( animator._listEnded.size() ) ); // 집의 StayHome 이 끝났다
    for ( const ScheduleActivityCue& cue : animator._listEnded )
    {
        SW_EXPECT_EQUAL( pierre, cue._npcIndex );
    }
}
