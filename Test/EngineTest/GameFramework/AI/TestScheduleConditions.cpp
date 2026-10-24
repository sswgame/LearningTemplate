#include "pch.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCondition.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"

#include "TestFramework/TestFramework.h"

// NPC 일정 조건의 확장 — 계절의 날 범위 · 주 · 계절을 넘는 날짜 범위(해를 넘는 것 포함) · 호감도 단계, 자정을 넘는 칸, 호감도가 계획을 다시 세우고 저장에 실리는 것.

using namespace sw;

namespace
{
    struct ScheduleConditionTestInternal
    {
        static vector<hashed_string> makeSeasonOrder() { return { hashed_string( "Spring" ), hashed_string( "Summer" ), hashed_string( "Fall" ), hashed_string( "Winter" ) }; }

        /** @brief `<Routine ... />` 한 줄에서 조건을 읽습니다. 어휘는 비워(이름 검사 안 함) 둡니다. */
        static ScheduleCondition readCondition( string_view attributes )
        {
            const string      xml = "<Routine id=\"r\" " + string( attributes ) + "/>";
            XMLDocument       document;
            ScheduleCondition condition;
            // 틀린 조건을 읽는 테스트도 이 도우미를 쓴다 — 결과는 각 테스트가 로그로 본다.
            const bool bParsed = document.parse( xml, "condition" ) && condition.readFromNode( document.getRoot( "Routine" ), ScheduleConditionVocabulary{}, "condition", "r" );
            (void)bParsed;
            return condition;
        }

        static ScheduleConditionContext makeContext( const vector<hashed_string>& listSeason, const utf8* pSeason, int32 dayOfSeason, int32 affinity = 0 )
        {
            ScheduleConditionContext context;
            context._pSeasonOrder = &listSeason;
            context._season       = hashed_string( pSeason );
            context._dayOfSeason  = dayOfSeason;
            context._affinity     = affinity;
            return context;
        }

        static constexpr const utf8* kXML = R"(
<Schedules>
  <Calendar days="Mon,Tue,Wed,Thu,Fri,Sat,Sun" seasons="Spring,Summer" weathers="sunny"/>
  <Place id="home" area="a" position="0 0 0"/>
  <Place id="cafe" area="a" position="10 0 0"/>
  <Place id="park" area="a" position="20 0 0"/>
  <Archetype id="villager"><Routine id="night"><Block start="22:00" end="6:00" overnight="true" activity="Sleep"/></Routine></Archetype>
  <Npc id="abigail" archetype="villager" home="home" speed="1">
    <Routine id="cafe_friend" minAffinity="4"><Block start="12:00" end="14:00" activity="GoTo" place="cafe"/></Routine>
    <Routine id="park_default"><Block start="12:00" end="14:00" activity="GoTo" place="park"/></Routine>
  </Npc>
</Schedules>
)";

        static ScheduleSystemSettings makeSettings()
        {
            ScheduleSystemSettings settings;
            settings._clock._listSeason    = { hashed_string( "Spring" ), hashed_string( "Summer" ) };
            settings._clock._daysPerSeason = 28;
            settings._seed                 = 5u;
            return settings;
        }
    };
} // namespace

/**
 * @brief [ScheduleConditionTest] 계절의 날은 범위(5-9)를 풀어 읽고, 거꾸로 된 범위 · 0 · 범위 밖은 경고하고 뺀다
 */
SW_TEST_CASE( ScheduleConditionTest, DaysOfSeasonAcceptRanges )
{
    using Internal                 = ScheduleConditionTestInternal;
    const ScheduleCondition ranged = Internal::readCondition( "daysOfSeason=\"1,5-7,20\"" );
    SW_ASSERT_EQUAL( size_t( 5 ), ranged._listDayOfSeason.size() );
    const vector<hashed_string> listSeason = Internal::makeSeasonOrder();
    for ( const int32 day : { 1, 5, 6, 7, 20 } )
    {
        SW_EXPECT_TRUE( ranged.matches( Internal::makeContext( listSeason, "Spring", day ) ) );
    }
    for ( const int32 day : { 2, 4, 8, 19, 21 } )
    {
        SW_EXPECT_FALSE( ranged.matches( Internal::makeContext( listSeason, "Spring", day ) ) );
    }

    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "bad day ranges" );
        const ScheduleCondition      bad = Internal::readCondition( "daysOfSeason=\"9-5,0,400,x\"" );
        SW_EXPECT_TRUE( bad._listDayOfSeason.empty() );
    }
    SW_EXPECT_TRUE( logs.countWithID( "Schedule.DayListInvalid" ) >= 4 );
}

