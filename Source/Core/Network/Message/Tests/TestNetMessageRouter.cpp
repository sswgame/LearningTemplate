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
            static constexpr uint32 kDefaultMaxPerPoll = 1024;

            CountingHandler( uint8 rangeBase, uint8 acceptedKind, uint32 wireVersion = kNoMessageTable, NetMessageDirection direction = NetMessageDirection::Both )
                : _wireVersion{ wireVersion }
                , _direction{ direction }
                , _maxPerPoll{ kDefaultMaxPerPoll }
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
            uint32          getMaxPerPoll( uint8 kind ) const override { return kind == _acceptedKind ? _maxPerPoll : 0u; }
            uint32          getWireVersion() const override { return _wireVersion; }
            void            setMaxPerPoll( uint32 maxPerPoll ) { _maxPerPoll = maxPerPoll; }
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
            uint32              _maxPerPoll;
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
    SW_EXPECT_TRUE( NetHandleResult::Handled == router.dispatch( NetPeerRole::Server, NetMessageContext{ nullptr, 0, 1 }, arrMessage, static_cast<int32>( sizeof( arrMessage ) ) ) );
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
        uint32                  getMaxPerPoll( uint8 ) const override { return 64u; }
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
    pair.getClient( 0 ).disconnect( 0, NetLinger::none() );
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
    serverRouter.openWorld( pair.getServer(), 0 ); // 아직 아무도 없다 — 붙을 때 받는다
    clientRouter.setLocalScene( pair.getClient( 0 ), 0 );
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
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( serverRouter.getWorldGeneration() ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( clientRouter.getWorldGeneration() ) );
    serverRouter.openWorld( pair.getServer(), 0 );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( clientRouter.getWorldGeneration() ) ); // 닿기 전이다
    pollAll();
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( clientRouter.getWorldGeneration() ) );

    // 세대는 서버가 정한다 — 클라이언트가 보내면 끊는다.
    NetMessageWriter messageWriter;
    messageWriter.begin( NetMessageRange::kRouterWorldKind ).writeBits( 77u, 8 );
    SW_REQUIRE_TRUE( messageWriter.send( pair.getClient( 0 ), 0, NetChannelKind::ReliableOrdered ) );
    SW_EXPECT_LOG( "Net.RouterWrongDirection" );
    pollAll();
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( serverRouter.getWorldGeneration() ) );
    SW_EXPECT_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) != NetConnectionState::Connected );
    clientRouter.closeSession();
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( clientRouter.getWorldGeneration() ) );
}

/**
 * @brief [NetMessageRouterTest] 표가 메시지 서술자의 방향에서 방향 마스크를 낸다 — 서버 쪽은 `ToServer` · `Both`, 클라이언트 쪽은 `ToClient` · `Both`
 */
