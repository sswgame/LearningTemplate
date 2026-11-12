#include "pch.h"

#include "Core/Diagnostics/ProfilerTimeline.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Connection/NetFlowLog.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Network/Tests/NetTestUtil.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Network/Transport/NetTransport.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

// 엔드포인트 — 핸드셰이크(도전 · 가득 참 · 다른 프로토콜 · 다른 와이어 판), 나쁜 망(지연 · 흔들림 · 손실 · 중복 · 깨짐)에서의 신뢰 순서, 끊기 · 타임아웃,
// 디버거 대기, 잃은 수락, 만료된 도전, 한가할 때의 유지 패킷, 많은 연결, 64 KB 조각, 끊김 뒤 조각 버림, 보내기 상한과 포화.

using namespace sw;
using test::NetTestUtil;

namespace
{
    struct NetEndpointTestInternal
    {
        /** @brief 흉내 거르개 — 서버가 보내는 `Accepted` 를 @p _remaining 개만 버린다. */
        struct AcceptedDropper
        {
            int32 _remaining{ 0 };
            int32 _droppedCount{ 0 };
        };

        static bool dropAccepted( const NetAddress& to, const uint8* pData, int32 size, void* pContext )
        {
            (void)to;
            AcceptedDropper& dropper = *static_cast<AcceptedDropper*>( pContext );
            if ( dropper._remaining <= 0 || NetEndpoint::peekPacketType( pData, size ) != NetEndpoint::PacketType::Accepted )
                return false;
            --dropper._remaining;
            ++dropper._droppedCount;
            return true;
        }

        /** @brief 클라이언트(엔드포인트 1)가 64 KB 신뢰 메시지 @p count 개를 보내고 서버가 모두 받기까지의 손 시각(초)입니다 — 1/60 초마다, @p maxSeconds 를 넘으면 −1. */
        static float64 measureArrivalSeconds( test::LoopbackCluster& cluster, int32 count, float64 maxSeconds )
        {
            for ( int32 index = 0; index < count; ++index )
            {
                const vector<uint8> message = NetTestUtil::makePatternMessage( static_cast<uint32>( 100 + index ), NetConnection::kMaxReliableMessageSize );
                if ( cluster.getClient( 0 ).sendMessage( 0, NetChannelType::ReliableOrdered, message ) == false )
                    return -1.0;
            }
            vector<vector<uint8>> listReceived;
            const float64         step = 1.0 / 60.0;
            for ( float64 elapsed = step; elapsed <= maxSeconds; elapsed += step )
            {
                cluster.step( step );
                NetTestUtil::pollServerMessages( cluster, listReceived );
                if ( static_cast<int32>( listReceived.size() ) >= count )
                    return elapsed;
            }
            return -1.0;
        }
    };
} // namespace

SW_TEST_CASE( NetEndpointTest, HostsHandshakeExchangeAndSurviveBadNetworks )
{
    NetEndpointConfig settings;
    settings._maxConnections = 2;
    test::LoopbackCluster pair( 77u );
    NetTestUtil::addServerAndClient( pair, settings );
    SW_REQUIRE_TRUE( pair.getServer().listen() );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
    SW_EXPECT_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) == NetConnectionState::Connecting );
    pair.run( 0.3 );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) == NetConnectionState::Connected );
    SW_EXPECT_EQUAL( 1, pair.getServer().getConnectedCount() );
    SW_EXPECT_EQUAL( 0, pair.getClient( 0 ).getClientIndex() );
    vector<NetEndpointEvent> listEvent;
    pair.getServer().pollEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.size() == 1 && listEvent[0]._kind == NetEndpointEvent::Kind::Connected );

    // 나쁜 망 — 두 엔드포인트의 보내는 쪽 흉내에 지연 80 ms ± 30, 손실 20 %, 중복 10 %, 깨짐 5 %. 신뢰 메시지 200 개가 순서대로 한 번씩.
    NetEmulationConditions conditions;
    conditions._latency       = 0.08;
    conditions._jitter        = 0.03;
    conditions._lossRate      = 0.2f;
    conditions._duplicateRate = 0.1f;
    conditions._corruptRate   = 0.05f;
    pair.setConditions( conditions );
    for ( int32 index = 0; index < 200; ++index )
    {
        const vector<uint8> buffer = NetTestUtil::makeMessage( index, 24 );
        SW_REQUIRE_TRUE( pair.getClient( 0 ).sendMessage( 0, NetChannelType::ReliableOrdered, buffer ) );
    }
    vector<int32> listReceived;
    for ( int32 frame = 0; frame < 60 * 20 && listReceived.size() < 200; ++frame )
    {
        pair.run( 1.0 / 60.0 );
        int32          connectionID = -1;
        NetChannelType channel      = NetChannelType::Unreliable;
        vector<uint8>  buffer;
        while ( pair.getServer().receiveMessage( connectionID, channel, buffer ) )
        {
            SW_EXPECT_EQUAL( 0, connectionID );
            listReceived.push_back( NetTestUtil::readMessageValue( buffer ) );
        }
    }
    SW_REQUIRE_TRUE( listReceived.size() == 200 );
    for ( int32 index = 0; index < 200; ++index )
    {
        SW_EXPECT_EQUAL( index, listReceived[static_cast<size_t>( index )] );
    }
    // RTT · 손실률 · 깨짐은 패킷이 쌓여야 보인다(메시지 200 개는 패킷 스무 개 남짓에 다 실렸다) — 6 초 동안 매 프레임 입력 같은 비신뢰 메시지를 보낸다.
    const NetConnectionStats& stats = pair.getClient( 0 ).findConnection( 0 )->getStats();
    for ( int32 frame = 0; frame < 60 * 6; ++frame )
    {
        const vector<uint8> input = NetTestUtil::makeMessage( frame, 8 );
        SW_REQUIRE_TRUE( pair.getClient( 0 ).sendMessage( 0, NetChannelType::Unreliable, input ) );
        pair.run( 1.0 / 60.0 );
    }
    SW_EXPECT_TRUE( pair.getServer().getRejectedPacketCount() > 0 );          // 깨진 패킷을 체크섬이 걸렀다
    SW_EXPECT_TRUE( stats._rtt > 0.12f && stats._rtt < 0.3f );                // 지연 80 ms 왕복 + 흔들림
    SW_EXPECT_TRUE( stats._packetLoss > 0.12f && stats._packetLoss < 0.45f ); // 보낸 쪽 손실 20 % + 깨짐 5 %
    SW_EXPECT_EQUAL( 1, pair.getServer().getConnectedCount() );               // 나쁜 망에서도 끊기지 않았다

    // 서버 → 클라이언트 방송.
    pair.setConditions( NetEmulationConditions{} );
    const vector<uint8> hello = NetTestUtil::makeMessage( 4242 );
    SW_EXPECT_EQUAL( 1, pair.getServer().broadcast( NetChannelType::ReliableOrdered, hello.data(), 4 ) );
    pair.run( 0.2 );
    int32          connectionID = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_REQUIRE_TRUE( pair.getClient( 0 ).receiveMessage( connectionID, channel, buffer ) );
    SW_EXPECT_EQUAL( 4242, NetTestUtil::readMessageValue( buffer ) );
    SW_EXPECT_TRUE( channel == NetChannelType::ReliableOrdered );
}

