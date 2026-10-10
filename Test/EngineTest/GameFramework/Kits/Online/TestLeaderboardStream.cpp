// 순위표 끝단(루프백) — 로그인 전 요청은 NotSignedIn, 클라이언트 점수 제출은 `_bClientSubmit` 표만(아니면 NotAllowed), 상위 · 내 둘레 · 통계 · 업적 조회,
// 업적 달성이 그 계정의 연결에 알림으로 오고 다른 서버에 붙은 계정이면 접속 상태 창구(원격 알림)로 간다.
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "EngineTest/GameFramework/Online/OnlineHostTestUtil.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardServer.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardService.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Shared/LeaderboardClient.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct LeaderboardRecorder
    {
        vector<LeaderboardClientReply> _listReply{};

        void onReply( const LeaderboardClientReply& reply ) { _listReply.push_back( reply ); }

        LeaderboardReplyDelegate makeDelegate() { return LeaderboardReplyDelegate::create<&LeaderboardRecorder::onReply>( this ); }

        const LeaderboardClientReply& getLast() const { return _listReply.back(); }
    };

    /** @brief 서버 하나의 순위표(로직 + 바인딩) — `start` 가 서버에 올려 서버가 내려가기 전에 `stop` 한다. */
    struct LeaderboardOnServer final : public test::IOnlineTestKit
    {
        LeaderboardService _service;
        LeaderboardServer  _binding;

        LeaderboardOnServer()
            : _service{}
            , _binding{}
        {
        }

        /** @brief 서버가 내려가기 전에 부른다(호스트 · 저장소 · 접속 상태가 살아 있다). 두 번 불려도 된다. */
        void stop() override
        {
            _binding.shutdown();
            _service.shutdown();
        }

        void start( test::OnlineTestServer& server )
        {
            SW_EXPECT_TRUE( server._host.registerService( &_binding ) );
            server.start();
            server.addKit( this );
            LeaderboardServiceDependencies dependencies;
            dependencies._pStore  = &server._store;
            dependencies._pRouter = server._host.getEphemeralRouter();
            _service.initialize( dependencies );
            _binding.initialize( &_service, nullptr, &server._presence );

            LeaderboardDefinition serverOnly;
            serverOnly._boardID = "kills";
            SW_EXPECT_TRUE( _service.registerBoard( serverOnly ) );
            LeaderboardDefinition casual;
            casual._boardID       = "casual";
            casual._bClientSubmit = SW_TRUE;
            SW_EXPECT_TRUE( _service.registerBoard( casual ) );
            AchievementDefinition achievement;
            achievement._achievementID = "first_win";
            achievement._statName      = "wins";
            achievement._threshold     = 1;
            SW_EXPECT_TRUE( _service.registerAchievement( achievement ) );
        }
    };
} // namespace

SW_TEST_CASE( LeaderboardStreamTest, ClientSubmitOnlyOnOpenBoardsAndAchievementPush )
{
    LoopbackStreamNetwork   network( 11u );
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    LeaderboardOnServer     leaderboard;
    test::OnlineTestServer  server( network, &database, &cacheDatabase, &hub, 1 );
    leaderboard.start( server );

    LeaderboardClient       leaderboardClient;
    test::OnlineTestClients clients( network );
    const int32             clientIndex = clients.connect( server._port, { &leaderboardClient } );
    leaderboardClient.initialize( &clients.getClient( clientIndex ) );
    test::tickAll( { &server }, clients, 0 );

    LeaderboardRecorder recorder;
    (void)leaderboardClient.requestTop( "casual", 0, 10, recorder.makeDelegate() ); // 로그인 전
    test::tickAll( { &server }, clients, 0 );
    SW_ASSERT_EQUAL( recorder._listReply.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( recorder.getLast()._errorCode, OnlineError::kUnauthenticated );
    SW_EXPECT_TRUE( recorder.getLast()._reply._result == LeaderboardResult::NotSignedIn );

    clients.login( clientIndex, 1 );
    test::tickAll( { &server }, clients, 0 );

    // 점수 제출 — 서버만 내는 표는 거절, 열린 표는 받는다
    (void)leaderboardClient.submitScore( "kills", 5, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 10 );
    SW_EXPECT_EQUAL( recorder.getLast()._errorCode, OnlineError::kOk );
    SW_EXPECT_TRUE( recorder.getLast()._reply._result == LeaderboardResult::NotAllowed );
    (void)leaderboardClient.submitScore( "casual", 42, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 20 );
    SW_EXPECT_TRUE( recorder.getLast()._reply._result == LeaderboardResult::Ok );
    SW_EXPECT_EQUAL( recorder.getLast()._reply._score, int64( 42 ) );

    // 서버 로직이 다른 계정의 점수를 낸다 → 상위 · 내 둘레
    leaderboard._service.submitScore( "casual", 2, "bob", 50, 30, 0 );
    test::tickAll( { &server }, clients, 30 );
    (void)leaderboardClient.requestTop( "casual", 0, 10, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 40 );
    SW_ASSERT_EQUAL( recorder.getLast()._reply._listEntry.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( recorder.getLast()._reply._listEntry[0]._accountID, AccountID( 2 ) );
    SW_EXPECT_STREQ( recorder.getLast()._reply._listEntry[0]._displayName.c_str(), "bob" );
    SW_EXPECT_EQUAL( recorder.getLast()._reply._listEntry[1]._rank, 2 );
    (void)leaderboardClient.requestAround( "casual", 1, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 50 );
    SW_ASSERT_EQUAL( recorder.getLast()._reply._listEntry.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( recorder.getLast()._reply._listEntry[1]._accountID, AccountID( 1 ) );

    // 통계가 업적 문턱을 넘음 → 그 계정의 연결에 알림, 조회에도 보인다
    leaderboard._service.changeStat( 1, "alice", "wins", 1, LeaderboardUpdate::Sum, 60, 0 );
    test::tickAll( { &server }, clients, 60 );
    vector<AchievementState> listUnlock;
    leaderboardClient.drainAchievementUnlocks( listUnlock );
    SW_ASSERT_EQUAL( listUnlock.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( listUnlock[0]._achievementID.c_str(), "first_win" );
    (void)leaderboardClient.requestAchievements( recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 70 );
    SW_EXPECT_EQUAL( recorder.getLast()._reply._listAchievement.size(), size_t( 1 ) );
    (void)leaderboardClient.requestStats( recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 80 );
    SW_ASSERT_EQUAL( recorder.getLast()._reply._listStat.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( recorder.getLast()._reply._listStat[0]._value, int64( 1 ) );

    // 다른 서버에 붙은 계정의 업적 — 접속 상태 창구로
    leaderboard._service.changeStat( 3, "carol", "wins", 1, LeaderboardUpdate::Sum, 90, 0 );
    test::tickAll( { &server }, clients, 90 );
    SW_ASSERT_EQUAL( server._presence._listRemotePush.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( server._presence._listRemotePush[0]._accountID, AccountID( 3 ) );
    SW_EXPECT_EQUAL( server._presence._listRemotePush[0]._kind, LeaderboardMethod::kPushAchievement );
}