SW_TEST_CASE( NetMessageRouterTest, TableBuildsDirectionMaskFromMessageDefs )
{
    using Up                                = NetLimitedMessageDef<NetMessageDef<0, NetMessageDirection::ToServer, NetChannelKind::ReliableOrdered>, 8>;
    using Down                              = NetMessageDef<1, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered>;
    using Both                              = NetLimitedMessageDef<NetMessageDef<2, NetMessageDirection::Both, NetChannelKind::ReliableOrdered>, 8>;
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
 * @brief [NetMessageRouterTest] 서버로 갈 수 있는 종류에 수신 상한이 없는 처리기는 등록을 받지 않고, 연결 번호가 없는 메시지는 처리기에 닿지 않는다
 */
SW_TEST_CASE( NetMessageRouterTest, ServerKindWithoutLimitIsRefused )
{
    NetMessageRouterTestInternal::CountingHandler up{ NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind1, INetMessageHandler::kNoMessageTable, NetMessageDirection::ToServer };
    NetMessageRouterTestInternal::CountingHandler down{ NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind2, INetMessageHandler::kNoMessageTable, NetMessageDirection::ToClient };
    up.setMaxPerPoll( 0 );
    down.setMaxPerPoll( 0 );
    NetMessageRouter router;
    SW_EXPECT_LOG( "Net.RouterKindWithoutLimit" );
    SW_EXPECT_FALSE( router.registerHandler( &up ).isActive() );
    const Subscription downSubscription = router.registerHandler( &down ); // 서버로 가지 않는 종류는 상한이 없다
    SW_EXPECT_TRUE( downSubscription.isActive() );
    const uint8 arrDown[] = { NetMessageRouterTestInternal::kGameKind2, 0x01 };
    SW_EXPECT_TRUE( NetHandleResult::Malformed == router.dispatch( NetPeerRole::Client, NetMessageContext{}, arrDown, static_cast<int32>( sizeof( arrDown ) ) ) );
    SW_EXPECT_TRUE( NetHandleResult::Malformed == down.handleMessage( -1, arrDown, static_cast<int32>( sizeof( arrDown ) ) ) );
    SW_EXPECT_EQUAL( 0, down.getHandledCount() );
}

/**
 * @brief [NetMessageRouterTest] 한 연결이 `poll` 한 번에 상한보다 많이 보내면 비신뢰는 남는 것을 버리고 세며, 신뢰는 연결을 끊는다. 다음 `poll` 은 처음부터 센다
 */
SW_TEST_CASE( NetMessageRouterTest, RequestsOverTheLimitAreDroppedOrDisconnect )
{
    constexpr uint32      kLimit = 3;
    test::LoopbackCluster pair( 83u );
    NetTestUtil::addServerAndClient( pair, NetEndpointConfig{} );
    SW_REQUIRE_TRUE( pair.getServer().listen() );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
    pair.run( 0.3 );
    SW_REQUIRE_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) == NetConnectionState::Connected );
    NetMessageRouterTestInternal::CountingHandler up{ NetMessageRange::kGame, NetMessageRouterTestInternal::kGameKind1, INetMessageHandler::kNoMessageTable, NetMessageDirection::ToServer };
    up.setMaxPerPoll( kLimit );
    NetMessageRouter   serverRouter;
    const Subscription subscription = serverRouter.registerHandler( &up );
    SW_REQUIRE_TRUE( subscription.isActive() );
    NetMessageWriter writer;
    const auto       sendMany = [&]( NetChannelKind channel )
    {
        for ( uint32 index = 0; index < kLimit + 2; ++index )
        {
            writer.begin( NetMessageRouterTestInternal::kGameKind1 ).writeVarUint( index );
            SW_EXPECT_TRUE( writer.send( pair.getClient( 0 ), 0, channel ) );
        }
        pair.run( 0.2 );
        (void)serverRouter.poll( pair.getServer() );
    };
    sendMany( NetChannelKind::Unreliable );
    SW_EXPECT_EQUAL( static_cast<int32>( kLimit ), up.getHandledCount() );
    SW_EXPECT_EQUAL( uint64{ 2 }, serverRouter.getOverLimitCount().get() );
    sendMany( NetChannelKind::Unreliable );
    SW_EXPECT_EQUAL( static_cast<int32>( kLimit ) * 2, up.getHandledCount() ); // 다음 `poll` 은 처음부터
    SW_EXPECT_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) == NetConnectionState::Connected );

    SW_EXPECT_LOG( "Net.RouterRequestLimit" );
    sendMany( NetChannelKind::ReliableOrdered );
    SW_EXPECT_EQUAL( static_cast<int32>( kLimit ) * 3, up.getHandledCount() );
    pair.run( 0.3 );
    SW_EXPECT_TRUE( pair.getClient( 0 ).getConnectionState( 0 ) != NetConnectionState::Connected );
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
        SW_EXPECT_TRUE( NetHandleResult::Malformed == router.dispatch( NetPeerRole::Server, NetMessageContext{ nullptr, 0, 1 }, arrDown, static_cast<int32>( sizeof( arrDown ) ) ) );
        SW_EXPECT_TRUE( NetHandleResult::Malformed == router.dispatch( NetPeerRole::Client, NetMessageContext{ nullptr, 0, 1 }, arrUp, static_cast<int32>( sizeof( arrUp ) ) ) );
        SW_EXPECT_TRUE( NetHandleResult::Handled == router.dispatch( NetPeerRole::Client, NetMessageContext{ nullptr, 0, 1 }, arrDown, static_cast<int32>( sizeof( arrDown ) ) ) );
        SW_EXPECT_TRUE( NetHandleResult::Handled == router.dispatch( NetPeerRole::Server, NetMessageContext{ nullptr, 0, 1 }, arrUp, static_cast<int32>( sizeof( arrUp ) ) ) );
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

