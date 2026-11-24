// HubCardTableTest — 카드 탁자의 차례 규칙: 남의 차례 · 손에 없는 패 · 깨진 바이트는 거절하고, 받은 행동 뒤의 차례는 판이 정하며, 같은 방의 행동을 다시 돌리면 같은 판이다.
//                    자리의 모습에는 자기 손패와 상대 장수만 있고, 두 월드를 이은 판은 탁자를 레지스트리로 찾아(개발 명령 hub.cards 의 길) 둔다(nogpu).
#include "pch.h"

#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"

#include "Games/Playground/Hub/HubCardTableComponent.h"

using namespace sw;

namespace
{
    struct HubCardTableTestUtil
    {
        static vector<uint8> encode( const CardAction& action )
        {
            vector<uint8> buffer;
            CardAction::encodeAction( action, buffer );
            return buffer;
        }

        static vector<uint8> makeDraw()
        {
            CardAction action;
            action._kind = static_cast<uint8>( SheddingCardActionKind::Draw );
            return encode( action );
        }

        /** @brief 정책이 허락한 행동을 중계 서버처럼 목록에 넣고 적용합니다. */
        static void commit( const HubCardTurnPolicy& policy, TurnRoom& room, int32 seat, const vector<uint8>& buffer )
        {
            SW_REQUIRE_TRUE( policy.isActionAllowed( room, seat, buffer ) );
            room._listAction.push_back( TurnAction{ buffer, seat } );
            policy.applyAction( room, seat, buffer );
        }
    };
    using Util = HubCardTableTestUtil;
} // namespace

/** @brief [HubCardTableTest] 차례가 아닌 자리 · 손에 없는 패 · 깨진 바이트는 거절한다 */
SW_TEST_CASE( HubCardTableTest, ForgedActionsAreRefused )
{
    const HubCardTurnPolicy policy;
    TurnRoom                room;
    room._roomID = 7;
    room._listSeat.resize( HubCardTurnPolicy::kSeatCount );
    SheddingCardGame game;
    SW_REQUIRE_TRUE( policy.replay( room, game ) );
    const int32 first = game.getCurrentPlayer();
    const int32 other = 1 - first;

    SW_EXPECT_FALSE( policy.isActionAllowed( room, other, Util::makeDraw() ) ); // 남의 차례
    SW_EXPECT_FALSE( policy.isActionAllowed( room, first, vector<uint8>{ 1, 2, 3 } ) );
    SW_EXPECT_FALSE( policy.isActionAllowed( room, first, vector<uint8>{} ) );
    CardAction stolen;
    stolen._kind   = static_cast<uint8>( SheddingCardActionKind::Play );
    stolen._cardID = game.getHand( other ).getAt( 0 )._id; // 남의 손패
    SW_EXPECT_FALSE( policy.isActionAllowed( room, first, Util::encode( stolen ) ) );
}

/** @brief [HubCardTableTest] 받은 행동 뒤의 차례는 판이 정하고, 행동을 다시 돌린 두 판은 같다 */
SW_TEST_CASE( HubCardTableTest, ReplayFollowsTheAppliedActions )
{
    const HubCardTurnPolicy policy;
    TurnRoom                room;
    room._roomID = 7;
    room._listSeat.resize( HubCardTurnPolicy::kSeatCount );
    SheddingCardGame game;
    SW_REQUIRE_TRUE( policy.replay( room, game ) );
    int32 seat = game.getCurrentPlayer();
    for ( int32 turn = 0; turn < 6; ++turn )
    {
        Util::commit( policy, room, seat, Util::makeDraw() );
        seat = room._currentSeat;
    }
    SheddingCardGame left;
    SheddingCardGame right;
    SW_REQUIRE_TRUE( policy.replay( room, left ) );
    SW_REQUIRE_TRUE( policy.replay( room, right ) );
    SW_EXPECT_EQUAL( left.getCurrentPlayer(), room._currentSeat );
    SW_EXPECT_EQUAL( left.getHand( 0 ).getCount(), right.getHand( 0 ).getCount() );
    SW_EXPECT_TRUE( left.getTopCard() == right.getTopCard() );
    SW_EXPECT_EQUAL( 20, left.getHand( 0 ).getCount() + left.getHand( 1 ).getCount() ); // 일곱 장씩 + 뽑은 여섯 장
}