/**
 * @brief [NetEndpointTest] 모든 채널의 메시지가 보낸 곳과 받은 곳에서 같은 흐름 id 를 하나씩 남긴다(두 방향 — 신뢰 순서 · 신뢰 순서 없음 · 순서만 · 비신뢰)
 * @details 두 호스트가 한 프로세스라 흐름 링 하나에 양쪽이 쌓인다. 다른 케이스의 흐름과 섞이지 않게 종류 바이트(0xE1 ~ 0xE6)로 거른다.
 *          id 는 채널마다 양쪽이 같게 아는 번호로 만든다 — 신뢰 순서는 순번, 신뢰 순서 없음은 신뢰 ID, 비신뢰 · 순서만은 (패킷 번호, 패킷 안 자리).
 */
SW_TEST_CASE( NetEndpointTest, EveryChannelPairsItsSendAndReceiveFlows )
{
    NetEndpointConfig settings;
    settings._maxConnections = 2;
    test::LoopbackCluster pair( 91u );
    NetTestUtil::addServerAndClient( pair, settings );
    SW_REQUIRE_TRUE( pair.getServer().listen() );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
    pair.run( 0.3 );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) == NetConnectionState::Connected );

    struct FlowCase
    {
        uint8          _kind;
        NetChannelType _channel;
        int32          _count;
        bool           _bFromServer;
    };
    // 순서만은 같은 종류의 아직 나가지 않은 것을 새것으로 바꾸므로 하나만 보낸다.
    const FlowCase arrCase[] = {
        {0xE1,     NetChannelType::ReliableOrdered, 3, false},
        {0xE3,          NetChannelType::Unreliable, 2, false},
        {0xE4, NetChannelType::UnreliableSequenced, 1, false},
        {0xE5,   NetChannelType::ReliableUnordered, 2, false},
        {0xE2,     NetChannelType::ReliableOrdered, 1,  true},
        {0xE6,          NetChannelType::Unreliable, 1,  true},
    };

    // 흐름 링은 프로세스에 하나라 앞 케이스 · 앞 회차의 흐름이 남아 있다 — 이 케이스가 기록한 구간만 읽는다.
    const uint64     beginNanos = static_cast<uint64>( MonotonicClock::nowNanoseconds() );
    ProfilerTimeline timeline; // 기록 중인 타임라인이 있어야 흐름이 남는다
    timeline.setRecording( true );
    size_t serverExpected = 0;
    size_t clientExpected = 0;
    for ( const FlowCase& flowCase : arrCase )
    {
        for ( int32 index = 0; index < flowCase._count; ++index )
        {
            const vector<uint8> message = NetTestUtil::makeMessage( flowCase._kind + ( index << 8 ) );
            if ( flowCase._bFromServer )
                SW_EXPECT_EQUAL( 1, pair.getServer().broadcast( flowCase._channel, message.data(), static_cast<int32>( message.size() ) ) );
            else
                SW_REQUIRE_TRUE( pair.getClient( 0 ).sendMessage( 0, flowCase._channel, message ) );
        }
        ( flowCase._bFromServer ? clientExpected : serverExpected ) += static_cast<size_t>( flowCase._count );
    }
    vector<vector<uint8>> listServerReceived;
    size_t                clientReceived = 0;
    for ( int32 frame = 0; frame < 60 && ( listServerReceived.size() < serverExpected || clientReceived < clientExpected ); ++frame )
    {
        pair.run( 1.0 / 60.0 );
        NetTestUtil::pollServerMessages( pair, listServerReceived );
        int32          connectionID = -1;
        NetChannelType channel      = NetChannelType::Unreliable;
        vector<uint8>  buffer;
        while ( pair.getClient( 0 ).receiveMessage( connectionID, channel, buffer ) )
        {
            ++clientReceived;
        }
    }
    timeline.setRecording( false );
    const uint64 endNanos = static_cast<uint64>( MonotonicClock::nowNanoseconds() );
    SW_REQUIRE_EQUAL( serverExpected, listServerReceived.size() );
    SW_REQUIRE_EQUAL( clientExpected, clientReceived );

    vector<NetFlowEvent> listFlow;
    NetFlowLog::collect( beginNanos, endNanos, listFlow );
    for ( const FlowCase& flowCase : arrCase )
    {
        vector<uint64> listSent;
        vector<uint64> listReceivedID;
        for ( const NetFlowEvent& flow : listFlow )
        {
            if ( flow._kind == flowCase._kind )
                ( flow._bReceive ? listReceivedID : listSent ).push_back( flow._id );
        }
        const string label = "kind " + sw::to_string( static_cast<uint32>( flowCase._kind ) );
        SW_EXPECT_TRUE_MSG( listSent.size() == static_cast<size_t>( flowCase._count ), label.c_str() );
        std::sort( listSent.begin(), listSent.end() );
        std::sort( listReceivedID.begin(), listReceivedID.end() );
        SW_EXPECT_TRUE_MSG( listSent == listReceivedID, label.c_str() ); // 보낸 것마다 같은 id 의 받은 것이 하나
    }
}

