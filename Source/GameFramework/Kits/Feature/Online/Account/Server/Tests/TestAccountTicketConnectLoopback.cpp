// AccountTicketConnectLoopbackTest — 접속 티켓 받기 도우미(`AccountTicketConnector`)의 시한 · 재접속 · 거절 길: 막지 않는 길(`begin` · `poll`)을 루프백 망 위의 가짜 계정 서버와
// 수동 시각으로 돌린다(실제 시간을 기다리지 않는다). 서버가 없음 · 로그인 답 없음 · 티켓 답 없음은 각자의 시한 이유로, 만료된 세션의 티켓 거절과 기다리는 중 끊김은 바로 실패로 끝나고
// 연결을 닫는다. 늦게 뜬 서버에는 재접속 간격 상한(`OnlineServiceClientConfig::_maxBackoffMs`) 안에 닿는다. 입구 검사는 AccountTicketConnectTest, 실제 TLS 두 프로세스 길은
// `net.ticketconnect` 시나리오가 본다. 서버 하니스(`OnlineHostTestUtil`)를 쓰므로 서버 계정 키트에 둔다 — 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Network/Connection/NetEndpointSecurity.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountTicketConnect.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"

using namespace sw;

namespace
{
    constexpr int64 kNowMs        = 1700000000000ll;
    constexpr int64 kStepMs       = 10; ///< 한 틱에 흐르는 수동 시각
    constexpr uint8 kTicketMarker = 0xA5;

    /** @brief 가짜 계정 서버 — 로그인은 토큰과 Ok, 티켓은 `_ticketResult` 로 답합니다. `_holdMethod` 의 요청은 세기만 하고 답하지 않습니다(시한 길). */
    class FakeTicketService final : public IOnlineServerService
    {
    public:
        LoginResult _ticketResult{ LoginResult::Ok };
        uint16      _holdMethod{ 0 };
        int32       _loginCount{ 0 };
        int32       _ticketCount{ 0 };

        const OnlineProtocolEntry& getProtocol() const override { return AccountProtocol::kOnlineEntry; }
        bool                       isAnonymousMethod( uint16 method ) const override
        {
            (void)method;
            return true;
        }

        void onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override
        {
            (void)body;
            if ( context._method == AccountMethod::Login::kID )
                ++_loginCount;
            else if ( context._method == AccountMethod::IssueGameTicket::kID )
                ++_ticketCount;
            if ( context._method == _holdMethod )
                return;
            if ( context._method == AccountMethod::Login::kID )
            {
                AccountGrantReply reply;
                reply._grant._identity._accountID = 42;
                reply._grant._token._sessionID    = 7;
                reply._grant._token._arrSecret[0] = 5;
                reply._result                     = LoginResult::Ok;
                (void)host.respondReply<AccountProtocol::MessageTypes>( context._token, reply );
                return;
            }
            if ( context._method == AccountMethod::IssueGameTicket::kID )
            {
                AccountTicketReply reply;
                reply._ticket._arrToken[0] = kTicketMarker;
                reply._ticket._expiresAtMs = kNowMs + 60000;
                reply._result              = _ticketResult;
                (void)host.respondReply<AccountProtocol::MessageTypes>( context._token, reply );
                return;
            }
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
        }
    };

    /** @brief 가짜 서버 + 루프백 전송을 빌린 도우미입니다. 선언 순서가 수명이다(도우미가 전송보다, 전송이 망보다 먼저 내려간다). */
    struct TicketConnectRig
    {
        FakeTicketService            _service{};
        test::OnlineTestFixture      _rig{ false };
        unique_ptr<IStreamTransport> _transport{ _rig._network.createTransport() };
        AccountTicketConnector       _connector{};
        NetConnectCredentials        _credentials{};
        MonotonicMilliseconds        _now{ kNowMs };
        uint8                        _bServerUp : 1;

        TicketConnectRig()
            : _bServerUp{ SW_FALSE }
        {
            SW_EXPECT_TRUE( _rig._server._host.registerServerService( &_service ) );
        }

        void startServer()
        {
            _rig._server.start();
            _bServerUp = SW_TRUE;
        }

        /** @brief 서버를 내립니다 — 열린 연결이 모두 닫히고 다시 붙을 곳이 없다. */
        void dropServer()
        {
            _rig._server._host.shutdown();
            _bServerUp = SW_FALSE;
        }

        expected<void> begin( int64 timeoutMs )
        {
            AccountTicketRequest request;
            request._loginAddress        = NetAddress::makeLoopback( _rig._server._port ).toString();
            request._loginName           = "alice";
            request._password            = "password123";
            request._serverID            = "game-1";
            request._gameBuild           = "1.0.0";
            request._platform            = "windows";
            request._timeoutMilliseconds = timeoutMs;
            return _connector.begin( std::move( request ), *_transport, OnlineClientSecurity::plaintextLoopback(), _now );
        }

        /** @brief 시각을 한 틱 넘기고 도우미 → 서버 순으로 돌립니다. */
        expected<AccountTicketStep> step()
        {
            _now += kStepMs;
            expected<AccountTicketStep> result = _connector.poll( _now, _credentials );
            if ( _bServerUp == SW_TRUE )
                _rig._server.update( _now.count() );
            return result;
        }

