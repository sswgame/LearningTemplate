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
