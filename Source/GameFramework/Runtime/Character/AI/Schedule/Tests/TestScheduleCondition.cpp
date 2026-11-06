#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCondition.h"

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

    /** @brief 속성 글 @p pAttributes 로 조건을 읽습니다. 절은 데이터가 적는 줄임말에서 나온다. */
    sw::ScheduleCondition readCondition( const char* pAttributes )
    {
        sw::XMLDocument       document;
        const sw::string      text = sw::string( "<Routine id=\"test\" " ) + pAttributes + "/>";
        sw::ScheduleCondition condition;
        if ( document.parse( text ) )
            (void)condition.readFromNode( document.getRoot(), sw::ScheduleConditionSchema{}, "test", "test" ); // 틀린 읽기는 오류 로그가 시험을 깬다
        return condition;
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

    const sw::ScheduleCondition       condition = readCondition( R"(days="Sat,Sun" seasons="Spring")" );
    const sw::ScheduleConditionResult result    = condition.evaluate( context );
    SW_EXPECT_FALSE( result.isPassed() );
    SW_EXPECT_TRUE( result.hasFailed( sw::ScheduleConditionClause::Weekday ) );
    SW_EXPECT_TRUE( result.hasChecked( sw::ScheduleConditionClause::Season ) );
    SW_EXPECT_FALSE( result.hasFailed( sw::ScheduleConditionClause::Season ) );
    SW_EXPECT_FALSE( result.hasChecked( sw::ScheduleConditionClause::Weather ) );

    SW_EXPECT_TRUE( readCondition( R"(days="Sat,Sun,Mon" seasons="Spring")" ).matches( context ) );
}

/**
 * @brief [ScheduleConditionTest] 주는 계절의 날 1 ~ 7 이 첫째 주이고, 날씨 · 하루의 때는 목록 · 마스크에 든 값만 참이다
 */
SW_TEST_CASE( ScheduleConditionTest, WeekWeatherAndPhase )
{
    sw::ScheduleConditionContext context = makeSpringContext( getSeasonOrder() );

    const sw::ScheduleCondition secondWeek = readCondition( R"(weeks="2")" );
    context._dayOfSeason                   = 7;
    SW_EXPECT_FALSE( secondWeek.matches( context ) );
    context._dayOfSeason = 8;
    SW_EXPECT_TRUE( secondWeek.matches( context ) );
    context._dayOfSeason = 14;
    SW_EXPECT_TRUE( secondWeek.matches( context ) );
    context._dayOfSeason = 15;
    SW_EXPECT_FALSE( secondWeek.matches( context ) );

    const sw::ScheduleCondition rainyEvening = readCondition( R"(weathers="Rain,Storm" phases="Dusk,Night")" );
    context._weather                         = sw::hashed_string( "Storm" );
    context._phase                           = sw::DayPhase::Dusk;
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

    const sw::ScheduleCondition springToSummer = readCondition( R"(from="Spring 20" to="Summer 5")" );
    context._dayOfSeason                       = 19;
    SW_EXPECT_FALSE( springToSummer.matches( context ) );
    context._dayOfSeason = 20;
    SW_EXPECT_TRUE( springToSummer.matches( context ) );
    context._season      = sw::hashed_string( "Summer" );
    context._dayOfSeason = 5;
    SW_EXPECT_TRUE( springToSummer.matches( context ) );
    context._dayOfSeason = 6;
    SW_EXPECT_FALSE( springToSummer.matches( context ) );

    const sw::ScheduleCondition winterBreak = readCondition( R"(from="Winter 20" to="Spring 5")" );
    context._season                         = sw::hashed_string( "Winter" );
    context._dayOfSeason                    = 25;
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

    const sw::ScheduleCondition friendsOnly = readCondition( R"(minAffinity="4")" );
    context._affinity                       = 3;
    SW_EXPECT_TRUE( friendsOnly.evaluate( context ).hasFailed( sw::ScheduleConditionClause::Affinity ) );
    context._affinity = 4;
    SW_EXPECT_TRUE( friendsOnly.matches( context ) );
    context._affinity = 1000;
    SW_EXPECT_TRUE( friendsOnly.matches( context ) );

    const sw::ScheduleCondition coinFlip = readCondition( R"(chance="0.5")" );
    context._chanceKey                   = 0x1234u;
    uint32 passedDays                    = 0;
    for ( int32 day = 0; day < 1000; ++day )
    {
        context._day       = day;
        const bool bFirst  = coinFlip.matches( context );
        const bool bSecond = coinFlip.matches( context );
        SW_EXPECT_EQUAL( bFirst, bSecond );
        passedDays += bFirst ? 1u : 0u;
    }
    SW_EXPECT_TRUE_MSG( passedDays > 400u && passedDays < 600u, "chance 0.5 over 1000 days should pass about half" );

    const sw::ScheduleCondition never = readCondition( R"(chance="0")" );
    SW_EXPECT_FALSE( never.matches( context ) );
}

