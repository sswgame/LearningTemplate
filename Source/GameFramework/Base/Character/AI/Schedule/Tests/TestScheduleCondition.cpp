#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleCondition.h"

// ScheduleConditionTest — NPC 일정 조건(루틴 · 일정 블록 · 축제 · 약속이 같이 쓴다)의 절마다 판정과 읽기 오류. 세계 상태는 문맥 구조로 직접 준다(nogpu).

namespace
{
    /** @brief 봄 3 일(월요일, 맑음, 낮)인 문맥입니다. 계절 순서는 봄 · 여름 · 가을 · 겨울입니다. */
    sw::ScheduleConditionContext makeSpringContext( const sw::vector<sw::hashed_string>& listSeasonOrder )
    {
        sw::ScheduleConditionContext context;
        context._weekday      = sw::hashed_string( "Mon" );
        context._season       = sw::hashed_string( "Spring" );
        context._weather      = sw::hashed_string( "Sunny" );
        context._dayOfSeason  = 3;
        context._day          = 2;
        context._pSeasonOrder = &listSeasonOrder;
        context._phase        = sw::DayPhase::Day;
        return context;
    }

    const sw::vector<sw::hashed_string>& getSeasonOrder()
    {
        static const sw::vector<sw::hashed_string> s_listSeason{ sw::hashed_string( "Spring" ), sw::hashed_string( "Summer" ), sw::hashed_string( "Fall" ),
                                                                 sw::hashed_string( "Winter" ) };
        return s_listSeason;
    }
} // namespace

/**
 * @brief [ScheduleConditionTest] 적지 않은 절은 늘 참이고, 적은 절은 본 절과 막은 절을 비트로 남긴다
 * @details "왜 여기 있나" 추적이 막은 절을 보여 주므로, 요일만 틀리면 요일 절만 실패로 남고 계절 절은 통과로 남아야 한다.
 */
SW_TEST_CASE( ScheduleConditionTest, ReportsCheckedAndFailedClauses )
{
    const sw::ScheduleConditionContext context = makeSpringContext( getSeasonOrder() );

    const sw::ScheduleCondition empty;
    SW_EXPECT_TRUE( empty.isEmpty() );
    SW_EXPECT_TRUE( empty.matches( context ) );

    sw::ScheduleCondition condition;
    condition._listWeekday                   = { sw::hashed_string( "Sat" ), sw::hashed_string( "Sun" ) };
    condition._listSeason                    = { sw::hashed_string( "Spring" ) };
    const sw::ScheduleConditionResult result = condition.evaluate( context );
    SW_EXPECT_FALSE( result.isPassed() );
    SW_EXPECT_TRUE( result.hasFailed( sw::ScheduleConditionClause::Weekday ) );
    SW_EXPECT_TRUE( result.hasChecked( sw::ScheduleConditionClause::Season ) );
    SW_EXPECT_FALSE( result.hasFailed( sw::ScheduleConditionClause::Season ) );
    SW_EXPECT_FALSE( result.hasChecked( sw::ScheduleConditionClause::Weather ) );

    condition._listWeekday.push_back( sw::hashed_string( "Mon" ) );
    SW_EXPECT_TRUE( condition.matches( context ) );
}

/**
 * @brief [ScheduleConditionTest] 주는 계절의 날 1 ~ 7 이 첫째 주이고, 날씨 · 하루의 때는 목록 · 마스크에 든 값만 참이다
 */
