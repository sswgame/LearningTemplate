#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Network/NetTurnRelay/TurnRelay.h"

using namespace sw;

namespace
{
    /** @brief 보낸 메시지를 쌓기만 하는 상대입니다. 창이 차면 `_bWindowFull` 로 거절합니다. */
    class FakeTurnPeers final : public INetPeers
    {
    public:
        struct Sent
        {
            vector<uint8> _bytes{};
            int32         _connectionID{ -1 };
        };

        vector<Sent>  _listSent{};
        vector<int32> _listDisconnected{};
        int32         _sendBudget{ -1 }; ///< 남은 보내기 수. 음수면 무한이다
        uint8         _bWindowFull{ SW_FALSE };

        bool sendMessage( int32 connectionID, NetChannelKind channel, const uint8* pData, int32 size ) override { return trySendMessage( connectionID, channel, pData, size ); }
        bool trySendMessage( int32 connectionID, NetChannelKind channel, const uint8* pData, int32 size ) override
        {
            (void)channel;
            if ( _bWindowFull != SW_FALSE || _sendBudget == 0 )
                return false;
            if ( _sendBudget > 0 )
                --_sendBudget;
            _listSent.push_back( Sent{ vector<uint8>( pData, pData + size ), connectionID } );
            return true;
        }
        bool               sendNotifiedMessage( int32, const uint8*, int32, uint32 ) override { return false; }
        int32              broadcast( NetChannelKind, const uint8*, int32, int32 ) override { return 0; }
        void               disconnect( int32 connectionID ) override { _listDisconnected.push_back( connectionID ); }
        void               pollInbound( NetInbound& ) override {}
        bool               isServer() const override { return true; }
        NetConnectionState getConnectionState( int32 ) const override { return NetConnectionState::Connected; }
        bool               getConnectionStats( int32, NetConnectionStats& ) const override { return false; }
        NetAddress         getConnectionAddress( int32 ) const override { return NetAddress{}; }
        uint64             getConnectionPrincipal( int32 ) const override { return 0; }
        optional<int32>    getClientIndex() const override { return nullopt; }
        int32              getConnectedCount() const override { return 0; }
        void               collectConnected( vector<int32>& ) const override {}
        bool               isConnectionSaturated( int32 ) const override { return false; }
        void               addSentKindStats( NetMessageKindStatsTable& ) const override {}
    };

    /** @brief 서버 하나와 클라이언트 여럿을 가짜 상대로 잇습니다. 연결 번호는 클라이언트 순번입니다. */
    struct TurnRelayTest
    {
        static constexpr uint64 kTokenSeed = 1234;

        struct Client
        {
            FakeTurnPeers   _peers;
            TurnRelayClient _relay;
        };

        FakeTurnPeers              _serverPeers;
        TurnRelayServer            _server;
        vector<unique_ptr<Client>> _listClient;

        TurnRelayTest()
            : _serverPeers{}
            , _server{}
            , _listClient{}
        {
            TurnRelayConfig config;
            config._seatCount = 2;
            SW_REQUIRE_TRUE( _server.initialize( &_serverPeers, config, nullptr, kTokenSeed ) );
        }

        Client& addClient()
        {
            _listClient.push_back( make_unique<Client>() );
            _listClient.back()->_relay.initialize( &_listClient.back()->_peers );
            return *_listClient.back();
        }

