#include "pch.h"

#include "Core/Network/Transport/UDPNetTransport.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Observability/ServerServiceRegistry.h"

#include "TestFramework/TestEnvironment.h"
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
    // 범위는 세션(네트워크 스레드)보다 먼저 서서 나중에 내려간다 — 클라이언트 스레드는 `initialize` 직후부터 연결 시도를 하고 그 실패를 스스로 로그에 쓴다.
    SW_TEST_DEFENSIVE_SCOPE( "a client that cannot reach the server reports and retries" ); // 거절 · 실패 로그가 이 테스트가 보려는 동작이다
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
    SW_TEST_DEFENSIVE_SCOPE( "a port in use leaves the server inactive" ); // 거절 · 실패 로그가 이 테스트가 보려는 동작이다
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
    atomic<uint32> connectedLineCount{ 0 };
    Subscription   subscription = Logger::registerGlobalListener( SW_DELEGATE_LAMBDA( LogWrittenDelegate, [&connectedLineCount]( const LogEntry& entry )
      {
        if ( string_view{ entry._message.c_str(), entry._message.size() }.find( "Net session client 0 connected from 127.0.0.1" ) != string_view::npos )
            connectedLineCount.fetch_add( 1 );
    } ) );
    server.shutdown();
    subscription.reset();
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
    GameNetSession       session;
    GameNetSessionParams params;
    SW_ASSERT_TRUE( session.makeServerParams( config, params ) );
    SW_EXPECT_TRUE( params._role == GameNetRole::Server );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( params._listenPort ) );
    SW_EXPECT_TRUE( params._bLoopbackOnly );

    config._listenAddress = "0.0.0.0";
    config._gamePort      = 7777;
#if defined( SW_SHIPPING )
    // Shipping 서버는 평문 게임 포트를 루프백 밖에 열지 않는다(결정 기록 5-9) — 접속 티켓 없이는 거절하고 인자를 비운다.
    {
        SW_TEST_DEFENSIVE_SCOPE( "a shipping server refuses a plain non-loopback game port" ); // 거절 로그가 이 호출이 보려는 동작이다
        SW_EXPECT_FALSE( session.makeServerParams( config, params ) );
    }
    SW_EXPECT_TRUE( params._role == GameNetRole::None );
#else
    SW_ASSERT_TRUE( session.makeServerParams( config, params ) );
    SW_EXPECT_EQUAL( 7777u, static_cast<uint32>( params._listenPort ) );
    SW_EXPECT_FALSE( params._bLoopbackOnly );
#endif
}

/**
 * @brief [GameNetSessionTest] 열 수 없는 서버 설정(특정 인터페이스 주소 · 주소가 아닌 글 · 포트 0 의 샤드 여럿 · 범위를 넘는 연이은 포트 · 리눅스가 아닌 한 포트 나누기)은 다른 값으로 대신 열지 않고 거절한다
 */
SW_TEST_CASE( GameNetSessionTest, ServerParamsRefuseAConfigTheyCannotOpen )
{
    SW_TEST_DEFENSIVE_SCOPE( "a server config that cannot be opened is refused with the reason" ); // 거절 · 실패 로그가 이 테스트가 보려는 동작이다
    GameNetSession       session;
    GameNetSessionParams params;
    ServerConfig         config;
    config._listenAddress = "10.0.0.5";
    SW_EXPECT_FALSE( session.makeServerParams( config, params ) );
    config._listenAddress = "not-an-address";
    SW_EXPECT_FALSE( session.makeServerParams( config, params ) );
    config._listenAddress      = "127.0.0.1";
    config._gamePort           = 0;
    config._gamePortShardCount = 2; // 운영체제가 고른 포트로는 "첫 포트 + 샤드" 배정을 지킬 수 없다
    SW_EXPECT_FALSE( session.makeServerParams( config, params ) );
    SW_EXPECT_TRUE( params._role == GameNetRole::None );
    config._gamePort = 65535; // 둘째 샤드가 65536 이 된다
    SW_EXPECT_FALSE( session.makeServerParams( config, params ) );
#if !defined( SW_PLATFORM_LINUX )
    config._gamePort       = 7777;
    config._bGamePortReuse = true; // SO_REUSEPORT 는 리눅스만 — 연이은 포트로 바꿔 열지 않는다
    SW_EXPECT_FALSE( session.makeServerParams( config, params ) );
#endif
}

