#include "pch.h"

#include "Core/Delegate/Subscription.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Message/NetStatsReport.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Network/Tests/NetTestUtil.h"
#include "Core/Network/Transport/NetTransport.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

// 메시지 라우터 — 영역 · 종류로 나눠 주기, 겹치는 처리기 거절, 라우터가 먼저 사라짐, 깨진 메시지 세기, 사건이 메시지보다 먼저, 상대 모두에게 보내기 · 중계.

using namespace sw;
using test::NetTestUtil;

namespace
{
    struct NetMessageRouterTestInternal
    {
        /** @brief 테스트용 처리기 — 맡은 영역에서 @p acceptedKind 만 맡고(가변 정수 하나가 몸) 받은 수를 셉니다. */
        class CountingHandler : public INetMessageHandler
        {
        public:
            CountingHandler( uint8 rangeBase, uint8 acceptedKind )
                : _rangeBase{ rangeBase }
                , _acceptedKind{ acceptedKind }
                , _handledCount{ 0 }
                , _lastConnectionID{ -1 }
            {
            }

            uint8           getMessageRangeBase() const override { return _rangeBase; }
            uint16          getMessageKindMask() const override { return static_cast<uint16>( 1u << ( _acceptedKind - _rangeBase ) ); }
            NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override
            {
                (void)body.readVarUint(); // 값은 쓰지 않는다 — 넘침은 바로 아래에서 본다
                if ( body.hasOverflowed() )
                    return NetHandleResult::Malformed;
                ++_handledCount;
                _lastConnectionID = context._connectionID;
                return NetHandleResult::Handled;
            }

            int32 getHandledCount() const { return _handledCount; }
            int32 getLastConnectionID() const { return _lastConnectionID; }

        private:
            uint8 _rangeBase;
            uint8 _acceptedKind;
            int32 _handledCount;
            int32 _lastConnectionID;
        };
    };
} // namespace

