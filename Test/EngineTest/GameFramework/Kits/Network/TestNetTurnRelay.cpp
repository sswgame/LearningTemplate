#include "pch.h"

#include "Core/Container/deque.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Transport/NetTransport.h"

#include "GameFramework/Kits/Network/NetSimulation/NetSimHarness.h"
#include "GameFramework/Kits/Network/NetTurnRelay/TurnRelay.h"

#include "TestFramework/TestFramework.h"

// 턴제 중계 키트 — 셋이 방에 들어와 시작, 차례가 아닌 행동 거절, 정책(리버스 · 규칙 위반), 끊긴 자리 유지와 표로 돌아와 놓친 행동 받기, 모두 같은 기록.
// 하니스 위 — 신뢰 창보다 많은 놓친 행동, 살아 있는 자리 빼앗기 · 한 연결 여러 자리 거절, 서버마다 다른 표.

using namespace sw;

namespace
{
    /** @brief 우노 흉내 — 'R' 은 방향을 뒤집고, 'X' 는 규칙 위반입니다. */
    class UnoLikePolicy final : public ITurnPolicy
    {
    public:
        bool isActionAllowed( const TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const override
        {
            return seat == room._currentSeat && ( actionBuffer.empty() || actionBuffer[0] != 'X' );
        }

        void applyAction( TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const override
        {
            if ( actionBuffer.empty() == false && actionBuffer[0] == 'R' )
                room._direction = -room._direction;
            ITurnPolicy::applyAction( room, seat, actionBuffer );
        }
    };

    struct TurnRelayScene
    {
        LoopbackNetwork         _network{ 3u };
        NetHost                 _serverHost;
        deque<NetHost>          _listClientHost; ///< deque — NetHost 는 옮길 수 없다
        TurnRelayServer         _server;
        vector<TurnRelayClient> _listClient;
        vector<uint8>           _listOnline;
        UnoLikePolicy           _policy;
        float64                 _time{ 0.0 };

        TurnRelayScene()
        {
            LoopbackConditions conditions;
            conditions._latency  = 0.04;
            conditions._lossRate = 0.1f;
            _network.setConditions( conditions );
            _serverHost.initialize( _network.createEndpoint( 4000 ), NetHostSettings{} );
            (void)_serverHost.listen();
            _server.initialize( &_serverHost, 3, &_policy );
            for ( int32 index = 0; index < 3; ++index )
                _listClientHost.emplace_back();
            _listClient.resize( 3 );
            _listOnline.assign( 3, SW_TRUE );
            for ( size_t index = 0; index < 3; ++index )
            {
                _listClientHost[index].initialize( _network.createEndpoint( static_cast<uint16>( 5000 + index ) ), NetHostSettings{} );
                (void)_listClientHost[index].connect( NetAddress::makeLoopback( 4000 ) );
                _listClient[index].initialize( &_listClientHost[index] );
            }
        }

        void run( float64 seconds )
        {
            vector<uint8> buffer;
            for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 60.0 )
            {
                _time += 1.0 / 60.0;
                _serverHost.update( _time );
                vector<NetHostEvent> listHostEvent;
                _serverHost.drainEvents( listHostEvent );
                for ( const NetHostEvent& event : listHostEvent )
                {
                    if ( event._kind == NetHostEvent::Kind::Disconnected )
                        _server.onConnectionClosed( event._connectionId, event._reason );
                }
                int32          connectionId = -1;
                NetChannelType channel      = NetChannelType::Unreliable;
                while ( _serverHost.receiveMessage( connectionId, channel, buffer ) )
                    (void)_server.handleMessage( connectionId, buffer );
                for ( size_t index = 0; index < 3; ++index )
                {
                    if ( _listOnline[index] == SW_FALSE )
                        continue;
                    _listClientHost[index].update( _time );
                    while ( _listClientHost[index].receiveMessage( connectionId, channel, buffer ) )
                        (void)_listClient[index].handleMessage( 0, buffer );
                }
            }
        }

        int32 findClientAtSeat( int32 seat ) const
        {
            for ( size_t index = 0; index < _listClient.size(); ++index )
            {
                if ( _listClient[index].getSeat() == seat )
                    return static_cast<int32>( index );
            }
            return -1;
        }

        bool isAllConnected() const
        {
            for ( const NetHost& host : _listClientHost )
            {
                if ( host.getConnectionState( 0 ) != NetConnectionState::Connected )
                    return false;
            }
            return true;
        }

        void play( int32 seat, uint8 card ) { (void)_listClient[static_cast<size_t>( findClientAtSeat( seat ) )].submitAction( vector<uint8>{ card } ); }
    };
    /** @brief 누구나 언제든 — 차례를 바꾸지 않습니다(보낼 줄 시험에서 한 사람이 행동을 몰아 낸다). */
    class AnyoneMayActPolicy final : public ITurnPolicy
    {
    public:
        bool isActionAllowed( const TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const override
        {
            (void)room;
            (void)seat;
            (void)actionBuffer;
            return true;
        }

        void applyAction( TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const override
        {
            (void)room;
            (void)seat;
            (void)actionBuffer;
        }
    };

    /** @brief 하니스 위 턴 중계 — 서버 월드는 게임의 `TurnRelayServer` 를 걸고 틱마다 `update` 하고, 클라이언트 월드는 시험이 가진 `TurnRelayClient` 를 그 월드 호스트에 붙인다. */
    class TurnRelayNetSimSession final : public INetSimSession
    {
    public:
        TurnRelayNetSimSession( NetSimWorld& world, TurnRelayServer* pServer, TurnRelayClient* pClient )
            : _pServer{ pServer }
        {
            if ( pServer != nullptr )
                world.getRouter().addHandler( pServer );
            if ( pClient != nullptr )
            {
                pClient->initialize( &world.getHost() );
                world.getRouter().addHandler( pClient );
            }
        }

        void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)world;
            (void)deltaTime;
            if ( _pServer != nullptr )
                _pServer->update();
        }