SW_TEST_CASE( NetEndpointTest, HostsRejectFullServersForeignProtocolsAndDetectDisconnects )
{
    NetEndpointConfig settings;
    settings._maxConnections = 1;
    settings._timeoutSeconds = 1.0;
    settings._debugHold      = NetDebugHold::Off; // 디버거 아래에서 돌려도 원래 시간 초과를 본다
    LoopbackNetwork network;
    NetEndpoint     server;
    NetEndpoint     first;
    NetEndpoint     second;
    NetEndpoint     foreign;
    server.initialize( network.createEndpoint( 4000 ), settings );
    first.initialize( network.createEndpoint( 5001 ), settings );
    second.initialize( network.createEndpoint( 5002 ), settings );
    NetEndpointConfig otherGame = settings;
    otherGame._gameID           = 0xDEADBEEFu;
    foreign.initialize( network.createEndpoint( 5003 ), otherGame );
    SW_EXPECT_TRUE( network.createEndpoint( 4000 ) == nullptr );
    SW_REQUIRE_TRUE( server.listen() );
    SW_REQUIRE_TRUE( first.connect( NetAddress::makeLoopback( 4000 ) ) );
    float64    time   = 0.0;
    const auto runAll = [&]( float64 seconds, bool bServer, bool bFirst )
    {
        for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 60.0 )
        {
            time += 1.0 / 60.0;
            if ( bServer )
                server.update( time );
            if ( bFirst )
                first.update( time );
            second.update( time );
            foreign.update( time );
        }
    };
    runAll( 0.3, true, true );
    SW_REQUIRE_TRUE( first.getConnectionState( 0 ) == NetConnectionState::Connected );

    // 가득 찬 서버 · 다른 게임.
    SW_REQUIRE_TRUE( second.connect( NetAddress::makeLoopback( 4000 ) ) );
    SW_REQUIRE_TRUE( foreign.connect( NetAddress::makeLoopback( 4000 ) ) );
    SW_EXPECT_LOG( "Net.ConnectionRefused" ); // 서버가 다른 게임의 요청을 거절하며 남긴다
    SW_EXPECT_LOG( "Net.ServerRefused" );     // 클라이언트가 거절을 받으며 남긴다
    runAll( 0.5, true, true );
    vector<NetEndpointEvent> listEvent;
    second.pollEvents( listEvent );
    SW_REQUIRE_TRUE( listEvent.size() == 1 );
    SW_EXPECT_TRUE( listEvent[0]._reason == NetDisconnectReason::ServerFull );
    // 다른 게임의 요청은 이유를 붙여 거절한다(타임아웃을 기다리지 않는다). 그 게임의 다른 패킷은 체크섬부터 틀린다.
    listEvent.clear();
    foreign.pollEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.size() == 1 && listEvent[0]._reason == NetDisconnectReason::Rejected );
    SW_EXPECT_TRUE( foreign.getConnectionState( 0 ) == NetConnectionState::Disconnected );
    SW_EXPECT_EQUAL( 1, server.getConnectedCount() );

    // 끊기 — 클라이언트가 끊으면 서버가 알고, 자리가 빈다.
    first.disconnect( 0 );
    runAll( 0.2, true, true );
    listEvent.clear();
    server.pollEvents( listEvent );
    SW_REQUIRE_TRUE( listEvent.empty() == false );
    SW_EXPECT_TRUE( listEvent.back()._kind == NetEndpointEvent::Kind::Disconnected && listEvent.back()._reason == NetDisconnectReason::Remote );
    SW_EXPECT_EQUAL( 0, server.getConnectedCount() );

    // 타임아웃 — 서버가 멈추면 클라이언트는 1 초 뒤 끊긴 것으로 안다.
    SW_REQUIRE_TRUE( first.connect( NetAddress::makeLoopback( 4000 ) ) );
    runAll( 0.3, true, true );
    SW_REQUIRE_TRUE( first.getConnectionState( 0 ) == NetConnectionState::Connected );
    first.pollEvents( listEvent );
    listEvent.clear();
    runAll( 1.5, false, true );
    first.pollEvents( listEvent );
    SW_REQUIRE_TRUE( listEvent.empty() == false );
    SW_EXPECT_TRUE( listEvent.back()._reason == NetDisconnectReason::Timeout );
}

/**
 * @brief [NetEndpointTest] 끊긴 연결의 번호는 게임 스레드가 끊김을 꺼내기 전에는 새 연결에 주지 않는다(감사 P0-8)
 * @details 슬롯 번호가 곧 연결 ID 라, 같은 갱신에서 A 의 끊김과 B 의 접속을 처리하면 게임 스레드가 A 에게 보낸 상태가 B 에게 갔다. 1 슬롯 서버에서
 *          A 가 끊긴 뒤 서버가 사건을 꺼내지 않은 동안 B 는 자리를 받지 못하고(가득 참), A 의 번호로 보낸 것은 거절된다. 꺼낸 뒤에는 B 가 들어온다.
 *          변이: 보류를 빼면 B 가 바로 번호 0 을 받아 첫 단언이 진다.
 */
SW_TEST_CASE( NetEndpointTest, ClosedSlotIsNotReusedBeforeTheGameThreadSeesTheDisconnect )
{
    NetEndpointConfig settings;
    settings._maxConnections = 1;
    settings._debugHold      = NetDebugHold::Off;
    LoopbackNetwork network;
    NetEndpoint     server;
    NetEndpoint     first;
    NetEndpoint     second;
    server.initialize( network.createEndpoint( 4100 ), settings );
    first.initialize( network.createEndpoint( 5101 ), settings );
    second.initialize( network.createEndpoint( 5102 ), settings );
    SW_REQUIRE_TRUE( server.listen() );
    SW_REQUIRE_TRUE( first.connect( NetAddress::makeLoopback( 4100 ) ) );
    float64    time   = 0.0;
    const auto runAll = [&]( float64 seconds )
    {
        for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 60.0 )
        {
            time += 1.0 / 60.0;
            server.update( time );
            first.update( time );
            second.update( time );
        }
    };
    runAll( 0.3 );
    vector<NetEndpointEvent> listEvent;
    server.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( 1, server.getConnectedCount() );

    first.disconnect( 0 );
    runAll( 0.2 ); // 서버가 끊김을 처리했지만 게임 스레드는 아직 꺼내지 않았다
    SW_REQUIRE_TRUE( second.connect( NetAddress::makeLoopback( 4100 ) ) );
    runAll( 0.5 );
    SW_EXPECT_TRUE( second.getConnectionState( 0 ) != NetConnectionState::Connected );
    const uint8 payload[] = { 1, 2, 3 };
    SW_EXPECT_FALSE( server.sendMessage( 0, NetChannelType::ReliableOrdered, payload, static_cast<int32>( sizeof( payload ) ) ) ); // A 의 번호는 비었다 — B 에게 가지 않는다

    listEvent.clear();
    server.pollEvents( listEvent ); // 게임 스레드가 A 의 끊김을 본다 — 이제 번호를 다시 준다
    SW_REQUIRE_TRUE( listEvent.empty() == false && listEvent.back()._kind == NetEndpointEvent::Kind::Disconnected );
    SW_REQUIRE_TRUE( second.connect( NetAddress::makeLoopback( 4100 ) ) );
    runAll( 0.5 );
    SW_EXPECT_TRUE( second.getConnectionState( 0 ) == NetConnectionState::Connected );
}

/**
 * @brief [NetEndpointTest] 디버거 대기를 알린 서버가 멈춰도(중단점) 클라이언트는 끊지 않고, 재개한 서버도 멈춘 동안을 시간 초과로 치지 않는다
 * @details 서버만 멈춤(클라이언트는 알림 비트를 보고 시한을 늘린다) → 둘 다 멈춤(한 프로세스가 통째로 멈춤 — 클라이언트는 알리지 않으므로 서버는
 *          가상 시계로만 버틴다) → 대기 시한(10 초)을 넘긴 멈춤은 끊긴다.
 */