SW_TEST_CASE( NetMessageRouterTest, MessageRouterDispatchesByRangeAndKeepsUnhandled )
{
    test::LoopbackCluster pair( 77u );
    NetTestUtil::addServerAndClient( pair, NetEndpointConfig{} );
    SW_REQUIRE_TRUE( pair.getServer().listen() );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
    pair.run( 0.3 );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) == NetConnectionState::Connected );

    // 한 영역에 처리기 둘 — 등록 순서대로 묻고 처음 받아들인 쪽에서 멈춘다.
    NetMessageRouterTestInternal::CountingHandler lockstep( NetMessageRange::kFramework + NetMessageRange::kSize, 0x21 );
    NetMessageRouterTestInternal::CountingHandler gameFirst( NetMessageRange::kGame, 0x81 );
    NetMessageRouterTestInternal::CountingHandler gameSecond( NetMessageRange::kGame, 0x82 );
    NetMessageRouter                              router;
    const sw::Subscription                        lockstepSubscription   = router.registerHandler( &lockstep );
    sw::Subscription                              gameFirstSubscription  = router.registerHandler( &gameFirst );
    const sw::Subscription                        gameSecondSubscription = router.registerHandler( &gameSecond );
    SW_REQUIRE_TRUE( lockstepSubscription.isActive() );
    SW_REQUIRE_TRUE( gameFirstSubscription.isActive() );
    SW_REQUIRE_TRUE( gameSecondSubscription.isActive() );

    NetMessageWriter messageWriter;
    const uint8      arrKind[] = { 0x21, 0x81, 0x82, 0x82, 0x83, 0x51, 0x22 };
    for ( const uint8 kind : arrKind )
    {
        messageWriter.begin( kind ).writeVarUint( kind );
        SW_REQUIRE_TRUE( messageWriter.send( pair.getClient( 0 ), 0, NetChannelType::ReliableOrdered ) );
    }
    pair.run( 0.2 );
    vector<NetReceivedMessage> listUnhandled;
    SW_EXPECT_EQUAL( 7, router.poll( pair.getServer(), &listUnhandled ) );
    SW_EXPECT_EQUAL( 1, lockstep.getHandledCount() );
    SW_EXPECT_EQUAL( 1, gameFirst.getHandledCount() );
    SW_EXPECT_EQUAL( 2, gameSecond.getHandledCount() );
    SW_EXPECT_EQUAL( 0, gameSecond.getLastConnectionID() );
    // 거절(0x83 · 0x22)과 처리기 없는 영역(0x51)은 받은 순서대로 남는다.
    SW_REQUIRE_EQUAL( 3, static_cast<int32>( listUnhandled.size() ) );
    SW_EXPECT_EQUAL( 0x83, static_cast<int32>( listUnhandled[0]._buffer[0] ) );
    SW_EXPECT_EQUAL( 0x51, static_cast<int32>( listUnhandled[1]._buffer[0] ) );
    SW_EXPECT_EQUAL( 0x22, static_cast<int32>( listUnhandled[2]._buffer[0] ) );
    SW_EXPECT_EQUAL( 0, listUnhandled[0]._connectionID );

    // 종류별 셈은 처리기가 없는 메시지까지 센다(`net.stats` 의 비중). 0x82 는 두 번(종류 1 바이트 + varint 2 바이트씩)이라 바이트가 가장 크다.
    SW_EXPECT_EQUAL( router.getKindStats().get( 0x82 )._messageCount, uint64{ 2 } );
    SW_EXPECT_EQUAL( router.getKindStats().get( 0x82 )._byteCount, uint64{ 6 } );
    SW_EXPECT_EQUAL( router.getKindStats().get( 0x51 )._messageCount, uint64{ 1 } );
    SW_EXPECT_EQUAL( router.getKindStats().get( 0x80 )._messageCount, uint64{ 0 } );
    // 보낸 쪽(클라이언트 호스트)은 큐에 넣은 메시지를 같은 표로 센다 — 받은 쪽 셈과 같아야 한다(신뢰 채널, 잃은 것 없음).
    NetMessageKindStatsTable sentStats;
    pair.getClient( 0 ).addSentKindStats( sentStats );
    SW_EXPECT_EQUAL( sentStats.get( 0x82 )._messageCount, uint64{ 2 } );
    SW_EXPECT_EQUAL( sentStats.get( 0x82 )._byteCount, uint64{ 6 } );
    NetStatsReport report;
    report.populateKinds( router.getKindStats(), sentStats );
    SW_REQUIRE_EQUAL( 6, static_cast<int32>( report._listKindEntry.size() ) );
    SW_EXPECT_EQUAL( 0x82, static_cast<int32>( report._listKindEntry[0]._kind ) );
    SW_EXPECT_EQUAL( report._listKindEntry[0]._messageCount, uint64{ 2 } );
    SW_REQUIRE_EQUAL( 6, static_cast<int32>( report._listSentKindEntry.size() ) );
    SW_EXPECT_EQUAL( 0x82, static_cast<int32>( report._listSentKindEntry[0]._kind ) );
    // 보고 줄은 두 방향을 다 실어 되읽힌다.
    NetStatsReport parsed;
    SW_REQUIRE_TRUE( NetStatsReport::tryParseLine( report.makeLine(), parsed ) );
    SW_EXPECT_EQUAL( 6, static_cast<int32>( parsed._listSentKindEntry.size() ) );
    SW_EXPECT_EQUAL( parsed._listSentKindEntry[0]._byteCount, uint64{ 6 } );

    // 뺀 처리기는 더 묻지 않는다. 빈 메시지는 누구에게도 가지 않는다.
    gameFirstSubscription.reset();
    messageWriter.begin( 0x81 ).writeVarUint( 1 );
    SW_EXPECT_TRUE( NetHandleResult::NotMine == router.dispatch( NetMessageContext{}, messageWriter.getBytes().data(), messageWriter.getByteCount() ) );
    SW_EXPECT_TRUE( NetHandleResult::NotMine == router.dispatch( NetMessageContext{}, nullptr, 0 ) );
    SW_EXPECT_EQUAL( 1, gameFirst.getHandledCount() );
}

/**
 * @brief [NetMessageRouterTest] 이미 맡은 종류를 맡으려는 처리기는 통째로 받지 않는다 — 메시지는 먼저 단 처리기가 받고, 겹치지 않는 처리기는 그대로 단다
 */