    private:
        TurnRelayServer* _pServer;
    };

    /** @brief 서버를 갖고, 다음에 들어오는 클라이언트 월드에 `_pNextClient` 를 붙입니다(떠났다 돌아온 클라이언트는 같은 객체 — 표 · 받은 행동이 남는다). */
    class TurnRelayNetSimGame final : public INetSimGame
    {
    public:
        TurnRelayNetSimGame( int32 seatCount, const ITurnPolicy* pPolicy, uint64 tokenSeed )
            : _server{}
            , _pPolicy{ pPolicy }
            , _pNextClient{ nullptr }
            , _tokenSeed{ tokenSeed }
            , _seatCount{ seatCount }
        {
        }

        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override
        {
            if ( world.isServer() )
            {
                _server.initialize( &world.getHost(), _seatCount, _pPolicy, _tokenSeed );
                return make_unique<TurnRelayNetSimSession>( world, &_server, nullptr );
            }
            return make_unique<TurnRelayNetSimSession>( world, nullptr, _pNextClient );
        }

        TurnRelayServer    _server;
        const ITurnPolicy* _pPolicy;
        TurnRelayClient*   _pNextClient;

    private:
        uint64 _tokenSeed;
        int32  _seatCount;
    };

    bool areTurnClientsConnected( const NetSimHarness& harness, void* pContext )
    {
        (void)pContext;
        return harness.areAllClientsConnected();
    }

    /** @brief 클라이언트 월드를 더하고 연결될 때까지 돕니다. 월드 번호입니다. */
    int32 addTurnClient( NetSimHarness& harness, TurnRelayNetSimGame& game, TurnRelayClient& client, const NetSimLinkConditions& link )
    {
        game._pNextClient = &client;
        const int32 world = harness.addClient( link );
        (void)harness.stepUntil( &areTurnClientsConnected, nullptr, 300 );
        return world;
    }