/**
 * @brief [ScheduleConditionTest] 주는 계절의 7 일 묶음(1 ~ 7 일 = 첫째 주)이다
 */
SW_TEST_CASE( ScheduleConditionTest, WeeksCountFromTheSeasonStart )
{
    using Internal                         = ScheduleConditionTestInternal;
    const ScheduleCondition     condition  = Internal::readCondition( "weeks=\"1,3\"" );
    const vector<hashed_string> listSeason = Internal::makeSeasonOrder();
    SW_EXPECT_TRUE( condition.matches( Internal::makeContext( listSeason, "Spring", 1 ) ) );
    SW_EXPECT_TRUE( condition.matches( Internal::makeContext( listSeason, "Spring", 7 ) ) );
    SW_EXPECT_FALSE( condition.matches( Internal::makeContext( listSeason, "Spring", 8 ) ) );
    SW_EXPECT_FALSE( condition.matches( Internal::makeContext( listSeason, "Spring", 14 ) ) );
    SW_EXPECT_TRUE( condition.matches( Internal::makeContext( listSeason, "Summer", 15 ) ) );
    SW_EXPECT_TRUE( condition.matches( Internal::makeContext( listSeason, "Spring", 21 ) ) );
    SW_EXPECT_FALSE( condition.matches( Internal::makeContext( listSeason, "Spring", 22 ) ) );
    SW_EXPECT_TRUE( condition.evaluate( Internal::makeContext( listSeason, "Spring", 8 ) ).hasFailed( ScheduleConditionClause::Week ) );
}

/**
 * @brief [ScheduleConditionTest] 날짜 범위(from · to)는 계절을 넘나들고, from 이 to 보다 늦으면 해를 넘는 범위이며, 한쪽만 적으면 열려 있다
 */
SW_TEST_CASE( ScheduleConditionTest, DateRangesCrossSeasonsAndTheYear )
{
    using Internal                         = ScheduleConditionTestInternal;
    const vector<hashed_string> listSeason = Internal::makeSeasonOrder();

    const ScheduleCondition across = Internal::readCondition( "from=\"Spring 20\" to=\"Summer 5\"" );
    SW_EXPECT_FALSE( across.matches( Internal::makeContext( listSeason, "Spring", 19 ) ) );
    SW_EXPECT_TRUE( across.matches( Internal::makeContext( listSeason, "Spring", 20 ) ) );
    SW_EXPECT_TRUE( across.matches( Internal::makeContext( listSeason, "Spring", 28 ) ) );
    SW_EXPECT_TRUE( across.matches( Internal::makeContext( listSeason, "Summer", 1 ) ) );
    SW_EXPECT_TRUE( across.matches( Internal::makeContext( listSeason, "Summer", 5 ) ) );
    SW_EXPECT_FALSE( across.matches( Internal::makeContext( listSeason, "Summer", 6 ) ) );
    SW_EXPECT_FALSE( across.matches( Internal::makeContext( listSeason, "Fall", 1 ) ) );

    const ScheduleCondition wrapsYear = Internal::readCondition( "from=\"Winter 20\" to=\"Spring 5\"" );
    SW_EXPECT_TRUE( wrapsYear.matches( Internal::makeContext( listSeason, "Winter", 25 ) ) );
    SW_EXPECT_TRUE( wrapsYear.matches( Internal::makeContext( listSeason, "Spring", 3 ) ) );
    SW_EXPECT_TRUE( wrapsYear.matches( Internal::makeContext( listSeason, "Spring", 5 ) ) );
    SW_EXPECT_FALSE( wrapsYear.matches( Internal::makeContext( listSeason, "Spring", 6 ) ) );
    SW_EXPECT_FALSE( wrapsYear.matches( Internal::makeContext( listSeason, "Summer", 1 ) ) );
    SW_EXPECT_FALSE( wrapsYear.matches( Internal::makeContext( listSeason, "Winter", 19 ) ) );

    const ScheduleCondition fromOnly = Internal::readCondition( "from=\"Summer 10\"" );
    SW_EXPECT_FALSE( fromOnly.matches( Internal::makeContext( listSeason, "Spring", 28 ) ) );
    SW_EXPECT_TRUE( fromOnly.matches( Internal::makeContext( listSeason, "Summer", 10 ) ) );
    SW_EXPECT_TRUE( fromOnly.matches( Internal::makeContext( listSeason, "Winter", 1 ) ) );
    const ScheduleCondition toOnly = Internal::readCondition( "to=\"Spring 3\"" );
    SW_EXPECT_TRUE( toOnly.matches( Internal::makeContext( listSeason, "Spring", 3 ) ) );
    SW_EXPECT_FALSE( toOnly.matches( Internal::makeContext( listSeason, "Spring", 4 ) ) );

    // 계절 순서를 모르는 쪽(조건을 판정하는 다른 시스템)은 날짜 범위를 만족하지 못한다.
    ScheduleConditionContext noOrder = Internal::makeContext( listSeason, "Spring", 22 );
    noOrder._pSeasonOrder            = nullptr;
    SW_EXPECT_FALSE( across.matches( noOrder ) );
    SW_EXPECT_FALSE( across.isEmpty() );
}