SW_TEST_CASE( NetEndpointTest, DebugHoldKeepsTheConnectionThroughABreakpoint )
{
    // 디버거 대기는 개발 빌드 기능이다 — 배포 구성은 `Tests/` 를 짓지 않으므로 이 케이스도 없다.
    NetEndpointConfig clientSettings;
    clientSettings._timeoutSeconds          = 1.0;
    clientSettings._debugHoldTimeoutSeconds = 10.0;
    clientSettings._debugHold               = NetDebugHold::Off;
    NetEndpointConfig serverSettings        = clientSettings;
    serverSettings._debugHold               = NetDebugHold::Always;
    LoopbackNetwork network;
    NetEndpoint     server;
    NetEndpoint     client;
    server.initialize( network.createEndpoint( 4000 ), serverSettings );
    client.initialize( network.createEndpoint( 5000 ), clientSettings );
    SW_REQUIRE_TRUE( server.listen() );
    SW_REQUIRE_TRUE( client.connect( NetAddress::makeLoopback( 4000 ) ) );
    float64    time          = 0.0;
    const auto runForSeconds = [&]( float64 seconds, bool bServer, bool bClient )
    {
        for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 60.0 )
        {
            time += 1.0 / 60.0;
            if ( bServer )
                server.update( time );
            if ( bClient )
                client.update( time );
        }
    };
    runForSeconds( 0.3, true, true );
    SW_REQUIRE_TRUE( client.getConnectionState( 0 ) == NetConnectionState::Connected );
    SW_EXPECT_TRUE( server.isDebugHoldAnnounced() );
    SW_EXPECT_FALSE( client.isDebugHoldAnnounced() );

    // 서버만 6 초 멈춘다 — 보통 시한(1 초)이면 끊겼을 것이다.
    runForSeconds( 6.0, false, true );
    SW_EXPECT_TRUE( client.getConnectionState( 0 ) == NetConnectionState::Connected );
    runForSeconds( 0.5, true, true ); // 서버가 쌓인 패킷을 비운다
    SW_EXPECT_EQUAL( 1, server.getConnectedCount() );

    // 둘 다 6 초 멈춘다 — 서버가 먼저 깨어난다. 클라이언트는 알리지 않았고 쌓인 패킷도 없으니 서버는 가상 시계로만 버틴다.
    time += 6.0;
    server.update( time );
    SW_EXPECT_EQUAL( 1, server.getConnectedCount() );
    client.update( time );
    runForSeconds( 0.5, true, true );
    SW_EXPECT_EQUAL( 1, server.getConnectedCount() );
    SW_EXPECT_TRUE( client.getConnectionState( 0 ) == NetConnectionState::Connected );
    const vector<uint8> hello = NetTestUtil::makeMessage( 77 );
    SW_REQUIRE_TRUE( client.sendMessage( 0, NetChannelType::ReliableOrdered, hello ) );
    runForSeconds( 0.3, true, true );
    int32          connectionID = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_REQUIRE_TRUE( server.receiveMessage( connectionID, channel, buffer ) );
    SW_EXPECT_EQUAL( 77, NetTestUtil::readMessageValue( buffer ) );

    // 대기 시한(10 초)을 넘긴 멈춤은 끊긴다 — 9 초에는 살아 있고 12 초에는 끊겼다.
    vector<NetEndpointEvent> listEvent;
    client.pollEvents( listEvent );
    listEvent.clear();
    runForSeconds( 9.0, false, true );
    SW_EXPECT_TRUE( client.getConnectionState( 0 ) == NetConnectionState::Connected );
    runForSeconds( 3.0, false, true );
    client.pollEvents( listEvent );
    SW_REQUIRE_TRUE( listEvent.empty() == false );
    SW_EXPECT_TRUE( listEvent.back()._reason == NetDisconnectReason::Timeout );
}

/**
 * @brief [NetEndpointTest] 와이어 판이 다른 같은 게임의 클라이언트는 VersionMismatch 로 바로 거절된다 — 연결 요청 · 거절은 판과 상관없는 머리로 싸여 서로 읽힌다
 * @details 판(`NetWireVersion` · 키트 판)은 프로토콜 id 에 섞인다. 다른 판의 패킷은 체크섬부터 틀려 버려지므로, 요청까지 버리면 클라이언트는 5 초 뒤
 *          Timeout 만 알고 왜 안 되는지 모른다.
 */
SW_TEST_CASE( NetEndpointTest, WireVersionMismatchIsRefusedWithReason )
{
    static_assert( NetProtocol::makeProtocolID( 7u, 1u ) != NetProtocol::makeProtocolID( 7u, 2u ), "the wire version is part of the protocol id" );
    static_assert( NetProtocol::makeProtocolID( 7u, 1u ) != NetProtocol::makeProtocolID( 8u, 1u ), "the game id is part of the protocol id" );
    static_assert( NetWireVersion::combine( { 1u, 2u } ) != NetWireVersion::combine( { 2u, 1u } ), "layer order is part of the version" );

    NetEndpointConfig serverSettings;
    serverSettings._wireVersion   = NetWireVersion::combine( { 1u, 4u } );
    NetEndpointConfig oldSettings = serverSettings;
    oldSettings._wireVersion      = NetWireVersion::combine( { 1u, 3u } );
    LoopbackNetwork network;
    NetEndpoint     server;
    NetEndpoint     current;
    NetEndpoint     outdated;
    server.initialize( network.createEndpoint( 4000 ), serverSettings );
    current.initialize( network.createEndpoint( 5001 ), serverSettings );
    outdated.initialize( network.createEndpoint( 5002 ), oldSettings );
    SW_EXPECT_TRUE( current.getProtocolID() != outdated.getProtocolID() );
    SW_REQUIRE_TRUE( server.listen() );
    SW_REQUIRE_TRUE( current.connect( NetAddress::makeLoopback( 4000 ) ) );
    SW_REQUIRE_TRUE( outdated.connect( NetAddress::makeLoopback( 4000 ) ) );
    float64 time = 0.0;
    SW_EXPECT_LOG( "Net.ConnectionRefused" ); // 서버가 다른 판의 요청을 이유와 함께 거절한다
    SW_EXPECT_LOG( "Net.ServerRefused" );     // 옛 판 클라이언트가 그 거절을 받는다
    for ( int32 frame = 0; frame < 20; ++frame )
    {
        time += 1.0 / 60.0;
        server.update( time );
        current.update( time );
        outdated.update( time );
    }
    SW_EXPECT_TRUE( current.getConnectionState( 0 ) == NetConnectionState::Connected );
    vector<NetEndpointEvent> listEvent;
    outdated.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._kind == NetEndpointEvent::Kind::Disconnected );
    SW_EXPECT_TRUE( listEvent[0]._reason == NetDisconnectReason::VersionMismatch );
    SW_EXPECT_EQUAL( 1, server.getConnectedCount() );
}

