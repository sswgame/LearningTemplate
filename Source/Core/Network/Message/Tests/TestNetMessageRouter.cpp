#include "pch.h"

#include "Core/Delegate/Subscription.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Message/NetMessageTable.h"
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
        /** @brief 게임 영역의 종류들입니다(영역 + n) — 게임 영역의 첫 값이 옮겨도 테스트가 따라간다. */
        static constexpr uint8 kGameKind0 = NetMessageRange::kGame;
        static constexpr uint8 kGameKind1 = NetMessageRange::kGame + 1;
        static constexpr uint8 kGameKind2 = NetMessageRange::kGame + 2;
        static constexpr uint8 kGameKind3 = NetMessageRange::kGame + 3;

        /** @brief 테스트용 처리기 — 맡은 영역에서 @p acceptedKind 만 맡고(가변 정수 하나가 몸) 받은 수를 셉니다. 방향은 @p direction 이다. */
        class CountingHandler : public INetMessageHandler
        {
        public:
            CountingHandler( uint8 rangeBase, uint8 acceptedKind, uint32 wireVersion = kNoMessageTable, NetMessageDirection direction = NetMessageDirection::Both )
                : _wireVersion{ wireVersion }
                , _direction{ direction }
                , _rangeBase{ rangeBase }
                , _acceptedKind{ acceptedKind }
                , _handledCount{ 0 }
                , _lastConnectionID{ -1 }
            {
            }

            uint8                   getMessageRangeBase() const override { return _rangeBase; }
            uint16                  getMessageKindMask() const override { return static_cast<uint16>( 1u << ( _acceptedKind - _rangeBase ) ); }
            NetMessageDirectionMask getMessageDirectionMask() const override
            {
                NetMessageDirectionMask mask{};
                mask._toServerMask = _direction != NetMessageDirection::ToClient ? getMessageKindMask() : uint16{ 0 };
                mask._toClientMask = _direction != NetMessageDirection::ToServer ? getMessageKindMask() : uint16{ 0 };
                return mask;
            }
            uint32          getWireVersion() const override { return _wireVersion; }
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
            uint32              _wireVersion;
            NetMessageDirection _direction;
            uint8               _rangeBase;
            uint8               _acceptedKind;
            int32               _handledCount;
            int32               _lastConnectionID;
        };
        /** @brief 판 맞춤 한 번의 결과입니다. */
        struct RouterVersionRunResult
        {
            int32              _serverHandledCount{ 0 };
            NetConnectionState _clientState{ NetConnectionState::Disconnected };
        };

        /** @brief 서버 · 클라이언트에 같은 영역의 처리기를 판 @p serverVersion · @p clientVersion 으로 달고 양쪽 라우터를 돌립니다. */
        static void connectWithTableVersions( uint32 serverVersion, uint32 clientVersion, RouterVersionRunResult& outResult )
        {
            test::LoopbackCluster pair( 78u );
            NetTestUtil::addServerAndClient( pair, NetEndpointConfig{} );
            SW_REQUIRE_TRUE( pair.getServer().listen() );
            SW_REQUIRE_TRUE( pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
            pair.run( 0.3 );
            SW_REQUIRE_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) == NetConnectionState::Connected );

            constexpr uint8        kRange = NetMessageRange::kFramework;
            CountingHandler        serverHandler{ kRange, static_cast<uint8>( kRange + 1 ), serverVersion };
            CountingHandler        clientHandler{ kRange, static_cast<uint8>( kRange + 2 ), clientVersion };
            NetMessageRouter       serverRouter;
            NetMessageRouter       clientRouter;
            const sw::Subscription serverSubscription = serverRouter.registerHandler( &serverHandler );
            const sw::Subscription clientSubscription = clientRouter.registerHandler( &clientHandler );
            for ( int32 round = 0; round < 10; ++round )
            {
                pair.run( 0.05 );
                (void)serverRouter.poll( pair.getServer() );
                (void)clientRouter.poll( pair.getClient( 0 ) );
            }
            // 판 맞춤이 끝난 뒤 클라이언트가 그 영역 메시지를 보낸다 — 판이 같으면 서버 처리기가 받는다.
            NetMessageWriter messageWriter;
            messageWriter.begin( static_cast<uint8>( kRange + 1 ) ).writeVarUint( 5 );
            (void)messageWriter.send( pair.getClient( 0 ), 0, NetChannelKind::ReliableOrdered ); // 끊겼으면 보내지 못한다 — 아래 상태가 본다
            for ( int32 round = 0; round < 6; ++round )
            {
                pair.run( 0.05 );
                (void)serverRouter.poll( pair.getServer() );
                (void)clientRouter.poll( pair.getClient( 0 ) );
            }
            outResult._serverHandledCount = serverHandler.getHandledCount();
            outResult._clientState        = pair.getClient( 0 ).getConnectionState( 0 );
        }
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
    NetMessageRouterTestInternal::CountingHandler gameFirst( NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind1 );
    NetMessageRouterTestInternal::CountingHandler gameSecond( NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind2 );
    NetMessageRouter                              router;
    const sw::Subscription                        lockstepSubscription   = router.registerHandler( &lockstep );
    sw::Subscription                              gameFirstSubscription  = router.registerHandler( &gameFirst );
    const sw::Subscription                        gameSecondSubscription = router.registerHandler( &gameSecond );
    SW_REQUIRE_TRUE( lockstepSubscription.isActive() );
    SW_REQUIRE_TRUE( gameFirstSubscription.isActive() );
    SW_REQUIRE_TRUE( gameSecondSubscription.isActive() );

    NetMessageWriter messageWriter;
    const uint8      arrKind[] = { 0x21, NetMessageRouterTestInternal::kGameKind1, NetMessageRouterTestInternal::kGameKind2, NetMessageRouterTestInternal::kGameKind2, NetMessageRouterTestInternal::kGameKind3, 0x51, 0x22 };
    for ( const uint8 kind : arrKind )
    {
        messageWriter.begin( kind ).writeVarUint( kind );
        SW_REQUIRE_TRUE( messageWriter.send( pair.getClient( 0 ), 0, NetChannelKind::ReliableOrdered ) );
    }
    pair.run( 0.2 );
    vector<NetReceivedMessage> listUnhandled;
    SW_EXPECT_EQUAL( 7, router.poll( pair.getServer(), &listUnhandled ) );
    SW_EXPECT_EQUAL( 1, lockstep.getHandledCount() );
    SW_EXPECT_EQUAL( 1, gameFirst.getHandledCount() );
    SW_EXPECT_EQUAL( 2, gameSecond.getHandledCount() );
    SW_EXPECT_EQUAL( 0, gameSecond.getLastConnectionID() );
    // 거절(게임 영역 + 3 · 0x22)과 처리기 없는 영역(0x51)은 받은 순서대로 남는다.
    SW_REQUIRE_EQUAL( 3, static_cast<int32>( listUnhandled.size() ) );
    SW_EXPECT_EQUAL( static_cast<int32>( NetMessageRouterTestInternal::kGameKind3 ), static_cast<int32>( listUnhandled[0]._buffer[0] ) );
    SW_EXPECT_EQUAL( 0x51, static_cast<int32>( listUnhandled[1]._buffer[0] ) );
    SW_EXPECT_EQUAL( 0x22, static_cast<int32>( listUnhandled[2]._buffer[0] ) );
    SW_EXPECT_EQUAL( 0, listUnhandled[0]._connectionID );

    // 종류별 셈은 처리기가 없는 메시지까지 센다(`net.stats` 의 비중). 게임 영역 + 2 는 두 번(종류 1 바이트 + varint 2 바이트씩)이라 바이트가 가장 크다.
    SW_EXPECT_EQUAL( router.getKindStats().get( NetMessageRouterTestInternal::kGameKind2 )._messageCount, uint64{ 2 } );
    SW_EXPECT_EQUAL( router.getKindStats().get( NetMessageRouterTestInternal::kGameKind2 )._byteCount, uint64{ 6 } );
    SW_EXPECT_EQUAL( router.getKindStats().get( 0x51 )._messageCount, uint64{ 1 } );
    SW_EXPECT_EQUAL( router.getKindStats().get( NetMessageRouterTestInternal::kGameKind0 )._messageCount, uint64{ 0 } );
    // 보낸 쪽(클라이언트 호스트)은 큐에 넣은 메시지를 같은 표로 센다 — 받은 쪽 셈과 같아야 한다(신뢰 채널, 잃은 것 없음).
    NetMessageKindStatsTable sentStats;
    pair.getClient( 0 ).addSentKindStats( sentStats );
    SW_EXPECT_EQUAL( sentStats.get( NetMessageRouterTestInternal::kGameKind2 )._messageCount, uint64{ 2 } );
    SW_EXPECT_EQUAL( sentStats.get( NetMessageRouterTestInternal::kGameKind2 )._byteCount, uint64{ 6 } );
    NetStatsReport report;
    report.populateKinds( router.getKindStats(), sentStats );
    SW_REQUIRE_EQUAL( 6, static_cast<int32>( report._listKindEntry.size() ) );
    SW_EXPECT_EQUAL( static_cast<int32>( NetMessageRouterTestInternal::kGameKind2 ), static_cast<int32>( report._listKindEntry[0]._kind ) );
    SW_EXPECT_EQUAL( report._listKindEntry[0]._messageCount, uint64{ 2 } );
    SW_REQUIRE_EQUAL( 6, static_cast<int32>( report._listSentKindEntry.size() ) );
    SW_EXPECT_EQUAL( static_cast<int32>( NetMessageRouterTestInternal::kGameKind2 ), static_cast<int32>( report._listSentKindEntry[0]._kind ) );
    // 보고 줄은 두 방향을 다 실어 되읽힌다.
    NetStatsReport parsed;
    SW_REQUIRE_TRUE( NetStatsReport::tryParseLine( report.makeLine(), parsed ) );
    SW_EXPECT_EQUAL( 6, static_cast<int32>( parsed._listSentKindEntry.size() ) );
    SW_EXPECT_EQUAL( parsed._listSentKindEntry[0]._byteCount, uint64{ 6 } );

    // 뺀 처리기는 더 묻지 않는다. 빈 메시지는 누구에게도 가지 않는다.
    gameFirstSubscription.reset();
    messageWriter.begin( NetMessageRouterTestInternal::kGameKind1 ).writeVarUint( 1 );
    SW_EXPECT_TRUE( NetHandleResult::NotMine == router.dispatch( NetPeerRole::Server, NetMessageContext{}, messageWriter.getBytes().data(), messageWriter.getByteCount() ) );
    SW_EXPECT_TRUE( NetHandleResult::NotMine == router.dispatch( NetPeerRole::Server, NetMessageContext{}, nullptr, 0 ) );
    SW_EXPECT_EQUAL( 1, gameFirst.getHandledCount() );
}

