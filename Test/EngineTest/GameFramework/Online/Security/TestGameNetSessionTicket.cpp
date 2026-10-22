#include "pch.h"

#include "Core/Network/Connection/NetHostSecurity.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/Net/GameConnectSecurity.h"
#include "Engine/Net/GameNetSession.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <cstring>
#include <thread>

// 접속 티켓을 내미는 클라이언트(OnlineKits F5 의 클라이언트 쪽) — 서버는 서버 설정 `_bGameConnectTicket` 으로 만든 실제 게임 포트이고,
// 암호 제공자는 GameFramework 가 서며 넣은 것이다(그래서 GameFramework 를 링크하는 EngineTest).

using namespace sw;

namespace
{
    constexpr int64 kWaitMilliseconds = 5000;

    /** @brief 로그인 키트 흉내 — 토큰 "ticket" → 세션 비밀(0x5A 32 바이트) · 주체 7. */
    class TicketAuthenticator final : public INetConnectAuthenticator
    {
    public:
        bool findSessionSecret( const uint8* pToken, int32 tokenSize, NetSessionSecret& outSecret, uint64& outPrincipalID ) override
        {
            if ( tokenSize != 6 || std::memcmp( pToken, "ticket", 6 ) != 0 )
                return false;
            std::memset( outSecret._arrByte, 0x5A, sizeof( outSecret._arrByte ) );
            outPrincipalID = 7;
            return true;
        }
    };

    NetConnectCredentials makeTicket( uint8 secretByte )
    {
        NetConnectCredentials credentials;
        credentials._tokenSize = 6;
        std::memcpy( credentials._arrToken, "ticket", 6 );
        std::memset( credentials._secret._arrByte, secretByte, sizeof( credentials._secret._arrByte ) );
        return credentials;
    }

    /** @brief 티켓 서버 — 서버 설정의 티켓 켜기가 만든 인자와 묶인 인증기. 소멸에서 인증기를 푼다. */
    struct TicketServer
    {
        TicketAuthenticator _authenticator{};
        GameNetSession      _session{};
        bool                _bOpened{ false };

        TicketServer()
        {
            ServerConfig config;
            config._listenAddress      = "127.0.0.1";
            config._gamePort           = 0;
            config._bGameConnectTicket = true;
            GameNetSessionParams params;
            if ( GameNetSession::makeServerParams( config, params ) == false )
                return;
            _bOpened = GameConnectSecurity::get().bindAuthenticator( &_authenticator ) && _session.initialize( params );
        }

        ~TicketServer()
        {
            _session.shutdown();
            GameConnectSecurity::get().unbindAuthenticator( &_authenticator );
        }
    };

    void pump( GameNetSession& server, GameNetSession& client, int64 milliseconds, bool ( *pfnDone )( GameNetSession&, GameNetSession& ) )
    {
        const Deadline deadline = Deadline::afterMilliseconds( milliseconds );
        while ( deadline.isExpired() == false && pfnDone( server, client ) == false )
        {
            server.update();
            client.update();
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
    }

    bool areConnected( GameNetSession& server, GameNetSession& client )
    {
        return server.getConnectedCount() == 1 && client.getConnectedCount() == 1;
    }

    bool neverDone( GameNetSession&, GameNetSession& )
    {
        return false;
    }
} // namespace

/**
 * @brief [GameNetSessionTicketTest] 티켓을 내미는 클라이언트는 티켓 서버(암호화 · 토큰 결속)에 붙는다
 */
SW_TEST_CASE( GameNetSessionTicketTest, ClientWithTheRightTicketConnects )
{
    TicketServer server;
    SW_ASSERT_TRUE( server._bOpened );
    GameNetSessionParams params;
    SW_ASSERT_TRUE( GameNetSession::makeTicketClientParams( NetAddress::makeLoopback( server._session.getListenPort() ), makeTicket( 0x5A ), params ) );
    GameNetSession client;
    SW_ASSERT_TRUE( client.initialize( params ) );
    pump( server._session, client, kWaitMilliseconds, &areConnected );
    SW_EXPECT_EQUAL( 1, server._session.getConnectedCount() );
    SW_EXPECT_EQUAL( 1, client.getConnectedCount() );
    client.shutdown();
}

/**
 * @brief [GameNetSessionTicketTest] 틀린 비밀의 티켓은 거절되고, 거절된 티켓은 지워져 같은 티켓으로 다시 붙지 않는다
 */
SW_TEST_CASE( GameNetSessionTicketTest, ARefusedTicketIsDroppedAndNeedsANewOne )
{
    TicketServer server;
    SW_ASSERT_TRUE( server._bOpened );
    GameNetSessionParams params;
    SW_ASSERT_TRUE( GameNetSession::makeTicketClientParams( NetAddress::makeLoopback( server._session.getListenPort() ), makeTicket( 0x11 ), params ) );
    GameNetSession client;
    SW_ASSERT_TRUE( client.initialize( params ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "the server and the client log the refused connection" );
        pump( server._session, client, 1500, &neverDone );
        SW_EXPECT_EQUAL( 0, server._session.getConnectedCount() );
        SW_EXPECT_EQUAL( 0, client.getConnectedCount() );
        SW_EXPECT_FALSE( client.reconnect() ); // 거절된 티켓을 지웠다 — 새 티켓 없이 자격 없는 연결로 대신 붙지 않는다
    }
    client.shutdown();
}

/**
 * @brief [GameNetSessionTicketTest] 자격 없이 붙는 클라이언트(`-gv_netConnectAddress` 의 길)는 티켓 서버에 붙지 못한다
 */
SW_TEST_CASE( GameNetSessionTicketTest, AClientWithoutCredentialsIsRefusedByATicketServer )
{
    TicketServer server;
    SW_ASSERT_TRUE( server._bOpened );
    GameNetSessionParams params;
    params._role          = GameNetRole::Client;
    params._serverAddress = NetAddress::makeLoopback( server._session.getListenPort() );
    GameNetSession client;
    SW_ASSERT_TRUE( client.initialize( params ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "the client logs the refused protocol" );
        pump( server._session, client, 1500, &neverDone );
    }
    SW_EXPECT_EQUAL( 0, server._session.getConnectedCount() );
    SW_EXPECT_EQUAL( 0, client.getConnectedCount() );
    client.shutdown();
}

/**
 * @brief [GameNetSessionTicketTest] 티켓 클라이언트 인자는 빈 티켓 · 잘못된 주소를 거절하고, 티켓은 평문 연결에 실리지 않는다
 */
SW_TEST_CASE( GameNetSessionTicketTest, TicketParamsRefuseWhatCannotBindACredential )
{
    GameNetSessionParams params;
    {
        SW_TEST_DEFENSIVE_SCOPE( "the refused params are logged" );
        SW_EXPECT_FALSE( GameNetSession::makeTicketClientParams( NetAddress::makeLoopback( 7777 ), NetConnectCredentials{}, params ) );
        SW_EXPECT_FALSE( GameNetSession::makeTicketClientParams( NetAddress{}, makeTicket( 0x5A ), params ) );
    }
    SW_ASSERT_TRUE( GameNetSession::makeTicketClientParams( NetAddress::makeLoopback( 7777 ), makeTicket( 0x5A ), params ) );
    params._hostSettings._security._mode = NetSecurityMode::Off; // 평문으로 바꾸면 비밀을 싣지 못한다
    GameNetSession session;
    {
        SW_TEST_DEFENSIVE_SCOPE( "the plaintext ticket client is logged" );
        SW_EXPECT_FALSE( session.initialize( params ) );
    }
    SW_EXPECT_FALSE( session.isActive() );
}
