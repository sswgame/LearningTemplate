// 부하 시험 봇(Tools/OnlineLoadBot) — 시나리오 읽기 오류(모르는 동작 · 칸 · 값), 백분위(가장 가까운 순위), 봇 200 이 한 프로세스 서버 조립(계정 · 디렉터리 ·
// 라이브 운영 · 채팅 · 순위 · 매칭)에 루프백으로 붙어 시나리오를 끝까지 돌고 지표가 정확히 맞으며 같은 씨앗이면 표가 바이트까지 같다, 끊고 다시 붙으면 토큰으로 재접속한다.
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatProtocol.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingProtocol.h"

#include "OnlineLoadBot/LoadBotLocalServer.h"
#include "OnlineLoadBot/LoadBotMetrics.h"
#include "OnlineLoadBot/LoadBotRunner.h"
#include "OnlineLoadBot/LoadBotScenario.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct OnlineLoadBotTestInternal
    {
        static constexpr int64  kTickMs        = 10;
        static constexpr int64  kServerStartMs = 1000000; ///< 서비스 시각(가짜 UTC) — 실행기 시각과 따로
        static constexpr int32  kMaxTickCount  = 60000;   ///< 가짜 10 분
        static constexpr uint16 kServerPort    = 7520;

        /** @brief 봇 200 — 모두 들어간 뒤 말하고(늘리기 2 초 < 기다림 3 초), 모두 다 말한 뒤 떠난다(말하기 끝 < 마지막 기다림). */
        static constexpr const utf8* kChatAndMatchScenario = R"({
            "_name": "test_chat_and_match", "_botCount": 200, "_rampUpSeconds": 2, "_durationSeconds": 600, "_seed": 7, "_region": "kr",
            "_listStep": [
                { "_action": "login", "_mode": "guest" },
                { "_action": "directory_status" },
                { "_action": "liveops_state" },
                { "_action": "chat_join", "_channel": "world.load" },
                { "_action": "wait", "_minMs": 3000, "_maxMs": 3000 },
                { "_action": "repeat", "_count": 3, "_listStep": [
                    { "_action": "chat_send", "_channel": "world.load", "_text": "hi from {bot} #{seq}" },
                    { "_action": "wait", "_minMs": 100, "_maxMs": 300 }
                ] },
                { "_action": "wait", "_minMs": 3000, "_maxMs": 3000 },
                { "_action": "leaderboard_top", "_board": "kills", "_count": 10 },
                { "_action": "matchmaking_queue", "_mode": "duo" },
                { "_action": "wait_match", "_timeoutMs": 60000 },
                { "_action": "logout" }
            ] })";

        static constexpr const utf8* kReconnectScenario = R"({
            "_name": "test_reconnect", "_botCount": 4, "_rampUpSeconds": 0, "_durationSeconds": 60, "_seed": 3,
            "_listStep": [
                { "_action": "login" },
                { "_action": "disconnect" },
                { "_action": "reconnect" },
                { "_action": "chat_join", "_channel": "world.again" },
                { "_action": "logout" }
            ] })";

        /** @brief 한 프로세스에서 서버 조립 + 실행기를 가짜 시계로 끝까지 돌리고 표(결정성 비교용)를 돌려줍니다. */
        static string runScenario( const utf8* pScenarioText, int32 networkSeed, LoadBotMetrics& outMetrics )
        {
            LoadBotScenario scenario;
            string          error;
            SW_EXPECT_TRUE_MSG( LoadBotScenario::loadText( pScenarioText, scenario, error ), error.c_str() );

            LoopbackStreamNetwork        network( static_cast<uint32>( networkSeed ) );
            unique_ptr<IStreamTransport> serverTransport = network.createTransport();
            unique_ptr<IStreamTransport> botTransport    = network.createTransport();
            LoadBotLocalServer           server;
            LoadBotLocalServerSettings   serverSettings;
            serverSettings._port               = kServerPort;
            serverSettings._bLightPasswordHash = SW_TRUE;
            SW_EXPECT_TRUE_MSG( server.initialize( serverTransport.get(), serverSettings, error ), error.c_str() );

            LoadBotRunner         runner;
            LoadBotRunnerSettings runnerSettings;
            runnerSettings._serverAddress = NetAddress::makeLoopback( kServerPort );
            SW_EXPECT_TRUE_MSG( runner.initialize( botTransport.get(), scenario, runnerSettings, error ), error.c_str() );

            int32 tickCount = 0;
            bool  bRunning  = true;
            while ( bRunning && tickCount < kMaxTickCount )
            {
                const int64 nowMs = tickCount * kTickMs;
                runner.setManualClock( nowMs );
                bRunning = runner.tick();
                server.tick( kServerStartMs + nowMs );
                ++tickCount;
            }
            SW_EXPECT_FALSE( bRunning ); // 시한 전에 모두 끝났다
            outMetrics         = runner.getMetrics();
            const string table = outMetrics.formatTable( runner.getElapsedMs() );
            runner.shutdown();
            server.shutdown();
            return table;
        }
    };
} // namespace