        /** @brief 받음 · 실패까지, 길어야 @p maxMs 만큼 돌립니다. 그때까지 기다리는 중이면 `Pending` 입니다. */
        expected<AccountTicketStep> runFor( int64 maxMs )
        {
            const MonotonicMilliseconds until = _now + maxMs;
            while ( _now < until )
            {
                expected<AccountTicketStep> result = step();
                if ( result.has_value() == false || *result == AccountTicketStep::Received )
                    return result;
            }
            return AccountTicketStep::Pending;
        }

        static bool failedWith( const expected<AccountTicketStep>& result, const utf8* pExpectedPart )
        {
            if ( result.has_value() )
                return false;
            const sw::string text = sw::string( result.error().message() ) + " " + sw::string( result.error().detail() );
            return text.find( pExpectedPart ) != string::npos;
        }
    };
} // namespace

/** @brief [AccountTicketConnectLoopbackTest] 기준 길 — 로그인 → 티켓을 한 번씩 보내고 받은 티켓으로 자격을 채운다(아래 실패 길이 하니스 탓이 아님을 보인다) */
SW_TEST_CASE( AccountTicketConnectLoopbackTest, TicketArrivesFromLoopbackServer )
{
    // 틀리면: 로그인 뒤 티켓을 보내지 않거나 자격을 채우지 않으면 Received · 표지 바이트 단언이 진다.
    TicketConnectRig rig;
    rig.startServer();
    SW_REQUIRE_TRUE( rig.begin( 5000 ).has_value() );
    const expected<AccountTicketStep> result = rig.runFor( 5000 );
    SW_REQUIRE_TRUE( result.has_value() );
    SW_EXPECT_TRUE( *result == AccountTicketStep::Received );
    SW_EXPECT_EQUAL( 1, rig._service._loginCount );
    SW_EXPECT_EQUAL( 1, rig._service._ticketCount );
    SW_EXPECT_EQUAL( NetGameTicket::kTokenSize, rig._credentials._tokenSize );
    SW_EXPECT_EQUAL( kTicketMarker, rig._credentials._arrToken[0] );
}

/** @brief [AccountTicketConnectLoopbackTest] 세션이 만료돼 서버가 티켓을 거절하면 그 결과를 이유에 싣고, 자격을 채우지 않고, 연결을 닫는다 */
SW_TEST_CASE( AccountTicketConnectLoopbackTest, ExpiredSessionTicketIsRefusedAndClosesTheConnection )
{
    // 틀리면: 거절을 받고도 기다리면 실패 단언이, 결과를 빼면 "result 7" 이, 연결을 남기면 다음 poll · 서버 연결 수 단언이 진다.
    static_assert( static_cast<int32>( LoginResult::Expired ) == 7 );
    TicketConnectRig rig;
    rig._service._ticketResult = LoginResult::Expired;
    rig.startServer();
    SW_REQUIRE_TRUE( rig.begin( 5000 ).has_value() );
    const expected<AccountTicketStep> result = rig.runFor( 5000 );
    SW_EXPECT_TRUE( TicketConnectRig::failedWith( result, "game ticket refused" ) );
    SW_EXPECT_TRUE( TicketConnectRig::failedWith( result, "result 7" ) );
    SW_EXPECT_EQUAL( 0, rig._credentials._tokenSize );
    SW_EXPECT_TRUE( TicketConnectRig::failedWith( rig.step(), "no ticket request in progress" ) );
    (void)rig.step();
    SW_EXPECT_EQUAL( 0, rig._rig._server._host.getConnectionCount() );
}

/** @brief [AccountTicketConnectLoopbackTest] 서버가 없으면 재접속을 이어 가다 시한에 "연결" 이유로 끝난다 — 시한보다 먼저 포기하지도, 넘겨 기다리지도 않는다 */
SW_TEST_CASE( AccountTicketConnectLoopbackTest, NoServerTimesOutConnectingAtTheDeadline )
{
    // 틀리면: 연결 실패 한 번에 포기하면 경과 하한이, 시한을 보지 않으면 실패 단언이, 이유를 잘못 고르면 문장 단언이 진다.
    TicketConnectRig rig;
    SW_REQUIRE_TRUE( rig.begin( 2000 ).has_value() );
    const MonotonicMilliseconds       started = rig._now;
    const expected<AccountTicketStep> result  = rig.runFor( 10000 );
    SW_EXPECT_TRUE( TicketConnectRig::failedWith( result, "timed out connecting to the login server" ) );
    const int64 elapsedMs = rig._now - started;
    SW_EXPECT_TRUE( elapsedMs >= 2000 );
    SW_EXPECT_TRUE( elapsedMs <= 2000 + kStepMs );
    SW_EXPECT_EQUAL( 0, rig._service._loginCount );
}