/**
 * @brief [NetMessageRouterTest] 이미 맡은 종류를 맡으려는 처리기는 통째로 받지 않는다 — 메시지는 먼저 단 처리기가 받고, 겹치지 않는 처리기는 그대로 단다
 */
SW_TEST_CASE( NetMessageRouterTest, MessageRouterRefusesAHandlerThatOverlapsAClaimedKind )
{
    NetMessageRouterTestInternal::CountingHandler first{ NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind1 };
    NetMessageRouterTestInternal::CountingHandler overlapping( NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind1 );
    NetMessageRouterTestInternal::CountingHandler disjoint( NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind2 );
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

    const uint8 arrMessage[] = { NetMessageRouterTestInternal::kGameKind1, 0x01 };
    SW_EXPECT_TRUE( NetHandleResult::Handled == router.dispatch( NetPeerRole::Server, NetMessageContext{}, arrMessage, static_cast<int32>( sizeof( arrMessage ) ) ) );
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
    NetMessageRouterTestInternal::CountingHandler handler( NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind1 );
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
 * @brief [NetMessageRouterTest] 세션이 닫히면 구독이 무효가 되고 번호가 오른다 — 다음 세션의 처리기가 같은 종류를 맡을 수 있고, 닫힌 세션의 구독을 놓아도 새 등록은 남는다
 */
SW_TEST_CASE( NetMessageRouterTest, ClosingTheSessionInvalidatesSubscriptions )
{
    NetMessageRouter                              router;
    NetMessageRouterTestInternal::CountingHandler first( NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind1 );
    NetMessageRouterTestInternal::CountingHandler second( NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind1 );
    SW_EXPECT_LOG( "Net.RouterKindClaimed" );
    sw::Subscription firstSubscription = router.registerHandler( &first );
    SW_REQUIRE_TRUE( firstSubscription.isActive() );
    const uint32 serial = router.getSessionSerial();

    router.closeSession();
    SW_EXPECT_FALSE( firstSubscription.isActive() );
    SW_EXPECT_NOT_EQUAL( serial, router.getSessionSerial() );

    const sw::Subscription secondSubscription = router.registerHandler( &second );
    SW_EXPECT_TRUE( secondSubscription.isActive() );
    firstSubscription.reset();
    SW_EXPECT_TRUE( secondSubscription.isActive() );
    SW_EXPECT_FALSE( router.registerHandler( &first ).isActive() ); // 새 세션의 등록이 남아 있다 — 같은 종류는 거절된다
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
    NetMessageRouterTestInternal::CountingHandler handler( NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind1 );
    NetMessageRouter                              router;
    const sw::Subscription                        subscription = router.registerHandler( &handler );
    NetMessageWriter                              messageWriter;
    (void)messageWriter.begin( NetMessageRouterTestInternal::kGameKind1 ); // 몸(가변 정수)이 없다
    SW_REQUIRE_TRUE( messageWriter.send( pair.getClient( 0 ), 0, NetChannelKind::ReliableOrdered ) );
    messageWriter.begin( NetMessageRouterTestInternal::kGameKind1 ).writeVarUint( 5 );
    SW_REQUIRE_TRUE( messageWriter.send( pair.getClient( 0 ), 0, NetChannelKind::ReliableOrdered ) );
    pair.run( 0.2 );
    vector<NetReceivedMessage> listUnhandled;
    SW_EXPECT_EQUAL( 2, router.poll( pair.getServer(), &listUnhandled ) );
    SW_EXPECT_EQUAL( 1, handler.getHandledCount() );
    SW_EXPECT_EQUAL( uint64{ 1 }, router.getMalformedCount().get() );
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
        uint8                   getMessageRangeBase() const override { return NetMessageRange::kGame; }
        uint32                  getWireVersion() const override { return kNoMessageTable; }
        NetMessageDirectionMask getMessageDirectionMask() const override { return NetMessageDirectionMask{ 0xFFFFu, 0xFFFFu }; } // 맡는 종류(기본 0xFFFF) 모두 양쪽
        NetHandleResult         handleNetMessage( const NetMessageContext& context, BitReader& body ) override
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
    messageWriter.begin( NetMessageRouterTestInternal::kGameKind0 ).writeVarUint( 1 );
    SW_REQUIRE_TRUE( messageWriter.send( pair.getClient( 0 ), 0, NetChannelKind::ReliableOrdered ) );
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
    SW_REQUIRE_TRUE( server.initialize( network.createEndpoint( 4000 ), NetEndpointConfig{} ) );
    SW_REQUIRE_TRUE( server.listen() );
    for ( int32 index = 0; index < 2; ++index )
    {
        SW_REQUIRE_TRUE( arrClient[index].initialize( network.createEndpoint( static_cast<uint16>( 6000 + index ) ), NetEndpointConfig{} ) );
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
    SW_EXPECT_EQUAL( 1, writer.sendToPeers( arrClient[0], NetChannelKind::Unreliable ) );
    runAll( 0.2 );
    int32          connectionID = -1;
    NetChannelKind channel      = NetChannelKind::ReliableOrdered;
    vector<uint8>  buffer;
    SW_REQUIRE_TRUE( server.receiveMessage( connectionID, channel, buffer ) );
    SW_EXPECT_TRUE( channel == NetChannelKind::Unreliable );

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
    SW_EXPECT_TRUE( channel == NetChannelKind::Unreliable );

    // 서버 → 모두.
    writer.begin( NetMessageRange::kGame ).writeVarUint( 9 );
    SW_EXPECT_EQUAL( 2, writer.sendToPeers( server, NetChannelKind::ReliableOrdered ) );
    runAll( 0.2 );
    for ( NetEndpoint& client : arrClient )
    {
        SW_EXPECT_TRUE( client.receiveMessage( connectionID, channel, buffer ) );
    }
}

/**
 * @brief [NetMessageRouterTest] 같은 영역의 메시지 표 판이 다른 두 빌드는 처리기를 단 뒤 라우터가 연결을 끊는다 — 게임이 판을 손으로 섞지 않아도 빠지지 않는다. 판이 같으면 붙어 있고 메시지가 간다
 */
SW_TEST_CASE( NetMessageRouterTest, DifferentMessageTableVersionsDisconnect )
{
    NetMessageRouterTestInternal::RouterVersionRunResult same;
    NetMessageRouterTestInternal::connectWithTableVersions( 101u, 101u, same );
    SW_EXPECT_TRUE( same._clientState == NetConnectionState::Connected );
    SW_EXPECT_EQUAL( 1, same._serverHandledCount );

    SW_EXPECT_LOG( "Net.MessageTableVersionMismatch" );
    NetMessageRouterTestInternal::RouterVersionRunResult different;
    NetMessageRouterTestInternal::connectWithTableVersions( 101u, 202u, different );
    SW_EXPECT_TRUE( different._clientState != NetConnectionState::Connected );
    SW_EXPECT_EQUAL( 0, different._serverHandledCount );
}

/**
 * @brief [NetMessageRouterTest] 한 영역은 표 하나다 — 판이 다른 처리기 둘을 한 영역에 달 수 없고, 라우터의 판 알림 종류는 처리기가 맡지 못한다
 */
SW_TEST_CASE( NetMessageRouterTest, OneRangeHasOneTableVersionAndTheRouterKindIsReserved )
{
    NetMessageRouter                              router;
    NetMessageRouterTestInternal::CountingHandler first{ NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind1, 7u };
    NetMessageRouterTestInternal::CountingHandler sameTable{ NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind2, 7u };
    NetMessageRouterTestInternal::CountingHandler otherTable{ NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind3, 8u };
    NetMessageRouterTestInternal::CountingHandler routerKind{ NetMessageRange::kEngine, NetMessageRange::kRouterWireVersionKind, 9u };
    const sw::Subscription                        firstSubscription = router.registerHandler( &first );
    const sw::Subscription                        sameSubscription  = router.registerHandler( &sameTable );
    SW_EXPECT_TRUE( firstSubscription.isActive() && sameSubscription.isActive() );
    SW_EXPECT_LOG( "Net.RouterTableVersionConflict" );
    SW_EXPECT_FALSE( router.registerHandler( &otherTable ).isActive() );
    SW_EXPECT_LOG( "Net.RouterKindReserved" );
    SW_EXPECT_FALSE( router.registerHandler( &routerKind ).isActive() );
    NetMessageRouterTestInternal::CountingHandler worldKind{ NetMessageRange::kEngine, NetMessageRange::kRouterWorldKind, 9u };
    SW_EXPECT_LOG( "Net.RouterKindReserved" );
    SW_EXPECT_FALSE( router.registerHandler( &worldKind ).isActive() );
}

/**
 * @brief [NetMessageRouterTest] 서버의 월드 세대가 연결된 클라이언트와 뒤에 붙은 클라이언트에 닿고, 세션이 닫히면 0 이 된다
 */
SW_TEST_CASE( NetMessageRouterTest, WorldGenerationReachesEveryClient )
{
    test::LoopbackCluster pair( 81u );
    NetTestUtil::addServerAndClient( pair, NetEndpointConfig{} );
    NetMessageRouter serverRouter;
    NetMessageRouter clientRouter;
    SW_REQUIRE_TRUE( pair.getServer().listen() );
    serverRouter.openWorld( pair.getServer() ); // 아직 아무도 없다 — 붙을 때 받는다
    SW_REQUIRE_TRUE( pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
    const auto pollAll = [&]()
    {
        for ( int32 round = 0; round < 10; ++round )
        {
            pair.run( 0.05 );
            (void)serverRouter.poll( pair.getServer() );
            (void)clientRouter.poll( pair.getClient( 0 ) );
        }
    };
    pollAll();
    SW_EXPECT_EQUAL( 1u, serverRouter.getWorldGeneration() );
    SW_EXPECT_EQUAL( 1u, clientRouter.getWorldGeneration() );
    serverRouter.openWorld( pair.getServer() );
    SW_EXPECT_EQUAL( 1u, clientRouter.getWorldGeneration() ); // 닿기 전이다
    pollAll();
    SW_EXPECT_EQUAL( 2u, clientRouter.getWorldGeneration() );

    // 세대는 서버가 정한다 — 클라이언트가 보내면 끊는다.
    NetMessageWriter messageWriter;
    messageWriter.begin( NetMessageRange::kRouterWorldKind ).writeUint32( 77u );
    SW_REQUIRE_TRUE( messageWriter.send( pair.getClient( 0 ), 0, NetChannelKind::ReliableOrdered ) );
    SW_EXPECT_LOG( "Net.RouterWrongDirection" );
    pollAll();
    SW_EXPECT_EQUAL( 2u, serverRouter.getWorldGeneration() );
    SW_EXPECT_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) != NetConnectionState::Connected );
    clientRouter.closeSession();
    SW_EXPECT_EQUAL( 0u, clientRouter.getWorldGeneration() );
}

/**
 * @brief [NetMessageRouterTest] 표가 메시지 서술자의 방향에서 방향 마스크를 낸다 — 서버 쪽은 `ToServer` · `Both`, 클라이언트 쪽은 `ToClient` · `Both`
 */
SW_TEST_CASE( NetMessageRouterTest, TableBuildsDirectionMaskFromMessageDefs )
{
    using Up                                = NetMessageDef<0, NetMessageDirection::ToServer, NetChannelKind::ReliableOrdered>;
    using Down                              = NetMessageDef<1, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered>;
    using Both                              = NetMessageDef<2, NetMessageDirection::Both, NetChannelKind::ReliableOrdered>;
    using Table                             = NetMessageTable<NetMessageRange::kGame, Up, Down, Both>;
    constexpr NetMessageDirectionMask kMask = Table::makeDirectionMask<Up, Down, Both>();
    static_assert( kMask._toServerMask == 0x5u, "ToServer and Both travel to the server" );
    static_assert( kMask._toClientMask == 0x6u, "ToClient and Both travel to the client" );
    static_assert( kMask.getInboundMask( NetPeerRole::Server ) == 0x5u, "the server receives what travels to the server" );
    static_assert( Table::makeDirectionMask<Down>().getDeclaredMask() == 0x2u, "a subset declares only its own kinds" );
    SW_EXPECT_EQUAL( 0x5, static_cast<int32>( kMask._toServerMask ) );
}

/**
 * @brief [NetMessageRouterTest] 방향을 적지 않은 종류를 맡으려는 처리기는 등록을 받지 않는다(폴백 없음)
 */
SW_TEST_CASE( NetMessageRouterTest, HandlerWithoutDirectionIsRefused )
{
    class MissingDirectionHandler final : public INetMessageHandler
    {
    public:
        uint8                   getMessageRangeBase() const override { return NetMessageRange::kGame; }
        uint16                  getMessageKindMask() const override { return 0x3u; }
        NetMessageDirectionMask getMessageDirectionMask() const override { return NetMessageDirectionMask{ 0x1u, 0x0u }; } // 종류 + 1 의 방향이 없다
        uint32                  getWireVersion() const override { return kNoMessageTable; }
        NetHandleResult         handleNetMessage( const NetMessageContext& context, BitReader& body ) override
        {
            (void)context;
            (void)body;
            return NetHandleResult::Handled;
        }
    };

    MissingDirectionHandler handler;
    NetMessageRouter        router;
    SW_EXPECT_LOG( "Net.RouterKindWithoutDirection" );
    SW_EXPECT_FALSE( router.registerHandler( &handler ).isActive() );
}

/**
 * @brief [NetMessageRouterTest] 받는 쪽 역할의 반대 방향 종류는 처리기에 가지 않고 Malformed 로 세며, `poll` 은 보낸 연결을 끊는다
 * @details 클라이언트가 서버 → 클라이언트 종류(`ToClient`)를 서버에 보내면 서버 처리기가 그 본문을 믿고 읽던 틈이다(XMachinaServer 의 방향 표 자리).
 */
SW_TEST_CASE( NetMessageRouterTest, WrongDirectionMessageIsMalformedAndDisconnects )
{
    // 직접 전달 — 서버 역할은 ToClient 종류를, 클라이언트 역할은 ToServer 종류를 받지 않는다.
    {
        NetMessageRouterTestInternal::CountingHandler down{ NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind1, INetMessageHandler::kNoMessageTable, NetMessageDirection::ToClient };
        NetMessageRouterTestInternal::CountingHandler up{ NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind2, INetMessageHandler::kNoMessageTable, NetMessageDirection::ToServer };
        NetMessageRouter                              router;
        const sw::Subscription                        downSubscription = router.registerHandler( &down );
        const sw::Subscription                        upSubscription   = router.registerHandler( &up );
        SW_REQUIRE_TRUE( downSubscription.isActive() && upSubscription.isActive() );
        const uint8 arrDown[] = { NetMessageRouterTestInternal::kGameKind1, 0x01 };
        const uint8 arrUp[]   = { NetMessageRouterTestInternal::kGameKind2, 0x01 };
        SW_EXPECT_TRUE( NetHandleResult::Malformed == router.dispatch( NetPeerRole::Server, NetMessageContext{}, arrDown, static_cast<int32>( sizeof( arrDown ) ) ) );
        SW_EXPECT_TRUE( NetHandleResult::Malformed == router.dispatch( NetPeerRole::Client, NetMessageContext{}, arrUp, static_cast<int32>( sizeof( arrUp ) ) ) );
        SW_EXPECT_TRUE( NetHandleResult::Handled == router.dispatch( NetPeerRole::Client, NetMessageContext{}, arrDown, static_cast<int32>( sizeof( arrDown ) ) ) );
        SW_EXPECT_TRUE( NetHandleResult::Handled == router.dispatch( NetPeerRole::Server, NetMessageContext{}, arrUp, static_cast<int32>( sizeof( arrUp ) ) ) );
        SW_EXPECT_EQUAL( 1, down.getHandledCount() );
        SW_EXPECT_EQUAL( 1, up.getHandledCount() );
        SW_EXPECT_EQUAL( uint64{ 2 }, router.getMalformedCount().get() );
    }

    // 실제 연결 — 클라이언트가 ToClient 종류를 보내면 서버 라우터가 끊고, 같은 꺼내기의 뒤 메시지도 넘기지 않는다.
    test::LoopbackCluster pair( 79u );
    NetTestUtil::addServerAndClient( pair, NetEndpointConfig{} );
    SW_REQUIRE_TRUE( pair.getServer().listen() );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
    pair.run( 0.3 );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) == NetConnectionState::Connected );
    NetMessageRouterTestInternal::CountingHandler down{ NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind1, INetMessageHandler::kNoMessageTable, NetMessageDirection::ToClient };
    NetMessageRouterTestInternal::CountingHandler up{ NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind2, INetMessageHandler::kNoMessageTable, NetMessageDirection::ToServer };
    NetMessageRouter                              serverRouter;
    const sw::Subscription                        downSubscription = serverRouter.registerHandler( &down );
    const sw::Subscription                        upSubscription   = serverRouter.registerHandler( &up );
    NetMessageWriter                              messageWriter;
    messageWriter.begin( NetMessageRouterTestInternal::kGameKind1 ).writeVarUint( 1 );
    SW_REQUIRE_TRUE( messageWriter.send( pair.getClient( 0 ), 0, NetChannelKind::ReliableOrdered ) );
    messageWriter.begin( NetMessageRouterTestInternal::kGameKind2 ).writeVarUint( 2 );
    SW_REQUIRE_TRUE( messageWriter.send( pair.getClient( 0 ), 0, NetChannelKind::ReliableOrdered ) );
    pair.run( 0.2 );
    SW_EXPECT_LOG( "Net.RouterWrongDirection" );
    (void)serverRouter.poll( pair.getServer() );
    pair.run( 0.3 );
    SW_EXPECT_EQUAL( 0, down.getHandledCount() );
    SW_EXPECT_EQUAL( 0, up.getHandledCount() ); // 끊은 연결의 뒤 메시지다
    SW_EXPECT_EQUAL( uint64{ 1 }, serverRouter.getMalformedCount().get() );
    SW_EXPECT_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) != NetConnectionState::Connected );
}

