// 부하 테스트 봇(Tools/OnlineLoadBot) 부하 — 봇 200 이 한 프로세스 서버 조립에 루프백으로 붙어 채팅 · 매칭 시나리오를 끝까지 돌고
// 지표가 정확히 맞으며 같은 씨앗이면 표가 바이트까지 같다. 정확성(봇 12)은 TestOnlineLoadBot.cpp(OnlineLoadBotTest)가 지킨다.
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

SW_TEST_LOAD_SUITE( OnlineLoadBotLoadTest, "runs 200 load bots through the chat and matchmaking scenario on the loopback server assembly" );
SW_TEST_TIME_BUDGET( OnlineLoadBotLoadTest, 2500, "Release 케이스 최대 513 ms(2026-10-15, Ryzen 7 6800H, 세 번 중 최대)의 네 배 — CI 러너 여유" );

namespace
{
    struct OnlineLoadBotLoadTestInternal
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
            serverSettings._startMs            = kServerStartMs; // 첫 tick 과 같은 서비스 시각 — 실행마다 같은 표가 나와야 한다
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
                bRunning = runner.update();
                server.update( kServerStartMs + nowMs );
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

SW_TEST_CASE( OnlineLoadBotLoadTest, TwoHundredBotsRunChatAndMatchmakingScenarioEndToEnd )
{
    LoadBotMetrics metrics;
    const string   firstTable = OnlineLoadBotLoadTestInternal::runScenario( OnlineLoadBotLoadTestInternal::kChatAndMatchScenario, 5, metrics );

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
    const string   secondTable = OnlineLoadBotLoadTestInternal::runScenario( OnlineLoadBotLoadTestInternal::kChatAndMatchScenario, 5, secondMetrics );
    SW_EXPECT_STREQ( firstTable.c_str(), secondTable.c_str() ); // 같은 씨앗 — 수 · 백분위 · 오류 · 알림이 바이트까지 같다
}