/** @brief [AccountTicketConnectLoopbackTest] 로그인 답이 오지 않으면 "로그인 답" 이유로 시한에 끝나고 티켓은 보내지 않는다 */
SW_TEST_CASE( AccountTicketConnectLoopbackTest, HeldLoginTimesOutWaitingForTheLoginReply )
{
    // 틀리면: 답 없이 티켓을 보내면 티켓 수가, 이유를 잘못 고르면 문장 단언이 진다.
    TicketConnectRig rig;
    rig._service._holdMethod = AccountMethod::Login::kID;
    rig.startServer();
    SW_REQUIRE_TRUE( rig.begin( 2000 ).has_value() );
    const expected<AccountTicketStep> result = rig.runFor( 10000 );
    SW_EXPECT_TRUE( TicketConnectRig::failedWith( result, "timed out waiting for the login reply" ) );
    SW_EXPECT_EQUAL( 1, rig._service._loginCount );
    SW_EXPECT_EQUAL( 0, rig._service._ticketCount );
}

/** @brief [AccountTicketConnectLoopbackTest] 티켓 답이 오지 않으면 "티켓" 이유로 시한에 끝난다(티켓 요청은 한 번만 — 기다리는 동안 다시 보내지 않는다) */
SW_TEST_CASE( AccountTicketConnectLoopbackTest, HeldTicketTimesOutWaitingForTheTicket )
{
    // 틀리면: 틱마다 티켓을 다시 보내면 티켓 수가, 이유를 잘못 고르면 문장 단언이 진다.
    TicketConnectRig rig;
    rig._service._holdMethod = AccountMethod::IssueGameTicket::kID;
    rig.startServer();
    SW_REQUIRE_TRUE( rig.begin( 2000 ).has_value() );
    const expected<AccountTicketStep> result = rig.runFor( 10000 );
    SW_EXPECT_TRUE( TicketConnectRig::failedWith( result, "timed out waiting for the game ticket" ) );
    SW_EXPECT_EQUAL( 1, rig._service._loginCount );
    SW_EXPECT_EQUAL( 1, rig._service._ticketCount );
}

/**
 * @brief [AccountTicketConnectLoopbackTest] 20 초 동안 없던 서버가 뜨면 재접속 간격 상한 안에 닿아 티켓을 받는다.
 * @details 간격이 상한 없이 두 배로 늘면(250 ms 부터) 20 초 뒤의 다음 시도는 31 초 무렵이라 상한(5 초) + 여유 안에 받지 못한다.
 */
SW_TEST_CASE( AccountTicketConnectLoopbackTest, LateServerIsReachedWithinTheBackoffCap )
{
    // 틀리면: 서버가 없는 동안 시한 전에 포기하면 첫 Pending 단언이, 재접속 간격에 상한이 없으면 두 번째 Received 단언이 진다.
    const int64      maxBackoffMs = OnlineServiceClientConfig{ OnlineClientSecurity::plaintextLoopback() }._maxBackoffMs;
    TicketConnectRig rig;
    SW_REQUIRE_TRUE( rig.begin( 60000 ).has_value() );
    const expected<AccountTicketStep> waiting = rig.runFor( 20000 );
    SW_REQUIRE_TRUE( waiting.has_value() );
    SW_EXPECT_TRUE( *waiting == AccountTicketStep::Pending );
    rig.startServer();
    const expected<AccountTicketStep> result = rig.runFor( maxBackoffMs + 1000 );
    SW_REQUIRE_TRUE( result.has_value() );
    SW_EXPECT_TRUE( *result == AccountTicketStep::Received );
    SW_EXPECT_EQUAL( 1, rig._service._ticketCount );
}

/** @brief [AccountTicketConnectLoopbackTest] 티켓을 기다리는 중 서버가 끊기면 남은 시한(60 초)을 기다리지 않고 바로 "티켓 거절" 로 끝난다 */
SW_TEST_CASE( AccountTicketConnectLoopbackTest, DropWhileWaitingForTheTicketFailsWithoutWaitingForTheDeadline )
{
    // 틀리면: 끊긴 요청의 실패를 보지 않으면 시한까지 Pending 이라 실패 단언과 경과 상한이 진다.
    TicketConnectRig rig;
    rig._service._holdMethod = AccountMethod::IssueGameTicket::kID;
    rig.startServer();
    SW_REQUIRE_TRUE( rig.begin( 60000 ).has_value() );
    const MonotonicMilliseconds started = rig._now;
    while ( rig._service._ticketCount == 0 && rig._now - started < 5000 )
    {
        const expected<AccountTicketStep> waiting = rig.step();
        SW_REQUIRE_TRUE( waiting.has_value() );
    }
    SW_REQUIRE_TRUE( rig._service._ticketCount == 1 );
    rig.dropServer();
    const MonotonicMilliseconds       droppedAt = rig._now;
    const expected<AccountTicketStep> result    = rig.runFor( 5000 );
    SW_EXPECT_TRUE( TicketConnectRig::failedWith( result, "game ticket refused" ) );
    SW_EXPECT_TRUE( rig._now - droppedAt < 1000 );
    SW_EXPECT_EQUAL( 0, rig._credentials._tokenSize );
}