namespace
{
    /** @brief 손 본문 메시지 하나를 받는 처리기 — 넘침을 보지 않고 Handled 를 돌려준다(빠뜨린 검사의 흉내). */
    struct NoOverflowCheckHandler
    {
        using Message = NetMessageDef<0, NetMessageDirection::Both, NetChannelKind::ReliableOrdered>;

        NetHandleResult receive( Message, const NetMessageContext& context, BitReader& body )
        {
            (void)context;
            (void)body.readVarUint();
            return NetHandleResult::Handled;
        }
    };
} // namespace

/**
 * @brief [NetMessageRouterTest] 손 본문 처리기가 넘침 검사를 빠뜨려도 표 받기가 깨진 메시지로 바꾼다
 */
SW_TEST_CASE( NetMessageRouterTest, DeliveryTurnsAnOverflowedHandBodyIntoMalformed )
{
    NoOverflowCheckHandler handler;
    const uint8            arrBody[] = { 0x05 };
    BitReader              whole( arrBody, 1 );
    BitReader              empty( arrBody, 0 );
    NetMessageContext      context;
    SW_EXPECT_TRUE( NetHandleResult::Handled == NetMessageDelivery::deliver<NoOverflowCheckHandler::Message>( handler, context, whole ) );
    SW_EXPECT_TRUE( NetHandleResult::Malformed == NetMessageDelivery::deliver<NoOverflowCheckHandler::Message>( handler, context, empty ) );
}