/**
 * @brief [NetEndpointTest] 서버의 `Accepted` 를 잃어도 클라이언트는 서버가 준 자기 번호를 안다 — 먼저 온 데이터 패킷으로는 연결로 치지 않고 응답을 다시 보내 수락을 받는다
 * @details 첫 데이터 패킷이 수락을 대신하면 번호가 영구히 −1 로 남는다(수락은 다시 오지 않는다). 락스텝 · 롤백은 그 번호 + 1 을 플레이어 번호로 써서
 *          서버가 그 클라이언트의 입력을 위조로 버리고 판이 멈춘다(`NetSimHarnessTest.LostAcceptedStillGivesLockstepPlayersTheirNumber`).
 */
SW_TEST_CASE( NetEndpointTest, LostAcceptedStillReportsClientIndex )
{
    LoopbackNetwork                          network;
    NetEmulationTransport                    serverLink( network.createEndpoint( 4000 ) );
    NetEndpointTestInternal::AcceptedDropper dropper;
    NetEmulationConditions                   conditions;
    conditions._pDropFilter        = &NetEndpointTestInternal::dropAccepted;
    conditions._pDropFilterContext = &dropper;
    serverLink.setDefaultConditions( conditions );
    NetEndpoint server;
    NetEndpoint first;
    NetEndpoint second;
    server.initialize( &serverLink, NetEndpointConfig{} );
    first.initialize( network.createEndpoint( 5000 ), NetEndpointConfig{} );
    second.initialize( network.createEndpoint( 5001 ), NetEndpointConfig{} );
    SW_REQUIRE_TRUE( server.listen() );
    float64    time   = 0.0;
    const auto runAll = [&]( float64 seconds )
    {
        for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 60.0 )
        {
            time += 1.0 / 60.0;
            server.update( time );
            first.update( time );
            second.update( time );
        }
    };
    // 첫째가 자리 0 을 가져가 둘째의 번호는 1 이다(기본값 0 과 갈린다).
    SW_REQUIRE_TRUE( first.connect( NetAddress::makeLoopback( 4000 ) ) );
    runAll( 0.3 );
    SW_REQUIRE_EQUAL( 0, first.getClientIndex() );
    dropper._remaining = 1;
    SW_REQUIRE_TRUE( second.connect( NetAddress::makeLoopback( 4000 ) ) );
    runAll( 0.5 );
    SW_EXPECT_EQUAL( 1, dropper._droppedCount );
    SW_REQUIRE_TRUE( second.getConnectionState( 0 ) == NetConnectionState::Connected );
    SW_EXPECT_EQUAL( 1, second.getClientIndex() );
    SW_EXPECT_EQUAL( 2, server.getConnectedCount() );
    // 수락 앞에 왔던 데이터 패킷의 신뢰 메시지는 재전송이 메운다.
    SW_REQUIRE_TRUE( server.sendMessage( 1, NetChannelType::ReliableOrdered, NetTestUtil::makeMessage( 4321 ) ) );
    runAll( 0.5 );
    int32          connectionID = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_REQUIRE_TRUE( second.receiveMessage( connectionID, channel, buffer ) );
    SW_EXPECT_EQUAL( 4321, NetTestUtil::readMessageValue( buffer ) );
}

/**
 * @brief [NetEndpointTest] 도전 값은 시간 칸에 묶여 있다 — 만든 칸과 다음 칸(5~10 초) 안에 돌아온 응답만 자리를 잡고, 그보다 늦게 온 응답(가로챈 응답의 재사용)은 버린다
 * @details 서버는 도전을 기억하지 않으므로(상태 없는 도전) 값 자체가 만든 때를 담아야 한다. 올림 지연 12 초 클라이언트의 응답은 요청이 도착한 칸보다 두 칸 뒤에
 *          닿아 끝내 연결되지 않고, 올림 지연 2 초 클라이언트는 연결된다.
 */
SW_TEST_CASE( NetEndpointTest, ExpiredChallengeIsRejected )
{
    NetEndpointConfig settings;
    settings._connectTimeout = 60.0;
    LoopbackNetwork        network;
    NetEmulationTransport  slowLink( network.createEndpoint( 5000 ) );
    NetEmulationTransport  fineLink( network.createEndpoint( 5001 ) );
    NetEmulationConditions slow;
    slow._latency = 12.0;
    slowLink.setDefaultConditions( slow );
    NetEmulationConditions fine;
    fine._latency = 2.0;
    fineLink.setDefaultConditions( fine );
    NetEndpoint server;
    NetEndpoint slowClient;
    NetEndpoint fineClient;
    server.initialize( network.createEndpoint( 4000 ), settings );
    slowClient.initialize( &slowLink, settings );
    fineClient.initialize( &fineLink, settings );
    SW_REQUIRE_TRUE( server.listen() );
    SW_REQUIRE_TRUE( slowClient.connect( NetAddress::makeLoopback( 4000 ) ) );
    SW_REQUIRE_TRUE( fineClient.connect( NetAddress::makeLoopback( 4000 ) ) );
    float64 time = 0.0;
    for ( int32 frame = 0; frame < 60 * 40; ++frame )
    {
        time += 1.0 / 60.0;
        server.update( time );
        slowClient.update( time );
        fineClient.update( time );
    }
    SW_EXPECT_TRUE( fineClient.getConnectionState( 0 ) == NetConnectionState::Connected );
    SW_EXPECT_TRUE( slowClient.getConnectionState( 0 ) == NetConnectionState::Connecting );
    SW_EXPECT_EQUAL( 1, server.getConnectedCount() );
}