        /** @brief 쌓인 메시지를 오갈 것이 없을 때까지 나릅니다. */
        void poll()
        {
            bool bMoved = true;
            while ( bMoved )
            {
                bMoved = false;
                for ( int32 index = 0; index < static_cast<int32>( _listClient.size() ); ++index )
                {
                    vector<FakeTurnPeers::Sent> listSent = std::move( _listClient[static_cast<size_t>( index )]->_peers._listSent );
                    _listClient[static_cast<size_t>( index )]->_peers._listSent.clear();
                    for ( const FakeTurnPeers::Sent& sent : listSent )
                    {
                        SW_EXPECT_TRUE( _server.handleMessage( index, sent._bytes.data(), static_cast<int32>( sent._bytes.size() ), NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
                        bMoved = true;
                    }
                }
                vector<FakeTurnPeers::Sent> listOut = std::move( _serverPeers._listSent );
                _serverPeers._listSent.clear();
                for ( const FakeTurnPeers::Sent& sent : listOut )
                {
                    SW_EXPECT_TRUE( _listClient[static_cast<size_t>( sent._connectionID )]->_relay.handleMessage( 0, sent._bytes.data(), static_cast<int32>( sent._bytes.size() ), NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
                    bMoved = true;
                }
            }
        }

        /** @brief 두 클라이언트가 방 @p roomID 에 들어와 시작합니다. */
        void startRoom( uint32 roomID = 7 )
        {
            addClient()._relay.join( roomID );
            addClient()._relay.join( roomID );
            poll();
        }

        [[nodiscard]] TurnRelayClient& relay( int32 index ) { return _listClient[static_cast<size_t>( index )]->_relay; }

        [[nodiscard]] static vector<TurnRelayEvent> pollAll( TurnRelayClient& client )
        {
            vector<TurnRelayEvent> listEvent;
            client.pollEvents( listEvent );
            return listEvent;
        }

        [[nodiscard]] vector<TurnRelayEvent> drainServer()
        {
            vector<TurnRelayEvent> listEvent;
            _server.pollEvents( listEvent );
            return listEvent;
        }

        /** @brief 클라이언트 @p index 의 연결이 끊긴 뒤 새 연결로 돌아오는 것을 흉내 냅니다(토큰과 받은 행동은 그대로). */
        void reconnect( int32 index )
        {
            _server.onConnectionClosed( index, NetDisconnectReason::Remote );
            relay( index ).join( 7 );
            poll();
        }

        static vector<uint8> makeAction( uint8 value ) { return vector<uint8>{ value }; }
    };

    /** @brief 짝수 값 행동만 허락하고 차례를 넘기지 않는 정책입니다. */
    class EvenOnlyPolicy final : public ITurnPolicy
    {
    public:
        bool isActionAllowed( const TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const override
        {
            return seat == room._currentSeat && actionBuffer.empty() == false && actionBuffer[0] % 2 == 0;
        }
        void applyAction( TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const override
        {
            (void)seat;
            (void)actionBuffer;
            room._direction = -room._direction;
        }
    };
} // namespace

// 자리 수만큼 차면 시작하고, 각자 자리 · 토큰을 받는다. 토큰은 서로 다르고 시드가 같으면 같다.
SW_TEST_CASE_F( TurnRelayTest, RoomStartsWhenFull )
{
    addClient()._relay.join( 7 );
    poll();
    SW_EXPECT_EQUAL( relay( 0 ).getSeat(), 0 );
    SW_EXPECT_FALSE( relay( 0 ).isStarted() );
    SW_REQUIRE_NOT_NULL( _server.findRoom( 7 ) );
    SW_EXPECT_TRUE( _server.findRoom( 7 )->_bStarted == SW_FALSE );

    addClient()._relay.join( 7 );
    poll();
    SW_EXPECT_EQUAL( relay( 1 ).getSeat(), 1 );
    SW_EXPECT_TRUE( relay( 0 ).isStarted() );
    SW_EXPECT_TRUE( relay( 1 ).isStarted() );
    SW_EXPECT_NOT_EQUAL( relay( 0 ).getToken(), relay( 1 ).getToken() );
    SW_EXPECT_NOT_EQUAL( relay( 0 ).getToken(), 0u );

    const vector<TurnRelayEvent> listEvent = pollAll( relay( 0 ) );
    SW_REQUIRE_EQUAL( listEvent.size(), size_t{ 2 } );
    SW_EXPECT_TRUE( listEvent[0]._kind == TurnRelayEvent::Kind::Joined );
    SW_EXPECT_TRUE( listEvent[1]._kind == TurnRelayEvent::Kind::Started );

    const vector<TurnRelayEvent> listServerEvent = drainServer();
    SW_REQUIRE_EQUAL( listServerEvent.size(), size_t{ 3 } );
    SW_EXPECT_TRUE( listServerEvent[2]._kind == TurnRelayEvent::Kind::Started );
}

// 원하는 빈 자리를 받고, 찬 자리를 원하면 첫 빈 자리다. 다 찬 방은 거절한다.
SW_TEST_CASE_F( TurnRelayTest, SeatChoiceAndFullRoom )
{
    addClient()._relay.join( 7, 1 );
    addClient()._relay.join( 7, 1 );
    addClient()._relay.join( 7 );
    poll();
    SW_EXPECT_EQUAL( relay( 0 ).getSeat(), 1 );
    SW_EXPECT_EQUAL( relay( 1 ).getSeat(), 0 );
    SW_EXPECT_EQUAL( relay( 2 ).getSeat(), -1 );
    const vector<TurnRelayEvent> listEvent = pollAll( relay( 2 ) );
    SW_REQUIRE_EQUAL( listEvent.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listEvent[0]._kind == TurnRelayEvent::Kind::Denied );
    SW_EXPECT_TRUE( listEvent[0]._reason == TurnRejectReason::RoomFull );
}

// 한 연결은 자리 하나다. 같은 방에 다시 들어오면 같은 자리, 다른 방이면 거절이다.
SW_TEST_CASE_F( TurnRelayTest, ConnectionHoldsOneSeat )
{
    startRoom();
    const uint32 token = relay( 0 ).getToken();
    relay( 0 ).join( 7 );
    poll();
    SW_EXPECT_EQUAL( relay( 0 ).getSeat(), 0 );
    SW_EXPECT_EQUAL( relay( 0 ).getToken(), token );

    (void)pollAll( relay( 0 ) );
    relay( 0 ).join( 99 );
    poll();
    const vector<TurnRelayEvent> listEvent = pollAll( relay( 0 ) );
    SW_REQUIRE_EQUAL( listEvent.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listEvent[0]._reason == TurnRejectReason::AlreadySeated );
    SW_EXPECT_TRUE( _server.findRoom( 99 ) == nullptr );
}

// 결함: 다른 방에 들어갈 때 시작 표시와 자리가 그대로 남아, 새 방이 시작하기 전에도 시작한 것으로 보였다.
SW_TEST_CASE_F( TurnRelayTest, JoiningAnotherRoomForgetsTheOldRoomsState )
{
    startRoom();
    SW_REQUIRE_TRUE( relay( 0 ).isStarted() );
    relay( 0 ).join( 99 );
    SW_EXPECT_FALSE( relay( 0 ).isStarted() );
    SW_EXPECT_EQUAL( relay( 0 ).getSeat(), -1 );
    SW_EXPECT_EQUAL( relay( 0 ).getToken(), 0u );
    SW_EXPECT_TRUE( relay( 0 ).getActions().empty() );
}

// 차례인 자리의 행동은 모두에게 번호 순서로 가고 차례가 넘어간다. 차례가 아니면 거절이다.
SW_TEST_CASE_F( TurnRelayTest, ActionsReachEveryoneInOrder )
{
    startRoom();
    (void)pollAll( relay( 0 ) );
    (void)pollAll( relay( 1 ) );

    const int32 rejected = relay( 1 ).submitAction( makeAction( 9 ) );
    poll();
    vector<TurnRelayEvent> listEvent = pollAll( relay( 1 ) );
    SW_REQUIRE_EQUAL( listEvent.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listEvent[0]._kind == TurnRelayEvent::Kind::ActionRejected );
    SW_EXPECT_TRUE( listEvent[0]._reason == TurnRejectReason::NotYourTurn );
    SW_EXPECT_EQUAL( listEvent[0]._index, rejected );

    (void)relay( 0 ).submitAction( makeAction( 1 ) );
    poll();
    (void)relay( 1 ).submitAction( makeAction( 2 ) );
    poll();

    for ( const int32 index : { 0, 1 } )
    {
        const vector<TurnAction>& listAction = relay( index ).getActions();
        SW_REQUIRE_EQUAL( listAction.size(), size_t{ 2 } );
        SW_EXPECT_EQUAL( listAction[0]._seat, 0 );
        SW_EXPECT_EQUAL( listAction[0]._buffer[0], static_cast<uint8>( 1 ) );
        SW_EXPECT_EQUAL( listAction[1]._seat, 1 );
    }
    SW_EXPECT_EQUAL( _server.findRoom( 7 )->_currentSeat, 0 );
    SW_EXPECT_EQUAL( _server.findRoom( 7 )->_listAction.size(), size_t{ 2 } );
}

// 시작 전의 행동 · 앉지 않은 방의 행동은 이유를 달아 거절한다.
SW_TEST_CASE_F( TurnRelayTest, ActionsBeforeStartOrOutsideAreRejected )
{
    addClient()._relay.join( 7 );
    poll();
    (void)pollAll( relay( 0 ) );
    (void)relay( 0 ).submitAction( makeAction( 1 ) );
    poll();
    vector<TurnRelayEvent> listEvent = pollAll( relay( 0 ) );
    SW_REQUIRE_EQUAL( listEvent.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listEvent[0]._reason == TurnRejectReason::NotStarted );

    Client& stranger = addClient();
    (void)stranger._relay.submitAction( makeAction( 1 ) ); // 방 0 은 없다
    poll();
    listEvent = pollAll( stranger._relay );
    SW_REQUIRE_EQUAL( listEvent.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listEvent[0]._reason == TurnRejectReason::UnknownRoom );
}

// 정책이 허용과 차례를 정한다. 자기 차례인데 거절되면 규칙 위반이다.
SW_TEST_CASE( TurnRelayPolicyTest, PolicyDecidesAndCanChangeDirection )
{
    TurnRelayTest   rig;
    EvenOnlyPolicy  policy;
    TurnRelayConfig config;
    config._seatCount = 2;
    SW_REQUIRE_TRUE( rig._server.initialize( &rig._serverPeers, config, &policy, TurnRelayTest::kTokenSeed ) );
    rig.startRoom();
    (void)TurnRelayTest::pollAll( rig.relay( 0 ) );

    (void)rig.relay( 0 ).submitAction( TurnRelayTest::makeAction( 3 ) );
    rig.poll();
    const vector<TurnRelayEvent> listEvent = TurnRelayTest::pollAll( rig.relay( 0 ) );
    SW_REQUIRE_EQUAL( listEvent.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listEvent[0]._reason == TurnRejectReason::InvalidAction );

    (void)rig.relay( 0 ).submitAction( TurnRelayTest::makeAction( 4 ) );
    rig.poll();
    SW_EXPECT_EQUAL( rig._server.findRoom( 7 )->_direction, -1 );
    SW_EXPECT_EQUAL( rig._server.findRoom( 7 )->_currentSeat, 0 );
}

// 기본 정책은 방향을 뒤집어도 자리 범위 안에서 돈다.
SW_TEST_CASE( TurnRelayPolicyTest, DefaultPolicyWrapsBothDirections )
{
    const ITurnPolicy policy;
    TurnRoom          room;
    room._listSeat.resize( 3 );
    policy.applyAction( room, 0, {} );
    SW_EXPECT_EQUAL( room._currentSeat, 1 );
    room._direction   = -1;
    room._currentSeat = 0;
    policy.applyAction( room, 0, {} );
    SW_EXPECT_EQUAL( room._currentSeat, 2 );
    SW_EXPECT_FALSE( policy.isActionAllowed( room, 0, {} ) );
    SW_EXPECT_TRUE( policy.isActionAllowed( room, 2, {} ) );
}

// 끊겼다 토큰으로 돌아오면 같은 자리와 놓친 행동을 받는다. 이미 가진 것은 다시 받지 않는다.
SW_TEST_CASE_F( TurnRelayTest, ReturningSeatReceivesMissedActions )
{
    startRoom();
    (void)relay( 0 ).submitAction( makeAction( 1 ) );
    poll();
    _server.onConnectionClosed( 1, NetDisconnectReason::Remote );
    (void)drainServer();

    SW_EXPECT_EQUAL( _server.findRoom( 7 )->_listSeat[1]._connectionID, -1 );
    SW_EXPECT_TRUE( _server.findRoom( 7 )->_listSeat[1]._bTaken == SW_TRUE );

    relay( 1 ).join( 7 );
    poll();
    SW_EXPECT_EQUAL( relay( 1 ).getSeat(), 1 );
    SW_EXPECT_EQUAL( relay( 1 ).getActions().size(), size_t{ 1 } );
    const vector<TurnRelayEvent> listServerEvent = drainServer();
    SW_REQUIRE_FALSE( listServerEvent.empty() );
    SW_EXPECT_TRUE( listServerEvent[0]._kind == TurnRelayEvent::Kind::SeatReturned );

    // 차례가 넘어가 다음 행동을 낸다 - 돌아온 클라이언트가 이어 받는다.
    (void)relay( 1 ).submitAction( makeAction( 2 ) );
    poll();
    SW_EXPECT_EQUAL( relay( 0 ).getActions().size(), size_t{ 2 } );
    SW_EXPECT_EQUAL( relay( 1 ).getActions().size(), size_t{ 2 } );
}

// 토큰이 맞아도 살아 있는 연결의 자리는 뺏지 않는다. 토큰 없는 새 연결은 끊긴 자리를 차지하지 못한다.
SW_TEST_CASE_F( TurnRelayTest, SeatsCannotBeStolen )
{
    startRoom();
    _server.onConnectionClosed( 1, NetDisconnectReason::Remote );
    Client& newcomer = addClient();
    newcomer._relay.join( 7, 1 );
    poll();
    SW_EXPECT_EQUAL( newcomer._relay.getSeat(), -1 );
    vector<TurnRelayEvent> listEvent = pollAll( newcomer._relay );
    SW_REQUIRE_EQUAL( listEvent.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listEvent[0]._reason == TurnRejectReason::RoomFull );

    // 서버가 아직 끊김을 모르는 동안 같은 토큰으로 돌아오면 SeatInUse 다. 기록의 토큰을 다른 연결이 내는 모양으로 흉내 낸다.
    Client& thief = addClient();
    thief._relay.join( 7 ); // 받는 쪽이 방 7 의 메시지를 보도록 - 이 입장은 RoomFull 로 끝난다
    poll();
    (void)pollAll( thief._relay );
    TurnJoinBody stolenJoin;
    stolenJoin._roomID = 7;
    stolenJoin._token  = relay( 0 ).getToken();

    stolenJoin._generation = _server.findRoom( 7 )->_generation;
    NetMessageWriter writer;
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Join>( stolenJoin ) ) );
    SW_EXPECT_TRUE( _server.handleMessage( 3, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
    poll();
    listEvent = pollAll( thief._relay );
    SW_REQUIRE_EQUAL( listEvent.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listEvent[0]._reason == TurnRejectReason::SeatInUse );
    SW_EXPECT_EQUAL( _server.findRoom( 7 )->_listSeat[0]._connectionID, 0 );
}

// 창이 차면 행동은 멈췄다 update 에서 빠짐없이 이어 간다.
SW_TEST_CASE_F( TurnRelayTest, FullWindowResumesInUpdate )
{
    startRoom();
    _serverPeers._bWindowFull = SW_TRUE;
    (void)relay( 0 ).submitAction( makeAction( 1 ) );
    poll();
    (void)relay( 1 ).submitAction( makeAction( 2 ) );
    poll();
    SW_EXPECT_TRUE( relay( 0 ).getActions().empty() );
    SW_EXPECT_EQUAL( _server.findRoom( 7 )->_listAction.size(), size_t{ 2 } );

    _serverPeers._bWindowFull = SW_FALSE;
    _server.update( 0 );
    poll();
    SW_EXPECT_EQUAL( relay( 0 ).getActions().size(), size_t{ 2 } );
    SW_EXPECT_EQUAL( relay( 1 ).getActions().size(), size_t{ 2 } );
    SW_EXPECT_EQUAL( relay( 1 ).getActions()[1]._buffer[0], static_cast<uint8>( 2 ) );
}

// 결함: 창이 찬 동안 쌓인 "입장 · 시작" 보다 행동이 먼저 나갈 수 있었다 - 알림이 줄 서 있으면 행동도 기다린다.
SW_TEST_CASE_F( TurnRelayTest, ActionsWaitBehindQueuedNotifications )
{
    addClient()._relay.join( 7 );
    addClient()._relay.join( 7 );
    _serverPeers._bWindowFull = SW_TRUE; // 시작 알림이 줄에 쌓인다
    {
        poll();
    }
    SW_REQUIRE_TRUE( _serverPeers._listSent.empty() );
    SW_REQUIRE_NOT_NULL( _server.findRoom( 7 ) );
    SW_REQUIRE_TRUE( _server.findRoom( 7 )->_bStarted == SW_TRUE );

    // 창이 하나 열린 프레임에 행동이 먼저 도착해도, 줄 선 알림보다 앞서 나가지 않는다.
    _serverPeers._bWindowFull = SW_FALSE;
    (void)relay( 0 ).submitAction( makeAction( 1 ) );
    vector<FakeTurnPeers::Sent> listSubmit = std::move( _listClient[0]->_peers._listSent );
    _listClient[0]->_peers._listSent.clear();
    SW_REQUIRE_EQUAL( listSubmit.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( _server.handleMessage( 0, listSubmit[0]._bytes.data(), static_cast<int32>( listSubmit[0]._bytes.size() ), NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
    SW_EXPECT_TRUE( _serverPeers._listSent.empty() );

    _server.update( 0 );
    vector<uint8> listKind;
    for ( const FakeTurnPeers::Sent& sent : _serverPeers._listSent )
    {
        if ( sent._connectionID == 0 )
            listKind.push_back( sent._bytes[0] );
    }
    SW_REQUIRE_EQUAL( listKind.size(), size_t{ 3 } );
    SW_EXPECT_EQUAL( listKind[0], NetTurnRelayMessage::kJoined );
    SW_EXPECT_EQUAL( listKind[1], NetTurnRelayMessage::kStarted );
    SW_EXPECT_EQUAL( listKind[2], NetTurnRelayMessage::kApplied );
}

// 알림을 읽지 않는 연결은 상한에서 끊는다. 끊긴 연결의 큐는 다음 연결이 물려받지 않는다.
SW_TEST_CASE_F( TurnRelayTest, ConnectionThatNeverDrainsIsDropped )
{
    startRoom();
    _serverPeers._bWindowFull = SW_TRUE;
    SW_EXPECT_LOG( "TurnRelay.ConnectionDoesNotAcknowledge" );
    for ( int32 index = 0; index <= TurnRelayServer::kMaxPendingPerConnection; ++index )
    {
        relay( 0 ).join( 99 ); // 거절 알림이 쌓인다
    }
    poll();
    SW_REQUIRE_EQUAL( _serverPeers._listDisconnected.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( _serverPeers._listDisconnected[0], 0 );

    _server.onConnectionClosed( 0, NetDisconnectReason::Remote );
    _serverPeers._bWindowFull = SW_FALSE;
    _server.update( 0 );
    SW_EXPECT_TRUE( _serverPeers._listSent.empty() );
}

// 행동 크기 상한: 넘으면 보내지 않고 번호를 쓰지 않는다. 서버는 꾸민 긴 행동을 손상으로 본다.
SW_TEST_CASE_F( TurnRelayTest, OversizedActionsAreRefused )
{
    startRoom();
    SW_EXPECT_LOG( "TurnRelay.TurnActionBytesExceeds" );
    SW_EXPECT_EQUAL( relay( 0 ).submitAction( vector<uint8>( static_cast<size_t>( NetTurnRelayMessage::kMaxActionBytes ) + 1, 0 ) ), -1 );
    SW_EXPECT_TRUE( _listClient[0]->_peers._listSent.empty() );
    SW_EXPECT_EQUAL( relay( 0 ).submitAction( makeAction( 1 ) ), 0 );

    // 상한 길이의 본문을 쓰면 본문 형식이 더 쓰지 못하게 막는다(쓰기 실패). 상한 본문 뒤에 바이트를 덧붙인 꾸민 메시지는 남는 비트라 손상이다.
    TurnActionBody action;
    action._roomID = 7;
    action._action.assign( static_cast<size_t>( NetTurnRelayMessage::kMaxActionBytes ) + 1, uint8{ 0 } );
    NetMessageWriter refusedWriter;
    SW_EXPECT_LOG( "Net.MessageBodyOutOfRange" );
    SW_EXPECT_FALSE( ( refusedWriter.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Action>( action ) ) );
    action._action.resize( static_cast<size_t>( NetTurnRelayMessage::kMaxActionBytes ) );
    BitWriter writer;
    writer.writeBits( NetTurnRelayMessage::kAction, 8 );
    SW_REQUIRE_TRUE( NetTurnRelayMessage::Action::writeBody( action, writer ) );
    writer.writeBits( 0xFFu, 8 );
    SW_EXPECT_TRUE( _server.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
}

// 결함: 받은 메시지마다 방 · 기록을 끝없이 늘릴 수 있었다 - 방 수와 방의 행동 수에 상한을 둔다(설정).
SW_TEST_CASE( TurnRelayPolicyTest, RoomAndHistoryLimitsRefuseMore )
{
    TurnRelayTest   rig;
    TurnRelayConfig config;
    config._seatCount         = 1;
    config._maxRoomCount      = 2;
    config._maxActionsPerRoom = 2;
    SW_REQUIRE_TRUE( rig._server.initialize( &rig._serverPeers, config, nullptr, TurnRelayTest::kTokenSeed ) );

    rig.addClient()._relay.join( 1 );
    rig.addClient()._relay.join( 2 );
    rig.addClient()._relay.join( 3 );
    rig.poll();
    SW_EXPECT_TRUE( rig._server.findRoom( 3 ) == nullptr );
    const vector<TurnRelayEvent> listDenied = TurnRelayTest::pollAll( rig.relay( 2 ) );
    SW_REQUIRE_EQUAL( listDenied.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listDenied[0]._reason == TurnRejectReason::ServerFull );

    (void)TurnRelayTest::pollAll( rig.relay( 0 ) );
    for ( uint8 value = 0; value < 3; ++value )
    {
        (void)rig.relay( 0 ).submitAction( TurnRelayTest::makeAction( value ) );
    }
    rig.poll();
    SW_EXPECT_EQUAL( rig._server.findRoom( 1 )->_listAction.size(), size_t{ 2 } );
    vector<TurnRelayEvent> listEvent    = TurnRelayTest::pollAll( rig.relay( 0 ) );
    bool                   bHistoryFull = false;
    for ( const TurnRelayEvent& event : listEvent )
    {
        bHistoryFull = bHistoryFull || ( event._kind == TurnRelayEvent::Kind::ActionRejected && event._reason == TurnRejectReason::HistoryFull );
    }
    SW_EXPECT_TRUE( bHistoryFull );
}

// 설정이 범위 밖이면 시작을 거절하고, 거절된 중계기는 메시지를 처리하지 않는다.
SW_TEST_CASE( TurnRelayPolicyTest, BadConfigRefusesToStart )
{
    SW_EXPECT_LOG( "TurnRelay.TurnRelayServerRefusedStartSettings" );
    FakeTurnPeers   peers;
    TurnRelayServer server;
    TurnRelayConfig config;
    config._seatCount = 0;
    SW_EXPECT_FALSE( server.initialize( &peers, config ) );
    server.update( 0 );

    TurnJoinBody join;
    join._roomID = 7;
    NetMessageWriter writer;
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Join>( join ) ) );
    SW_EXPECT_TRUE( server.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
    SW_EXPECT_TRUE( server.findRoom( 7 ) == nullptr );
    SW_EXPECT_TRUE( peers._listSent.empty() );
}

// 잘린 메시지는 손상이다. 서버가 죽거나 방을 만들지 않는다.
SW_TEST_CASE_F( TurnRelayTest, TruncatedMessagesAreMalformed )
{
    const uint8 arrJoin[] = { NetTurnRelayMessage::kJoin };
    SW_EXPECT_TRUE( _server.handleMessage( 0, arrJoin, 1, NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
    const uint8 arrAction[] = { NetTurnRelayMessage::kAction, 7 };
    SW_EXPECT_TRUE( _server.handleMessage( 0, arrAction, 2, NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
    SW_EXPECT_TRUE( _server.findRoom( 7 ) == nullptr );

    Client& client = addClient();
    client._relay.join( 7 );
    const uint8 arrShort[] = { NetTurnRelayMessage::kApplied, 7 };
    SW_EXPECT_TRUE( client._relay.handleMessage( 0, arrShort, 2, NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
    SW_EXPECT_TRUE( client._relay.getActions().empty() );
}

// 결함: 거절 이유 바이트를 범위 검사 없이 열거형으로 바꿨다 - 본문 형식이 등록 열거자 밖 값을 손상으로 거절한다.
SW_TEST_CASE_F( TurnRelayTest, UnknownRejectReasonIsMalformed )
{
    Client& client = addClient();
    client._relay.join( 7 );
    BitWriter writer;
    writer.writeBits( NetTurnRelayMessage::kRejected, 8 );
    writer.writeVarUint( 7 );     // 방
    writer.writeVarUint( 1 );     // 세대
    writer.writeVarInt( 0 );      // 행동 번호
    writer.writeBits( 0xFFu, 8 ); // 열거자 범위(10 개) 밖
    SW_EXPECT_TRUE( client._relay.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
    SW_EXPECT_TRUE( pollAll( client._relay ).empty() );
}

// 본문 판은 본문 형식의 스키마 해시다 - 형식이 다르면 표 판이 달라 라우터가 상대와 맞출 때 끊는다(`NetMessageRouterTest.DifferentMessageTableVersionsDisconnect`).
SW_TEST_CASE( TurnRelayWireTest, BodyRevisionFollowsTheBodySchema )
{
    using JoinTable   = NetMessageTable<NetKitMessageRange::kTurnRelay, NetMessageBodyDef<0, NetMessageDirection::ToServer, NetChannelKind::ReliableOrdered, TurnJoinBody>>;
    using JoinedTable = NetMessageTable<NetKitMessageRange::kTurnRelay, NetMessageBodyDef<0, NetMessageDirection::ToServer, NetChannelKind::ReliableOrdered, TurnJoinedBody>>;
    SW_EXPECT_NOT_EQUAL( NetTurnRelayMessage::Join::getBodyRevision(), NetTurnRelayMessage::Joined::getBodyRevision() );
    SW_EXPECT_NOT_EQUAL( JoinTable::computeWireVersion(), JoinedTable::computeWireVersion() );
    SW_EXPECT_EQUAL( JoinTable::computeWireVersion(), JoinTable::computeWireVersion() ); // 같은 형식은 같은 판(결정적)
    TurnRelayServer server;
    SW_EXPECT_EQUAL( server.getWireVersion(), NetTurnRelayMessage::Table::computeWireVersion() );
}

// 서술자가 있는 메시지는 서술자의 채널로만 나간다 — 호출부가 채널을 고르는 함수로 보내려 하면 오류와 함께 보내지 않는다.
SW_TEST_CASE( TurnRelayWireTest, TypedMessagesCannotTakeACallerChosenChannel )
{
    FakeTurnPeers    peers;
    TurnJoinBody     join;
    NetMessageWriter writer;
    join._roomID = 7;
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Join>( join ) ) );
    SW_EXPECT_TRUE( writer.getMessageChannel().has_value() && *writer.getMessageChannel() == NetTurnRelayMessage::Join::kChannel );
    SW_EXPECT_LOG( "Net.MessageChannelChosenByCaller" );
    SW_EXPECT_FALSE( writer.send( peers, 0, NetChannelKind::Unreliable ) );
    SW_EXPECT_TRUE( peers._listSent.empty() );
    SW_EXPECT_TRUE( writer.sendMessage( peers, 0 ) );
    SW_EXPECT_EQUAL( peers._listSent.size(), size_t{ 1 } );
}

namespace
{
    /** @brief 자리마다 자기 번호와 행동 수만 보이는 정책입니다. */
    class SeatViewTestPolicy final : public ITurnPolicy
    {
    public:
        bool writeSeatView( const TurnRoom& room, int32 seat, vector<uint8>& outView ) const override
        {
            outView = { static_cast<uint8>( seat ), static_cast<uint8>( room._listAction.size() ) };
            return true;
        }
    };
} // namespace

/** @brief [TurnRelayViewTest] 자리의 모습은 그 자리에만 간다 — 시작 때, 행동마다, 돌아왔을 때 가장 새 것 하나. 창이 차면 `update` 가 그때의 모습을 보낸다 */
SW_TEST_CASE( TurnRelayViewTest, EachSeatReceivesOnlyItsOwnView )
{
    SeatViewTestPolicy policy;
    TurnRelayTest      rig;
    TurnRelayConfig    config;
    config._seatCount = 2;
    SW_REQUIRE_TRUE( rig._server.initialize( &rig._serverPeers, config, &policy, TurnRelayTest::kTokenSeed ) );
    rig.startRoom();
    for ( int32 index = 0; index < 2; ++index )
    {
        SW_REQUIRE_EQUAL( size_t( 2 ), rig.relay( index ).getView().size() );
        SW_EXPECT_EQUAL( rig.relay( index ).getSeat(), static_cast<int32>( rig.relay( index ).getView()[0] ) );
        SW_EXPECT_EQUAL( uint8( 0 ), rig.relay( index ).getView()[1] );
    }

    const int32 firstSeat = rig._server.findRoom( 7 )->_currentSeat;
    for ( int32 index = 0; index < 2; ++index )
    {
        if ( rig.relay( index ).getSeat() == firstSeat )
            (void)rig.relay( index ).submitAction( vector<uint8>{ 1 } );
    }
    rig.poll();
    for ( int32 index = 0; index < 2; ++index )
    {
        SW_EXPECT_EQUAL( rig.relay( index ).getSeat(), static_cast<int32>( rig.relay( index ).getView()[0] ) );
        SW_EXPECT_EQUAL( uint8( 1 ), rig.relay( index ).getView()[1] );
    }

    // 다른 방으로 가면 지난 방의 모습을 버린다.
    rig.relay( 0 ).join( 8 );
    SW_EXPECT_TRUE( rig.relay( 0 ).getView().empty() );
}

namespace
{
    /** @brief 행동 하나로 판이 끝나는 정책입니다. */
    class OneActionPolicy final : public ITurnPolicy
    {
    public:
        bool isRoomFinished( const TurnRoom& room ) const override { return room._listAction.empty() == false; }
    };

    /** @brief 방 하나뿐인 서버입니다 — 되돌리지 않으면 다음 판을 열 수 없습니다. */
    struct TurnRelayReleaseTest : TurnRelayTest
    {
        static constexpr int64 kSecond = constant::time::kNanosecondsPerSecond;

        OneActionPolicy _policy;

        TurnRelayReleaseTest()
            : TurnRelayTest{}
            , _policy{}
        {
            TurnRelayConfig config;
            config._seatCount            = 2;
            config._maxRoomCount         = 1;
            config._finishedRoomSeconds  = 10.0f;
            config._abandonedRoomSeconds = 20.0f;
            SW_REQUIRE_TRUE( _server.initialize( &_serverPeers, config, &_policy, kTokenSeed ) );
        }

        /** @brief 두 사람이 앉아 첫 행동으로 판을 끝냅니다. */
        void finishRoom()
        {
            startRoom();
            (void)relay( 0 ).submitAction( makeAction( 1 ) );
            poll();
            SW_REQUIRE_TRUE( _server.findRoom( 7 ) != nullptr && _server.findRoom( 7 )->_bFinished == SW_TRUE );
        }

        [[nodiscard]] static TurnRejectReason takeLastReason( TurnRelayClient& client )
        {
            const vector<TurnRelayEvent> listEvent = pollAll( client );
            return listEvent.empty() ? TurnRejectReason::NotYourTurn : listEvent.back()._reason;
        }
    };
} // namespace

// 판이 끝나고 두 자리가 떠나면(떠남 메시지 · 끊김) 방을 되돌려 같은 번호로 새 판을 연다.
SW_TEST_CASE_F( TurnRelayReleaseTest, FinishedRoomIsReusedAfterBothLeave )
{
    finishRoom();
    const uint32 firstGeneration = _server.findRoom( 7 )->_generation;
    (void)relay( 1 ).submitAction( makeAction( 2 ) ); // 끝난 판은 행동을 더 받지 않는다
    poll();
    SW_EXPECT_TRUE( takeLastReason( relay( 1 ) ) == TurnRejectReason::RoomClosed );

    relay( 0 ).leave();
    SW_EXPECT_EQUAL( relay( 0 ).getSeat(), -1 );
    poll();
    _server.update( 0 );
    SW_REQUIRE_TRUE( _server.findRoom( 7 ) != nullptr ); // 한 사람이 남아 결과를 보고 있다
    _server.onConnectionClosed( 1, NetDisconnectReason::Remote );
    _server.update( 0 );
    SW_EXPECT_TRUE( _server.findRoom( 7 ) == nullptr );

    relay( 0 ).join( 7 );
    addClient()._relay.join( 7 );
    poll();
    SW_REQUIRE_TRUE( _server.findRoom( 7 ) != nullptr );
    SW_EXPECT_NOT_EQUAL( firstGeneration, _server.findRoom( 7 )->_generation );
    SW_EXPECT_TRUE( _server.findRoom( 7 )->_listAction.empty() );
    SW_EXPECT_TRUE( relay( 0 ).isStarted() && relay( 2 ).isStarted() );
    SW_EXPECT_TRUE( relay( 0 ).getActions().empty() );
}

// 같은 번호를 다시 쓴 방에는 지난 판의 늦은 행동 · 떠남 · 토큰이 닿지 않고, 지난 판의 늦은 알림은 새 자리의 클라이언트가 버린다.
SW_TEST_CASE_F( TurnRelayReleaseTest, LateMessagesOfThePreviousRoomAreIgnored )
{
    finishRoom();
    const uint32 firstGeneration = _server.findRoom( 7 )->_generation;
    relay( 0 ).leave();
    poll();
    _server.onConnectionClosed( 1, NetDisconnectReason::Remote ); // 클라이언트 1 은 토큰을 쥐고 있다
    _server.update( 0 );
    relay( 0 ).join( 7 );
    addClient()._relay.join( 7 );
    poll();
    SW_REQUIRE_TRUE( _server.findRoom( 7 ) != nullptr && _server.findRoom( 7 )->_bStarted == SW_TRUE );
    (void)pollAll( relay( 0 ) );

    // 새 판의 첫 차례인 연결 0 이 지난 판에서 보낸 행동 · 떠남이 이제 닿는다
    TurnActionBody lateAction;
    lateAction._roomID     = 7;
    lateAction._generation = firstGeneration;
    lateAction._action     = makeAction( 9 );
    NetMessageWriter writer;
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Action>( lateAction ) ) );
    SW_EXPECT_TRUE( _server.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
    TurnRoomBody lateLeave;
    lateLeave._roomID     = 7;
    lateLeave._generation = firstGeneration;
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Leave>( lateLeave ) ) );
    SW_EXPECT_TRUE( _server.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
    poll();
    SW_EXPECT_TRUE( _server.findRoom( 7 )->_listAction.empty() );
    SW_EXPECT_EQUAL( _server.findRoom( 7 )->_listSeat[0]._connectionID, 0 );
    SW_EXPECT_TRUE( pollAll( relay( 0 ) ).empty() ); // 지난 세대의 거절 알림은 새 판의 것이 아니다

    // 지난 판의 되돌림 · 적용 알림도 새 판에 닿지 않는다
    TurnAppliedBody lateApplied;
    lateApplied._roomID     = 7;
    lateApplied._generation = firstGeneration;
    lateApplied._action     = makeAction( 9 );
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Applied>( lateApplied ) ) );
    SW_EXPECT_TRUE( relay( 0 ).handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Closed>( lateLeave ) ) );
    SW_EXPECT_TRUE( relay( 0 ).handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
    SW_EXPECT_TRUE( relay( 0 ).getActions().empty() );
    SW_EXPECT_EQUAL( relay( 0 ).getSeat(), 0 );

    // 지난 판의 토큰으로는 새 판의 자리를 열지 못한다. 클라이언트는 토큰을 버리고 다음 입장은 처음부터다
    relay( 1 ).join( 7 );
    poll();
    SW_EXPECT_TRUE( takeLastReason( relay( 1 ) ) == TurnRejectReason::RoomClosed );
    SW_EXPECT_EQUAL( relay( 1 ).getSeat(), -1 );
    SW_EXPECT_EQUAL( relay( 1 ).getToken(), 0u );
    relay( 1 ).join( 7 );
    poll();
    SW_EXPECT_TRUE( takeLastReason( relay( 1 ) ) == TurnRejectReason::RoomFull );
}

// 끝난 판에 누가 남아 있어도, 끝나지 않은 판에 아무도 잇지 않아도 시간이 지나면 되돌린다. 그 전에 돌아오면 이어 둔다.
SW_TEST_CASE_F( TurnRelayReleaseTest, IdleRoomsAreReleasedAfterTheirTime )
{
    startRoom();
    _server.onConnectionClosed( 0, NetDisconnectReason::Remote );
    _server.onConnectionClosed( 1, NetDisconnectReason::Remote );
    _server.update( 0 );
    _server.update( 19 * kSecond );
    SW_REQUIRE_TRUE( _server.findRoom( 7 ) != nullptr );
    relay( 0 ).join( 7 );
    poll();
    SW_EXPECT_EQUAL( relay( 0 ).getSeat(), 0 );
    _server.update( 30 * kSecond ); // 돌아왔으니 시간을 다시 센다
    (void)relay( 0 ).submitAction( makeAction( 1 ) );
    poll();
    SW_REQUIRE_TRUE( _server.findRoom( 7 ) != nullptr && _server.findRoom( 7 )->_bFinished == SW_TRUE );
    _server.update( 31 * kSecond );
    _server.update( 40 * kSecond );
    SW_REQUIRE_TRUE( _server.findRoom( 7 ) != nullptr );
    (void)pollAll( relay( 0 ) );
    _server.update( 41 * kSecond );
    poll();
    SW_EXPECT_TRUE( _server.findRoom( 7 ) == nullptr );
    const vector<TurnRelayEvent> listEvent = pollAll( relay( 0 ) );
    SW_REQUIRE_EQUAL( listEvent.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listEvent[0]._kind == TurnRelayEvent::Kind::RoomClosed );
    SW_EXPECT_EQUAL( relay( 0 ).getSeat(), -1 );
    SW_EXPECT_FALSE( relay( 0 ).isStarted() );

    // 시작하지 못한 방은 돌아올 판이 없다 — 바로 되돌린다
    relay( 0 ).join( 7 );
    poll();
    SW_EXPECT_EQUAL( relay( 0 ).getSeat(), 0 );
    _server.onConnectionClosed( 0, NetDisconnectReason::Remote );
    _server.update( 41 * kSecond );
    SW_EXPECT_TRUE( _server.findRoom( 7 ) == nullptr );

    // 아무도 돌아오지 않은 판
    relay( 1 ).leave();
    relay( 0 ).leave();
    poll();
    relay( 0 ).join( 7 );
    relay( 1 ).join( 7 );
    poll();
    SW_REQUIRE_TRUE( _server.findRoom( 7 ) != nullptr && _server.findRoom( 7 )->_bStarted == SW_TRUE );
    _server.onConnectionClosed( 0, NetDisconnectReason::Remote );
    _server.onConnectionClosed( 1, NetDisconnectReason::Remote );
    _server.update( 50 * kSecond );
    SW_EXPECT_TRUE( _server.findRoom( 7 ) != nullptr );
    _server.update( 70 * kSecond );
    SW_EXPECT_TRUE( _server.findRoom( 7 ) == nullptr );
}

// 두 사람이 두던 판에서 스스로 떠나면 시간을 기다리지 않고 되돌린다 — 앉았다 떠나기를 되풀이해 방을 모두 차지하지 못한다.
SW_TEST_CASE_F( TurnRelayReleaseTest, RoomEveryoneLeftIsReleasedAtOnce )
{
    startRoom();
    relay( 0 ).leave();
    poll();
    _server.update( 0 );
    SW_EXPECT_TRUE( _server.findRoom( 7 ) != nullptr );
    addClient()._relay.join( 7 ); // 떠난 사람의 자리는 두던 판이라 남이 앉지 못한다
    poll();
    SW_EXPECT_TRUE( takeLastReason( relay( 2 ) ) == TurnRejectReason::RoomFull );
    relay( 1 ).leave();
    poll();
    _server.update( 0 );
    SW_EXPECT_TRUE( _server.findRoom( 7 ) == nullptr );
}

// 시작하기 전에 떠난 자리는 빈 자리다 — 다음 사람이 떠난 사람과 판을 시작하지 않는다.
SW_TEST_CASE_F( TurnRelayReleaseTest, LeavingBeforeTheStartFreesTheSeat )
{
    addClient()._relay.join( 7 );
    poll();
    relay( 0 ).leave();
    addClient()._relay.join( 7 );
    poll();
    SW_EXPECT_EQUAL( relay( 1 ).getSeat(), 0 );
    SW_EXPECT_FALSE( relay( 1 ).isStarted() );
}