/**
 * @brief [NetMessageRouterTest] 손 본문을 서술자 채널 함수로, 서술자가 있는 메시지를 호출부 채널 함수로 보내려 하면 오류와 함께 보내지 않는다
 * @details 서술자가 있는 메시지의 채널은 서술자 하나에서만 나온다 — 스냅샷 확인이 서술자(비신뢰)와 다른 채널(순서 있는 비신뢰)로 나가던 자리.
 */
SW_TEST_CASE( NetMessageRouterTest, WriterSendsTypedMessagesOnlyOnTheirDescriptorChannel )
{
    test::LoopbackCluster pair( 80u );
    NetTestUtil::addServerAndClient( pair, NetEndpointConfig{} );
    SW_REQUIRE_TRUE( pair.getServer().listen() );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
    pair.run( 0.3 );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) == NetConnectionState::Connected );

    NetMessageWriter writer;
    writer.begin( NetMessageRouterTestInternal::kGameKind0 ).writeVarUint( 1 );
    SW_EXPECT_FALSE( writer.getMessageChannel().has_value() );
    SW_EXPECT_LOG( "Net.MessageNotTyped" );
    SW_EXPECT_FALSE( writer.sendMessage( pair.getClient( 0 ), 0 ) );
    SW_EXPECT_TRUE( writer.send( pair.getClient( 0 ), 0, NetChannelKind::Unreliable ) ); // 손 본문은 호출부가 고른다
    pair.run( 0.2 );
    int32          connectionID = -1;
    NetChannelKind channel      = NetChannelKind::ReliableOrdered;
    vector<uint8>  buffer;
    SW_REQUIRE_TRUE( pair.getServer().receiveMessage( connectionID, channel, buffer ) );
    SW_EXPECT_TRUE( channel == NetChannelKind::Unreliable );
}