    bool hasDeniedEvent( TurnRelayClient& client, TurnRejectReason reason )
    {
        vector<TurnRelayEvent> listEvent;
        client.drainEvents( listEvent );
        bool bFound = false;
        for ( const TurnRelayEvent& event : listEvent )
            bFound = bFound || ( event._kind == TurnRelayEvent::Kind::Denied && event._reason == reason );
        return bFound;
    }

    int32 countTakenSeats( const TurnRoom& room )
    {
        int32 count = 0;
        for ( const TurnSeat& seat : room._listSeat )
            count += seat._bTaken != SW_FALSE ? 1 : 0;
        return count;
    }
} // namespace

SW_TEST_CASE( NetTurnRelayTest, RoomsEnforceTurnsAndResyncReturningPlayers )
{
    TurnRelayScene scene;
    // 손실 10 % — 핸드셰이크는 몇 번 되풀이될 수 있다.
    for ( int32 step = 0; step < 300 && scene.isAllConnected() == false; ++step )
        scene.run( 1.0 / 60.0 );
    SW_ASSERT_TRUE( scene.isAllConnected() );
    for ( TurnRelayClient& client : scene._listClient )
        client.join( 7 );
    scene.run( 1.0 );
    for ( const TurnRelayClient& client : scene._listClient )
    {
        SW_EXPECT_TRUE( client.isStarted() );
        SW_EXPECT_TRUE( client.getSeat() >= 0 );
    }
    SW_ASSERT_TRUE( scene.findClientAtSeat( 0 ) >= 0 && scene.findClientAtSeat( 1 ) >= 0 && scene.findClientAtSeat( 2 ) >= 0 );

    // 차례가 아니면 거절.
    const int32 seatOneClient = scene.findClientAtSeat( 1 );
    const int32 rejectedId    = scene._listClient[static_cast<size_t>( seatOneClient )].submitAction( vector<uint8>{ 'z' } );
    scene.run( 0.5 );
    vector<TurnRelayEvent> listEvent;
    scene._listClient[static_cast<size_t>( seatOneClient )].drainEvents( listEvent );
    bool bRejected = false;
    for ( const TurnRelayEvent& event : listEvent )
        bRejected = bRejected || ( event._kind == TurnRelayEvent::Kind::ActionRejected && event._index == rejectedId && event._reason == TurnRejectReason::NotYourTurn );
    SW_EXPECT_TRUE( bRejected );

    // 0:a → 1:R(뒤집기) → 0:X(위반) → 0:b → 2:c
    scene.play( 0, 'a' );
    scene.run( 0.4 );
    scene.play( 1, 'R' );
    scene.run( 0.4 );
    scene.play( 0, 'X' );
    scene.run( 0.4 );
    scene.play( 0, 'b' );
    scene.run( 0.4 );
    SW_EXPECT_EQUAL( 2, scene._server.findRoom( 7 )->_currentSeat );
    scene.play( 2, 'c' );
    scene.run( 0.4 );

    // 자리 2 가 끊긴다 — 그동안 1:d → 0:e. 자리는 남는다.
    const int32 seatTwoClient = scene.findClientAtSeat( 2 );
    scene._listClientHost[static_cast<size_t>( seatTwoClient )].disconnect( 0 );
    scene._listOnline[static_cast<size_t>( seatTwoClient )] = SW_FALSE;
    scene.run( 0.5 );
    scene.play( 1, 'd' );
    scene.run( 0.4 );
    scene.play( 0, 'e' );
    scene.run( 0.4 );
    SW_EXPECT_TRUE( scene._server.findRoom( 7 )->_listSeat[2]._connectionId < 0 );
    SW_EXPECT_EQUAL( 4, static_cast<int32>( scene._listClient[static_cast<size_t>( seatTwoClient )].getActions().size() ) ); // a R b c 까지만

    // 돌아온다 — 표로 같은 자리, 놓친 d · e 를 받는다.
    const uint32 token = scene._listClient[static_cast<size_t>( seatTwoClient )].getToken();
    SW_EXPECT_TRUE( token != 0 );
    scene._listOnline[static_cast<size_t>( seatTwoClient )] = SW_TRUE;
    SW_ASSERT_TRUE( scene._listClientHost[static_cast<size_t>( seatTwoClient )].connect( NetAddress::makeLoopback( 4000 ) ) );
    for ( int32 step = 0; step < 300 && scene.isAllConnected() == false; ++step )
        scene.run( 1.0 / 60.0 );
    scene._listClient[static_cast<size_t>( seatTwoClient )].join( 7 );
    scene.run( 1.0 );
    SW_EXPECT_EQUAL( 2, scene._listClient[static_cast<size_t>( seatTwoClient )].getSeat() );
    scene.play( 2, 'f' );
    scene.run( 1.0 );

    const utf8* pExpected = "aRbcdef";
    for ( const TurnRelayClient& client : scene._listClient )
    {
        SW_ASSERT_TRUE( client.getActions().size() == 7 );
        for ( size_t index = 0; index < 7; ++index )
            SW_EXPECT_EQUAL( static_cast<int32>( pExpected[index] ), static_cast<int32>( client.getActions()[index]._buffer[0] ) );
    }
    vector<TurnRelayEvent> listServerEvent;
    scene._server.drainEvents( listServerEvent );
    int32 leftCount     = 0;
    int32 returnedCount = 0;
    for ( const TurnRelayEvent& event : listServerEvent )
    {
        leftCount += event._kind == TurnRelayEvent::Kind::SeatLeft ? 1 : 0;
        returnedCount += event._kind == TurnRelayEvent::Kind::SeatReturned ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 1, leftCount );
    SW_EXPECT_EQUAL( 1, returnedCount );

    // 가득 찬 방 — 넷째는 거절.
    NetHost         fourthHost;
    TurnRelayClient fourth;
    fourthHost.initialize( scene._network.createEndpoint( 6000 ), NetHostSettings{} );
    (void)fourthHost.connect( NetAddress::makeLoopback( 4000 ) );
    fourth.initialize( &fourthHost );
    bool bJoinSent = false;
    for ( int32 frame = 0; frame < 300; ++frame )
    {
        scene.run( 1.0 / 60.0 );
        fourthHost.update( scene._time );
        if ( bJoinSent == false && fourthHost.getConnectionState( 0 ) == NetConnectionState::Connected )
        {
            fourth.join( 7 );
            bJoinSent = true;
        }
        int32          connectionId = -1;
        NetChannelType channel      = NetChannelType::Unreliable;
        vector<uint8>  buffer;
        while ( fourthHost.receiveMessage( connectionId, channel, buffer ) )
            (void)fourth.handleMessage( 0, buffer );
    }
    listEvent.clear();
    fourth.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.empty() == false );
    SW_EXPECT_TRUE( listEvent.back()._kind == TurnRelayEvent::Kind::Denied && listEvent.back()._reason == TurnRejectReason::RoomFull );
}

