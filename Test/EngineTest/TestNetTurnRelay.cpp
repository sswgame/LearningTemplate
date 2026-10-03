#include "pch.h"

#include "Core/Container/deque.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetTransport.h"

#include "GameFramework/Kits/Network/NetTurnRelay/TurnRelay.h"

#include "TestFramework/TestFramework.h"

// 턴제 중계 키트 — 셋이 방에 들어와 시작, 차례가 아닌 행동 거절, 정책(리버스 · 규칙 위반), 끊긴 자리 유지와 표로 돌아와 놓친 행동 받기, 모두 같은 기록.

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
            conditions._latency  = 0.04f;
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
                        _server.onDisconnected( event._connectionId );
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
                        (void)_listClient[index].handleMessage( buffer );
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
            (void)fourth.handleMessage( buffer );
    }
    listEvent.clear();
    fourth.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.empty() == false );
    SW_EXPECT_TRUE( listEvent.back()._kind == TurnRelayEvent::Kind::Denied && listEvent.back()._reason == TurnRejectReason::RoomFull );
}