namespace
{
    /** @brief 열쇠(첫 값)가 준비됐을 때만 처리하는 보류 처리기입니다. 처리한 (열쇠, 값)을 순서대로 적습니다. */
    class HoldingHandler final : public NetTableHandler<HoldingHandler, NetMessageTable<NetMessageRange::kGame, NetHeldMessageDef<NetLimitedMessageDef<NetMessageDef<0, NetMessageDirection::Both, NetChannelKind::ReliableOrdered>, 1024>, 1000>>,
                                                        NetHeldMessageDef<NetLimitedMessageDef<NetMessageDef<0, NetMessageDirection::Both, NetChannelKind::ReliableOrdered>, 1024>, 1000>>
    {
    public:
        using Message = NetHeldMessageDef<NetLimitedMessageDef<NetMessageDef<0, NetMessageDirection::Both, NetChannelKind::ReliableOrdered>, 1024>, 1000>;

        struct Entry
        {
            uint64 _key{ 0 };
            uint64 _number{ 0 };
        };

        vector<uint64> _listReadyKey{};
        vector<Entry>  _listHandled{};
        vector<uint64> _listFailedKey{};

        uint64 readHoldKey( Message, BitReader& body ) const { return body.readVarUint(); }

        NetHandleResult receive( Message, const NetMessageContext& context, BitReader& body )
        {
            (void)context;
            const uint64 key    = body.readVarUint();
            const uint64 number = body.readVarUint();
            for ( const uint64 ready : _listReadyKey )
            {
                if ( ready != key )
                    continue;
                _listHandled.push_back( Entry{ key, number } );
                return NetHandleResult::Handled;
            }
            return NetHandleResult::NotReady;
        }

        void onMessageDeliveryFailed( int32 connectionID, uint8 kind, uint64 key, NetDeliveryFailure reason ) override
        {
            (void)connectionID;
            (void)kind;
            (void)reason;
            _listFailedKey.push_back( key );
        }
    };

    /** @brief 테스트가 정하는 시계입니다 — 보류 시한이 실행 속도와 무관하다. */
    struct HoldTestClock
    {
        static int64& getNow()
        {
            static int64 s_now = 0; // NOLINT(sw-mutable-static): 시계 함수 포인터가 읽는 테스트 값이다
            return s_now;
        }
        static int64 readNow() { return getNow(); }
    };

    /** @brief 서버 · 클라이언트 라우터 짝입니다. */
    struct HoldTestRig
    {
        test::LoopbackCluster _pair{ 83u };
        NetMessageRouter      _serverRouter{};
        NetMessageRouter      _clientRouter{};

        HoldTestRig()
        {
            HoldTestClock::getNow() = 0;
            NetTestUtil::addServerAndClient( _pair, NetEndpointConfig{} );
            SW_REQUIRE_TRUE( _pair.getServer().listen() );
            SW_REQUIRE_TRUE( _pair.getClient( 0 ).connect( NetAddress::makeLoopback( 4000 ) ) );
            _serverRouter.setClock( &HoldTestClock::readNow );
            _clientRouter.setClock( &HoldTestClock::readNow );
            pollAll();
        }

        void pollAll()
        {
            for ( int32 round = 0; round < 12; ++round )
            {
                _pair.run( 0.05 );
                (void)_serverRouter.poll( _pair.getServer() );
                (void)_clientRouter.poll( _pair.getClient( 0 ) );
            }
        }