/**
 * @brief [NetTurnRelayTest] 놓친 행동이 신뢰 창(255)보다 많아도 돌아온 사람이 모두 받는다 — 서버가 자리마다 보낸 수를 들고 창이 비는 대로 이어 보낸다
 */
SW_TEST_CASE( NetTurnRelayTest, RejoinCatchesUpBeyondReliableWindow )
{
    AnyoneMayActPolicy  policy;
    TurnRelayNetSimGame game( 2, &policy, 0 );
    NetSimHarness       harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    NetSimLinkConditions link;
    link._upstream._latency  = 0.03;
    link._upstream._lossRate = 0.05f;
    link._downstream         = link._upstream;
    TurnRelayClient clientA;
    TurnRelayClient clientB;
    (void)addTurnClient( harness, game, clientA, link );
    const int32 worldB = addTurnClient( harness, game, clientB, link );
    clientA.join( 7 );
    clientB.join( 7 );
    harness.stepTicks( 60 );
    SW_ASSERT_TRUE( clientA.isStarted() && clientB.isStarted() );
    const int32 seatB = clientB.getSeat();

    // B 가 떠난 동안 A 가 300 개를 낸다.
    harness.removeClient( worldB );
    harness.stepTicks( 10 );
    constexpr int32 kActionCount = 300;
    for ( int32 index = 0; index < kActionCount; ++index )
    {
        (void)clientA.submitAction( vector<uint8>{ static_cast<uint8>( index & 0xFF ), static_cast<uint8>( index >> 8 ) } );
        if ( index % 5 == 4 )
            harness.stepTicks( 1 );
    }
    harness.stepTicks( 120 );
    SW_ASSERT_EQUAL( size_t{ kActionCount }, clientA.getActions().size() );
    SW_EXPECT_TRUE( clientB.getActions().empty() );

    // 같은 객체(표 · 받은 수)로 돌아온다 — 300 개를 모두 받는다.
    (void)addTurnClient( harness, game, clientB, link );
    clientB.join( 7 );
    harness.stepTicks( 240 );
    SW_EXPECT_EQUAL( seatB, clientB.getSeat() );
    SW_ASSERT_EQUAL( size_t{ kActionCount }, clientB.getActions().size() );
    int32 mismatchCount = 0;
    for ( size_t index = 0; index < clientB.getActions().size(); ++index )
        mismatchCount += clientB.getActions()[index]._buffer == clientA.getActions()[index]._buffer ? 0 : 1;
    SW_EXPECT_EQUAL( 0, mismatchCount );
}