/**
 * @brief [GameNetSessionTest] 샤드 여럿의 서버 설정은 첫 포트와 샤드 수를 그대로 넘긴다
 */
SW_TEST_CASE( GameNetSessionTest, ServerParamsCarryTheShardLayout )
{
    ServerConfig config;
    config._listenAddress      = "127.0.0.1";
    config._gamePort           = 7777;
    config._gamePortShardCount = 4;
    GameNetSession       session;
    GameNetSessionParams params;
    SW_ASSERT_TRUE( session.makeServerParams( config, params ) );
    SW_EXPECT_EQUAL( 4, params._shardCount );
    SW_EXPECT_EQUAL( 7777u, static_cast<uint32>( params._listenPort ) );
    SW_EXPECT_FALSE( params._bShardsSharePort );
}

/**
 * @brief [GameNetSessionTest] 샤드 둘인 서버는 연이은 두 포트를 열고, 각 포트에 붙은 클라이언트가 접힌 연결 ID(샤드 × 간격 + 샤드 안의 ID)로 하나의 목록에 보인다
 * @details 키트는 `getPeers()` 만 보므로, 두 ID 가 겹치지 않고 각 ID 로 보낸 메시지가 그 샤드의 클라이언트에게만 닿아야 한다.
 */
SW_TEST_CASE( GameNetSessionTest, ShardedServerFoldsBothShardsIntoOnePeerList )
{
    // 연이은 두 포트가 비어 있어야 한다 — 하나를 고르고 다음 포트도 열어 본다.
    uint16 firstPort = 0;
    for ( int32 attempt = 0; attempt < 8 && firstPort == 0; ++attempt )
    {
        const uint16    candidate = findFreePort();
        UDPNetTransport probe;
        const bool      bNextFree = candidate != 0 && candidate < 65535 && probe.open( static_cast<uint16>( candidate + 1 ), true );
        probe.close();
        firstPort = bNextFree ? candidate : uint16{ 0 };
    }
    SW_ASSERT_TRUE( firstPort != 0 );

    GameNetSessionParams serverParams;
    serverParams._role                         = GameNetRole::Server;
    serverParams._listenPort                   = firstPort;
    serverParams._shardCount                   = 2;
    serverParams._hostSettings._maxConnections = 8;
    GameNetSession server;
    SW_ASSERT_TRUE( server.initialize( serverParams ) );
    SW_EXPECT_EQUAL( 2, server.getShardCount() );
    SW_EXPECT_EQUAL( firstPort, server.getListenPort() );

    GameNetSessionParams firstParams;
    firstParams._role                 = GameNetRole::Client;
    firstParams._serverAddress        = NetAddress::makeLoopback( firstPort );
    GameNetSessionParams secondParams = firstParams;
    secondParams._serverAddress       = NetAddress::makeLoopback( static_cast<uint16>( firstPort + 1 ) );
    GameNetSession first;
    GameNetSession second;
    SW_ASSERT_TRUE( first.initialize( firstParams ) );
    SW_ASSERT_TRUE( second.initialize( secondParams ) );
    const Deadline deadline = Deadline::afterMilliseconds( kConnectWaitMilliseconds );
    while ( ( first.getConnectedCount() == 0 || second.getConnectedCount() == 0 || server.getConnectedCount() < 2 ) && deadline.isExpired() == false )
    {
        first.update();
        second.update();
        server.update();
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }
    SW_ASSERT_EQUAL( 2, server.getConnectedCount() );

    vector<int32> listConnection;
    server.getPeers().collectConnected( listConnection );
    SW_ASSERT_EQUAL( size_t{ 2 }, listConnection.size() );
    SW_EXPECT_EQUAL( 0, listConnection[0] ); // 첫 샤드의 첫 연결
    SW_EXPECT_EQUAL( 8, listConnection[1] ); // 둘째 샤드의 첫 연결 = 1 × 간격(8) + 0
    SW_EXPECT_TRUE( server.getPeers().getConnectionAddress( 8 ).isValid() );
    SW_EXPECT_TRUE( server.getPeers().getConnectionState( 16 ) == NetConnectionState::Disconnected ); // 없는 샤드

    first.shutdown();
    second.shutdown();
    server.shutdown();
}