        void send( uint64 key, uint64 number )
        {
            NetMessageWriter writer;
            BitWriter&       body = writer.begin( NetMessageRange::kGame );
            body.writeVarUint( key );
            body.writeVarUint( number );
            SW_REQUIRE_TRUE( writer.send( _pair.getServer(), 0, NetChannelKind::ReliableOrdered ) );
        }
    };
} // namespace

/** @brief [NetMessageRouterTest] 대상보다 먼저 온 메시지는 보류됐다가 대상이 서면 순서대로 실행되고, 다른 대상의 메시지는 기다리지 않는다 */
SW_TEST_CASE( NetMessageRouterTest, HeldMessagesRunInOrderWhenTheirTargetIsReady )
{
    HoldTestRig            rig;
    HoldingHandler         server;
    HoldingHandler         client;
    const sw::Subscription serverSubscription = rig._serverRouter.registerHandler( &server );
    const sw::Subscription clientSubscription = rig._clientRouter.registerHandler( &client );
    client._listReadyKey.push_back( 2 );
    rig.send( 1, 10 );
    rig.send( 2, 20 );
    rig.send( 1, 11 );
    rig.pollAll();
    SW_REQUIRE_EQUAL( size_t{ 1 }, client._listHandled.size() ); // 대상 2 는 막히지 않는다
    SW_EXPECT_EQUAL( uint64{ 20 }, client._listHandled[0]._number );
    SW_EXPECT_EQUAL( 2, rig._clientRouter.getHeldMessageCount() );
    client._listReadyKey.push_back( 1 );
    rig.send( 1, 12 ); // 대상이 선 뒤에 온 것도 앞의 것을 앞지르지 않는다
    rig.pollAll();
    SW_REQUIRE_EQUAL( size_t{ 4 }, client._listHandled.size() );
    SW_EXPECT_EQUAL( uint64{ 10 }, client._listHandled[1]._number );
    SW_EXPECT_EQUAL( uint64{ 11 }, client._listHandled[2]._number );
    SW_EXPECT_EQUAL( uint64{ 12 }, client._listHandled[3]._number );
    SW_EXPECT_EQUAL( 0, rig._clientRouter.getHeldMessageCount() );
    SW_EXPECT_TRUE( server._listFailedKey.empty() );
}

/** @brief [NetMessageRouterTest] 선언한 시간을 넘긴 보류는 조용히 버리지 않고 보낸 쪽에 알린다. 줄은 상한에서 멈춘다 */
SW_TEST_CASE( NetMessageRouterTest, FailedHeldMessagesAreReportedToTheSender )
{
    HoldTestRig            rig;
    HoldingHandler         server;
    HoldingHandler         client;
    const sw::Subscription serverSubscription = rig._serverRouter.registerHandler( &server );
    const sw::Subscription clientSubscription = rig._clientRouter.registerHandler( &client );
    rig.send( 7, 1 );
    rig.pollAll();
    SW_EXPECT_EQUAL( 1, rig._clientRouter.getHeldMessageCount() );
    HoldTestClock::getNow() = 1001;
    SW_EXPECT_LOG( "Net.RouterDeliveryFailed" );
    rig.pollAll();
    SW_EXPECT_EQUAL( 0, rig._clientRouter.getHeldMessageCount() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, server._listFailedKey.size() );
    SW_EXPECT_EQUAL( uint64{ 7 }, server._listFailedKey[0] );

    for ( int32 index = 0; index < NetMessageRouter::kMaxHeldMessages; ++index )
    {
        rig.send( 9, static_cast<uint64>( index ) );
    }
    rig.pollAll();
    SW_EXPECT_EQUAL( NetMessageRouter::kMaxHeldMessages, rig._clientRouter.getHeldMessageCount() );
    SW_EXPECT_LOG( "Net.RouterDeliveryFailed" );
    rig.send( 9, 99 );
    rig.pollAll();
    SW_EXPECT_EQUAL( NetMessageRouter::kMaxHeldMessages, rig._clientRouter.getHeldMessageCount() );
    SW_EXPECT_EQUAL( size_t{ 2 }, server._listFailedKey.size() );
}