SW_TEST_CASE( NetEndpointTest, IdleConnectionsSendKeepAlivesInsteadOfEveryInterval )
{
    test::LoopbackCluster pair( 77u );
    NetTestUtil::addServerAndClient( pair, NetEndpointConfig{} );
    SW_REQUIRE_TRUE( pair.getServer().listen() );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
    pair.run( 0.5 );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) == NetConnectionState::Connected );

    // 4 초 동안 아무것도 보내지 않는다 — 보낼 간격(1/30 초)마다가 아니라 유지 간격(0.25 초)마다 유지 패킷과 그 확인만 오간다.
    const NetConnectionStats& clientStats = pair.getClient( 0 ).findConnection( 0 )->getStats();
    const NetConnectionStats& serverStats = pair.getServer().findConnection( 0 )->getStats();
    const uint64              clientSent  = clientStats._sentPacketCount;
    const uint64              serverSent  = serverStats._sentPacketCount;
    pair.run( 4.0 );
    const uint64 clientIdleSent = clientStats._sentPacketCount - clientSent;
    const uint64 serverIdleSent = serverStats._sentPacketCount - serverSent;
    SW_EXPECT_TRUE( clientIdleSent >= 8 && clientIdleSent <= 40 ); // 30 Hz 였다면 120
    SW_EXPECT_TRUE( serverIdleSent >= 8 && serverIdleSent <= 40 );
    SW_EXPECT_TRUE( clientStats._rtt > 0.0f && clientStats._rtt < 0.1f ); // 유지 패킷도 RTT 를 측정한다(지연 없는 망)
    SW_EXPECT_TRUE( clientStats._packetLoss < 0.01f );                    // 확인만 담은 답은 확인을 바라지 않으니 잃은 것으로 세지 않는다
    SW_EXPECT_EQUAL( 1, pair.getServer().getConnectedCount() );

    // 보낼 것이 생기면 바로 — 다음 유지 시각을 기다리지 않는다. 메시지 길이 칸(11 비트)의 끝 1024 바이트(조각 하나)까지 한 패킷에 실린다.
    const vector<uint8> largest = NetTestUtil::makeMessage( 777, NetConnection::kMaxSingleMessageSize );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).sendMessage( 0, NetChannelType::ReliableOrdered, largest ) );
    {
        SW_EXPECT_LOG( "Net.MessageTooLarge" ); // 조각나지 않는 채널은 조각 하나를 넘는 메시지를 오류와 함께 거절한다
        SW_EXPECT_FALSE( pair.getClient( 0 ).sendMessage( 0, NetChannelType::Unreliable, NetTestUtil::makeMessage( 0, NetConnection::kMaxSingleMessageSize + 1 ) ) );
    }
    pair.run( 2.0 / 30.0 );
    int32          connectionID = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_REQUIRE_TRUE( pair.getServer().receiveMessage( connectionID, channel, buffer ) );
    SW_EXPECT_EQUAL( NetConnection::kMaxSingleMessageSize, static_cast<int32>( buffer.size() ) );
    SW_EXPECT_EQUAL( 777, NetTestUtil::readMessageValue( buffer ) );
}

// 감사 N-8 — 깨어날 때마다 정원(여기서는 256) 전 슬롯을 훑지 않는다. 한가한 연결은 유지 간격 · 받은 패킷 때만 들여다본다.
SW_TEST_CASE( NetEndpointTest, UpdateVisitsOnlyDueConnectionsNotEverySlot )
{
    constexpr int32   kClientCount  = 8;
    constexpr int32   kUpdateCount  = 600;
    constexpr float64 kUpdateSecond = 1.0 / 600.0; // 받을 것이 올 때마다 깨는 네트워크 스레드처럼 잦게 돈다
    NetEndpointConfig settings;
    settings._maxConnections = 256;
    settings._debugHold      = NetDebugHold::Off;
    LoopbackNetwork network;
    NetEndpoint     server;
    server.initialize( network.createEndpoint( 4000 ), settings );
    SW_REQUIRE_TRUE( server.listen() );
    NetEndpoint arrClient[kClientCount];
    for ( int32 index = 0; index < kClientCount; ++index )
    {
        arrClient[index].initialize( network.createEndpoint( static_cast<uint16>( 6000 + index ) ), settings );
        SW_REQUIRE_TRUE( arrClient[index].connect( NetAddress::makeLoopback( 4000 ) ) );
    }
    float64    time      = 0.0;
    const auto runSteady = [&]( int32 updateCount )
    {
        for ( int32 step = 0; step < updateCount; ++step )
        {
            time += kUpdateSecond;
            server.update( time );
            for ( NetEndpoint& client : arrClient )
            {
                client.update( time );
            }
        }
    };
    runSteady( 300 );
    SW_REQUIRE_EQUAL( kClientCount, server.getConnectedCount() );

    const uint64 visitBefore = server.getSlotVisitCount();
    runSteady( kUpdateCount );
    const uint64 visited = server.getSlotVisitCount() - visitBefore;
    // 전 슬롯 훑기는 256 × 600, 살아 있는 연결만 매번 훑어도 8 × 600 = 4800 이다. 한가한 연결은 유지 간격(0.25 초)과 받은 유지 패킷 때만 본다.
    SW_EXPECT_TRUE_MSG( visited > 0 && visited < static_cast<uint64>( kClientCount * kUpdateCount / 4 ), "idle connections are visited only when due" );
    SW_EXPECT_EQUAL( kClientCount, server.getConnectedCount() ); // 기한 큐가 유지 패킷 · 타임아웃을 놓치지 않는다

    // 보낼 것이 생기면 다음 유지 시각을 기다리지 않는다 — 넣은 메시지가 그 연결을 바로 기한으로 만든다.
    SW_REQUIRE_TRUE( server.sendMessage( 3, NetChannelType::ReliableOrdered, NetTestUtil::makeMessage( 33 ) ) );
    runSteady( 40 ); // 보내기 간격(1/30 초)이 지나기를 기다릴 수 있다 — 유지 간격(0.25 초)보다 짧다
    int32          connectionID = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    bool           bArrived = false;
    for ( NetEndpoint& client : arrClient )
    {
        bArrived = bArrived || ( client.receiveMessage( connectionID, channel, buffer ) && NetTestUtil::readMessageValue( buffer ) == 33 );
    }
    SW_EXPECT_TRUE( bArrived );
}

SW_TEST_CASE( NetEndpointTest, ServerTellsManyClientsApartByAddress )
{
    constexpr int32   kClientCount = 40;
    NetEndpointConfig settings;
    settings._maxConnections = 48;
    LoopbackNetwork network;
    NetEndpoint     server;
    server.initialize( network.createEndpoint( 4000 ), settings );
    SW_REQUIRE_TRUE( server.listen() );
    NetEndpoint arrClient[kClientCount];
    for ( int32 index = 0; index < kClientCount; ++index )
    {
        arrClient[index].initialize( network.createEndpoint( static_cast<uint16>( 6000 + index ) ), settings );
        SW_REQUIRE_TRUE( arrClient[index].connect( NetAddress::makeLoopback( 4000 ) ) );
    }
    float64    time   = 0.0;
    const auto runAll = [&]( float64 seconds )
    {
        for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 60.0 )
        {
            time += 1.0 / 60.0;
            server.update( time );
            for ( NetEndpoint& client : arrClient )
            {
                client.update( time );
            }
        }
    };
    runAll( 0.5 );
    SW_REQUIRE_EQUAL( kClientCount, server.getConnectedCount() );

    // 클라이언트마다 자기 번호를 보낸다 — 서버의 연결 id 가 그 클라이언트의 주소와 맞아야 한다.
    for ( int32 index = 0; index < kClientCount; ++index )
    {
        SW_REQUIRE_TRUE( arrClient[index].sendMessage( 0, NetChannelType::ReliableOrdered, NetTestUtil::makeMessage( index ) ) );
    }
    runAll( 0.2 );
    int32          connectionID = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    int32          receivedCount = 0;
    while ( server.receiveMessage( connectionID, channel, buffer ) )
    {
        const int32 clientIndex = NetTestUtil::readMessageValue( buffer );
        SW_EXPECT_EQUAL( static_cast<int32>( 6000 + clientIndex ), static_cast<int32>( server.getConnectionAddress( connectionID )._port ) );
        ++receivedCount;
    }
    SW_EXPECT_EQUAL( kClientCount, receivedCount );

    // 하나가 끊고 다시 붙어도 주소로 바른 자리를 찾는다.
    arrClient[7].disconnect( 0 );
    runAll( 0.2 );
    SW_EXPECT_EQUAL( kClientCount - 1, server.getConnectedCount() );
    SW_REQUIRE_TRUE( arrClient[7].connect( NetAddress::makeLoopback( 4000 ) ) );
    runAll( 0.5 );
    SW_REQUIRE_EQUAL( kClientCount, server.getConnectedCount() );
    SW_REQUIRE_TRUE( arrClient[7].sendMessage( 0, NetChannelType::ReliableOrdered, NetTestUtil::makeMessage( 7 ) ) );
    runAll( 0.2 );
    SW_REQUIRE_TRUE( server.receiveMessage( connectionID, channel, buffer ) );
    SW_EXPECT_EQUAL( 6007, static_cast<int32>( server.getConnectionAddress( connectionID )._port ) );
}