SW_TEST_CASE( ScheduleConditionTest, WeekWeatherAndPhase )
{
    sw::ScheduleConditionContext context = makeSpringContext( getSeasonOrder() );

    sw::ScheduleCondition secondWeek;
    secondWeek._listWeek = { 2 };
    context._dayOfSeason = 7;
    SW_EXPECT_FALSE( secondWeek.matches( context ) );
    context._dayOfSeason = 8;
    SW_EXPECT_TRUE( secondWeek.matches( context ) );
    context._dayOfSeason = 14;
    SW_EXPECT_TRUE( secondWeek.matches( context ) );
    context._dayOfSeason = 15;
    SW_EXPECT_FALSE( secondWeek.matches( context ) );

    sw::ScheduleCondition rainyEvening;
    rainyEvening._listWeather = { sw::hashed_string( "Rain" ), sw::hashed_string( "Storm" ) };
    rainyEvening._phaseMask   = sw::makeDayPhaseBit( sw::DayPhase::Dusk ) | sw::makeDayPhaseBit( sw::DayPhase::Night );
    context._weather          = sw::hashed_string( "Storm" );
    context._phase            = sw::DayPhase::Dusk;
    SW_EXPECT_TRUE( rainyEvening.matches( context ) );
    context._phase = sw::DayPhase::Day;
    SW_EXPECT_TRUE( rainyEvening.evaluate( context ).hasFailed( sw::ScheduleConditionClause::Phase ) );
    context._phase   = sw::DayPhase::Night;
    context._weather = sw::hashed_string( "Sunny" );
    SW_EXPECT_TRUE( rainyEvening.evaluate( context ).hasFailed( sw::ScheduleConditionClause::Weather ) );
}

/**
 * @brief [ScheduleConditionTest] 날짜 범위는 계절을 넘나들고, 해를 넘겨 감싸며(겨울 20 ~ 봄 5), 계절 순서를 모르면 거짓이다
 */
SW_TEST_CASE( ScheduleConditionTest, DateRangeAcrossSeasonsAndYear )
{
    sw::ScheduleConditionContext context = makeSpringContext( getSeasonOrder() );

    sw::ScheduleCondition springToSummer;
    springToSummer._fromSeason = sw::hashed_string( "Spring" );
    springToSummer._fromDay    = 20;
    springToSummer._toSeason   = sw::hashed_string( "Summer" );
    springToSummer._toDay      = 5;
    context._dayOfSeason       = 19;
    SW_EXPECT_FALSE( springToSummer.matches( context ) );
    context._dayOfSeason = 20;
    SW_EXPECT_TRUE( springToSummer.matches( context ) );
    context._season      = sw::hashed_string( "Summer" );
    context._dayOfSeason = 5;
    SW_EXPECT_TRUE( springToSummer.matches( context ) );
    context._dayOfSeason = 6;
    SW_EXPECT_FALSE( springToSummer.matches( context ) );

    sw::ScheduleCondition winterBreak;
    winterBreak._fromSeason = sw::hashed_string( "Winter" );
    winterBreak._fromDay    = 20;
    winterBreak._toSeason   = sw::hashed_string( "Spring" );
    winterBreak._toDay      = 5;
    context._season         = sw::hashed_string( "Winter" );
    context._dayOfSeason    = 25;
    SW_EXPECT_TRUE( winterBreak.matches( context ) );
    context._season      = sw::hashed_string( "Spring" );
    context._dayOfSeason = 3;
    SW_EXPECT_TRUE( winterBreak.matches( context ) );
    context._season = sw::hashed_string( "Fall" );
    SW_EXPECT_FALSE( winterBreak.matches( context ) );

    context._season       = sw::hashed_string( "Spring" );
    context._pSeasonOrder = nullptr;
    SW_EXPECT_TRUE( winterBreak.evaluate( context ).hasFailed( sw::ScheduleConditionClause::DateRange ) );
}

/**
 * @brief [ScheduleConditionTest] 호감도는 한쪽만 적어도 범위이고, 확률은 같은 날 · 같은 키면 같은 답이며 전체 비율이 확률에 가깝다
 * @details 확률은 날과 키의 해시라 저장 · 재생 · 화면 밖 시뮬레이션이 같은 결과를 낸다. 0.5 로 1000 날을 굴리면 대략 절반이 참이다(해시가 고르다).
 */