/** @brief [NetMessageRouterTest] 처리기를 달기 전(씬을 읽는 동안)에 온 신뢰 메시지는 처리기가 서면 온 순서대로 전해진다 */
SW_TEST_CASE( NetMessageRouterTest, ReliableMessagesWaitForTheirHandler )
{
    HoldTestRig rig;
    rig.send( 1, 10 );
    rig.send( 1, 11 );
    rig.pollAll();
    SW_EXPECT_EQUAL( 2, rig._clientRouter.getHeldMessageCount() );
    HoldingHandler client;
    client._listReadyKey.push_back( 1 );
    const sw::Subscription clientSubscription = rig._clientRouter.registerHandler( &client );
    rig.send( 1, 12 );
    rig.pollAll();
    SW_REQUIRE_EQUAL( size_t{ 3 }, client._listHandled.size() );
    SW_EXPECT_EQUAL( uint64{ 10 }, client._listHandled[0]._number );
    SW_EXPECT_EQUAL( uint64{ 12 }, client._listHandled[2]._number );
    SW_EXPECT_EQUAL( uint64{ 0 }, rig._clientRouter.getDeliveryFailedCount().get() );
}

namespace
{
    /** @brief 월드 범위 표를 받는 처리기입니다 — 받은 값을 순서대로 적습니다. */
    struct WorldTestMessage
    {
        using Order = NetLimitedMessageDef<NetMessageDef<0, NetMessageDirection::Both, NetChannelKind::ReliableOrdered>, 1024>;
        using Pose  = NetLimitedMessageDef<NetMessageDef<1, NetMessageDirection::Both, NetChannelKind::Unreliable>, 1024>;
        using Table = NetWorldMessageTable<NetMessageRange::kGame, Order, Pose>;
    };

    class WorldTestHandler final : public NetTableHandler<WorldTestHandler, WorldTestMessage::Table, WorldTestMessage::Order, WorldTestMessage::Pose>
    {
    public:
        vector<uint64> _listNumber{};
        vector<int32>  _listEntered{};
        vector<int32>  _listLeft{};
        uint32         _openedCount{ 0 };

        void            onPeerEnteredWorld( int32 connectionID ) override { _listEntered.push_back( connectionID ); }
        void            onPeerLeftWorld( int32 connectionID ) override { _listLeft.push_back( connectionID ); }
        void            onWorldOpened( uint32 ) override { ++_openedCount; }
        NetHandleResult receive( WorldTestMessage::Order, const NetMessageContext& context, BitReader& body )
        {
            (void)context;
            _listNumber.push_back( body.readVarUint() );
            return NetHandleResult::Handled;
        }
        NetHandleResult receive( WorldTestMessage::Pose, const NetMessageContext& context, BitReader& body )
        {
            return receive( WorldTestMessage::Order{}, context, body );
        }
    };

    struct WorldTestUtil
    {
        static constexpr uint64 kFirstScene  = 11;
        static constexpr uint64 kSecondScene = 22;

        /** @brief 같은 월드의 상대 모두에게 보내고 보낸 수를 돌려줍니다. */
        template <typename TMessage>
        static int32 send( INetPeers& peers, uint64 number )
        {
            NetMessageWriter writer;
            writer.beginMessage<WorldTestMessage::Table, TMessage>().writeVarUint( number );
            return writer.sendMessageToPeers( peers );
        }
    };
} // namespace

/**
 * @brief [NetMessageRouterTest] 월드 범위 메시지는 같은 씬을 세운 클라이언트에만 나간다 — 서버가 새 씬을 먼저 열면 클라이언트가 들어올 때까지 보내지 않고, 들어오면 처리기가 한 번 안다
 */