/** @brief [HubCardTableTest] 자리의 모습은 자기 손패 · 낼 수 있는 패 · 상대 장수뿐이고, 상대의 패 번호는 들어 있지 않다. 비밀 키가 다르면 다른 판이다 */
SW_TEST_CASE( HubCardTableTest, SeatViewHidesTheOtherHand )
{
    HubCardTurnPolicy policy;
    policy.setShuffleKey( SipHashKey{ 1, 2 } );
    TurnRoom room;
    room._roomID = 7;
    room._listSeat.resize( HubCardTurnPolicy::kSeatCount );
    SheddingCardGame game;
    SW_REQUIRE_TRUE( policy.replay( room, game ) );
    for ( int32 seat = 0; seat < HubCardTurnPolicy::kSeatCount; ++seat )
    {
        vector<uint8>   bytes;
        HubCardSeatView view;
        SW_REQUIRE_TRUE( policy.writeSeatView( room, seat, bytes ) );
        SW_REQUIRE_TRUE( HubCardTurnPolicy::readSeatView( bytes, view ) );
        SW_REQUIRE_EQUAL( size_t( 7 ), view._listHandCardID.size() );
        for ( const uint16 cardID : view._listHandCardID )
        {
            SW_EXPECT_TRUE( game.getHand( seat ).containsID( cardID ) );
            SW_EXPECT_FALSE( game.getHand( 1 - seat ).containsID( cardID ) );
        }
        for ( const uint16 cardID : view._listPlayableCardID )
            SW_EXPECT_TRUE( game.getHand( seat ).containsID( cardID ) );
        SW_EXPECT_EQUAL( 7, view._opponentCardCount );
        SW_EXPECT_EQUAL( game.getTopCard()._id, view._topCardID );
        SW_EXPECT_TRUE( seat == game.getCurrentPlayer() || view._listPlayableCardID.empty() );
        bytes.pop_back();
        SW_EXPECT_FALSE( HubCardTurnPolicy::readSeatView( bytes, view ) );
    }
    HubCardTurnPolicy otherPolicy;
    otherPolicy.setShuffleKey( SipHashKey{ 3, 4 } );
    SheddingCardGame otherGame;
    SW_REQUIRE_TRUE( otherPolicy.replay( room, otherGame ) );
    SW_EXPECT_FALSE( game.getHand( 0 ).getCards() == otherGame.getHand( 0 ).getCards() ); // 방만으로는 판을 알 수 없다

    room._generation = 1;
    SheddingCardGame nextGame;
    SW_REQUIRE_TRUE( policy.replay( room, nextGame ) );
    SW_EXPECT_FALSE( game.getHand( 0 ).getCards() == nextGame.getHand( 0 ).getCards() ); // 같은 방을 다시 쓴 다음 판은 다른 판이다
}

namespace
{
    /** @brief 보낸 메시지를 쌓기만 하는 상대입니다. */
    class FakeCardPeers final : public INetPeers
    {
    public:
        struct Sent
        {
            vector<uint8> _bytes{};
            int32         _connectionID{ -1 };
        };

        vector<Sent> _listSent{};

        explicit FakeCardPeers( bool bServer )
            : _bServer{ static_cast<uint8>( bServer ? SW_TRUE : SW_FALSE ) }
            , _reserved{ 0 }
        {
        }

        bool sendMessage( int32 connectionID, NetChannelKind channel, const uint8* pData, int32 size ) override { return trySendMessage( connectionID, channel, pData, size ); }
        bool trySendMessage( int32 connectionID, NetChannelKind, const uint8* pData, int32 size ) override
        {
            _listSent.push_back( Sent{ vector<uint8>( pData, pData + size ), connectionID } );
            return true;
        }
        bool               sendNotifiedMessage( int32, const uint8*, int32, uint32 ) override { return false; }
        int32              broadcast( NetChannelKind, const uint8*, int32, int32 ) override { return 0; }
        void               disconnect( int32, NetLinger ) override {}
        void               pollInbound( NetInbound& ) override {}
        bool               isServer() const override { return _bServer != SW_FALSE; }
        NetConnectionState getConnectionState( int32 ) const override { return NetConnectionState::Connected; }
        bool               getConnectionStats( int32, NetConnectionStats& ) const override { return false; }
        NetAddress         getConnectionAddress( int32 ) const override { return NetAddress{}; }
        uint64             getConnectionPrincipal( int32 ) const override { return 0; }
        optional<int32>    getClientIndex() const override { return nullopt; }
        int32              getConnectedCount() const override { return 1; }
        void               collectConnected( vector<int32>& outListConnection ) const override { outListConnection.clear(); }
        bool               isConnectionSaturated( int32 ) const override { return false; }
        void               addSentKindStats( NetMessageKindStatsTable& ) const override {}

    private:
        uint8                  _bServer  : 1;
        [[maybe_unused]] uint8 _reserved : 7;
    };

    /** @brief 카드 탁자 하나가 있는 월드입니다. */
    struct HubCardTestWorld
    {
        FakeCardPeers          _peers;
        NetMessageRouter       _router;
        GameObjectManager      _manager;
        HubCardTableComponent* _pTable;

        explicit HubCardTestWorld( NetLinkRole role )
            : _peers{ role == NetLinkRole::Server }
            , _router{}
            , _manager{}
            , _pTable{ nullptr }
        {
            NetLinkValue link;
            link._pPeers          = &_peers;
            link._pRouter         = &_router;
            link._role            = role;
            link._bTicketRequired = SW_TRUE;
            SW_REQUIRE_TRUE( _manager.getSubsystem<SceneNetLink>().bind( link, 0 ) );
            _pTable = _manager.createGameObject( hashed_string( "CardTable" ) )->addComponent<HubCardTableComponent>();
            _manager.beginPlay();
            _manager.update( 1.0f / 60.0f );
        }