SW_TEST_CASE( ScheduleConditionTest, AffinityRangeAndDeterministicChance )
{
    sw::ScheduleConditionContext context = makeSpringContext( getSeasonOrder() );

    sw::ScheduleCondition friendsOnly;
    friendsOnly._minAffinity = 4;
    context._affinity        = 3;
    SW_EXPECT_TRUE( friendsOnly.evaluate( context ).hasFailed( sw::ScheduleConditionClause::Affinity ) );
    context._affinity = 4;
    SW_EXPECT_TRUE( friendsOnly.matches( context ) );
    context._affinity = 1000;
    SW_EXPECT_TRUE( friendsOnly.matches( context ) );

    sw::ScheduleCondition coinFlip;
    coinFlip._chance   = 0.5f;
    context._chanceKey = 0x1234u;
    uint32 passedDays  = 0;
    for ( int32 day = 0; day < 1000; ++day )
    {
        context._day       = day;
        const bool bFirst  = coinFlip.matches( context );
        const bool bSecond = coinFlip.matches( context );
        SW_EXPECT_EQUAL( bFirst, bSecond );
        passedDays += bFirst ? 1u : 0u;
    }
    SW_EXPECT_TRUE_MSG( passedDays > 400u && passedDays < 600u, "chance 0.5 over 1000 days should pass about half" );

    sw::ScheduleCondition never;
    never._chance = 0.0f;
    SW_EXPECT_FALSE( never.matches( context ) );
}

/**
 * @brief [ScheduleConditionTest] 읽기: 날짜 · 목록 · 때를 읽고, 뒤집힌 호감도 범위와 문법이 틀린 플래그 식은 오류다
 */
SW_TEST_CASE( ScheduleConditionTest, ReadsAttributesAndRejectsBrokenData )
{
    sw::ScheduleConditionVocabulary vocabulary;
    vocabulary._listSeason = getSeasonOrder();

    sw::XMLDocument document;
    SW_EXPECT_TRUE( document.parse( R"(<Routine id="rainy" days="Sat,Sun" weathers="rain" daysOfSeason="1,2,5-9" weeks="1,3" from="Spring 20" to="Summer 5" phases="Dawn,Day" minAffinity="2"/>)" ) );
    sw::ScheduleCondition condition;
    SW_EXPECT_TRUE( condition.readFromNode( document.getRoot(), vocabulary, "test", "rainy" ) );
    SW_EXPECT_EQUAL( size_t( 2 ), condition._listWeekday.size() );
    SW_EXPECT_EQUAL( size_t( 7 ), condition._listDayOfSeason.size() ); // 1, 2, 5 ~ 9
    SW_EXPECT_EQUAL( 20, condition._fromDay );
    SW_EXPECT_EQUAL( 5, condition._toDay );
    SW_EXPECT_EQUAL( 2, condition._minAffinity );
    SW_EXPECT_EQUAL( uint8( sw::makeDayPhaseBit( sw::DayPhase::Dawn ) | sw::makeDayPhaseBit( sw::DayPhase::Day ) ), condition._phaseMask );

    SW_EXPECT_LOG( "Schedule.AffinityRangeEmpty" );
    sw::XMLDocument inverted;
    SW_EXPECT_TRUE( inverted.parse( R"(<Routine id="never" minAffinity="5" maxAffinity="2"/>)" ) );
    sw::ScheduleCondition invertedCondition;
    SW_EXPECT_FALSE( invertedCondition.readFromNode( inverted.getRoot(), vocabulary, "test", "never" ) );

    SW_EXPECT_LOG( "Schedule.FlagsInvalid" );
    SW_EXPECT_LOG( "Flags.InvalidCondition" ); // 식을 읽는 GameFlags 가 어디가 틀렸는지 먼저 경고한다
    sw::XMLDocument brokenFlags;
    SW_EXPECT_TRUE( brokenFlags.parse( R"(<Routine id="broken" flags="bridgeFixed &amp;&amp;"/>)" ) );
    sw::ScheduleCondition brokenCondition;
    SW_EXPECT_FALSE( brokenCondition.readFromNode( brokenFlags.getRoot(), vocabulary, "test", "broken" ) );
}
