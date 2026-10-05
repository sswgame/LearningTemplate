#include "pch.h"

#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Transport/NetEmulation.h"

#include "GameFramework/Kits/Network/NetSimulation/NetSimHarness.h"
#include "GameFramework/Kits/Network/NetTurnRelay/TurnRelay.h"

#include "TestFramework/TestFramework.h"

// 턴제 중계 키트(모두 하니스 위) — 셋이 방에 들어와 시작, 차례가 아닌 행동 거절, 정책(리버스 · 규칙 위반), 끊긴 자리 유지와 표로 돌아와 놓친 행동 받기,
// 모두 같은 기록, 신뢰 창보다 많은 놓친 행동, 살아 있는 자리 빼앗기 · 한 연결 여러 자리 거절, 서버마다 다른 표.

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

    /** @brief @p seat 에 앉은 클라이언트의 번호입니다. 없으면 -1 입니다. */
    int32 findClientAtSeat( const vector<TurnRelayClient>& listClient, int32 seat )
    {
        for ( size_t index = 0; index < listClient.size(); ++index )
        {
            if ( listClient[index].getSeat() == seat )
                return static_cast<int32>( index );
        }
        return -1;
    }

    /** @brief @p seat 의 클라이언트가 카드 한 장을 냅니다. */
    void playAtSeat( vector<TurnRelayClient>& listClient, int32 seat, uint8 card )
    {
        (void)listClient[static_cast<size_t>( findClientAtSeat( listClient, seat ) )].submitAction( vector<uint8>{ card } );
    }
} // namespace

SW_TEST_CASE( NetTurnRelayTest, RoomsEnforceTurnsAndResyncReturningPlayers )
{
    UnoLikePolicy       policy;
    TurnRelayNetSimGame game( 3, &policy, 0 );
    NetSimHarness       harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    // 손실 10 % — 핸드셰이크는 몇 번 되풀이될 수 있다.
    NetSimLinkConditions link;
    link._upstream._latency  = 0.04;
    link._upstream._lossRate = 0.1f;
    link._downstream         = link._upstream;
    vector<TurnRelayClient> listClient( 3 ); // 크기를 바꾸지 않는다 — 하니스 게임이 주소를 든다
    int32                   arrWorld[3] = {};
    for ( int32 index = 0; index < 3; ++index )
    {
        arrWorld[index] = addTurnClient( harness, game, listClient[static_cast<size_t>( index )], link );
    }
    SW_ASSERT_TRUE( harness.areAllClientsConnected() );
    for ( TurnRelayClient& client : listClient )
    {
        client.join( 7 );
    }
    harness.stepTicks( 60 );
    for ( const TurnRelayClient& client : listClient )
    {
        SW_EXPECT_TRUE( client.isStarted() );
        SW_EXPECT_TRUE( client.getSeat() >= 0 );
    }
    SW_ASSERT_TRUE( findClientAtSeat( listClient, 0 ) >= 0 && findClientAtSeat( listClient, 1 ) >= 0 && findClientAtSeat( listClient, 2 ) >= 0 );

    // 차례가 아니면 거절.
    const int32 seatOneClient = findClientAtSeat( listClient, 1 );
    const int32 rejectedId    = listClient[static_cast<size_t>( seatOneClient )].submitAction( vector<uint8>{ 'z' } );
    harness.stepTicks( 30 );
    vector<TurnRelayEvent> listEvent;
    listClient[static_cast<size_t>( seatOneClient )].drainEvents( listEvent );
    bool bRejected = false;
    for ( const TurnRelayEvent& event : listEvent )
    {
        bRejected = bRejected || ( event._kind == TurnRelayEvent::Kind::ActionRejected && event._index == rejectedId && event._reason == TurnRejectReason::NotYourTurn );
    }
    SW_EXPECT_TRUE( bRejected );

    // 0:a → 1:R(뒤집기) → 0:X(위반) → 0:b → 2:c
    playAtSeat( listClient, 0, 'a' );
    harness.stepTicks( 24 );
    playAtSeat( listClient, 1, 'R' );
    harness.stepTicks( 24 );
    playAtSeat( listClient, 0, 'X' );
    harness.stepTicks( 24 );
    playAtSeat( listClient, 0, 'b' );
    harness.stepTicks( 24 );
    SW_EXPECT_EQUAL( 2, game._server.findRoom( 7 )->_currentSeat );
    playAtSeat( listClient, 2, 'c' );
    harness.stepTicks( 24 );

    // 자리 2 가 떠난다(끊김 알림) — 그동안 1:d → 0:e. 자리는 남는다.
    const int32 seatTwoClient = findClientAtSeat( listClient, 2 );
    harness.removeClient( arrWorld[seatTwoClient] );
    harness.stepTicks( 30 );
    playAtSeat( listClient, 1, 'd' );
    harness.stepTicks( 24 );
    playAtSeat( listClient, 0, 'e' );
    harness.stepTicks( 24 );
    SW_EXPECT_TRUE( game._server.findRoom( 7 )->_listSeat[2]._connectionId < 0 );
    SW_EXPECT_EQUAL( 4, static_cast<int32>( listClient[static_cast<size_t>( seatTwoClient )].getActions().size() ) ); // a R b c 까지만

    // 같은 객체(표 · 받은 행동)로 돌아온다 — 표로 같은 자리, 놓친 d · e 를 받는다.
    const uint32 token = listClient[static_cast<size_t>( seatTwoClient )].getToken();
    SW_EXPECT_TRUE( token != 0 );
    arrWorld[seatTwoClient] = addTurnClient( harness, game, listClient[static_cast<size_t>( seatTwoClient )], link );
    SW_ASSERT_TRUE( arrWorld[seatTwoClient] > 0 && harness.areAllClientsConnected() );
    listClient[static_cast<size_t>( seatTwoClient )].join( 7 );
    harness.stepTicks( 60 );
    SW_EXPECT_EQUAL( 2, listClient[static_cast<size_t>( seatTwoClient )].getSeat() );
    playAtSeat( listClient, 2, 'f' );
    harness.stepTicks( 60 );

    const utf8* pExpected = "aRbcdef";
    for ( const TurnRelayClient& client : listClient )
    {
        SW_ASSERT_TRUE( client.getActions().size() == 7 );
        for ( size_t index = 0; index < 7; ++index )
        {
            SW_EXPECT_EQUAL( static_cast<int32>( pExpected[index] ), static_cast<int32>( client.getActions()[index]._buffer[0] ) );
        }
    }
    vector<TurnRelayEvent> listServerEvent;
    game._server.drainEvents( listServerEvent );
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
    TurnRelayClient fourth;
    (void)addTurnClient( harness, game, fourth, link );
    fourth.join( 7 );
    harness.stepTicks( 300 );
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