SW_TEST_CASE( NetMessageRouterTest, MessageRouterRefusesAHandlerThatOverlapsAClaimedKind )
{
    NetMessageRouterTestInternal::CountingHandler first( NetMessageRange::kGame, 0x81 );
    NetMessageRouterTestInternal::CountingHandler overlapping( NetMessageRange::kGame, 0x81 );
    NetMessageRouterTestInternal::CountingHandler disjoint( NetMessageRange::kGame, 0x82 );
    NetMessageRouter                              router;
    SW_EXPECT_LOG( "Net.RouterKindClaimed" ); // 겹치는 처리기 · 같은 처리기를 다시 단 것은 오류와 함께 거절한다
    SW_EXPECT_LOG( "Net.RouterNullHandler" ); // 빈 처리기도
    sw::Subscription firstSubscription = router.registerHandler( &first );
    SW_EXPECT_TRUE( firstSubscription.isActive() );
    SW_EXPECT_FALSE( router.registerHandler( &overlapping ).isActive() );
    const sw::Subscription disjointSubscription = router.registerHandler( &disjoint );
    SW_EXPECT_TRUE( disjointSubscription.isActive() );
    // 같은 처리기를 다시 달면 받지 않는다 — 두 구독이 한 등록을 나눠 가지면 먼저 풀린 쪽이 다른 쪽의 처리기를 뺀다
    SW_EXPECT_FALSE( router.registerHandler( &first ).isActive() );
    SW_EXPECT_FALSE( router.registerHandler( nullptr ).isActive() );

    const uint8 arrMessage[] = { 0x81, 0x01 };
    SW_EXPECT_TRUE( NetHandleResult::Handled == router.dispatch( NetMessageContext{}, arrMessage, static_cast<int32>( sizeof( arrMessage ) ) ) );
    SW_EXPECT_EQUAL( 1, first.getHandledCount() );
    SW_EXPECT_EQUAL( 0, overlapping.getHandledCount() );

    // 거절된 처리기는 목록에 없다 — 먼저 단 처리기의 구독을 풀면 비로소 단다
    firstSubscription.reset();
    const sw::Subscription overlappingSubscription = router.registerHandler( &overlapping );
    SW_EXPECT_TRUE( overlappingSubscription.isActive() );
}

/**
 * @brief [NetMessageRouterTest] 라우터가 먼저 사라지면 처리기 구독은 무효가 되고, 구독의 소멸은 사라진 라우터를 부르지 않는다
 */
SW_TEST_CASE( NetMessageRouterTest, MessageRouterDestroyedFirstInvalidatesTheSubscription )
{
    NetMessageRouterTestInternal::CountingHandler handler( NetMessageRange::kGame, 0x81 );
    sw::Subscription                              subscription;
    {
        NetMessageRouter router;
        subscription = router.registerHandler( &handler );
        SW_EXPECT_TRUE( subscription.isActive() );
    }
    SW_EXPECT_FALSE( subscription.isActive() );
    subscription.reset();
}

/**
 * @brief [NetMessageRouterTest] 맡은 종류인데 몸이 깨진 메시지는 라우터가 세고 버린다 — 다른 처리기 · "처리기 없음" 목록으로 가지 않는다
 * @details `bool` 하나가 "내 것 아님" 과 "깨짐" 을 함께 뜻하던 때는 깨진 메시지가 다음 처리기로 넘어가 결국 처리기 없는 메시지 목록에 섞였다.
 */
SW_TEST_CASE( NetMessageRouterTest, MalformedMessageIsCountedNotForwarded )
{
    test::LoopbackCluster pair( 77u );
    NetTestUtil::addServerAndClient( pair, NetEndpointConfig{} );
    SW_REQUIRE_TRUE( pair.getServer().listen() );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
    pair.run( 0.3 );
    NetMessageRouterTestInternal::CountingHandler handler( NetMessageRange::kGame, 0x81 );
    NetMessageRouter                              router;
    const sw::Subscription                        subscription = router.registerHandler( &handler );
    NetMessageWriter                              messageWriter;
    (void)messageWriter.begin( 0x81 ); // 몸(가변 정수)이 없다
    SW_REQUIRE_TRUE( messageWriter.send( pair.getClient( 0 ), 0, NetChannelType::ReliableOrdered ) );
    messageWriter.begin( 0x81 ).writeVarUint( 5 );
    SW_REQUIRE_TRUE( messageWriter.send( pair.getClient( 0 ), 0, NetChannelType::ReliableOrdered ) );
    pair.run( 0.2 );
    vector<NetReceivedMessage> listUnhandled;
    SW_EXPECT_EQUAL( 2, router.poll( pair.getServer(), &listUnhandled ) );
    SW_EXPECT_EQUAL( 1, handler.getHandledCount() );
    SW_EXPECT_EQUAL( uint64{ 1 }, router.getMalformedCount() );
    SW_EXPECT_TRUE( listUnhandled.empty() );
}

/**
 * @brief [NetMessageRouterTest] 라우터는 연결 사건을 메시지보다 먼저 모든 처리기에 알린다 — 연결 · 끊김이 한 번의 꺼내기 안에서 메시지와 순서를 지킨다
 */