SW_TEST_CASE( NetMessageRouterTest, WorldMessagesReachOnlyTheirOwnScene )
{
    HoldTestRig            rig;
    WorldTestHandler       serverHandler;
    const sw::Subscription serverSubscription = rig._serverRouter.registerHandler( &serverHandler );
    rig._clientRouter.setLocalScene( rig._pair.getClient( 0 ), WorldTestUtil::kFirstScene );
    SW_EXPECT_EQUAL( 0, WorldTestUtil::send<WorldTestMessage::Order>( rig._pair.getClient( 0 ), 80 ) ); // 아직 서버의 월드를 모른다
    rig._serverRouter.openWorld( rig._pair.getServer(), WorldTestUtil::kFirstScene );
    SW_EXPECT_EQUAL( 0, WorldTestUtil::send<WorldTestMessage::Order>( rig._pair.getServer(), 70 ) ); // 아직 아무도 들어오지 않았다
    WorldTestHandler firstScene;
    sw::Subscription firstSubscription = rig._clientRouter.registerHandler( &firstScene );
    rig.pollAll();
    SW_EXPECT_EQUAL( size_t{ 1 }, serverHandler._listEntered.size() );
    SW_EXPECT_EQUAL( 1u, firstScene._openedCount );
    SW_EXPECT_EQUAL( 1, WorldTestUtil::send<WorldTestMessage::Order>( rig._pair.getServer(), 1 ) );
    rig.pollAll();
    SW_REQUIRE_EQUAL( size_t{ 1 }, firstScene._listNumber.size() );

    // 서버가 다음 씬을 연다 — 클라이언트는 아직 앞 씬이라 받지 않고, 앞 씬에서 이미 보낸 것은 새 씬에 닿지 않는다.
    SW_EXPECT_EQUAL( 1, WorldTestUtil::send<WorldTestMessage::Order>( rig._pair.getClient( 0 ), 90 ) );
    rig._serverRouter.openWorld( rig._pair.getServer(), WorldTestUtil::kSecondScene );
    SW_EXPECT_EQUAL( size_t{ 1 }, serverHandler._listLeft.size() );
    SW_EXPECT_EQUAL( 0, WorldTestUtil::send<WorldTestMessage::Order>( rig._pair.getServer(), 2 ) );
    rig.pollAll();
    SW_EXPECT_EQUAL( size_t{ 1 }, firstScene._listNumber.size() );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( rig._clientRouter.getWorldGeneration() ) );
    SW_EXPECT_TRUE( serverHandler._listNumber.empty() );
    SW_EXPECT_TRUE( rig._serverRouter.getOtherWorldCount().get() >= 1u );

    // 새 씬이 서면 들어온다.
    firstSubscription.reset();
    WorldTestHandler       secondScene;
    const sw::Subscription secondSubscription = rig._clientRouter.registerHandler( &secondScene );
    rig._clientRouter.setLocalScene( rig._pair.getClient( 0 ), WorldTestUtil::kSecondScene );
    SW_EXPECT_EQUAL( 1, WorldTestUtil::send<WorldTestMessage::Order>( rig._pair.getClient( 0 ), 91 ) );
    rig.pollAll();
    SW_EXPECT_EQUAL( size_t{ 2 }, serverHandler._listEntered.size() );
    SW_EXPECT_EQUAL( 1, WorldTestUtil::send<WorldTestMessage::Order>( rig._pair.getServer(), 4 ) );
    rig.pollAll();
    SW_REQUIRE_EQUAL( size_t{ 1 }, secondScene._listNumber.size() );
    SW_EXPECT_EQUAL( uint64{ 4 }, secondScene._listNumber[0] );
    SW_REQUIRE_EQUAL( size_t{ 1 }, serverHandler._listNumber.size() );
    SW_EXPECT_EQUAL( uint64{ 91 }, serverHandler._listNumber[0] );
}