/**
 * @brief [ScheduleConditionTest] 잘못된 날짜 · 계절 이름 · 거꾸로 된 호감도 범위는 경고한다
 */
SW_TEST_CASE( ScheduleConditionTest, BadDatesAndAffinityRangesWarn )
{
    using Internal = ScheduleConditionTestInternal;
    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "bad date and affinity data" );
        const ScheduleCondition      noDay = Internal::readCondition( "from=\"Spring\"" );
        const ScheduleCondition      zero  = Internal::readCondition( "to=\"Spring 0\"" );
        const ScheduleCondition      flip  = Internal::readCondition( "minAffinity=\"8\" maxAffinity=\"3\"" );
        SW_EXPECT_TRUE( noDay._fromDay == 0 && zero._toDay == 0 );
        SW_EXPECT_FALSE( flip.matches( Internal::makeContext( Internal::makeSeasonOrder(), "Spring", 1, 5 ) ) );
    }
    SW_EXPECT_TRUE( logs.countWithID( "Schedule.DateInvalid" ) >= 2 );
    SW_EXPECT_TRUE( logs.countWithID( "Schedule.AffinityRangeEmpty" ) >= 1 );
}

/**
 * @brief [ScheduleConditionTest] 호감도 단계 — 하한 · 상한 · 둘 다, 적지 않으면 늘 참이다
 */
SW_TEST_CASE( ScheduleConditionTest, AffinityRangeBoundsTheRelationshipStage )
{
    using Internal                         = ScheduleConditionTestInternal;
    const vector<hashed_string> listSeason = Internal::makeSeasonOrder();
    const ScheduleCondition     minOnly    = Internal::readCondition( "minAffinity=\"4\"" );
    SW_EXPECT_FALSE( minOnly.matches( Internal::makeContext( listSeason, "Spring", 1, 3 ) ) );
    SW_EXPECT_TRUE( minOnly.matches( Internal::makeContext( listSeason, "Spring", 1, 4 ) ) );
    SW_EXPECT_TRUE( minOnly.matches( Internal::makeContext( listSeason, "Spring", 1, 10 ) ) );
    const ScheduleCondition between = Internal::readCondition( "minAffinity=\"4\" maxAffinity=\"7\"" );
    SW_EXPECT_TRUE( between.matches( Internal::makeContext( listSeason, "Spring", 1, 7 ) ) );
    SW_EXPECT_FALSE( between.matches( Internal::makeContext( listSeason, "Spring", 1, 8 ) ) );
    SW_EXPECT_TRUE( between.evaluate( Internal::makeContext( listSeason, "Spring", 1, 8 ) ).hasFailed( ScheduleConditionClause::Affinity ) );
    const ScheduleCondition negative = Internal::readCondition( "maxAffinity=\"-1\"" ); // 적대 단계
    SW_EXPECT_TRUE( negative.matches( Internal::makeContext( listSeason, "Spring", 1, -3 ) ) );
    SW_EXPECT_FALSE( negative.matches( Internal::makeContext( listSeason, "Spring", 1, 0 ) ) );
    SW_EXPECT_TRUE( Internal::readCondition( "" ).isEmpty() );
    SW_EXPECT_STREQ( "weeks", toString( ScheduleConditionClause::Week ) );
}

/**
 * @brief [ScheduleConditionTest] 자정을 넘는 칸(overnight)은 두 칸으로 나뉘고, 표시가 없는 start ≥ end 는 여전히 경고하고 뺀다
 */
