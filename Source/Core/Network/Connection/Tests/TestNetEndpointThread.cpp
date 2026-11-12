#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Connection/NetEndpointThread.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Network/Tests/NetTestUtil.h"
#include "Core/Network/Transport/UDPNetTransport.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestWait.h"
#include "Core/Testing/ThreadTestAccess.h"
#include "Core/Time/MonotonicClock.h"

// 네트워크 스레드 · 비동기 — 전용 스레드가 도는 엔드포인트(루프백 · 실제 UDP), 비동기 연결(성공 · 가득 참 · 타임아웃 · 끊음 · 잘못된 주소),
// 여러 게임 스레드가 동시에 보내고 받기(스레드마다 순서 · 빠짐 · 겹침 없음), 게임 스레드가 멈춰도 끊기지 않기.

using namespace sw;
using test::NetTestUtil;

namespace
{
    struct NetEndpointThreadTestInternal
    {
        static constexpr uint32 kFutureWaitMilli = 5000;

        static vector<uint8> makeTaggedMessage( int32 sender, int32 sequence )
        {
            vector<uint8> buffer( 8, 0 );
            for ( int32 index = 0; index < 4; ++index )
            {
                buffer[static_cast<size_t>( index )]     = static_cast<uint8>( sender >> ( index * 8 ) );
                buffer[static_cast<size_t>( index + 4 )] = static_cast<uint8>( sequence >> ( index * 8 ) );
            }
            return buffer;
        }

        static int32 readInt( const vector<uint8>& buffer, int32 offset )
        {
            int32 value = 0;
            for ( int32 index = 0; index < 4; ++index )
            {
                value |= static_cast<int32>( buffer[static_cast<size_t>( offset + index )] ) << ( index * 8 );
            }
            return value;
        }

        /** @brief 서버(포트 4000, listen) + 클라이언트 @p clientCount(포트 5000 부터 — 연결은 케이스가)를 세우고 모두 자기 네트워크 스레드에서 돌립니다. */
        static bool startThreadedCluster( test::LoopbackCluster& cluster, int32 clientCount, const NetEndpointConfig& settings )
        {
            if ( cluster.addEndpoint( 4000, settings ).has_value() == false || cluster.getServer().listen() == false )
                return false;
            for ( int32 index = 0; index < clientCount; ++index )
            {
                if ( cluster.addEndpoint( static_cast<uint16>( 5000 + index ), settings ).has_value() == false )
                    return false;
            }
            return cluster.startThreads();
        }
    };
} // namespace

