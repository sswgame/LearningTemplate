// LoadBotMetricsTest — 부하 봇 지표의 조회: 동작마다의 오류 수, 종류마다의 푸시 수, 연결 열림 · 실패 · 끊김 수가 기록한 대로 나온다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Programs/OnlineLoadBot/LoadBotMetrics.h"
#include "Programs/OnlineLoadBot/LoadBotScenario.h"

using namespace sw;

/**
 * @brief [LoadBotMetricsTest] 오류는 그 동작에만 세고 성공은 세지 않는다 — 합은 `getTotalErrorCount` 와 같다
 */
SW_TEST_CASE( LoadBotMetricsTest, ErrorsAreCountedPerAction )
{
    LoadBotMetrics metrics;
    metrics.recordLatency( LoadBotAction::Login, 100, "" );
    metrics.recordLatency( LoadBotAction::Login, 200, "timeout" );
    metrics.recordLatency( LoadBotAction::ChatSend, 300, "timeout" );
    metrics.recordLatency( LoadBotAction::ChatSend, 400, "refused" );

    SW_EXPECT_EQUAL( int64( 1 ), metrics.getErrorCount( LoadBotAction::Login ) );
    SW_EXPECT_EQUAL( int64( 2 ), metrics.getErrorCount( LoadBotAction::ChatSend ) );
    SW_EXPECT_EQUAL( int64( 0 ), metrics.getErrorCount( LoadBotAction::Logout ) );
    SW_EXPECT_EQUAL( int64( 3 ), metrics.getTotalErrorCount() );
    SW_EXPECT_EQUAL( int64( 2 ), metrics.getCompletedCount( LoadBotAction::Login ) ); // 오류로 끝난 동작도 끝난 동작이다
}

/**
 * @brief [LoadBotMetricsTest] 푸시는 종류마다, 연결은 열림 · 실패 · 끊김으로 따로 센다 — 받은 적 없는 푸시 종류는 0 이다
 */
SW_TEST_CASE( LoadBotMetricsTest, PushesAndConnectionsAreCountedSeparately )
{
    LoadBotMetrics metrics;
    SW_EXPECT_EQUAL( int64( 0 ), metrics.getPushCount( 7 ) );
    metrics.recordPush( 7 );
    metrics.recordPush( 7 );
    metrics.recordPush( 9 );
    SW_EXPECT_EQUAL( int64( 2 ), metrics.getPushCount( 7 ) );
    SW_EXPECT_EQUAL( int64( 1 ), metrics.getPushCount( 9 ) );
    SW_EXPECT_EQUAL( int64( 0 ), metrics.getPushCount( 8 ) );

    metrics.recordConnection( true );
    metrics.recordConnection( true );
    metrics.recordConnection( false );
    metrics.recordDisconnect();
    SW_EXPECT_EQUAL( int64( 2 ), metrics.getOpenedConnectionCount() );
    SW_EXPECT_EQUAL( int64( 1 ), metrics.getFailedConnectionCount() );
    SW_EXPECT_EQUAL( int64( 1 ), metrics.getDisconnectCount() );
}