/** @brief [NetMessageRouterTest] 늦게 단 서버 처리기도 이미 들어와 있는 연결을 한 번 알고, 같은 씬을 다시 세우거나(클라이언트) 다시 열면(서버) 다시 들어온 것이다 */
SW_TEST_CASE( NetMessageRouterTest, EnteredPeersReachLateHandlersAndReenterSameScene )
{
    HoldTestRig rig;
    rig._serverRouter.openWorld( rig._pair.getServer(), WorldTestUtil::kFirstScene );
    rig._clientRouter.setLocalScene( rig._pair.getClient( 0 ), WorldTestUtil::kFirstScene );
    rig.pollAll();
    WorldTestHandler       lateHandler; // 서버 씬의 처리기는 씬이 시작할 때 달린다 — 그 앞에 들어온 클라이언트가 있다
    const sw::Subscription lateSubscription = rig._serverRouter.registerHandler( &lateHandler );
    WorldTestHandler       clientHandler;
    const sw::Subscription clientSubscription = rig._clientRouter.registerHandler( &clientHandler );
    rig.pollAll();
    SW_EXPECT_EQUAL( size_t{ 1 }, lateHandler._listEntered.size() );

    rig._clientRouter.setLocalScene( rig._pair.getClient( 0 ), WorldTestUtil::kFirstScene ); // 이쪽만 같은 씬을 다시 세웠다
    rig.pollAll();
    SW_EXPECT_EQUAL( size_t{ 1 }, lateHandler._listLeft.size() );
    SW_EXPECT_EQUAL( size_t{ 2 }, lateHandler._listEntered.size() );

    rig._serverRouter.openWorld( rig._pair.getServer(), WorldTestUtil::kFirstScene ); // 서버가 같은 씬을 다시 열었다(재대결)
    rig.pollAll();
    SW_EXPECT_EQUAL( size_t{ 3 }, lateHandler._listEntered.size() );
    SW_EXPECT_EQUAL( 1u, clientHandler._openedCount ); // 앞 월드의 상태를 버리는 자리다

    rig._clientRouter.setLocalScene( rig._pair.getClient( 0 ), WorldTestUtil::kSecondScene ); // 다른 씬으로 나갔다
    rig.pollAll();
    SW_EXPECT_EQUAL( size_t{ 3 }, lateHandler._listLeft.size() );
    SW_EXPECT_EQUAL( 0, rig._pair.getServer().getInWorldCount() );
}

/** @brief [NetMessageRouterTest] 들고 남을 되풀이해 월드 상태를 거듭 받아 가는 연결은 끊고, 서버가 보낸 들고 남은 거절한다 */
SW_TEST_CASE( NetMessageRouterTest, PresenceSpamAndWrongDirectionDisconnect )
{
    {
        HoldTestRig rig;
        rig._serverRouter.openWorld( rig._pair.getServer(), WorldTestUtil::kFirstScene );
        rig.pollAll();
        SW_EXPECT_LOG( "Net.RouterMessageMalformed" );
        NetMessageWriter writer;
        writer.begin( NetMessageRange::kRouterPresenceKind ).writeBits( 1, 8 );
        for ( int32 index = 0; index <= NetMessageRouter::kMaxPresencePerWindow; ++index )
        {
            SW_REQUIRE_TRUE( writer.send( rig._pair.getClient( 0 ), 0, NetChannelKind::ReliableOrdered ) );
        }
        rig.pollAll();
        SW_EXPECT_EQUAL( 0, rig._pair.getServer().getConnectedCount() );
        SW_EXPECT_EQUAL( 0, rig._pair.getServer().getInWorldCount() );
    }
    {
        HoldTestRig rig;
        SW_EXPECT_LOG( "Net.RouterWrongDirection" );
        NetMessageWriter writer;
        writer.begin( NetMessageRange::kRouterPresenceKind ).writeBits( 1, 8 );
        SW_REQUIRE_TRUE( writer.send( rig._pair.getServer(), 0, NetChannelKind::ReliableOrdered ) );
        rig.pollAll();
        SW_EXPECT_TRUE( rig._pair.getClient( 0 ).getConnectionState( 0 ) != NetConnectionState::Connected );
    }
}

