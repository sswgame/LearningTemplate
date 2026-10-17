#include "pch.h"

#include "Core/Network/Transport/UDPNetTransport.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Observability/ServerServiceRegistry.h"

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
        if ( probe.open( 0, true ) == false )
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
    SW_ASSERT_TRUE( holder.open( 0, true ) );
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
 * @note 그 줄은 Info 라서, Info 를 컴파일하지 않는 배포본(Shipping)에서는 줄을 세지 않고 shutdown 이 닫히는 것만 본다.
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

#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
    atomic<uint32>       connectedLineCount{ 0 };
    const DelegateHandle handle = Logger::registerGlobalListener( SW_DELEGATE_LAMBDA( LogWrittenDelegate, [&connectedLineCount]( const LogEntry& entry )
    {
        if ( string_view{ entry._message.c_str(), entry._message.size() }.find( "Net session client 0 connected from 127.0.0.1" ) != string_view::npos )
            connectedLineCount.fetch_add( 1 );
    } ) );
    server.shutdown();
    Logger::unregisterGlobalListener( handle );
    client.shutdown();
    SW_EXPECT_EQUAL( 1u, connectedLineCount.load() );
#else
    server.shutdown();
    client.shutdown();
    SW_EXPECT_FALSE( server.isActive() );
#endif
}

/**
 * @brief [GameNetSessionTest] 전용 서버의 세션 인자는 서버 설정(`_gamePort` · `_listenAddress`)에서 온다 — 127.0.0.1 은 루프백만, 0.0.0.0 은 모든 인터페이스, 포트 0 은 운영체제가 고름
 */
SW_TEST_CASE( GameNetSessionTest, ServerParamsComeFromTheServerConfig )
{
    ServerConfig config;
    config._listenAddress = "127.0.0.1";
    config._gamePort      = 0;
    GameNetSessionParams params;
    SW_ASSERT_TRUE( GameNetSession::makeServerParams( config, params ) );
    SW_EXPECT_TRUE( params._role == GameNetRole::Server );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( params._listenPort ) );
    SW_EXPECT_TRUE( params._bLoopbackOnly );

    config._listenAddress = "0.0.0.0";
    config._gamePort      = 7777;
    SW_ASSERT_TRUE( GameNetSession::makeServerParams( config, params ) );
    SW_EXPECT_EQUAL( 7777u, static_cast<uint32>( params._listenPort ) );
    SW_EXPECT_FALSE( params._bLoopbackOnly );
}

/**
 * @brief [GameNetSessionTest] 열 수 없는 서버 설정(특정 인터페이스 주소 · 주소가 아닌 글 · 샤드 2 개 이상)은 다른 값으로 대신 열지 않고 거절한다
 */
SW_TEST_CASE( GameNetSessionTest, ServerParamsRefuseAConfigTheyCannotOpen )
{
    GameNetSessionParams params;
    ServerConfig         config;
    config._listenAddress = "10.0.0.5";
    SW_EXPECT_FALSE( GameNetSession::makeServerParams( config, params ) );
    config._listenAddress = "not-an-address";
    SW_EXPECT_FALSE( GameNetSession::makeServerParams( config, params ) );
    config._listenAddress      = "127.0.0.1";
    config._gamePortShardCount = 2;
    SW_EXPECT_FALSE( GameNetSession::makeServerParams( config, params ) );
    SW_EXPECT_TRUE( params._role == GameNetRole::None );
}

/**
 * @brief [GameNetSessionTest] 다시 선 게임 인스턴스(핫 리로드)의 `openFromLaunch` 는 열린 세션을 그대로 둔다 — 포트가 같고 붙은 클라이언트가 끊기지 않는다
 * @details 세션이 게임 인스턴스 수명이던 때는 리로드가 세션을 닫고 다시 열어 클라이언트가 시간 초과로 끊겼다.
 */
SW_TEST_CASE( GameNetSessionTest, OpenFromLaunchKeepsAnOpenSessionAcrossInstances )
{
    ServerConfig config;
    config._listenAddress = "127.0.0.1";
    config._gamePort      = 0;
    ServerServiceContext context;
    context._pServerConfig = &config;
    ServerServiceRegistry::bindHostContext( &context );
    GameNetSession server;
    server.openFromLaunch(); // 첫 게임 인스턴스
    ServerServiceRegistry::bindHostContext( nullptr );
    SW_ASSERT_TRUE( server.isActive() );
    const uint16 port = server.getListenPort();

    GameNetSessionParams clientParams;
    clientParams._role          = GameNetRole::Client;
    clientParams._serverAddress = NetAddress::makeLoopback( port );
    GameNetSession client;
    SW_ASSERT_TRUE( client.initialize( clientParams ) );
    const Deadline deadline = Deadline::afterMilliseconds( kConnectWaitMilliseconds );
    while ( ( client.getConnectedCount() == 0 || server.getConnectedCount() == 0 ) && deadline.isExpired() == false )
    {
        client.update();
        server.update();
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }
    SW_ASSERT_EQUAL( 1, server.getConnectedCount() );

    server.openFromLaunch(); // 리로드로 다시 선 인스턴스 — 서버 문맥이 없어도 열린 세션을 이어받는다
    SW_EXPECT_TRUE( server.isActive() );
    SW_EXPECT_EQUAL( port, server.getListenPort() );
    client.update();
    server.update();
    SW_EXPECT_EQUAL( 1, server.getConnectedCount() );
    SW_EXPECT_EQUAL( 1, client.getConnectedCount() );
    client.shutdown();
    server.shutdown();
}

/**
 * @brief [GameNetSessionTest] 실행 인자가 없으면 서버 문맥(전용 서버)이 있을 때만 그 설정의 게임 포트로 연다 — 리슨 호스트 · 테스트처럼 문맥이 없으면 열지 않는다
 */
SW_TEST_CASE( GameNetSessionTest, LaunchParamsOpenTheGamePortOnlyOnADedicatedServer )
{
    GameNetSessionParams params;
    SW_EXPECT_FALSE( GameNetSession::makeLaunchParams( params ) ); // 문맥이 없다(App · 테스트)
    ServerConfig config;
    config._listenAddress = "127.0.0.1";
    config._gamePort      = 0;
    ServerServiceContext context;
    context._pServerConfig = &config;
    ServerServiceRegistry::bindHostContext( &context );
    const bool bServer = GameNetSession::makeLaunchParams( params );
    ServerServiceRegistry::bindHostContext( nullptr );
    SW_ASSERT_TRUE( bServer );
    SW_EXPECT_TRUE( params._role == GameNetRole::Server );
    SW_EXPECT_TRUE( params._bLoopbackOnly );
}