/**
 * @brief [ScheduleConditionTest] 읽기: 날짜 · 목록 · 때를 읽고, 뒤집힌 호감도 범위와 문법이 틀린 플래그 식은 오류다
 */
SW_TEST_CASE( ScheduleConditionTest, ReadsAttributesAndRejectsBrokenData )
{
    sw::ScheduleConditionSchema vocabulary;
    vocabulary._listSeason = getSeasonOrder();

    sw::XMLDocument document;
    SW_EXPECT_TRUE( document.parse( R"(<Routine id="rainy" days="Sat,Sun" weathers="rain" daysOfSeason="1,2,5-9" weeks="1,3" from="Spring 20" to="Summer 5" phases="Dawn,Day" minAffinity="2"/>)" ) );
    sw::ScheduleCondition condition;
    SW_EXPECT_TRUE( condition.readFromNode( document.getRoot(), vocabulary, "test", "rainy" ) );
    SW_EXPECT_EQUAL( size_t( 7 ), condition._listClause.size() ); // 요일 · 날씨 · 계절의 날 · 주 · 날짜 · 때 · 호감도

    // 범위는 풀어서 읽는다(1, 2, 5 ~ 9) — 맨 끝 날 9 는 참이고 3 은 거짓이다.
    const sw::ScheduleCondition  daysOfSeason = readCondition( R"(daysOfSeason="1,2,5-9")" );
    sw::ScheduleConditionContext context      = makeSpringContext( getSeasonOrder() );
    context._dayOfSeason                      = 9;
    SW_EXPECT_TRUE( daysOfSeason.matches( context ) );
    context._dayOfSeason = 3;
    SW_EXPECT_FALSE( daysOfSeason.matches( context ) );

    SW_EXPECT_LOG( "Schedule.AffinityRangeEmpty" );
    sw::XMLDocument inverted;
    SW_EXPECT_TRUE( inverted.parse( R"(<Routine id="never" minAffinity="5" maxAffinity="2"/>)" ) );
    sw::ScheduleCondition invertedCondition;
    SW_EXPECT_FALSE( invertedCondition.readFromNode( inverted.getRoot(), vocabulary, "test", "never" ) );

    SW_EXPECT_LOG( "Schedule.FlagsInvalid" );
    SW_EXPECT_LOG( "ConditionExpr.Invalid" ); // 식을 읽는 쪽이 어디가 틀렸는지 먼저 알린다
    sw::XMLDocument brokenFlags;
    SW_EXPECT_TRUE( brokenFlags.parse( R"(<Routine id="broken" flags="bridgeFixed &amp;&amp;"/>)" ) );
    sw::ScheduleCondition brokenCondition;
    SW_EXPECT_FALSE( brokenCondition.readFromNode( brokenFlags.getRoot(), vocabulary, "test", "broken" ) );
}

/**
 * @brief [ScheduleConditionTest] 줄임말과 같은 이름을 식으로 적어도 값이 같다 — 스케줄이 공통 어휘를 쓴다
 */
SW_TEST_CASE( ScheduleConditionTest, ShorthandCompilesToSharedVocabulary )
{
    sw::ScheduleConditionContext context = makeSpringContext( getSeasonOrder() );
    sw::GameplayContext          world;
    context.fillGameplayContext( world );

    sw::ConditionExpr expr;
    SW_REQUIRE_TRUE( sw::ConditionExpr::compile( "weather(Sunny, Rain) && season(Spring) && weekday(Mon) && dayOfSeason(3) && week(1) && !phase(Dusk)", "test", expr ) );
    SW_EXPECT_TRUE( expr.evaluate( world ) );
    SW_REQUIRE_TRUE( sw::ConditionExpr::compile( "date(fromSeason=Spring, fromDay=4)", "test", expr ) );
    SW_EXPECT_FALSE( expr.evaluate( world ) );

    context._affinity = 5;
    SW_REQUIRE_TRUE( sw::ConditionExpr::compile( "affinity >= 4", "test", expr ) );
    SW_EXPECT_TRUE( expr.evaluate( world ) ); // 문맥은 복사가 아니라 주소로 든다
}
