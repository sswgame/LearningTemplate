// LoadBotScheduleTest — 부하 봇의 보내기 방식(감사 O-12): 열린 루프는 기다림을 보내려던 시각부터 세고 늦게 보낸 단계의 지연을 그 시각부터 잰다,
// 닫힌 루프는 응답 뒤부터 센다, 시나리오는 `_pacing` 을 반드시 적는다. 봇 모듈(`OnlineLoadBotCore`)에 들어 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Programs/OnlineLoadBot/LoadBotScenario.h"
#include "Programs/OnlineLoadBot/LoadBotSchedule.h"

using namespace sw;

/** @brief [LoadBotScheduleTest] 열린 루프: 응답이 기다림보다 오래 걸려도 다음 단계의 지연은 보내려던 시각부터 잰다 — 닫힌 루프는 그 시간을 지연에서 숨긴다(coordinated omission) */
SW_TEST_CASE( LoadBotScheduleTest, OpenPacingMeasuresFromTargetSendTime )
{
    LoadBotSchedule open{ LoadBotPacing::Open };
    open.start( 0 );
    SW_EXPECT_EQUAL( int64( 0 ), open.beginStep( 0 ) );           // 첫 요청을 0 에 보냈다
    SW_EXPECT_EQUAL( int64( 1000 ), open.addWait( 1000, 5000 ) ); // 응답은 5000 에 왔다 — 다음은 1000 에 보내려던 것이다(이미 지났다)
    SW_EXPECT_EQUAL( int64( 1000 ), open.beginStep( 5000 ) );     // 지연은 1000 부터 잰다
    SW_EXPECT_EQUAL( int64( 2000 ), open.addWait( 1000, 5200 ) ); // 보내는 간격은 서버 속도와 무관하다
    SW_EXPECT_EQUAL( int64( 2000 ), open.beginStep( 6000 ) );     // 6000 에 보냈어도 2000 에 보내려던 것이다
    SW_EXPECT_EQUAL( int64( 7000 ), open.beginStep( 7000 ) );     // 기다림 없이 이어진 단계는 실제로 보낸 시각부터다(앞 응답이 있어야 보낸다)

    LoadBotSchedule closed{ LoadBotPacing::Closed };
    closed.start( 0 );
    SW_EXPECT_EQUAL( int64( 0 ), closed.beginStep( 0 ) );
    SW_EXPECT_EQUAL( int64( 6000 ), closed.addWait( 1000, 5000 ) ); // 응답 뒤부터 센다
    SW_EXPECT_EQUAL( int64( 6000 ), closed.beginStep( 6000 ) );
}

/** @brief [LoadBotScheduleTest] 열린 루프에서 시각표보다 일찍 깨면(틱 간격) 실제로 보낸 시각부터 잰다 — 음수 지연이 생기지 않는다 */
SW_TEST_CASE( LoadBotScheduleTest, OpenPacingNeverAnchorsAfterTheSend )
{
    LoadBotSchedule open{ LoadBotPacing::Open };
    open.start( 100 );
    (void)open.beginStep( 100 );
    SW_EXPECT_EQUAL( int64( 1100 ), open.addWait( 1000, 300 ) );
    SW_EXPECT_EQUAL( int64( 1100 ), open.beginStep( 1100 ) );
}

/** @brief [LoadBotScheduleTest] 시나리오는 `_pacing` 을 반드시 적고(숨은 기본값 없음), 모르는 값은 거절한다 */
SW_TEST_CASE( LoadBotScheduleTest, ScenarioRequiresPacing )
{
    constexpr string_view kSteps = R"("_listStep": [ { "_action": "logout" } ] })";
    SW_EXPECT_FALSE( LoadBotScenario::loadFromXMLText( string( R"({ "_name": "x", )" ) + string( kSteps ) ).has_value() );
    SW_EXPECT_FALSE( LoadBotScenario::loadFromXMLText( string( R"({ "_name": "x", "_pacing": "fast", )" ) + string( kSteps ) ).has_value() );
    const expected<LoadBotScenario> open = LoadBotScenario::loadFromXMLText( string( R"({ "_name": "x", "_pacing": "open", )" ) + string( kSteps ) );
    SW_REQUIRE_TRUE( open.has_value() );
    SW_EXPECT_TRUE( open->_pacing == LoadBotPacing::Open );
}