        void receive( int32 connectionID, const vector<uint8>& bytes )
        {
            NetMessageContext context;
            context._connectionID = connectionID;
            SW_EXPECT_TRUE( _router.dispatch( _peers.isServer() ? NetPeerRole::Server : NetPeerRole::Client, context, bytes.data(), static_cast<int32>( bytes.size() ) ) == NetHandleResult::Handled );
        }

        ~HubCardTestWorld()
        {
            _manager.endPlay();
            _manager.getSubsystem<SceneNetLink>().unbind();
        }
    };
} // namespace

/** @brief [HubCardTableTest] 두 클라이언트가 탁자를 레지스트리에서 찾아 앉고(hub.cards 의 길), 서버가 나눈 패로 번갈아 둔다 — 서버가 보낸 모습만으로 낼 패를 고르고 거절이 없다 */
SW_TEST_CASE( HubCardTableTest, TwoClientsPlayOnViewsDealtByTheServer )
{
    HubCardTestWorld server( NetLinkRole::Server );
    HubCardTestWorld arrClient[2] = { HubCardTestWorld( NetLinkRole::Client ), HubCardTestWorld( NetLinkRole::Client ) };

    const auto poll = [&server, &arrClient]()
    {
        for ( int32 round = 0; round < 4; ++round )
        {
            for ( int32 index = 0; index < 2; ++index )
            {
                vector<FakeCardPeers::Sent> listSent = std::move( arrClient[index]._peers._listSent );
                arrClient[index]._peers._listSent.clear();
                for ( const FakeCardPeers::Sent& sent : listSent )
                    server.receive( index, sent._bytes );
            }
            server._manager.update( 1.0f / 60.0f );
            vector<FakeCardPeers::Sent> listOut = std::move( server._peers._listSent );
            server._peers._listSent.clear();
            for ( const FakeCardPeers::Sent& sent : listOut )
                arrClient[sent._connectionID].receive( 0, sent._bytes );
            for ( HubCardTestWorld& client : arrClient )
                client._manager.update( 1.0f / 60.0f );
        }
    };

    HubCardTableComponent* arrTable[2];
    for ( int32 index = 0; index < 2; ++index )
    {
        arrTable[index] = HubCardTableComponent::findClientTable( arrClient[index]._manager );
        SW_REQUIRE_NOT_NULL( arrTable[index] );
        SW_EXPECT_TRUE( arrTable[index] == arrClient[index]._pTable );
        SW_REQUIRE_TRUE( arrTable[index]->requestSeat() );
        poll();
    }
    SW_EXPECT_TRUE( HubCardTableComponent::findClientTable( server._manager ) == nullptr ); // 서버의 탁자는 앉는 자리가 아니다
    for ( HubCardTableComponent* pTable : arrTable )
    {
        SW_EXPECT_EQUAL( 1, pTable->getStartedValue() );
        SW_EXPECT_EQUAL( 7, pTable->getHandCount() );
        SW_EXPECT_EQUAL( 7, pTable->getOpponentCardCount() );
    }
    SW_EXPECT_EQUAL( 1, arrTable[0]->getMyTurnValue() + arrTable[1]->getMyTurnValue() );

    SW_EXPECT_FALSE( arrTable[0]->requestSeat() ); // 두는 판이 있으면 다른 탁자에 앉지 않는다

    for ( int32 turn = 0; turn < 12; ++turn )
    {
        HubCardTableComponent* const pMover = arrTable[0]->getMyTurnValue() != 0 ? arrTable[0] : arrTable[1];
        if ( pMover->getView()._winner >= 0 )
            break;
        SW_REQUIRE_TRUE( pMover->requestPlay( turn % 3 == 0 ) );
        poll();
        SW_EXPECT_EQUAL( turn + 1, arrTable[0]->getActionCount() );
        SW_EXPECT_EQUAL( arrTable[0]->getHandCount(), arrTable[1]->getOpponentCardCount() );
        SW_EXPECT_EQUAL( arrTable[1]->getHandCount(), arrTable[0]->getOpponentCardCount() );
    }
    SW_EXPECT_EQUAL( 0, arrTable[0]->getRejectedCount() + arrTable[1]->getRejectedCount() );

    SW_REQUIRE_TRUE( arrTable[0]->requestLeave() );
    SW_EXPECT_FALSE( arrTable[0]->requestLeave() );
    poll();
    SW_EXPECT_EQUAL( -1, arrTable[0]->getSeat() );
    SW_EXPECT_EQUAL( 0, arrTable[0]->getHandCount() );
    SW_EXPECT_EQUAL( 0, arrTable[0]->getStartedValue() );
    SW_REQUIRE_TRUE( arrTable[0]->requestSeat() ); // 첫 탁자는 차 있으니 다음 탁자에 앉는다
    poll();
    SW_EXPECT_EQUAL( 0, arrTable[0]->getSeat() );
    SW_EXPECT_EQUAL( 0, arrTable[0]->getStartedValue() );
}
