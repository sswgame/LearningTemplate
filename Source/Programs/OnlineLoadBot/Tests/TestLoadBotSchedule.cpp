// LoadBotScheduleTest — 부하 봇의 보내기 방식(감사 O-12): 열린 루프는 기다림을 보내려던 시각부터 세고 늦게 보낸 단계의 지연을 그 시각부터 잰다,
// 닫힌 루프는 응답 뒤부터 센다, 시나리오는 `_pacing` · `_executor` 를 반드시 적는다. 도착률 실행기는 서버 속도와 무관하게 정한 속도로 봇을 띄우고 풀 밖의 도착은 버린다(가짜 시계),
// `pick` 의 비율은 자식마다 `_weight` 로 적는다. 봇 모듈(`OnlineLoadBotCore`)에 들어 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Network/NetTypes.h"
#include "Core/Network/Tests/LoopbackStreamTransport.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/TestFramework/TestFramework.h"

#include "Programs/OnlineLoadBot/LoadBotRunner.h"
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
    constexpr string_view kSteps = R"("_executor": "per_vu_iterations", "_listStep": [ { "_action": "logout" } ] })";
    SW_EXPECT_FALSE( LoadBotScenario::loadFromXMLText( string( R"({ "_name": "x", )" ).append( kSteps ) ).has_value() );
    SW_EXPECT_FALSE( LoadBotScenario::loadFromXMLText( string( R"({ "_name": "x", "_pacing": "fast", )" ).append( kSteps ) ).has_value() );
    const expected<LoadBotScenario> open = LoadBotScenario::loadFromXMLText( string( R"({ "_name": "x", "_pacing": "open", )" ).append( kSteps ) );
    SW_REQUIRE_TRUE( open.has_value() );
    SW_EXPECT_TRUE( open->_pacing == LoadBotPacing::Open );
}

/** @brief [LoadBotScheduleTest] 시나리오는 `_executor` 를 반드시 적고, 도착률이면 속도와 끝 대기를 적는다(숨은 기본값 없음). 닫힌 모델에 속도를 적으면 오류다 */
SW_TEST_CASE( LoadBotScheduleTest, ScenarioRequiresExecutor )
{
    constexpr string_view kHead  = R"({ "_name": "x", "_pacing": "open", )";
    constexpr string_view kSteps = R"("_listStep": [ { "_action": "logout" } ] })";
    SW_EXPECT_FALSE( LoadBotScenario::loadFromXMLText( string( kHead ).append( kSteps ) ).has_value() );
    SW_EXPECT_FALSE( LoadBotScenario::loadFromXMLText( string( kHead ).append( R"("_executor": "constant_arrival_rate", )" ).append( kSteps ) ).has_value() );
    SW_EXPECT_FALSE( LoadBotScenario::loadFromXMLText( string( kHead ).append( R"("_executor": "per_vu_iterations", "_arrivalRatePerSecond": 5, )" ).append( kSteps ) ).has_value() );
    const expected<LoadBotScenario> arrival =
        LoadBotScenario::loadFromXMLText( string( kHead ).append( R"("_executor": "constant_arrival_rate", "_arrivalRatePerSecond": 5, "_gracefulStopMs": 100, )" ).append( kSteps ) );
    SW_REQUIRE_TRUE( arrival.has_value() );
    SW_EXPECT_TRUE( arrival->_executor == LoadBotExecutor::ConstantArrivalRate );
    SW_EXPECT_EQUAL( 5, arrival->_arrivalRatePerSecond );
    SW_EXPECT_EQUAL( int64( 100 ), arrival->_gracefulStopMs );
}

/** @brief [LoadBotScheduleTest] `pick` 의 자식은 모두 `_weight`(1 이상)를 적고, 그 밖의 단계는 `_weight` 를 적지 못한다 — 행동 비율은 데이터가 정한다 */
SW_TEST_CASE( LoadBotScheduleTest, PickRequiresWeights )
{
    constexpr string_view kHead = R"({ "_name": "x", "_pacing": "closed", "_executor": "per_vu_iterations", "_listStep": [ )";
    SW_EXPECT_FALSE( LoadBotScenario::loadFromXMLText( string( kHead ).append( R"({ "_action": "pick", "_listStep": [ { "_action": "logout" } ] } ] })" ) ).has_value() );
    SW_EXPECT_FALSE( LoadBotScenario::loadFromXMLText( string( kHead ).append( R"({ "_action": "pick", "_listStep": [ { "_action": "logout", "_weight": 0 } ] } ] })" ) ).has_value() );
    SW_EXPECT_FALSE( LoadBotScenario::loadFromXMLText( string( kHead ).append( R"({ "_action": "logout", "_weight": 3 } ] })" ) ).has_value() );
    const expected<LoadBotScenario> pick = LoadBotScenario::loadFromXMLText(
        string( kHead ).append( R"({ "_action": "pick", "_listStep": [ { "_action": "economy_wallet", "_weight": 3 }, { "_action": "guild_deposit", "_asset": "cur.gold", "_amount": 2, "_weight": 1 } ] } ] })" ) );
    SW_REQUIRE_TRUE( pick.has_value() );
    SW_REQUIRE_EQUAL( size_t( 2 ), pick->_listStep[0]._listStep.size() );
    SW_EXPECT_EQUAL( 3, pick->_listStep[0]._listStep[0]._weight );
    SW_EXPECT_EQUAL( int64( 2 ), pick->_listStep[0]._listStep[1]._amount );
}