/**
 * @brief [NetEndpointTest] 신뢰 순서 메시지는 64 KB 까지 조각으로 가고, 손실 · 중복 · 순서 바뀜 회선에서도 받는 쪽이 한 번 · 바이트 그대로 · 보낸 순서로 받는다
 * @details 조각 하나가 신뢰 id 하나다 — 잃은 조각만 다시 가고, 받는 쪽은 마지막 조각까지 모아 메시지 하나로 건넨다. 늦게 온 중복 조각은 메시지를 다시 만들지 않는다.
 */
SW_TEST_CASE( NetEndpointTest, ReliableMessagesUpTo64KbArriveWholeOverABadLine )
{
    NetEndpointConfig settings;
    settings._sendInterval = 1.0 / 60.0;
    test::LoopbackCluster cluster( 41u );
    NetTestUtil::addServerAndClient( cluster, settings );
    SW_REQUIRE_TRUE( cluster.connectClients( 30 ) );
    NetEmulationConditions conditions;
    conditions._latency       = 0.03;
    conditions._jitter        = 0.01;
    conditions._lossRate      = 0.15f;
    conditions._duplicateRate = 0.05f;
    conditions._reorderRate   = 0.1f;
    cluster.setConditions( conditions );

    // 64 KB(조각 64) · 7 B · 40000 B(40) · 1025 B(2) — 조각 107 개, 창(255) 안.
    const vector<vector<uint8>> listSent = { NetTestUtil::makePatternMessage( 1u, NetConnection::kMaxReliableMessageSize ), NetTestUtil::makePatternMessage( 2u, 7 ),
                                             NetTestUtil::makePatternMessage( 3u, 40000 ), NetTestUtil::makePatternMessage( 4u, NetConnection::kMaxSingleMessageSize + 1 ) };
    for ( const vector<uint8>& message : listSent )
    {
        SW_REQUIRE_TRUE( cluster.getClient( 0 ).sendMessage( 0, NetChannelType::ReliableOrdered, message ) );
    }

    vector<vector<uint8>> listReceived;
    for ( int32 step = 0; step < 60 * 20 && listReceived.size() < listSent.size(); ++step )
    {
        cluster.step();
        NetTestUtil::pollServerMessages( cluster, listReceived );
    }
    cluster.run( 1.0 ); // 늦게 온 중복 조각 · 재전송이 메시지를 하나 더 만들지 않는다
    NetTestUtil::pollServerMessages( cluster, listReceived );

    SW_REQUIRE_EQUAL( listSent.size(), listReceived.size() );
    for ( size_t index = 0; index < listSent.size(); ++index )
    {
        SW_EXPECT_TRUE_MSG( listReceived[index] == listSent[index], "every message arrives whole, byte for byte, in the order sent" );
    }
    const NetConnection* pClientSide = cluster.getClient( 0 ).findConnection( 0 );
    SW_REQUIRE_TRUE( pClientSide != nullptr );
    SW_EXPECT_TRUE( pClientSide->getStats()._resentMessageCount > 0 ); // 잃은 조각을 다시 보냈다(회선이 실제로 나빴다)
    SW_EXPECT_EQUAL( 0, pClientSide->getPendingReliableCount() );
}

/**
 * @brief [NetEndpointTest] 큰 신뢰 메시지를 받는 도중 연결이 끊기면 모으던 조각은 버려진다 — 같은 자리에 다시 들어온 연결의 첫 메시지 앞에 옛 조각이 붙지 않는다
 */
SW_TEST_CASE( NetEndpointTest, DisconnectMidMessageLeavesNoFragmentsBehind )
{
    test::LoopbackCluster cluster( 43u );
    NetTestUtil::addServerAndClient( cluster, NetEndpointConfig{} );
    SW_REQUIRE_TRUE( cluster.connectClients( 30 ) );
    const vector<uint8> large = NetTestUtil::makePatternMessage( 8u, NetConnection::kMaxReliableMessageSize );
    SW_REQUIRE_TRUE( cluster.getClient( 0 ).sendMessage( 0, NetChannelType::ReliableOrdered, large ) );
    cluster.run( 0.2 ); // 조각 몇 개만 — 64 개가 다 가기 전
    vector<vector<uint8>> listReceived;
    NetTestUtil::pollServerMessages( cluster, listReceived );
    SW_REQUIRE_TRUE( listReceived.empty() );
    const NetConnection* pServerSide = cluster.getServer().findConnection( 0 );
    SW_REQUIRE_TRUE( pServerSide != nullptr );
    SW_REQUIRE_TRUE( pServerSide->getStats()._receivedPacketCount > 3 ); // 서버는 앞 조각을 모으는 중이다

    cluster.getClient( 0 ).disconnect( 0 );
    cluster.run( 0.5 );
    SW_REQUIRE_EQUAL( 0, cluster.getServer().getConnectedCount() );
    SW_REQUIRE_TRUE( cluster.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
    cluster.run( 0.5 );
    SW_REQUIRE_TRUE( cluster.areClientsConnected() );

    const vector<uint8> smallMessage = NetTestUtil::makePatternMessage( 9u, 16 );
    SW_REQUIRE_TRUE( cluster.getClient( 0 ).sendMessage( 0, NetChannelType::ReliableOrdered, smallMessage ) );
    cluster.run( 0.5 );
    NetTestUtil::pollServerMessages( cluster, listReceived );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listReceived.size() );
    SW_EXPECT_TRUE_MSG( listReceived[0] == smallMessage, "no fragment of the old connection is glued to the new connection's first message" );
}