/**
 * @brief [GameNetSessionTest] 서버가 보낸 서버 틱 번호를 클라이언트가 받아 답한다(클라이언트 쪽 상태 해시 기록이 이 번호를 쓴다)
 */
SW_TEST_CASE( GameNetSessionTest, ClientLearnsTheServerTick )
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

    uint64 tick = 0;
    SW_EXPECT_FALSE( client.tryGetServerTick( tick ) ); // 아직 받지 않았다
    const Deadline deadline = Deadline::afterMilliseconds( kConnectWaitMilliseconds );
    while ( client.tryGetServerTick( tick ) == false && deadline.isExpired() == false )
    {
        server.sendServerTick( 42 );
        server.update();
        client.update();
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }
    SW_ASSERT_TRUE( client.tryGetServerTick( tick ) );
    SW_EXPECT_EQUAL( uint64{ 42 }, tick );
    SW_EXPECT_FALSE( server.tryGetServerTick( tick ) ); // 서버는 받는 쪽이 아니다

    client.shutdown();
    server.shutdown();
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
    GameNetSession server;
    bool           bFirstOpened = false;
    {
        // 전용 서버처럼 서버 문맥을 범위에 빌려 건다 — 나가면 풀린다.
        test::ScopedEngineServiceOverride hostScope;
        SW_ASSERT_TRUE( hostScope.bindObject( &context ) );
        SW_ASSERT_TRUE( hostScope.bind() == nullptr );
        bFirstOpened = server.openFromLaunch(); // 첫 게임 인스턴스
    }
    SW_ASSERT_TRUE( bFirstOpened );
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

    SW_EXPECT_TRUE( server.openFromLaunch() ); // 리로드로 다시 선 인스턴스 — 서버 문맥이 없어도 열린 세션을 이어받는다
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
    GameNetSession       session;
    GameNetSessionParams params;
    SW_EXPECT_TRUE( session.makeLaunchParams( params ) == GameNetLaunch::None ); // 문맥이 없다(App · 테스트)
    ServerConfig config;
    config._listenAddress = "127.0.0.1";
    config._gamePort      = 0;
    ServerServiceContext context;
    context._pServerConfig = &config;
    GameNetLaunch launch   = GameNetLaunch::None;
    {
        test::ScopedEngineServiceOverride hostScope;
        SW_ASSERT_TRUE( hostScope.bindObject( &context ) );
        SW_ASSERT_TRUE( hostScope.bind() == nullptr );
        launch = session.makeLaunchParams( params );
    }
    SW_ASSERT_TRUE( launch == GameNetLaunch::Open );
    SW_EXPECT_TRUE( params._role == GameNetRole::Server );
    SW_EXPECT_TRUE( params._bLoopbackOnly );
}

/**
 * @brief [GameNetSessionTest] 전용 서버 설정이 틀리면 "세션 없음" 이 아니라 `Invalid` 이고 `openFromLaunch` 가 false 다 — 게임 인스턴스가 혼자 하는 게임으로 서지 않는다
 */
SW_TEST_CASE( GameNetSessionTest, AnInvalidServerConfigStopsTheLaunchInsteadOfStartingAlone )
{
    SW_TEST_DEFENSIVE_SCOPE( "an invalid server config stops the launch" ); // 거절 · 실패 로그가 이 테스트가 보려는 동작이다
    ServerConfig config;
    config._listenAddress = "10.0.0.5"; // 게임 포트는 0.0.0.0 · 127.0.0.1 만 묶는다
    config._gamePort      = 0;
    ServerServiceContext context;
    context._pServerConfig = &config;
    GameNetSession       session;
    GameNetSessionParams params;
    GameNetLaunch        launch  = GameNetLaunch::None;
    bool                 bOpened = true;
    {
        test::ScopedEngineServiceOverride hostScope;
        SW_ASSERT_TRUE( hostScope.bindObject( &context ) );
        SW_ASSERT_TRUE( hostScope.bind() == nullptr );
        launch  = session.makeLaunchParams( params );
        bOpened = session.openFromLaunch();
    }
    SW_EXPECT_TRUE( launch == GameNetLaunch::Invalid );
    SW_EXPECT_FALSE( bOpened );
    SW_EXPECT_FALSE( session.isActive() );
}