/**
 * @brief [NetTurnRelayTest] 표가 맞아도 그 자리 연결이 살아 있으면 다른 연결이 빼앗지 못한다(`SeatInUse`)
 */
SW_TEST_CASE( NetTurnRelayTest, LiveSeatCannotBeReclaimed )
{
    TurnRelayNetSimGame game( 2, nullptr, 0 );
    NetSimHarness       harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    TurnRelayClient clientA;
    TurnRelayClient clientB;
    TurnRelayClient intruder;
    (void)addTurnClient( harness, game, clientA, NetSimLinkConditions{} );
    (void)addTurnClient( harness, game, clientB, NetSimLinkConditions{} );
    clientA.join( 7 );
    clientB.join( 7 );
    harness.stepTicks( 30 );
    SW_ASSERT_TRUE( clientA.isStarted() && clientA.getToken() != 0 );
    const int32 seatA       = clientA.getSeat();
    const int32 connectionA = game._server.findRoom( 7 )->_listSeat[static_cast<size_t>( seatA )]._connectionId;

    // 침입자 — 가득 찬 방이라 거절되고, A 의 표를 그대로 내도 A 가 살아 있으니 거절된다.
    const int32 intruderWorld = addTurnClient( harness, game, intruder, NetSimLinkConditions{} );
    intruder.join( 7 );
    harness.stepTicks( 10 );
    SW_EXPECT_TRUE( hasDeniedEvent( intruder, TurnRejectReason::RoomFull ) );
    NetMessageWriter writer;
    BitWriter&       body = writer.begin( NetTurnRelayMessage::kJoin );
    body.writeVarUint( 7 );
    body.writeVarInt( -1 );
    body.writeUint32( clientA.getToken() );
    body.writeVarUint( 0 );
    SW_ASSERT_TRUE( writer.send( harness.findClient( intruderWorld )->getHost(), 0, NetChannelType::ReliableOrdered ) );
    harness.stepTicks( 10 );
    SW_EXPECT_TRUE( hasDeniedEvent( intruder, TurnRejectReason::SeatInUse ) );
    SW_EXPECT_EQUAL( -1, intruder.getSeat() );
    SW_EXPECT_EQUAL( connectionA, game._server.findRoom( 7 )->_listSeat[static_cast<size_t>( seatA )]._connectionId );
}