/** @brief [LoadBotScheduleTest] 도착 시각표: 0 에 첫 도착, 그 뒤 1 / 속도마다 하나, 길이 안의 수(속도 × 길이)를 넘지 않는다 */
SW_TEST_CASE( LoadBotScheduleTest, ArrivalScheduleFollowsTheRate )
{
    const LoadBotArrivalSchedule schedule{ 10, 2000 }; // 초당 10 개, 2 초
    SW_EXPECT_EQUAL( int64( 20 ), schedule.getTotalCount() );
    SW_EXPECT_EQUAL( int64( 0 ), schedule.computeDueCount( -1 ) );
    SW_EXPECT_EQUAL( int64( 1 ), schedule.computeDueCount( 0 ) );
    SW_EXPECT_EQUAL( int64( 1 ), schedule.computeDueCount( 99999 ) );
    SW_EXPECT_EQUAL( int64( 2 ), schedule.computeDueCount( 100000 ) );
    SW_EXPECT_EQUAL( int64( 11 ), schedule.computeDueCount( 1000000 ) );
    SW_EXPECT_EQUAL( int64( 20 ), schedule.computeDueCount( 5000000 ) ); // 길이가 지나면 더 오지 않는다
}

namespace
{
    /** @brief 도착률 시나리오(초당 10 개, 끝 대기 0) — 단계는 로그아웃 하나다(서버가 없는 주소라 연결도 못 연다). */
    expected<LoadBotScenario> makeArrivalScenario( int64 rampUpMs )
    {
        expected<LoadBotScenario> scenario = LoadBotScenario::loadFromXMLText(
            R"({ "_name": "arrival", "_pacing": "open", "_executor": "constant_arrival_rate", "_arrivalRatePerSecond": 10, "_gracefulStopMs": 0, "_listStep": [ { "_action": "logout" } ] })" );
        if ( scenario.has_value() )
        {
            scenario->_botCount   = 4; // 봇 풀
            scenario->_rampUpMs   = rampUpMs;
            scenario->_durationMs = 1000;
        }
        return scenario;
    }
} // namespace

/**
 * @brief [LoadBotScheduleTest] 도착률 실행기(가짜 시계): 응답과 무관하게 정한 속도로 봇을 띄우고, 풀(`<Load clients>`)이 바닥나면 그 도착은 미루지 않고 버린 수로 센다
 * @details 서버가 없는 주소라 봇은 연결도 못 연다 — 그래도 띄우는 속도는 같다(열린 모델). 변이: 도착을 풀이 빌 때까지 미루면 버린 수가 0 이라 진다.
 */
SW_TEST_CASE( LoadBotScheduleTest, ArrivalRunnerLaunchesAtTheRate )
{
    const expected<LoadBotScenario> scenario = makeArrivalScenario( 0 );
    SW_REQUIRE_TRUE( scenario.has_value() );
    LoadBotRunnerSettings settings;
    settings._serverAddress = NetAddress::makeLoopback( 1 );
    LoopbackStreamNetwork        network;
    unique_ptr<IStreamTransport> transport = network.createTransport();
    LoadBotRunner                runner;
    SW_REQUIRE_TRUE( runner.initialize( transport.get(), *scenario, settings ).has_value() );

    runner.setManualClock( MonotonicMilliseconds{ 0 } );
    SW_EXPECT_EQUAL( 0, runner.getRunningBotCount() ); // 아직 아무도 띄우지 않았다
    (void)runner.update();
    SW_EXPECT_EQUAL( 1, runner.getLaunchedCount() ); // 0 에 첫 도착
    SW_EXPECT_TRUE( runner.getRunningBotCount() <= runner.getLaunchedCount() );
    runner.setManualClock( MonotonicMilliseconds{ 250 } );
    (void)runner.update();
    SW_EXPECT_EQUAL( 3, runner.getLaunchedCount() ); // 0 · 100 · 200
    runner.setManualClock( MonotonicMilliseconds{ 999 } );
    (void)runner.update();
    SW_EXPECT_EQUAL( 4, runner.getLaunchedCount() );                             // 풀이 넷이다
    SW_EXPECT_EQUAL( int64( 6 ), runner.getMetrics().getDroppedArrivalCount() ); // 도착 열 가운데 여섯은 버렸다
    runner.setManualClock( MonotonicMilliseconds{ 5000 } );
    (void)runner.update();
    SW_EXPECT_EQUAL( int64( 6 ), runner.getMetrics().getDroppedArrivalCount() ); // 길이가 지나면 더 오지 않는다
    runner.shutdown();
}

/** @brief [LoadBotScheduleTest] 도착률 실행기는 늘리는 시간을 받지 않는다 — 받으면 그 값이 무엇을 뜻하는지 숨는다 */
SW_TEST_CASE( LoadBotScheduleTest, ArrivalRunnerRefusesRampUp )
{
    const expected<LoadBotScenario> scenario = makeArrivalScenario( 1000 );
    SW_REQUIRE_TRUE( scenario.has_value() );
    LoadBotRunnerSettings settings;
    settings._serverAddress = NetAddress::makeLoopback( 1 );
    LoopbackStreamNetwork        network;
    unique_ptr<IStreamTransport> transport = network.createTransport();
    LoadBotRunner                runner;
    SW_EXPECT_FALSE( runner.initialize( transport.get(), *scenario, settings ).has_value() );
}