/**
 * @brief [GameNetSessionTest] `net.stats` 의 복제 셈은 올린 누적 값을 그대로(줄어든 값도 덮어) 바이트 큰 순서로 상한까지만 싣는다 — 에디터 Profiler 표가 읽는 약속이다
 * @details 표는 두 보고의 차로 초당 값을 내고 줄어든 값 · 새 이름을 0 으로 읽는다(`EditorNetProfilerUtil::computeReplicaRates`). 그래서 세션은 값을 대신 눌러 앉히지 않고(최댓값 유지 없음) 올린 그대로 싣는다.
 */
SW_TEST_CASE( GameNetSessionTest, ReplicaStatsAreReportedAsUploadedLargestBytesFirstUpToTheCap )
{
    const uint16 port = findFreePort();
    SW_ASSERT_TRUE( port != 0 );
    GameNetSessionParams params;
    params._role       = GameNetRole::Server;
    params._listenPort = port;
    GameNetSession session;
    SW_ASSERT_TRUE( session.initialize( params ) );

    for ( int32 index = 0; index < NetStatsReport::kMaxReplicaEntry + 2; ++index )
    {
        SW_ASSERT_TRUE( session.setReplicaStats( "kit" + to_string( index ), 1, static_cast<uint64>( 100 + index ) ) );
    }
    NetStatsReport report = session.makeStatsReport();
    SW_ASSERT_EQUAL( static_cast<size_t>( NetStatsReport::kMaxReplicaEntry ), report._listReplicaEntry.size() );
    SW_EXPECT_TRUE( report._listReplicaEntry[0]._name == "kit9" ); // 바이트가 가장 큰 것이 앞, 가장 작은 둘(kit0 · kit1)은 상한 밖

    // 키트가 다시 시작해 누적 값이 줄면 줄어든 값 그대로 싣는다 — 읽는 쪽이 줄어듦을 0 으로 읽는다.
    SW_ASSERT_TRUE( session.setReplicaStats( "kit9", 0, 5 ) );
    report = session.makeStatsReport();
    SW_ASSERT_EQUAL( static_cast<size_t>( NetStatsReport::kMaxReplicaEntry ), report._listReplicaEntry.size() );
    SW_EXPECT_TRUE( report._listReplicaEntry[0]._name == "kit8" );
    SW_EXPECT_TRUE( report._listReplicaEntry.back()._name == "kit1" );
    session.shutdown();
}

/**
 * @brief [GameNetSessionTest] 복제 셈의 상한은 누적 바이트뿐 아니라 지난 보고 뒤 늘어난 바이트로도 고른다 — 총량이 작아도 지금 활발한 이름이 보고에서 빠지지 않는다
 * @details Profiler 표는 두 보고의 차(초당 바이트)로 순위를 낸다. 누적만 보면 오래 큰 이름이 상한을 채워 새로 활발해진 작은 이름이 표에 오르지 못한다.
 */
SW_TEST_CASE( GameNetSessionTest, ReplicaStatsCapKeepsRecentlyActiveSmallNames )
{
    const uint16 port = findFreePort();
    SW_ASSERT_TRUE( port != 0 );
    GameNetSessionParams params;
    params._role       = GameNetRole::Server;
    params._listenPort = port;
    GameNetSession session;
    SW_ASSERT_TRUE( session.initialize( params ) );

    for ( int32 index = 0; index < NetStatsReport::kMaxReplicaEntry + 2; ++index )
    {
        SW_ASSERT_TRUE( session.setReplicaStats( "big" + to_string( index ), 1, static_cast<uint64>( 100000 + index ) ) );
    }
    SW_ASSERT_TRUE( session.setReplicaStats( "small", 1, 10 ) );
    (void)session.makeStatsReport(); // 기준선: 이 뒤의 늘어남만 센다

    // 큰 이름들은 가만히 있고 작은 이름만 늘었다 — 누적으로는 상한 밖이지만 늘어난 양으로는 첫째다.
    SW_ASSERT_TRUE( session.setReplicaStats( "small", 5, 500 ) );
    const NetStatsReport report = session.makeStatsReport();
    SW_ASSERT_EQUAL( static_cast<size_t>( NetStatsReport::kMaxReplicaEntry ), report._listReplicaEntry.size() );
    bool bFoundSmall = false;
    for ( const NetStatsReplicaEntry& entry : report._listReplicaEntry )
    {
        bFoundSmall = bFoundSmall || entry._name == "small";
    }
    SW_EXPECT_TRUE( bFoundSmall );
    session.shutdown();
}