/**
 * @brief [NetTurnRelayTest] 한 연결은 자리 하나 — 같은 방에 두 번 들어와도 같은 자리, 다른 방은 `AlreadySeated` 로 거절, 방은 다른 연결이 와야 시작한다
 */
SW_TEST_CASE( NetTurnRelayTest, OneConnectionTakesOneSeat )
{
    TurnRelayNetSimGame game( 2, nullptr, 0 );
    NetSimHarness       harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    TurnRelayClient clientA;
    TurnRelayClient clientB;
    (void)addTurnClient( harness, game, clientA, NetSimLinkConditions{} );
    clientA.join( 7 );
    clientA.join( 7 );
    harness.stepTicks( 10 );
    SW_ASSERT_NOT_NULL( game._server.findRoom( 7 ) );
    SW_EXPECT_EQUAL( 1, countTakenSeats( *game._server.findRoom( 7 ) ) );
    SW_EXPECT_FALSE( clientA.isStarted() );
    const int32  seatA  = clientA.getSeat();
    const uint32 tokenA = clientA.getToken();

    clientA.join( 9 );
    harness.stepTicks( 10 );
    SW_EXPECT_TRUE( hasDeniedEvent( clientA, TurnRejectReason::AlreadySeated ) );
    SW_EXPECT_NULL( game._server.findRoom( 9 ) );
    // 원래 방으로 — 같은 자리와 표를 다시 받는다.
    clientA.join( 7 );
    harness.stepTicks( 10 );
    SW_EXPECT_EQUAL( seatA, clientA.getSeat() );
    SW_EXPECT_EQUAL( tokenA, clientA.getToken() );

    (void)addTurnClient( harness, game, clientB, NetSimLinkConditions{} );
    clientB.join( 7 );
    harness.stepTicks( 10 );
    SW_EXPECT_EQUAL( 2, countTakenSeats( *game._server.findRoom( 7 ) ) );
    SW_EXPECT_TRUE( clientA.isStarted() && clientB.isStarted() );
    SW_EXPECT_TRUE( clientA.getSeat() != clientB.getSeat() );
}

/**
 * @brief [NetTurnRelayTest] 자리 표는 서버마다 다르다 — 씨앗을 주지 않으면 운영체제 난수 비밀에서 섞는다(고정 씨앗이면 첫 표를 누구나 셈한다)
 */
SW_TEST_CASE( NetTurnRelayTest, SeatTokensDifferBetweenServers )
{
    uint32 arrToken[2] = {};
    for ( int32 run = 0; run < 2; ++run )
    {
        TurnRelayNetSimGame game( 1, nullptr, 0 );
        NetSimHarness       harness;
        SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
        TurnRelayClient client;
        (void)addTurnClient( harness, game, client, NetSimLinkConditions{} );
        client.join( 7 );
        harness.stepTicks( 10 );
        arrToken[run] = client.getToken();
        SW_EXPECT_TRUE( arrToken[run] != 0 );
    }
    SW_EXPECT_TRUE( arrToken[0] != arrToken[1] );
}

/**
 * @brief [NetTurnRelayTest] 상한을 넘는 행동은 보내지 않고 −1 — 서버가 깨짐으로 버려 조용히 사라지지 않게
 */
SW_TEST_CASE( NetTurnRelayTest, OversizeActionIsRefusedBeforeSending )
{
    TurnRelayClient client;
    SW_TEST_DEFENSIVE_SCOPE( "an action over the relay limit is refused with an error" );
    SW_EXPECT_EQUAL( -1, client.submitAction( vector<uint8>( static_cast<size_t>( NetTurnRelayMessage::kMaxActionBytes ) + 1, 0x11 ) ) );
}