/** @brief [NetMessageRouterTest] 월드의 서버 시계는 같은 씬의 클라이언트에만 닿고, 새 월드에서는 처음부터다 — 앞 월드의 시각을 이어 보지 않는다 */
SW_TEST_CASE( NetMessageRouterTest, WorldClockReachesOnlyItsOwnScene )
{
    constexpr float64 kTolerance = static_cast<float64>( NetMessageRouter::kClockSendTicks );
    HoldTestRig       rig;
    rig._serverRouter.openWorld( rig._pair.getServer(), WorldTestUtil::kFirstScene );
    rig._clientRouter.setLocalScene( rig._pair.getClient( 0 ), WorldTestUtil::kFirstScene );
    rig.pollAll();
    SW_EXPECT_FALSE( rig._clientRouter.getWorldClock().hasServerTick() );
    for ( int32 frame = 0; frame < 30; ++frame )
    {
        rig._serverRouter.advanceWorldClock( rig._pair.getServer(), GameSeconds{ NetClock::kTickSeconds } );
    }
    rig.pollAll();
    SW_EXPECT_EQUAL( 30u, rig._serverRouter.getWorldClock().getWholeTick() );
    SW_EXPECT_NEAR( 30.0, rig._clientRouter.getWorldClock().getServerTick(), kTolerance );

    rig._serverRouter.openWorld( rig._pair.getServer(), WorldTestUtil::kSecondScene );
    SW_EXPECT_EQUAL( 0u, rig._serverRouter.getWorldClock().getWholeTick() );
    for ( int32 frame = 0; frame < 12; ++frame )
    {
        rig._serverRouter.advanceWorldClock( rig._pair.getServer(), GameSeconds{ NetClock::kTickSeconds } );
    }
    rig.pollAll();
    SW_EXPECT_FALSE( rig._clientRouter.getWorldClock().hasServerTick() ); // 아직 앞 씬이다

    rig._clientRouter.setLocalScene( rig._pair.getClient( 0 ), WorldTestUtil::kSecondScene );
    for ( int32 frame = 0; frame < 6; ++frame )
    {
        rig._serverRouter.advanceWorldClock( rig._pair.getServer(), GameSeconds{ NetClock::kTickSeconds } );
    }
    rig.pollAll();
    SW_EXPECT_NEAR( 18.0, rig._clientRouter.getWorldClock().getServerTick(), kTolerance );
}

/** @brief [NetMessageRouterTest] 결함: 씬을 아직 세우지 않은 클라이언트가 서버의 문서 없는 월드(씬 번호 0)에 들어온 것으로 셌다 — 세운 뒤에만 들어온다 */
SW_TEST_CASE( NetMessageRouterTest, ClientWithoutASceneStaysOutOfTheUndocumentedWorld )
{
    HoldTestRig            rig;
    WorldTestHandler       serverHandler;
    const sw::Subscription serverSubscription = rig._serverRouter.registerHandler( &serverHandler );
    rig._serverRouter.openWorld( rig._pair.getServer(), 0 );
    rig.pollAll();
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( rig._clientRouter.getWorldGeneration() ) );
    SW_EXPECT_TRUE( serverHandler._listEntered.empty() );
    rig._clientRouter.setLocalScene( rig._pair.getClient( 0 ), 0 );
    rig.pollAll();
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( rig._clientRouter.getWorldGeneration() ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, serverHandler._listEntered.size() );
}

/** @brief [NetMessageRouterTest] 세대는 255 에서 1 로 돌아도 앞뒤를 안다 */
SW_TEST_CASE( NetMessageRouterTest, WorldGenerationWrapsAround )
{
    HoldTestRig rig;
    for ( int32 index = 0; index < 255; ++index )
    {
        rig._serverRouter.openWorld( rig._pair.getServer(), 0 );
        if ( index % 32 == 31 )
            rig.pollAll(); // 알림은 신뢰 메시지다 — 상대가 확인할 틈이 없으면 신뢰 창이 차 연결이 닫힌다
    }
    SW_EXPECT_EQUAL( 255u, static_cast<uint32>( rig._serverRouter.getWorldGeneration() ) );
    SW_EXPECT_TRUE( rig._serverRouter.isLaterWorld( 1 ) );
    SW_EXPECT_FALSE( rig._serverRouter.isLaterWorld( 254 ) );
    rig._serverRouter.openWorld( rig._pair.getServer(), 0 );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( rig._serverRouter.getWorldGeneration() ) );
}