SW_TEST_CASE( NetMessageRouterTest, RouterTellsHandlersAboutConnectionsBeforeTheirMessages )
{
    class ConnectionTracker final : public INetMessageHandler
    {
    public:
        uint8           getMessageRangeBase() const override { return NetMessageRange::kGame; }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override
        {
            (void)context;
            (void)body;
            _log.push_back( _bOpen ? 'M' : 'x' );
            return NetHandleResult::Handled;
        }
        void onConnectionOpened( int32 connectionID ) override
        {
            (void)connectionID;
            _bOpen = true;
            _log.push_back( 'O' );
        }
        void onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override
        {
            (void)reason;
            (void)connectionID;
            _bOpen = false;
            _log.push_back( 'C' );
        }

        string _log;
        bool   _bOpen{ false }; ///< NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
    };

    test::LoopbackCluster pair( 77u );
    NetTestUtil::addServerAndClient( pair, NetEndpointConfig{} );
    SW_REQUIRE_TRUE( pair.getServer().listen() );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
    ConnectionTracker      tracker;
    NetMessageRouter       router;
    const sw::Subscription subscription = router.registerHandler( &tracker );
    pair.run( 0.3 );
    NetMessageWriter messageWriter;
    messageWriter.begin( 0x80 ).writeVarUint( 1 );
    SW_REQUIRE_TRUE( messageWriter.send( pair.getClient( 0 ), 0, NetChannelType::ReliableOrdered ) );
    pair.run( 0.2 );
    (void)router.poll( pair.getServer() );
    pair.getClient( 0 ).disconnect( 0 );
    pair.run( 0.2 );
    (void)router.poll( pair.getServer() );
    SW_EXPECT_TRUE( tracker._log == "OMC" );
}

/**
 * @brief [NetMessageRouterTest] 상대 모두에게 보내기 — 클라이언트는 서버에게만, 서버는 모두에게. 서버가 받은 것을 중계하면 보낸 쪽 말고 모두가 받는다(받은 채널 그대로)
 */
SW_TEST_CASE( NetMessageRouterTest, SendToPeersAndRelayReachEveryOtherPeer )
{
    LoopbackNetwork network;
    NetEndpoint     server;
    NetEndpoint     arrClient[2];
    server.initialize( network.createEndpoint( 4000 ), NetEndpointConfig{} );
    SW_REQUIRE_TRUE( server.listen() );
    for ( int32 index = 0; index < 2; ++index )
    {
        arrClient[index].initialize( network.createEndpoint( static_cast<uint16>( 6000 + index ) ), NetEndpointConfig{} );
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
    SW_REQUIRE_EQUAL( 2, server.getConnectedCount() );

    // 클라이언트 0 → 서버만.
    NetMessageWriter writer;
    writer.begin( NetMessageRange::kGame ).writeVarUint( 7 );
    SW_EXPECT_EQUAL( 1, writer.sendToPeers( arrClient[0], NetChannelType::Unreliable ) );
    runAll( 0.2 );
    int32          connectionID = -1;
    NetChannelType channel      = NetChannelType::ReliableOrdered;
    vector<uint8>  buffer;
    SW_REQUIRE_TRUE( server.receiveMessage( connectionID, channel, buffer ) );
    SW_EXPECT_TRUE( channel == NetChannelType::Unreliable );

    // 서버의 중계 — 보낸 클라이언트 0 말고 1 만, 같은 채널로.
    NetMessageContext context;
    context._pMessage     = buffer.data();
    context._messageSize  = static_cast<int32>( buffer.size() );
    context._connectionID = connectionID;
    context._channel      = channel;
    context._kind         = buffer[0];
    SW_EXPECT_EQUAL( 1, NetMessageRouter::relayToOtherPeers( server, context ) );
    SW_EXPECT_EQUAL( 0, NetMessageRouter::relayToOtherPeers( arrClient[0], context ) ); // 클라이언트는 중계하지 않는다
    runAll( 0.2 );
    SW_EXPECT_FALSE( arrClient[0].receiveMessage( connectionID, channel, buffer ) );
    SW_REQUIRE_TRUE( arrClient[1].receiveMessage( connectionID, channel, buffer ) );
    SW_EXPECT_TRUE( channel == NetChannelType::Unreliable );

    // 서버 → 모두.
    writer.begin( NetMessageRange::kGame ).writeVarUint( 9 );
    SW_EXPECT_EQUAL( 2, writer.sendToPeers( server, NetChannelType::ReliableOrdered ) );
    runAll( 0.2 );
    for ( NetEndpoint& client : arrClient )
    {
        SW_EXPECT_TRUE( client.receiveMessage( connectionID, channel, buffer ) );
    }
}