SW_TEST_CASE( OnlineLoadBotTest, ScenarioRejectsUnknownActionsKeysAndValues )
{
    LoadBotScenario scenario;
    string          error;
    SW_EXPECT_FALSE( LoadBotScenario::loadText( R"({ "_botCount": 1, "_listStep": [ { "_action": "fly" } ] })", scenario, error ) );
    SW_EXPECT_TRUE( error.find( "fly" ) != string::npos );
    SW_EXPECT_FALSE( LoadBotScenario::loadText( R"({ "_botCount": 1, "_listStep": [ { "_action": "wait", "_minms": 5 } ] })", scenario, error ) ); // 대소문자를 가린다
    SW_EXPECT_TRUE( error.find( "_minms" ) != string::npos );
    SW_EXPECT_FALSE( LoadBotScenario::loadText( R"({ "_botcount": 1, "_listStep": [ { "_action": "wait" } ] })", scenario, error ) );
    SW_EXPECT_FALSE( LoadBotScenario::loadText( R"({ "_botCount": 1, "_listStep": [ { "_action": "login", "_mode": "magic" } ] })", scenario, error ) );
    SW_EXPECT_FALSE( LoadBotScenario::loadText( R"({ "_botCount": 1, "_listStep": [ { "_action": "wait", "_minMs": 20, "_maxMs": 10 } ] })", scenario, error ) );
    SW_EXPECT_FALSE( LoadBotScenario::loadText( R"({ "_botCount": 1, "_listStep": [] })", scenario, error ) );
    SW_ASSERT_TRUE_MSG( LoadBotScenario::loadText( R"({ "_botCount": 2, "_listStep": [ { "_action": "repeat", "_count": 3,
                                                          "_listStep": [ { "_action": "wait", "_minMs": 10, "_maxMs": 20 } ] } ] })",
                                                   scenario, error ),
                        error.c_str() );
    SW_EXPECT_EQUAL( scenario._botCount, 2 );
    SW_ASSERT_EQUAL( scenario._listStep.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( scenario._listStep[0]._action == LoadBotAction::Repeat );
    SW_EXPECT_EQUAL( scenario._listStep[0]._count, 3 );
    SW_ASSERT_EQUAL( scenario._listStep[0]._listStep.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( scenario._listStep[0]._listStep[0]._maxMs, int64( 20 ) );
    SW_EXPECT_STREQ( toString( LoadBotAction::SocialFriendRandom ), "social_friend_random" );
}

SW_TEST_CASE( OnlineLoadBotTest, PercentilesUseNearestRank )
{
    vector<int64> listSample;
    for ( int64 value = 1; value <= 100; ++value )
    {
        listSample.push_back( value );
    }
    SW_EXPECT_EQUAL( LoadBotMetrics::computePercentile( listSample, 50 ), int64( 50 ) );
    SW_EXPECT_EQUAL( LoadBotMetrics::computePercentile( listSample, 95 ), int64( 95 ) );
    SW_EXPECT_EQUAL( LoadBotMetrics::computePercentile( listSample, 99 ), int64( 99 ) );
    SW_EXPECT_EQUAL( LoadBotMetrics::computePercentile( listSample, 100 ), int64( 100 ) );
    SW_EXPECT_EQUAL( LoadBotMetrics::computePercentile( vector<int64>{ 7 }, 99 ), int64( 7 ) );
    SW_EXPECT_EQUAL( LoadBotMetrics::computePercentile( vector<int64>{ 1, 2, 3 }, 50 ), int64( 2 ) );
    SW_EXPECT_EQUAL( LoadBotMetrics::computePercentile( vector<int64>{}, 50 ), int64( 0 ) );

    LoadBotMetrics metrics;
    metrics.recordLatency( LoadBotAction::ChatSend, 1500, "" );
    metrics.recordLatency( LoadBotAction::ChatSend, 2500, "code7" );
    metrics.recordMatch( 9 );
    metrics.recordMatch( 9 );
    metrics.recordMatch( 4 );
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::ChatSend ), int64( 2 ) );
    SW_EXPECT_EQUAL( metrics.getErrorCount( LoadBotAction::ChatSend ), int64( 1 ) );
    SW_EXPECT_EQUAL( metrics.getErrorKeyCount( "code7" ), int64( 1 ) );
    SW_EXPECT_EQUAL( metrics.getDistinctMatchCount(), int64( 2 ) );
}