/**
 * @brief [NetMessageRouterTest] 손으로 쓰는 표 메시지도 서술자 채널로 나간다 — 보내는 쪽이 고르는 서술자만 시작에서 채널을 고르고, 호출부 채널 함수는 거절한다
 */
SW_TEST_CASE( NetMessageRouterTest, HandWrittenTableMessagesTakeTheirChannelFromTheDescriptor )
{
    using Fixed         = NetMessageDef<0, NetMessageDirection::Both, NetChannelKind::ReliableOrdered>;
    using SenderChannel = NetMessageSenderChannelDef<1, NetMessageDirection::Both, NetChannelKind::Unreliable>;
    using Table         = NetMessageTable<NetMessageRange::kGame, Fixed, SenderChannel>;

    test::LoopbackCluster pair( 81u );
    NetTestUtil::addServerAndClient( pair, NetEndpointConfig{} );
    SW_REQUIRE_TRUE( pair.getServer().listen() );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
    pair.run( 0.3 );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) == NetConnectionState::Connected );

    NetMessageWriter writer;
    writer.beginMessage<Table, Fixed>().writeVarUint( 1 );
    SW_EXPECT_LOG( "Net.MessageChannelChosenByCaller" );
    SW_EXPECT_FALSE( writer.send( pair.getClient( 0 ), 0, NetChannelKind::Unreliable ) ); // 서술자와 다른 채널을 고를 수 없다
    SW_EXPECT_TRUE( writer.sendMessage( pair.getClient( 0 ), 0 ) );
    writer.beginMessage<Table, SenderChannel>( NetChannelKind::ReliableUnordered ).writeVarUint( 2 );
    SW_EXPECT_TRUE( writer.sendMessage( pair.getClient( 0 ), 0 ) );
    pair.run( 0.2 );

    int32          connectionID = -1;
    NetChannelKind channel      = NetChannelKind::Unreliable;
    vector<uint8>  buffer;
    int32          receivedCount = 0;
    while ( pair.getServer().receiveMessage( connectionID, channel, buffer ) )
    {
        ++receivedCount;
        if ( buffer[0] == Table::getKindOf<Fixed>() )
            SW_EXPECT_TRUE( channel == NetChannelKind::ReliableOrdered );
        else
            SW_EXPECT_TRUE( channel == NetChannelKind::ReliableUnordered );
    }
    SW_EXPECT_EQUAL( 2, receivedCount );
}