SW_TEST_CASE( NetEndpointThreadTest, ThreadedHostsConnectAsyncAndExchangeWithoutGameUpdates )
{
    test::LoopbackCluster cluster( 3u );
    SW_REQUIRE_TRUE( NetEndpointThreadTestInternal::startThreadedCluster( cluster, 1, NetEndpointConfig{} ) );
    NetEndpoint& client = cluster.getClient( 0 );

    // 비동기 연결 — 게임 스레드는 update 를 부르지 않는다. 후속 작업은 네트워크 스레드에서 잠금 밖에서 불린다.
    atomic<int32>                      continuationCount{ 0 };
    const TaskFuture<NetConnectResult> future = client.connectAsync( NetAddress::makeLoopback( 4000 ) );
    const TaskFuture<int32>            next   = future.then( [&continuationCount]( const NetConnectResult& result )
    {
        continuationCount.fetch_add( 1 );
        return result._clientIndex;
    } );
    SW_REQUIRE_TRUE( future.waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    SW_REQUIRE_TRUE( future.get().isConnected() );
    SW_EXPECT_EQUAL( 0, future.get()._clientIndex );
    SW_REQUIRE_TRUE( next.waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    SW_EXPECT_EQUAL( 0, next.get() );
    SW_EXPECT_EQUAL( 1, continuationCount.load() );
    SW_EXPECT_EQUAL( 0, client.getClientIndex() );
    SW_REQUIRE_TRUE( test::waitUntil( [&cluster]()
    { return cluster.getServer().getConnectedCount() == 1; } ) );

    // 서버 → 클라이언트, 클라이언트 → 서버 신뢰 메시지 — 받는 쪽은 게임 스레드에서 꺼내기만 한다.
    for ( int32 index = 0; index < 50; ++index )
    {
        SW_REQUIRE_TRUE( cluster.getServer().sendMessage( 0, NetChannelType::ReliableOrdered, NetEndpointThreadTestInternal::makeTaggedMessage( 1, index ) ) );
        SW_REQUIRE_TRUE( client.sendMessage( 0, NetChannelType::ReliableOrdered, NetEndpointThreadTestInternal::makeTaggedMessage( 2, index ) ) );
    }
    int32          clientReceived = 0;
    int32          serverReceived = 0;
    int32          connectionID   = -1;
    NetChannelType channel        = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    const bool     bAllReceived = test::waitUntil( [&]()
    {
        while ( client.receiveMessage( connectionID, channel, buffer ) )
        {
            if ( NetEndpointThreadTestInternal::readInt( buffer, 0 ) == 1 && NetEndpointThreadTestInternal::readInt( buffer, 4 ) == clientReceived )
                ++clientReceived;
        }
        while ( cluster.getServer().receiveMessage( connectionID, channel, buffer ) )
        {
            if ( NetEndpointThreadTestInternal::readInt( buffer, 0 ) == 2 && NetEndpointThreadTestInternal::readInt( buffer, 4 ) == serverReceived )
                ++serverReceived;
        }
        return clientReceived == 50 && serverReceived == 50;
    } );
    SW_EXPECT_TRUE( bAllReceived );
    SW_EXPECT_EQUAL( 50, clientReceived );
    SW_EXPECT_EQUAL( 50, serverReceived );
    SW_EXPECT_TRUE( cluster.getThread( 0 ).getUpdateCount() > 0 && cluster.getThread( 1 ).getUpdateCount() > 0 );

    // 통계는 사본으로 — 스레드가 도는 중에도 안전하다.
    NetConnectionStats stats;
    SW_REQUIRE_TRUE( client.getConnectionStats( 0, stats ) );
    SW_EXPECT_TRUE( stats._sentPacketCount > 0 && stats._receivedPacketCount > 0 );
    SW_EXPECT_FALSE( client.getConnectionStats( 3, stats ) );
}

SW_TEST_CASE( NetEndpointThreadTest, GameThreadsSendAndReceiveConcurrently )
{
    // 클라이언트 둘(각자 스레드) — 게임 스레드 넷이 동시에 보내고(한 클라이언트에 둘씩), 서버 쪽은 두 스레드가 동시에 꺼낸다.
    constexpr int32   kSenderCount      = 4;
    constexpr int32   kMessagePerSender = 300;
    NetEndpointConfig settings;
    settings._sendInterval = 1.0 / 120.0;
    test::LoopbackCluster cluster( 3u );
    SW_REQUIRE_TRUE( NetEndpointThreadTestInternal::startThreadedCluster( cluster, 2, settings ) );
    for ( int32 index = 0; index < cluster.getClientCount(); ++index )
    {
        SW_REQUIRE_TRUE( cluster.getClient( index ).connectAsync( NetAddress::makeLoopback( 4000 ) ).waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    }
    SW_REQUIRE_TRUE( test::waitUntil( [&cluster]()
    { return cluster.getServer().getConnectedCount() == 2; } ) );

    vector<sw::thread> listSender;
    for ( int32 sender = 0; sender < kSenderCount; ++sender )
    {
        // 호스트는 스레드를 띄우기 전에 고른다 — 클러스터의 호스트 목록을 여러 스레드가 동시에 만지지 않게(데이터 경쟁 검출기가 잡는다).
        NetEndpoint& client = cluster.getClient( sender % 2 );
        listSender.emplace_back( ThreadTestAccess::getKey(), [&client, sender]()
        {
            for ( int32 sequence = 0; sequence < kMessagePerSender; ++sequence )
            {
                const vector<uint8> message = NetEndpointThreadTestInternal::makeTaggedMessage( sender, sequence );
                // 신뢰 창이 차면 거절된다 — 잠깐 쉬었다가 다시(보내는 속도를 줄이는 쪽은 게임이다).
                while ( client.trySendMessage( 0, NetChannelType::ReliableOrdered, message ) == false )
                {
                    ThreadUtil::sleepForMicroseconds( 200 );
                }
            }
        } );
    }

    // 받는 스레드 둘 — 같은 호스트에서 동시에 꺼낸다. 한 메시지는 한 번만 나와야 한다.
    atomic<int32>      receivedTotal{ 0 };
    atomic<bool>       bStop{ false };
    vector<int32>      arrLastSequence[2];
    vector<int32>      arrCount[2];
    atomic<int32>      orderErrorCount{ 0 };
    vector<sw::thread> listReceiver;
    NetEndpoint&       server = cluster.getServer();
    for ( int32 receiver = 0; receiver < 2; ++receiver )
    {
        arrLastSequence[receiver].assign( kSenderCount, -1 );
        arrCount[receiver].assign( kSenderCount, 0 );
        listReceiver.emplace_back( ThreadTestAccess::getKey(), [&, receiver]()
        {
            int32          connectionID = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            vector<uint8>  buffer;
            while ( bStop.load() == false )
            {
                if ( server.receiveMessage( connectionID, channel, buffer ) == false )
                {
                    ThreadUtil::sleepForMicroseconds( 100 );
                    continue;
                }
                const int32 sender         = NetEndpointThreadTestInternal::readInt( buffer, 0 );
                const int32 sequence       = NetEndpointThreadTestInternal::readInt( buffer, 4 );
                const bool  bInvalidSender = sender < 0 || sender >= kSenderCount || connectionID != ( sender % 2 == 0 ? 0 : 1 );
                if ( bInvalidSender )
                {
                    orderErrorCount.fetch_add( 1 );
                    continue;
                }
                // 받는 스레드 하나 안에서는 같은 보낸 쪽의 번호가 늘기만 한다.
                if ( sequence <= arrLastSequence[receiver][static_cast<size_t>( sender )] )
                    orderErrorCount.fetch_add( 1 );
                arrLastSequence[receiver][static_cast<size_t>( sender )] = sequence;
                ++arrCount[receiver][static_cast<size_t>( sender )];
                receivedTotal.fetch_add( 1 );
            }
        } );
    }
    for ( sw::thread& thread : listSender )
    {
        thread.join();
    }
    const bool bAllReceived = test::waitUntil( [&receivedTotal]()
    { return receivedTotal.load() >= kSenderCount * kMessagePerSender; }, 10.0 );
    // 겹쳐 나올 것이 있으면 세어지게 — 서버 네트워크 스레드가 몇 번 더 돌 때까지(고정 시간이 아니라 돈 횟수로) 받는 스레드를 둔다.
    const uint64 serverUpdateCount = cluster.getThread( 0 ).getUpdateCount();
    SW_EXPECT_TRUE( test::waitUntil( [&cluster, serverUpdateCount]()
    { return cluster.getThread( 0 ).getUpdateCount() >= serverUpdateCount + 10; } ) );
    bStop.store( true );
    for ( sw::thread& thread : listReceiver )
    {
        thread.join();
    }
    SW_EXPECT_TRUE( bAllReceived );
    SW_EXPECT_EQUAL( kSenderCount * kMessagePerSender, receivedTotal.load() );
    SW_EXPECT_EQUAL( 0, orderErrorCount.load() );
    for ( int32 sender = 0; sender < kSenderCount; ++sender )
    {
        SW_EXPECT_EQUAL( kMessagePerSender, arrCount[0][static_cast<size_t>( sender )] + arrCount[1][static_cast<size_t>( sender )] );
    }
}

// 감사 N-8 — 살아 있는 목록 · 빈 슬롯 목록을 네트워크 스레드(열기 · 타임아웃 · 끊김 처리), 보내는 스레드(보내기 · 방송 · 창 넘침 닫기), 꺼내는 스레드(슬롯 놓기)가
// 함께 바꾼다. 연결이 끊기고 다시 붙는 동안 보내고 꺼내도 단언 없이 끝나고, 끝에는 정원만큼 다시 연결돼 번호가 겹치지 않아야 한다.
SW_TEST_CASE( NetEndpointThreadTest, ConnectionsChurnWhileGameThreadsSendAndPoll )
{
    constexpr int32   kClientCount = 4;
    constexpr int32   kChurnRound  = 6;
    NetEndpointConfig settings;
    settings._maxConnections = kClientCount; // 붙잡힌 슬롯을 놓아야 다시 붙는다 — 놓기 길도 경쟁에 든다
    settings._sendInterval   = 1.0 / 120.0;
    settings._debugHold      = NetDebugHold::Off;
    test::LoopbackCluster cluster( 5u );
    SW_REQUIRE_TRUE( NetEndpointThreadTestInternal::startThreadedCluster( cluster, kClientCount, settings ) );
    for ( int32 index = 0; index < kClientCount; ++index )
    {
        SW_REQUIRE_TRUE( cluster.getClient( index ).connectAsync( NetAddress::makeLoopback( 4000 ) ).waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    }
    NetEndpoint& server = cluster.getServer();
    SW_REQUIRE_TRUE( test::waitUntil( [&server]()
    { return server.getConnectedCount() == kClientCount; } ) );

    atomic<bool> bStop{ false };
    // 보내는 스레드 — 연결된 번호마다 신뢰 · 비신뢰를 넣고 방송한다(닫힌 번호면 거절될 뿐이다).
    sw::thread sender( ThreadTestAccess::getKey(), [&server, &bStop]()
    {
        vector<int32> listConnection;
        int32         sequence = 0;
        while ( bStop.load() == false )
        {
            server.collectConnected( listConnection );
            for ( const int32 connectionID : listConnection )
            {
                (void)server.trySendMessage( connectionID, NetChannelType::ReliableOrdered, NetEndpointThreadTestInternal::makeTaggedMessage( connectionID, sequence ) );
                (void)server.sendMessage( connectionID, NetChannelType::Unreliable, NetEndpointThreadTestInternal::makeTaggedMessage( connectionID, sequence ) );
            }
            const vector<uint8> message = NetEndpointThreadTestInternal::makeTaggedMessage( -1, sequence );
            (void)server.broadcast( NetChannelType::Unreliable, message.data(), static_cast<int32>( message.size() ) );
            ++sequence;
            ThreadUtil::sleepForMilliseconds( 1 ); // 클라이언트는 꺼내지 않는다 — 받은 큐 상한(1 MiB)에 닿지 않을 만큼만
        }
    } );
    // 꺼내는 스레드 — 끊김 사건을 꺼내 슬롯을 놓는다(`pollInbound`).
    atomic<int32> disconnectedCount{ 0 };
    sw::thread    poller( ThreadTestAccess::getKey(), [&server, &bStop, &disconnectedCount]()
       {
        NetInbound inbound;
        while ( bStop.load() == false )
        {
            server.pollInbound( inbound );
            for ( const NetEndpointEvent& event : inbound._listEvent )
            {
                disconnectedCount.fetch_add( event._kind == NetEndpointEvent::Kind::Disconnected ? 1 : 0 );
            }
            ThreadUtil::sleepForMicroseconds( 100 );
        }
    } );

    // 이 스레드는 클라이언트를 끊었다 다시 붙인다 — 서버 네트워크 스레드가 끊김을 받아 슬롯을 닫고, 놓인 슬롯을 다시 연다.
    bool bAllReconnected = true;
    for ( int32 round = 0; round < kChurnRound; ++round )
    {
        NetEndpoint& client = cluster.getClient( round % kClientCount );
        client.disconnect( 0 );
        const TaskFuture<NetConnectResult> future = client.connectAsync( NetAddress::makeLoopback( 4000 ) );
        bAllReconnected                           = bAllReconnected && future.waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) && future.get().isConnected();
    }
    const bool bFull = test::waitUntil( [&server]()
    { return server.getConnectedCount() == kClientCount; } );
    bStop.store( true );
    sender.join();
    poller.join();

    SW_EXPECT_TRUE( bAllReconnected );
    SW_EXPECT_TRUE( bFull );
    SW_EXPECT_EQUAL( kChurnRound, disconnectedCount.load() );
    vector<int32> listConnection;
    server.collectConnected( listConnection );
    SW_REQUIRE_EQUAL( kClientCount, static_cast<int32>( listConnection.size() ) );
    for ( int32 index = 0; index < kClientCount; ++index )
    {
        SW_EXPECT_EQUAL( index, listConnection[static_cast<size_t>( index )] ); // 번호 순 · 겹침 없음 · 정원 안
    }
}

SW_TEST_CASE( NetEndpointThreadTest, ConnectAsyncReportsEveryFailure )
{
    NetEndpointConfig settings;
    settings._maxConnections = 1;
    settings._connectTimeout = 0.3;
    test::LoopbackCluster cluster( 3u );
    SW_REQUIRE_TRUE( NetEndpointThreadTestInternal::startThreadedCluster( cluster, 4, settings ) );

    // 잘못된 주소 — 시작부터 못 한다(이미 채워진 future).
    const TaskFuture<NetConnectResult> invalid = cluster.getClient( 0 ).connectAsync( NetAddress{} );
    SW_REQUIRE_TRUE( invalid.isReady() );
    SW_EXPECT_TRUE( invalid.get()._reason == NetDisconnectReason::Rejected );

    // 첫째는 들어가고, 둘째는 가득 참.
    SW_REQUIRE_TRUE( cluster.getClient( 0 ).connectAsync( NetAddress::makeLoopback( 4000 ) ).waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    const TaskFuture<NetConnectResult> full = cluster.getClient( 1 ).connectAsync( NetAddress::makeLoopback( 4000 ) );
    SW_REQUIRE_TRUE( full.waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    SW_EXPECT_TRUE( full.get()._reason == NetDisconnectReason::ServerFull );
    SW_EXPECT_EQUAL( -1, full.get()._clientIndex );

    // 아무도 없는 주소 — 연결 제한 시간 뒤 Timeout.
    const TaskFuture<NetConnectResult> nobody = cluster.getClient( 2 ).connectAsync( NetAddress::makeLoopback( 4999 ) );
    SW_REQUIRE_TRUE( nobody.waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    SW_EXPECT_TRUE( nobody.get()._reason == NetDisconnectReason::Timeout );

    // 기다리는 중에 끊으면 Requested, 다시 연결하면 앞의 것도 Requested.
    const TaskFuture<NetConnectResult> abandoned = cluster.getClient( 3 ).connectAsync( NetAddress::makeLoopback( 4999 ) );
    cluster.getClient( 3 ).disconnect( 0 );
    SW_REQUIRE_TRUE( abandoned.waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    SW_EXPECT_TRUE( abandoned.get()._reason == NetDisconnectReason::Requested );
    const TaskFuture<NetConnectResult> replaced = cluster.getClient( 3 ).connectAsync( NetAddress::makeLoopback( 4999 ) );
    SW_REQUIRE_TRUE( cluster.getClient( 3 ).connect( NetAddress::makeLoopback( 4998 ) ) );
    SW_REQUIRE_TRUE( replaced.waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    SW_EXPECT_TRUE( replaced.get()._reason == NetDisconnectReason::Requested );
}

SW_TEST_CASE( NetEndpointThreadTest, ConnectionsSurviveStalledGameThread )
{
    // 타임아웃 0.5 초 — 게임 스레드가 1 초 멈춰도(로딩 · 긴 프레임) 네트워크 스레드가 유지 패킷을 주고받아 끊기지 않는다.
    NetEndpointConfig settings;
    settings._timeoutSeconds = 0.5;
    test::LoopbackCluster cluster( 3u );
    SW_REQUIRE_TRUE( NetEndpointThreadTestInternal::startThreadedCluster( cluster, 1, settings ) );
    SW_REQUIRE_TRUE( cluster.getClient( 0 ).connectAsync( NetAddress::makeLoopback( 4000 ) ).waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    // 멈추기 전에 보낸 순서 보장 메시지는 멈춘 동안 네트워크 스레드가 실어 나르고, 깨어난 게임 스레드가 순서대로 받는다.
    constexpr int32 kStallMessageCount = 10;
    for ( int32 index = 0; index < kStallMessageCount; ++index )
    {
        SW_REQUIRE_TRUE( cluster.getClient( 0 ).sendMessage( 0, NetChannelType::ReliableOrdered, NetEndpointThreadTestInternal::makeTaggedMessage( 9, index ) ) );
    }
    // 멈춤 자체가 이 케이스의 입력이다 — 게임 스레드는 연결 타임아웃(0.5 초)의 두 배 동안 아무것도 부르지 않는다.
    ThreadUtil::sleepForMilliseconds( 1000 );
    SW_EXPECT_EQUAL( 1, cluster.getServer().getConnectedCount() );
    SW_EXPECT_TRUE( cluster.getClient( 0 ).getConnectionState( 0 ) == NetConnectionState::Connected );
    vector<NetEndpointEvent> listEvent;
    cluster.getClient( 0 ).pollEvents( listEvent );
    SW_REQUIRE_EQUAL( 1, static_cast<int32>( listEvent.size() ) ); // Connected 하나뿐 — 끊김이 없었다
    SW_EXPECT_TRUE( listEvent[0]._kind == NetEndpointEvent::Kind::Connected );
    NetConnectionStats stats;
    SW_REQUIRE_TRUE( cluster.getClient( 0 ).getConnectionStats( 0, stats ) );
    // 확인이 게임 프레임을 기다리지 않는다 — 기다렸다면 왕복 시간이 멈춘 길이(1 초)에 닿는다. 상한은 절대 지연이 아니라 멈춤과 가르는 선이라
    // 부하 아래의 루프백 지연(수십 ms)에 넉넉하게 연결 타임아웃(0.5 초)으로 둔다.
    SW_EXPECT_TRUE( stats._rtt > 0.0f && stats._rtt < static_cast<float32>( settings._timeoutSeconds ) );
    int32          received     = 0;
    int32          connectionID = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_EXPECT_TRUE( test::waitUntil( [&]()
    {
        while ( cluster.getServer().receiveMessage( connectionID, channel, buffer ) )
        {
            received += NetEndpointThreadTestInternal::readInt( buffer, 4 ) == received ? 1 : 0;
        }
        return received == kStallMessageCount;
    } ) );
    SW_EXPECT_EQUAL( kStallMessageCount, received );

    // 스레드는 두 번 띄울 수 없고, 멈춤은 여러 번 불러도 된다.
    SW_EXPECT_FALSE( cluster.getThread( 0 ).start( &cluster.getServer() ) );
    cluster.getThread( 1 ).stop();
    cluster.getThread( 1 ).stop();
    SW_EXPECT_FALSE( cluster.getThread( 1 ).isRunning() );
}

SW_TEST_CASE( NetEndpointThreadTest, UDPHostsRunOnThreadsOverLocalhost )
{
    UDPNetTransport serverTransport;
    UDPNetTransport clientTransport;
    SW_REQUIRE_TRUE( serverTransport.open( 0, true ) );
    SW_REQUIRE_TRUE( clientTransport.open( 0, true ) );
    NetEndpoint server;
    NetEndpoint client;
    server.initialize( &serverTransport, NetEndpointConfig{} );
    client.initialize( &clientTransport, NetEndpointConfig{} );
    SW_REQUIRE_TRUE( server.listen() );
    NetEndpointThread serverThread;
    NetEndpointThread clientThread;
    SW_REQUIRE_TRUE( serverThread.start( &server ) );
    SW_REQUIRE_TRUE( clientThread.start( &client ) );

    const TaskFuture<NetConnectResult> future = client.connectAsync( serverTransport.getLocalAddress() );
    SW_REQUIRE_TRUE( future.waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    SW_REQUIRE_TRUE( future.get().isConnected() );
    for ( int32 index = 0; index < 20; ++index )
    {
        SW_REQUIRE_TRUE( client.sendMessage( 0, NetChannelType::ReliableOrdered, NetEndpointThreadTestInternal::makeTaggedMessage( 7, index ) ) );
    }
    int32          received     = 0;
    int32          connectionID = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_EXPECT_TRUE( test::waitUntil( [&]()
    {
        while ( server.receiveMessage( connectionID, channel, buffer ) )
        {
            received += NetEndpointThreadTestInternal::readInt( buffer, 4 ) == received ? 1 : 0;
        }
        return received == 20;
    } ) );
    SW_EXPECT_EQUAL( 20, received );

    // 끊김 알림은 바로 나간다 — 서버가 타임아웃 전에 안다.
    client.disconnect( 0 );
    SW_EXPECT_TRUE( test::waitUntil( [&server]()
    { return server.getConnectedCount() == 0; }, 1.0 ) );
    clientThread.stop();
    serverThread.stop();
}

/**
 * @brief [NetEndpointThreadTest] 게임 스레드가 보낸 큰 신뢰 메시지(64 KB · 30000 B · 1000 B)가 네트워크 스레드 위에서 조각으로 가 바이트 그대로 · 순서대로 온다
 * @details 보내기(게임 스레드 — 조각을 창에 넣는다)와 패킷 쓰기 · 모으기(네트워크 스레드)가 같은 연결을 잠금 하나로 나눈다.
 */
SW_TEST_CASE( NetEndpointThreadTest, LargeReliableMessagesCrossHostThreads )
{
    NetEndpointConfig settings;
    settings._sendInterval = 1.0 / 240.0; // 스레드가 2 ms 마다 돈다 — 조각 107 개가 0.5 초 안팎
    test::LoopbackCluster cluster( 3u );
    SW_REQUIRE_TRUE( NetEndpointThreadTestInternal::startThreadedCluster( cluster, 1, settings ) );
    NetEndpoint& client = cluster.getClient( 0 );
    SW_REQUIRE_TRUE( client.connectAsync( NetAddress::makeLoopback( 4000 ) ).waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    SW_REQUIRE_TRUE( test::waitUntil( [&cluster]()
    { return cluster.getServer().getConnectedCount() == 1; } ) );

    vector<vector<uint8>> listSent;
    for ( const int32 size : { NetConnection::kMaxReliableMessageSize, 30000, 1000 } )
    {
        vector<uint8> message( static_cast<size_t>( size ), 0 );
        for ( size_t index = 0; index < message.size(); ++index )
        {
            message[index] = static_cast<uint8>( index * 7u + static_cast<size_t>( size ) );
        }
        SW_REQUIRE_TRUE( client.sendMessage( 0, NetChannelType::ReliableOrdered, message ) );
        listSent.push_back( std::move( message ) );
    }
    vector<vector<uint8>> listReceived;
    int32                 connectionID = -1;
    NetChannelType        channel      = NetChannelType::Unreliable;
    vector<uint8>         buffer;
    const bool            bAllReceived = test::waitUntil( [&]()
    {
        while ( cluster.getServer().receiveMessage( connectionID, channel, buffer ) )
        {
            listReceived.push_back( buffer );
        }
        return listReceived.size() >= listSent.size();
    }, 10.0 );
    SW_EXPECT_TRUE( bAllReceived );
    SW_REQUIRE_EQUAL( listSent.size(), listReceived.size() );
    for ( size_t index = 0; index < listSent.size(); ++index )
    {
        SW_EXPECT_TRUE( listReceived[index] == listSent[index] );
    }
}

/**
 * @brief [NetEndpointThreadTest] 네트워크 스레드 위에서도 연결 상한을 넘겨 보내지 않는다 — 64 KB 를 200 KB/s 로(처음 쌓인 몫 13 KB 를 빼도 0.26 초 넘게)
 */
SW_TEST_CASE( NetEndpointThreadTest, SendRateCapHoldsOnHostThreads )
{
    NetEndpointConfig settings;
    settings._maxBytesPerSecond = 200000;
    test::LoopbackCluster cluster( 3u );
    SW_REQUIRE_TRUE( NetEndpointThreadTestInternal::startThreadedCluster( cluster, 1, settings ) );
    NetEndpoint& client = cluster.getClient( 0 );
    SW_REQUIRE_TRUE( client.connectAsync( NetAddress::makeLoopback( 4000 ) ).waitFor( NetEndpointThreadTestInternal::kFutureWaitMilli ) );
    SW_REQUIRE_TRUE( test::waitUntil( [&cluster]()
    { return cluster.getServer().getConnectedCount() == 1; } ) );

    const vector<uint8> message( static_cast<size_t>( NetConnection::kMaxReliableMessageSize ), static_cast<uint8>( 0x5A ) );
    const Stopwatch     stopwatch;
    SW_REQUIRE_TRUE( client.sendMessage( 0, NetChannelType::ReliableOrdered, message ) );
    int32          connectionID = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_REQUIRE_TRUE( test::waitUntil( [&]()
    { return cluster.getServer().receiveMessage( connectionID, channel, buffer ); }, 5.0 ) );
    const int64 milliseconds = stopwatch.getElapsedMilliseconds();
    SW_EXPECT_TRUE( buffer == message );
    SW_EXPECT_TRUE_MSG( milliseconds >= 200, "the host thread does not send faster than the cap" );
}