SW_TEST_CASE( OnlineLoadBotTest, TwoHundredBotsRunChatAndMatchmakingScenarioEndToEnd )
{
    LoadBotMetrics metrics;
    const string   firstTable = OnlineLoadBotTestInternal::runScenario( OnlineLoadBotTestInternal::kChatAndMatchScenario, 5, metrics );

    SW_EXPECT_EQUAL( metrics.getOpenedConnectionCount(), int64( 200 ) );
    SW_EXPECT_EQUAL( metrics.getFailedConnectionCount(), int64( 0 ) );
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::Login ), int64( 200 ) );
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::DirectoryStatus ), int64( 200 ) );
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::LiveOpsState ), int64( 200 ) );
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::ChatJoin ), int64( 200 ) );
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::ChatSend ), int64( 600 ) ); // 봇마다 셋(글이 달라 도배 막이 안)
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::LeaderboardTop ), int64( 200 ) );
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::MatchmakingQueue ), int64( 200 ) );
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::WaitMatch ), int64( 200 ) );
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::Logout ), int64( 200 ) );
    SW_EXPECT_TRUE_MSG( metrics.getTotalErrorCount() == 0, firstTable.c_str() );
    SW_EXPECT_EQUAL( metrics.getDistinctMatchCount(), int64( 50 ) );                         // 2 대 2 — 200 은 넷씩 나뉜다
    SW_EXPECT_EQUAL( metrics.getPushCount( ChatMethod::kPushMessage ), int64( 600 * 200 ) ); // 같은 채널 — 보낸 이 포함
    SW_EXPECT_EQUAL( metrics.getPushCount( MatchmakingMethod::kPushMatch ), int64( 200 ) );  // 경기 결과는 봇마다 한 번

    LoadBotMetrics secondMetrics;
    const string   secondTable = OnlineLoadBotTestInternal::runScenario( OnlineLoadBotTestInternal::kChatAndMatchScenario, 5, secondMetrics );
    SW_EXPECT_STREQ( firstTable.c_str(), secondTable.c_str() ); // 같은 씨앗 — 수 · 백분위 · 오류 · 알림이 바이트까지 같다
}

SW_TEST_CASE( OnlineLoadBotTest, DisconnectThenReconnectResumesTheSession )
{
    LoadBotMetrics metrics;
    const string   table = OnlineLoadBotTestInternal::runScenario( OnlineLoadBotTestInternal::kReconnectScenario, 9, metrics );
    SW_EXPECT_EQUAL( metrics.getOpenedConnectionCount(), int64( 8 ) ); // 봇 넷 × 두 번
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::Disconnect ), int64( 4 ) );
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::Reconnect ), int64( 4 ) );
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::ChatJoin ), int64( 4 ) ); // 재접속한 세션으로 — 로그인 없이
    SW_EXPECT_EQUAL( metrics.getCompletedCount( LoadBotAction::Logout ), int64( 4 ) );
    SW_EXPECT_TRUE_MSG( metrics.getTotalErrorCount() == 0, table.c_str() );
}