SW_TEST_CASE( ScheduleConditionTest, OvernightBlockSplitsAtMidnight )
{
    constexpr const utf8*    kXML = R"(
<Schedules>
  <Place id="home" position="0 0 0"/>
  <Npc id="x" home="home">
    <Routine id="night">
      <Block start="22:00" end="6:00" overnight="true" activity="Sleep"/>
      <Block start="9:00" end="8:00" activity="StayHome"/>
    </Routine>
    <Routine id="evening"><Block start="18:00" end="0:00" overnight="true" activity="StayHome"/></Routine>
  </Npc>
</Schedules>
)";
    ScheduleCatalog          catalog;
    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "a backwards block without overnight" );
        SW_ASSERT_TRUE( catalog.loadFromXMLText( kXML, "night" ) );
    }
    SW_EXPECT_TRUE( logs.countWithID( "Schedule.BlockTimeOrder" ) >= 1 );
    const ScheduleNpcDef* pNpc = catalog.findNpc( "x" );
    SW_ASSERT_NOT_NULL( pNpc );
    SW_ASSERT_EQUAL( size_t( 2 ), pNpc->_listRoutine.size() );
    const vector<ScheduleBlockDef>& listBlock = pNpc->_listRoutine[0]._listBlock;
    SW_ASSERT_EQUAL( size_t( 2 ), listBlock.size() ); // Sleep 둘(표시 없는 9:00-8:00 은 뺐다)
    SW_EXPECT_EQUAL( 22 * 60, listBlock[0]._startMinute );
    SW_EXPECT_EQUAL( kScheduleMinutesPerDay, listBlock[0]._endMinute );
    SW_EXPECT_EQUAL( 0, listBlock[1]._startMinute );
    SW_EXPECT_EQUAL( 6 * 60, listBlock[1]._endMinute );
    SW_EXPECT_TRUE( listBlock[1]._activity == hashed_string( "Sleep" ) );
    const vector<ScheduleBlockDef>& listEvening = pNpc->_listRoutine[1]._listBlock;
    SW_ASSERT_EQUAL( size_t( 1 ), listEvening.size() ); // 끝 0:00 은 24:00 이고 새벽 쪽 칸은 없다
    SW_EXPECT_EQUAL( 18 * 60, listEvening[0]._startMinute );
    SW_EXPECT_EQUAL( kScheduleMinutesPerDay, listEvening[0]._endMinute );
}

/**
 * @brief [ScheduleConditionTest] 일정 시스템 — 자정을 넘는 잠은 밤과 새벽에 모두 Sleep 이고, 호감도가 바뀌면 계획을 다시 세워 다른 장소로 가며, 저장에 실린다
 */
SW_TEST_CASE( ScheduleConditionTest, AffinityReplansAndOvernightSleepRuns )
{
    using Internal = ScheduleConditionTestInternal;
    ScheduleCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXMLText( Internal::kXML, "town" ) );
    ScheduleSystem system;
    SW_ASSERT_TRUE( system.initialize( &catalog, Internal::makeSettings(), 0 ) );
    const int32 abigail = system.findNpcIndex( "abigail" );
    SW_ASSERT_TRUE( abigail >= 0 );

    system.advanceTo( 3 * 60 );
    SW_EXPECT_TRUE( system.getNpcView( abigail )._activity == hashed_string( "Sleep" ) ); // 어제 22:00 에 시작한 잠의 새벽 쪽
    system.advanceTo( 12 * 60 + 30 );
    SW_EXPECT_NEAR_EQUAL( 20.0f, system.getNpcView( abigail )._target._position._x, 1.0e-3f ); // 호감도 0 — 먼저 적은 cafe_friend 는 조건 밖이라 공원
    SW_EXPECT_EQUAL( 0, system.getNpcAffinity( abigail ) );

    system.setNpcAffinity( abigail, 5 );
    system.advanceTo( 12 * 60 + 31 );
    SW_EXPECT_NEAR_EQUAL( 10.0f, system.getNpcView( abigail )._target._position._x, 1.0e-3f ); // 호감도 5 — 카페로 계획을 다시 세웠다

    ScheduleSaveState saved;
    system.fillSaveState( saved );
    SW_ASSERT_EQUAL( size_t( 1 ), saved._listNpc.size() );
    SW_EXPECT_EQUAL( 5, saved._listNpc[0]._affinity );

    ScheduleSystem restored;
    SW_ASSERT_TRUE( restored.initialize( &catalog, Internal::makeSettings(), 0 ) );
    restored.restoreSaveState( saved );
    SW_EXPECT_EQUAL( 5, restored.getNpcAffinity( restored.findNpcIndex( "abigail" ) ) );

    system.advanceTo( 23 * 60 );
    SW_EXPECT_TRUE( system.getNpcView( abigail )._activity == hashed_string( "Sleep" ) ); // 같은 잠의 밤 쪽
    SW_EXPECT_EQUAL( 0, system.getNpcAffinity( 99 ) );                                    // 없는 NPC
}