/**
 * @brief [NetEndpointTest] 연결 상한(초당 바이트)이 큰 메시지의 속도를 정한다 — 몫이 남으면 한 차례에 패킷 여럿, 다 쓰면 확인 · 유지만 가 타임아웃보다 오래 포화돼도 연결이 산다
 */
SW_TEST_CASE( NetEndpointTest, SendRateCapPacesLargeMessagesAndKeepsTheConnection )
{
    // 기본 상한(100 KB/s) — 64 KB 셋(192 KB)이 2 초 남짓. 차례(1/30 초)마다 패킷 하나였다면 36 KB/s 라 5 초를 넘는다.
    {
        test::LoopbackCluster cluster( 51u );
        NetTestUtil::addServerAndClient( cluster, NetEndpointConfig{} );
        SW_REQUIRE_TRUE( cluster.connectClients( 30 ) );
        const float64 seconds = NetEndpointTestInternal::measureArrivalSeconds( cluster, 3, 10.0 );
        SW_EXPECT_TRUE_MSG( seconds > 1.5 && seconds < 3.5, "the default cap moves 192 KB at about 100 KB/s, several packets per send turn" );
    }
    // 상한 20 KB/s — 192 KB 는 머리 몫까지 10 초 남짓. 타임아웃(5 초)보다 오래 포화돼도 확인 · 유지가 가 연결이 산다.
    {
        NetEndpointConfig settings;
        settings._maxBytesPerSecond = 20000;
        test::LoopbackCluster cluster( 52u );
        NetTestUtil::addServerAndClient( cluster, settings );
        SW_REQUIRE_TRUE( cluster.connectClients( 30 ) );
        const float64 seconds = NetEndpointTestInternal::measureArrivalSeconds( cluster, 3, 20.0 );
        SW_EXPECT_TRUE_MSG( seconds > 9.0 && seconds < 12.5, "a 20 KB/s cap paces 192 KB to about ten seconds" );
        SW_EXPECT_TRUE( cluster.areClientsConnected() );
        SW_EXPECT_EQUAL( 1, cluster.getServer().getConnectedCount() );
        const NetConnection* pClientSide = cluster.getClient( 0 ).findConnection( 0 );
        SW_REQUIRE_TRUE( pClientSide != nullptr );
        SW_EXPECT_TRUE( pClientSide->getStats()._sentBandwidth < 20000.0f * 8.0f / 1000.0f * 1.2f ); // 최근 1 초(킬로비트) — 상한 안
    }
}

/**
 * @brief [NetEndpointTest] 대역폭을 넘게 비신뢰를 쌓으면 그 연결이 포화로 보이고, 줄이 나이 상한으로 비고 토큰이 차면 다시 포화가 아니다(언리얼 IsNetReady)
 */
SW_TEST_CASE( NetEndpointTest, SaturatedConnectionIsReportedUntilTheQueueDrains )
{
    NetEndpointConfig settings;
    settings._maxBytesPerSecond = 2400; // 패킷 두 개쯤 — 초당
    test::LoopbackCluster cluster( 61u );
    NetTestUtil::addServerAndClient( cluster, settings );
    SW_REQUIRE_TRUE( cluster.connectClients( 30 ) );
    NetEndpoint& client = cluster.getClient( 0 );
    SW_EXPECT_FALSE( client.isConnectionSaturated( 0 ) );
    SW_EXPECT_FALSE( client.isConnectionSaturated( 5 ) ); // 없는 연결
    const vector<uint8> payload = NetTestUtil::makePatternMessage( 9u, 1000 );
    for ( int32 index = 0; index < 20; ++index )
    {
        SW_REQUIRE_TRUE( client.sendMessage( 0, NetChannelType::Unreliable, payload ) );
    }
    cluster.step( 1.0 / 60.0 );
    SW_EXPECT_TRUE( client.isConnectionSaturated( 0 ) );
    for ( int32 frame = 0; frame < 120; ++frame )
    {
        cluster.step( 1.0 / 60.0 );
    }
    SW_EXPECT_FALSE( client.isConnectionSaturated( 0 ) );
    NetConnectionStats stats;
    SW_REQUIRE_TRUE( client.getConnectionStats( 0, stats ) );
    SW_EXPECT_TRUE( stats._expiredUnreliableCount > 0 ); // 다 싣지 못한 것은 기다리다 버렸다
    SW_EXPECT_TRUE( cluster.areClientsConnected() );
}

/**
 * @brief [NetEndpointTest] 상대가 확인하지 않아 신뢰 창이 차면 `trySendMessage` 는 false 만 돌려주고, `sendMessage` 는 신뢰 메시지를 버리지 않고 연결을 `ReliableOverflow` 로 닫는다(감사 N-3)
 */
SW_TEST_CASE( NetEndpointTest, FullReliableWindowClosesInsteadOfDroppingMessages )
{
    NetEndpointConfig settings;
    settings._debugHold = NetDebugHold::Off;
    LoopbackNetwork network;
    NetEndpoint     server;
    NetEndpoint     client;
    server.initialize( network.createEndpoint( 4200 ), settings );
    client.initialize( network.createEndpoint( 5201 ), settings );
    SW_REQUIRE_TRUE( server.listen() );
    SW_REQUIRE_TRUE( client.connect( NetAddress::makeLoopback( 4200 ) ) );
    float64 time = 0.0;
    for ( int32 step = 0; step < 20; ++step )
    {
        time += 1.0 / 60.0;
        server.update( time );
        client.update( time );
    }
    vector<NetEndpointEvent> listEvent;
    server.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( 1, server.getConnectedCount() );

    // 클라이언트가 멈췄다(확인이 오지 않는다) — 창이 찰 때까지 넣는다
    const uint8 payload[]   = { 7, 1, 2 };
    int32       queuedCount = 0;
    while ( server.trySendMessage( 0, NetChannelType::ReliableOrdered, payload, static_cast<int32>( sizeof( payload ) ) ) )
    {
        ++queuedCount;
    }
    SW_EXPECT_EQUAL( NetConnection::kReliableWindow - 1, queuedCount );
    SW_EXPECT_EQUAL( 1, server.getConnectedCount() ); // 역압만 알렸다

    SW_EXPECT_LOG( "Net.ReliableOverflow" );
    SW_EXPECT_FALSE( server.sendMessage( 0, NetChannelType::ReliableOrdered, payload, static_cast<int32>( sizeof( payload ) ) ) );
    listEvent.clear();
    server.pollEvents( listEvent );
    SW_REQUIRE_TRUE( listEvent.empty() == false );
    SW_EXPECT_TRUE( listEvent.back()._kind == NetEndpointEvent::Kind::Disconnected );
    SW_EXPECT_TRUE( listEvent.back()._reason == NetDisconnectReason::ReliableOverflow );
}
