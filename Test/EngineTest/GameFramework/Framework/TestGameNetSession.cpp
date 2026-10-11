#include "pch.h"

#include "Core/Network/Transport/UDPNetTransport.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Base/Foundation/Framework/Net/GameNetSession.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

using namespace sw;

namespace
{
    /** @brief 연결을 기다리는 시한(밀리초) — 횟수가 아니라 시간이다. */
    constexpr int64 kConnectWaitMilliseconds = 10000;

    /** @brief 비어 있는 UDP 포트 하나 — 잠깐 열었다 닫는다. */
    uint16 findFreePort()
    {
        UDPNetTransport probe;
        if ( probe.open( 0 ) == false )
            return 0;
        const uint16 port = probe.getLocalAddress()._port;
        probe.close();
        return port;
    }
} // namespace

/**
 * @brief [GameNetSessionTest] 클라이언트가 서버보다 먼저 서도 시간 초과 뒤 다시 시도해 붙는다(멀티 프로세스 재생은 프로세스 기동 순서를 보장하지 않는다)
 * @details 연결 시한을 짧게 줘 첫 시도가 서버 없이 시간 초과로 끝나게 한 뒤 서버를 연다. 다시 시도하지 않으면 클라이언트는 영영 붙지 않는다.
 */
SW_TEST_CASE( GameNetSessionTest, ClientRetriesUntilTheServerStarts )
{
    const uint16 port = findFreePort();
    SW_ASSERT_TRUE( port != 0 );

    GameNetSessionParams clientParams;
    clientParams._role                         = GameNetRole::Client;
    clientParams._serverAddress                = NetAddress::makeLoopback( port );
    clientParams._hostSettings._connectTimeout = 0.2;
    GameNetSession client;
    SW_ASSERT_TRUE( client.initialize( clientParams ) );
    SW_EXPECT_TRUE( client.getRole() == GameNetRole::Client );

    // 첫 시도가 서버 없이 시간 초과로 끝날 만큼 기다린다.
    const Deadline firstAttempt = Deadline::afterMilliseconds( 500 );
    while ( firstAttempt.isExpired() == false )
    {
        client.update();
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }
    SW_EXPECT_EQUAL( 0, client.getConnectedCount() );

    GameNetSessionParams serverParams;
    serverParams._role       = GameNetRole::Server;
    serverParams._listenPort = port;
    GameNetSession server;
    SW_ASSERT_TRUE( server.initialize( serverParams ) );
    SW_EXPECT_EQUAL( port, server.getListenPort() );

    const Deadline deadline = Deadline::afterMilliseconds( kConnectWaitMilliseconds );
    while ( ( client.getConnectedCount() == 0 || server.getConnectedCount() == 0 ) && deadline.isExpired() == false )
    {
        client.update();
        server.update();
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }
    SW_EXPECT_EQUAL( 1, client.getConnectedCount() );
    SW_EXPECT_EQUAL( 1, server.getConnectedCount() );

    client.shutdown();
    server.shutdown();
    SW_EXPECT_FALSE( client.isActive() );
    SW_EXPECT_FALSE( server.isActive() );
}

/**
 * @brief [GameNetSessionTest] 이미 쓰이는 포트로는 서버를 열지 못하고 세션은 비활성으로 남는다(게임은 혼자 뜬다)
 */
SW_TEST_CASE( GameNetSessionTest, ServerOnAPortInUseStaysInactive )
{
    UDPNetTransport holder;
    SW_ASSERT_TRUE( holder.open( 0 ) );
    GameNetSessionParams params;
    params._role       = GameNetRole::Server;
    params._listenPort = holder.getLocalAddress()._port;
    GameNetSession session;
    SW_EXPECT_FALSE( session.initialize( params ) );
    SW_EXPECT_FALSE( session.isActive() );
    holder.close();
}

/**
 * @brief [GameNetSessionTest] 서버가 update 로 거두지 못한 연결도 shutdown 이 로그에 남긴다(접속 직후 같은 틱에 종료 명령이 온 전용 서버)
 * @details 전용 서버는 틱 앞에서 콘솔 명령을 먼저 돌려 quit 이면 그 틱을 돌지 않는다. 그때 네트워크 스레드가 받아 둔 연결 사건을 shutdown 이 버리면
 *          `Net session client 0 connected` 줄이 없어 접속을 로그로 확인하는 쪽(ServerBootTest · 멀티 프로세스 재생)이 놓친다.
 */
SW_TEST_CASE( GameNetSessionTest, ShutdownLogsConnectionsTheServerDidNotUpdate )
{
    const uint16 port = findFreePort();
    SW_ASSERT_TRUE( port != 0 );

    GameNetSessionParams serverParams;
    serverParams._role       = GameNetRole::Server;
    serverParams._listenPort = port;
    GameNetSession server;
    SW_ASSERT_TRUE( server.initialize( serverParams ) );

    GameNetSessionParams clientParams;
    clientParams._role          = GameNetRole::Client;
    clientParams._serverAddress = NetAddress::makeLoopback( port );
    GameNetSession client;
    SW_ASSERT_TRUE( client.initialize( clientParams ) );

    // 서버는 update 하지 않는다 — 연결은 네트워크 스레드가 받고, 사건은 서버 호스트에 쌓여 있다.
    const Deadline deadline = Deadline::afterMilliseconds( kConnectWaitMilliseconds );
    while ( ( client.getConnectedCount() == 0 || server.getConnectedCount() == 0 ) && deadline.isExpired() == false )
    {
        client.update();
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }
    SW_ASSERT_EQUAL( 1, server.getConnectedCount() );

    atomic<uint32>       connectedLineCount{ 0 };
    const DelegateHandle handle = Logger::addGlobalListener( SW_DELEGATE_LAMBDA( LogWrittenDelegate, [&connectedLineCount]( const LogEntry& entry )
    {
        if ( string_view{ entry._message.c_str(), entry._message.size() }.find( "Net session client 0 connected from 127.0.0.1" ) != string_view::npos )
            connectedLineCount.fetch_add( 1 );
    } ) );
    server.shutdown();
    Logger::removeGlobalListener( handle );
    client.shutdown();
    SW_EXPECT_EQUAL( 1u, connectedLineCount.load() );
}
