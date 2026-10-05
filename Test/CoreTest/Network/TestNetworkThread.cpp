#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/deque.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Connection/NetHostThread.h"
#include "Core/Network/Transport/NetTransport.h"
#include "Core/Network/Transport/UdpNetTransport.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

// 네트워크 스레드 · 비동기 — 전용 스레드가 도는 호스트(루프백 · 실제 UDP), 비동기 연결(성공 · 가득 참 · 타임아웃 · 끊음 · 잘못된 주소),
// 여러 게임 스레드가 동시에 보내고 받기(스레드마다 순서 · 빠짐 · 겹침 없음), 게임 스레드가 멈춰도 끊기지 않기.

using namespace sw;

namespace
{
    constexpr uint32 kFutureWaitMilli = 3000;

    /** @brief @p predicate 가 참이 될 때까지 짧게 자며 기다립니다(최대 @p timeoutSeconds). */
    template <typename FPredicate>
    bool waitUntil( FPredicate&& predicate, float64 timeoutSeconds = 3.0 )
    {
        const sw::Deadline deadline = sw::Deadline::afterMilliseconds( static_cast<int64>( timeoutSeconds * 1000.0 ) );
        while ( predicate() == false )
        {
            if ( deadline.isExpired() )
                return false;
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        return true;
    }

    vector<uint8> makeTaggedMessage( int32 sender, int32 sequence )
    {
        vector<uint8> buffer( 8, 0 );
        for ( int32 index = 0; index < 4; ++index )
        {
            buffer[static_cast<size_t>( index )]     = static_cast<uint8>( sender >> ( index * 8 ) );
            buffer[static_cast<size_t>( index + 4 )] = static_cast<uint8>( sequence >> ( index * 8 ) );
        }
        return buffer;
    }

    int32 readInt( const vector<uint8>& buffer, int32 offset )
    {
        int32 value = 0;
        for ( int32 index = 0; index < 4; ++index )
            value |= static_cast<int32>( buffer[static_cast<size_t>( offset + index )] ) << ( index * 8 );
        return value;
    }

    /** @brief 루프백 망 위의 서버 하나 + 클라이언트 여럿, 모두 자기 네트워크 스레드에서 돈다. */
    struct ThreadedCluster
    {
        LoopbackNetwork       _network{ 3u };
        NetHost               _server;
        deque<NetHost>        _listClient;
        deque<NetHostThread>  _listThread;
        NetHostThreadSettings _threadSettings;

        ThreadedCluster( int32 clientCount, const NetHostSettings& settings )
        {
            _server.initialize( _network.createEndpoint( 4000 ), settings );
            (void)_server.listen();
            for ( int32 index = 0; index < clientCount; ++index )
            {
                _listClient.emplace_back();
                _listClient.back().initialize( _network.createEndpoint( static_cast<uint16>( 5000 + index ) ), settings );
            }
            _listThread.emplace_back();
            (void)_listThread.back().start( &_server, _threadSettings );
            for ( NetHost& client : _listClient )
            {
                _listThread.emplace_back();
                (void)_listThread.back().start( &client, _threadSettings );
            }
        }

        ~ThreadedCluster()
        {
            for ( NetHostThread& thread : _listThread )
                thread.stop();
        }
    };
} // namespace

SW_TEST_CASE( NetworkThreadTest, ThreadedHostsConnectAsyncAndExchangeWithoutGameUpdates )
{
    ThreadedCluster cluster( 1, NetHostSettings{} );
    NetHost&        client = cluster._listClient[0];

    // 비동기 연결 — 게임 스레드는 update 를 부르지 않는다. 후속 작업은 네트워크 스레드에서 잠금 밖에서 불린다.
    atomic<int32>                      continuationCount{ 0 };
    const TaskFuture<NetConnectResult> future = client.connectAsync( NetAddress::makeLoopback( 4000 ) );
    const TaskFuture<int32>            next   = future.then( [&continuationCount]( const NetConnectResult& result )
    {
        continuationCount.fetch_add( 1 );
        return result._clientIndex;
    } );
    SW_ASSERT_TRUE( future.waitFor( kFutureWaitMilli ) );
    SW_ASSERT_TRUE( future.get().isConnected() );
    SW_EXPECT_EQUAL( 0, future.get()._clientIndex );
    SW_ASSERT_TRUE( next.waitFor( kFutureWaitMilli ) );
    SW_EXPECT_EQUAL( 0, next.get() );
    SW_EXPECT_EQUAL( 1, continuationCount.load() );
    SW_EXPECT_EQUAL( 0, client.getClientIndex() );
    SW_ASSERT_TRUE( waitUntil( [&cluster]()
    { return cluster._server.getConnectedCount() == 1; } ) );

    // 서버 → 클라이언트, 클라이언트 → 서버 신뢰 메시지 — 받는 쪽은 게임 스레드에서 꺼내기만 한다.
    for ( int32 index = 0; index < 50; ++index )
    {
        SW_ASSERT_TRUE( cluster._server.sendMessage( 0, NetChannelType::ReliableOrdered, makeTaggedMessage( 1, index ) ) );
        SW_ASSERT_TRUE( client.sendMessage( 0, NetChannelType::ReliableOrdered, makeTaggedMessage( 2, index ) ) );
    }
    int32          clientReceived = 0;
    int32          serverReceived = 0;
    int32          connectionId   = -1;
    NetChannelType channel        = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    const bool     bAllReceived = waitUntil( [&]()
    {
        while ( client.receiveMessage( connectionId, channel, buffer ) )
        {
            if ( readInt( buffer, 0 ) == 1 && readInt( buffer, 4 ) == clientReceived )
                ++clientReceived;
        }
        while ( cluster._server.receiveMessage( connectionId, channel, buffer ) )
        {
            if ( readInt( buffer, 0 ) == 2 && readInt( buffer, 4 ) == serverReceived )
                ++serverReceived;
        }
        return clientReceived == 50 && serverReceived == 50;
    } );
    SW_EXPECT_TRUE( bAllReceived );
    SW_EXPECT_EQUAL( 50, clientReceived );
    SW_EXPECT_EQUAL( 50, serverReceived );
    SW_EXPECT_TRUE( cluster._listThread[0].getUpdateCount() > 0 && cluster._listThread[1].getUpdateCount() > 0 );

    // 통계는 사본으로 — 스레드가 도는 중에도 안전하다.
    NetConnectionStats stats;
    SW_ASSERT_TRUE( client.getConnectionStats( 0, stats ) );
    SW_EXPECT_TRUE( stats._sentPacketCount > 0 && stats._receivedPacketCount > 0 );
    SW_EXPECT_FALSE( client.getConnectionStats( 3, stats ) );
}

SW_TEST_CASE( NetworkThreadTest, GameThreadsSendAndReceiveConcurrently )
{
    // 클라이언트 둘(각자 스레드) — 게임 스레드 넷이 동시에 보내고(한 클라이언트에 둘씩), 서버 쪽은 두 스레드가 동시에 꺼낸다.
    constexpr int32 kSenderCount      = 4;
    constexpr int32 kMessagePerSender = 300;
    NetHostSettings settings;
    settings._sendInterval = 1.0 / 120.0;
    ThreadedCluster cluster( 2, settings );
    for ( NetHost& client : cluster._listClient )
        SW_ASSERT_TRUE( client.connectAsync( NetAddress::makeLoopback( 4000 ) ).waitFor( kFutureWaitMilli ) );
    SW_ASSERT_TRUE( waitUntil( [&cluster]()
    { return cluster._server.getConnectedCount() == 2; } ) );

    vector<std::thread> listSender;
    for ( int32 sender = 0; sender < kSenderCount; ++sender )
    {
        listSender.emplace_back( [&cluster, sender]()
        {
            NetHost& client = cluster._listClient[static_cast<size_t>( sender % 2 )];
            for ( int32 sequence = 0; sequence < kMessagePerSender; ++sequence )
            {
                const vector<uint8> message = makeTaggedMessage( sender, sequence );
                // 신뢰 창이 차면 거절된다 — 잠깐 쉬었다가 다시(보내는 속도를 줄이는 쪽은 게임이다).
                while ( client.sendMessage( 0, NetChannelType::ReliableOrdered, message ) == false )
                    std::this_thread::sleep_for( std::chrono::microseconds( 200 ) );
            }
        } );
    }

    // 받는 스레드 둘 — 같은 호스트에서 동시에 꺼낸다. 한 메시지는 한 번만 나와야 한다.
    atomic<int32>       receivedTotal{ 0 };
    atomic<bool>        bStop{ false };
    vector<int32>       arrLastSequence[2];
    vector<int32>       arrCount[2];
    atomic<int32>       orderErrorCount{ 0 };
    vector<std::thread> listReceiver;
    for ( int32 receiver = 0; receiver < 2; ++receiver )
    {
        arrLastSequence[receiver].assign( kSenderCount, -1 );
        arrCount[receiver].assign( kSenderCount, 0 );
        listReceiver.emplace_back( [&, receiver]()
        {
            int32          connectionId = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            vector<uint8>  buffer;
            while ( bStop.load() == false )
            {
                if ( cluster._server.receiveMessage( connectionId, channel, buffer ) == false )
                {
                    std::this_thread::sleep_for( std::chrono::microseconds( 100 ) );
                    continue;
                }
                const int32 sender   = readInt( buffer, 0 );
                const int32 sequence = readInt( buffer, 4 );
                if ( sender < 0 || sender >= kSenderCount || connectionId != ( sender % 2 == 0 ? 0 : 1 ) )
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
    for ( std::thread& thread : listSender )
        thread.join();
    const bool bAllReceived = waitUntil( [&receivedTotal]()
    { return receivedTotal.load() >= kSenderCount * kMessagePerSender; }, 10.0 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) ); // 겹쳐 나온 것이 있으면 세어지게
    bStop.store( true );
    for ( std::thread& thread : listReceiver )
        thread.join();
    SW_EXPECT_TRUE( bAllReceived );
    SW_EXPECT_EQUAL( kSenderCount * kMessagePerSender, receivedTotal.load() );
    SW_EXPECT_EQUAL( 0, orderErrorCount.load() );
    for ( int32 sender = 0; sender < kSenderCount; ++sender )
        SW_EXPECT_EQUAL( kMessagePerSender, arrCount[0][static_cast<size_t>( sender )] + arrCount[1][static_cast<size_t>( sender )] );
}

SW_TEST_CASE( NetworkThreadTest, ConnectAsyncReportsEveryFailure )
{
    NetHostSettings settings;
    settings._maxConnections = 1;
    settings._connectTimeout = 0.3;
    ThreadedCluster cluster( 4, settings );

    // 잘못된 주소 — 시작부터 못 한다(이미 채워진 future).
    const TaskFuture<NetConnectResult> invalid = cluster._listClient[0].connectAsync( NetAddress{} );
    SW_ASSERT_TRUE( invalid.isReady() );
    SW_EXPECT_TRUE( invalid.get()._reason == NetDisconnectReason::Rejected );

    // 첫째는 들어가고, 둘째는 가득 참.
    SW_ASSERT_TRUE( cluster._listClient[0].connectAsync( NetAddress::makeLoopback( 4000 ) ).waitFor( kFutureWaitMilli ) );
    const TaskFuture<NetConnectResult> full = cluster._listClient[1].connectAsync( NetAddress::makeLoopback( 4000 ) );
    SW_ASSERT_TRUE( full.waitFor( kFutureWaitMilli ) );
    SW_EXPECT_TRUE( full.get()._reason == NetDisconnectReason::ServerFull );
    SW_EXPECT_EQUAL( -1, full.get()._clientIndex );

    // 아무도 없는 주소 — 연결 제한 시간 뒤 Timeout.
    const TaskFuture<NetConnectResult> nobody = cluster._listClient[2].connectAsync( NetAddress::makeLoopback( 4999 ) );
    SW_ASSERT_TRUE( nobody.waitFor( kFutureWaitMilli ) );
    SW_EXPECT_TRUE( nobody.get()._reason == NetDisconnectReason::Timeout );

    // 기다리는 중에 끊으면 Requested, 다시 연결하면 앞의 것도 Requested.
    const TaskFuture<NetConnectResult> abandoned = cluster._listClient[3].connectAsync( NetAddress::makeLoopback( 4999 ) );
    cluster._listClient[3].disconnect( 0 );
    SW_ASSERT_TRUE( abandoned.waitFor( kFutureWaitMilli ) );
    SW_EXPECT_TRUE( abandoned.get()._reason == NetDisconnectReason::Requested );
    const TaskFuture<NetConnectResult> replaced = cluster._listClient[3].connectAsync( NetAddress::makeLoopback( 4999 ) );
    SW_ASSERT_TRUE( cluster._listClient[3].connect( NetAddress::makeLoopback( 4998 ) ) );
    SW_ASSERT_TRUE( replaced.waitFor( kFutureWaitMilli ) );
    SW_EXPECT_TRUE( replaced.get()._reason == NetDisconnectReason::Requested );
}

SW_TEST_CASE( NetworkThreadTest, ConnectionsSurviveStalledGameThread )
{
    // 타임아웃 0.5 초 — 게임 스레드가 1 초 멈춰도(로딩 · 긴 프레임) 네트워크 스레드가 유지 패킷을 주고받아 끊기지 않는다.
    NetHostSettings settings;
    settings._timeout = 0.5;
    ThreadedCluster cluster( 1, settings );
    SW_ASSERT_TRUE( cluster._listClient[0].connectAsync( NetAddress::makeLoopback( 4000 ) ).waitFor( kFutureWaitMilli ) );
    // 멈추기 전에 보낸 순서 보장 메시지는 멈춘 동안 네트워크 스레드가 실어 나르고, 깨어난 게임 스레드가 순서대로 받는다.
    constexpr int32 kStallMessageCount = 10;
    for ( int32 index = 0; index < kStallMessageCount; ++index )
        SW_ASSERT_TRUE( cluster._listClient[0].sendMessage( 0, NetChannelType::ReliableOrdered, makeTaggedMessage( 9, index ) ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
    SW_EXPECT_EQUAL( 1, cluster._server.getConnectedCount() );
    SW_EXPECT_TRUE( cluster._listClient[0].getConnectionState( 0 ) == NetConnectionState::Connected );
    vector<NetHostEvent> listEvent;
    cluster._listClient[0].drainEvents( listEvent );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( listEvent.size() ) ); // Connected 하나뿐 — 끊김이 없었다
    SW_EXPECT_TRUE( listEvent[0]._kind == NetHostEvent::Kind::Connected );
    NetConnectionStats stats;
    SW_ASSERT_TRUE( cluster._listClient[0].getConnectionStats( 0, stats ) );
    // 확인이 게임 프레임을 기다리지 않는다 — 기다렸다면 왕복 시간이 멈춘 길이(1 초)에 닿는다. 상한은 절대 지연이 아니라 멈춤과 가르는 선이라
    // 부하 아래의 루프백 지연(수십 ms)에 넉넉하게 연결 타임아웃(0.5 초)으로 둔다.
    SW_EXPECT_TRUE( stats._rtt > 0.0f && stats._rtt < static_cast<float32>( settings._timeout ) );
    int32          received     = 0;
    int32          connectionId = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_EXPECT_TRUE( waitUntil( [&]()
    {
        while ( cluster._server.receiveMessage( connectionId, channel, buffer ) )
            received += readInt( buffer, 4 ) == received ? 1 : 0;
        return received == kStallMessageCount;
    } ) );
    SW_EXPECT_EQUAL( kStallMessageCount, received );

    // 스레드는 두 번 띄울 수 없고, 멈춤은 여러 번 불러도 된다.
    SW_EXPECT_FALSE( cluster._listThread[0].start( &cluster._server ) );
    cluster._listThread[1].stop();
    cluster._listThread[1].stop();
    SW_EXPECT_FALSE( cluster._listThread[1].isRunning() );
}

SW_TEST_CASE( NetworkThreadTest, UdpHostsRunOnThreadsOverLocalhost )
{
    UdpNetTransport serverTransport;
    UdpNetTransport clientTransport;
    SW_ASSERT_TRUE( serverTransport.open( 0 ) );
    SW_ASSERT_TRUE( clientTransport.open( 0 ) );
    NetHost server;
    NetHost client;
    server.initialize( &serverTransport, NetHostSettings{} );
    client.initialize( &clientTransport, NetHostSettings{} );
    SW_ASSERT_TRUE( server.listen() );
    NetHostThread serverThread;
    NetHostThread clientThread;
    SW_ASSERT_TRUE( serverThread.start( &server ) );
    SW_ASSERT_TRUE( clientThread.start( &client ) );

    const TaskFuture<NetConnectResult> future = client.connectAsync( serverTransport.getLocalAddress() );
    SW_ASSERT_TRUE( future.waitFor( kFutureWaitMilli ) );
    SW_ASSERT_TRUE( future.get().isConnected() );
    for ( int32 index = 0; index < 20; ++index )
        SW_ASSERT_TRUE( client.sendMessage( 0, NetChannelType::ReliableOrdered, makeTaggedMessage( 7, index ) ) );
    int32          received     = 0;
    int32          connectionId = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_EXPECT_TRUE( waitUntil( [&]()
    {
        while ( server.receiveMessage( connectionId, channel, buffer ) )
            received += readInt( buffer, 4 ) == received ? 1 : 0;
        return received == 20;
    } ) );
    SW_EXPECT_EQUAL( 20, received );

    // 끊김 알림은 바로 나간다 — 서버가 타임아웃 전에 안다.
    client.disconnect( 0 );
    SW_EXPECT_TRUE( waitUntil( [&server]()
    { return server.getConnectedCount() == 0; }, 1.0 ) );
    clientThread.stop();
    serverThread.stop();
}
