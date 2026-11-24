// ShooterNetSeatTest — 매치 서버의 자리: 접속마다 몸이 하나고, 자리가 다 차면 더 앉히지 않으며, 떠난 사람의 몸은 거두고 같은 연결 id 의 다른 계정은 앞사람의 몸을 물려받지 않는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "GameFramework/Runtime/Character/Control/FirstPersonCameraComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "Games/Playground/Shooter3D/ShooterAutoAimControllerComponent.h"
#include "Games/Playground/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Playground/Shooter3D/ShooterGameModeComponent.h"
#include "Games/Playground/Shooter3D/ShooterNetPawnComponent.h"
#include "Games/Playground/Shooter3D/ShooterPlayerComponent.h"

using namespace sw;

namespace
{
    /** @brief 붙은 연결과 그 계정을 테스트가 정하는 상대입니다. 보낸 것은 버립니다. */
    class SeatTestPeers final : public INetPeers
    {
    public:
        struct Link
        {
            uint64 _accountID{ 0 };
            int32  _connectionID{ -1 };
        };

        vector<Link> _listLink{};

        void connect( int32 connectionID, uint64 accountID ) { _listLink.push_back( Link{ accountID, connectionID } ); }
        void drop( int32 connectionID )
        {
            VectorUtil::removeIf( _listLink, [connectionID]( const Link& link )
            { return link._connectionID == connectionID; } );
        }

        bool               sendMessage( int32, NetChannelKind, const uint8*, int32 ) override { return true; }
        bool               trySendMessage( int32, NetChannelKind, const uint8*, int32 ) override { return true; }
        bool               sendNotifiedMessage( int32, const uint8*, int32, uint32 ) override { return true; }
        int32              broadcast( NetChannelKind, const uint8*, int32, int32 ) override { return 0; }
        void               disconnect( int32 ) override {}
        void               pollInbound( NetInbound& ) override {}
        bool               isServer() const override { return true; }
        NetConnectionState getConnectionState( int32 connectionID ) const override { return VectorUtil::findBy<&Link::_connectionID>( _listLink, connectionID ) != nullptr ? NetConnectionState::Connected : NetConnectionState::Disconnected; }
        bool               getConnectionStats( int32, NetConnectionStats& ) const override { return false; }
        NetAddress         getConnectionAddress( int32 ) const override { return NetAddress{}; }
        uint64             getConnectionPrincipal( int32 connectionID ) const override
        {
            const Link* pLink = VectorUtil::findBy<&Link::_connectionID>( _listLink, connectionID );
            return pLink != nullptr ? pLink->_accountID : 0;
        }
        optional<int32> getClientIndex() const override { return nullopt; }
        int32           getConnectedCount() const override { return static_cast<int32>( _listLink.size() ); }
        void            collectConnected( vector<int32>& outListConnection ) const override
        {
            outListConnection.clear();
            for ( const Link& link : _listLink )
                outListConnection.push_back( link._connectionID );
        }
        bool isConnectionSaturated( int32 ) const override { return false; }
        void addSentKindStats( NetMessageKindStatsTable& ) const override {}
    };

    /** @brief 자리 둘(씬의 몸 + 손님 하나)인 서버 월드입니다. 적은 내지 않는다(개발 스위치). 무기 표가 없어 플레이어의 규칙은 서지 않는다 — 자리와 몸만 본다. */
    struct SeatTestWorld
    {
        static constexpr float32 kFrame = 1.0f / 20.0f; ///< 고정 틱(30 Hz) 하나보다 길다

        SeatTestPeers            _peers;
        NetMessageRouter         _router;
        ShooterAutoPlayOptions   _options;
        SubsystemScope           _scope;
        GameObjectManager        _manager;
        GameObject*              _pPlayer;
        ShooterNetPawnComponent* _pNetPawn;
        int32                    _enemiesOff;

        SeatTestWorld()
            : _peers{}
            , _router{}
            , _options{}
            , _scope{}
            , _manager{}
            , _pPlayer{ nullptr }
            , _pNetPawn{ nullptr }
            , _enemiesOff{ 1 }
        {
            _options._pEnemiesOff = &_enemiesOff;
            SW_REQUIRE_TRUE( _scope.bind<ShooterAutoPlayOptions>( &_options ) );
            NetLinkValue link;
            link._pPeers  = &_peers;
            link._pRouter = &_router;
            link._role    = NetLinkRole::Server;
            SW_REQUIRE_TRUE( _manager.getSubsystem<SceneNetLink>().bind( link ) );
            GameObject* pGameMode = _manager.createGameObject( hashed_string( "GameMode" ) );
            (void)pGameMode->addComponent<ShooterGameModeComponent>();
            _pPlayer = _manager.createGameObject( hashed_string( "Player" ) );
            _pPlayer->addComponent<PawnComponent>()->setButtonNames( { hashed_string( "Jump" ), hashed_string( "Fire" ) } );
            (void)_pPlayer->addComponent<ShooterBodyMovementComponent>();
            (void)_pPlayer->addComponent<FirstPersonCameraComponent>();
            _pPlayer->addComponent<ShooterPlayerComponent>()->bindGameMode( pGameMode->getHandle() );
            _pNetPawn = _pPlayer->addComponent<ShooterNetPawnComponent>();
            SW_REQUIRE_TRUE( XMLSerializer::deserialize( _pNetPawn, *ShooterNetPawnComponent::StaticType(), "<ShooterNetPawnComponent><_listGuestSpawn><item>4,0,-16</item></_listGuestSpawn></ShooterNetPawnComponent>" ) );
        }

        ~SeatTestWorld()
        {
            _manager.endPlay();
            _manager.getSubsystem<SceneNetLink>().unbind();
        }

        SeatTestWorld( const SeatTestWorld& )            = delete;
        SeatTestWorld& operator=( const SeatTestWorld& ) = delete;

        /** @brief 앉고 · 떠나고 · 몸을 세우는 일이 모두 끝날 만큼 돕니다. */
        void advance()
        {
            for ( int32 frame = 0; frame < 4; ++frame )
                _manager.update( kFrame );
        }

        [[nodiscard]] vector<GameObjectHandle> collectPlayers() const
        {
            vector<GameObjectHandle> listObject;
            _pNetPawn->collectPlayerObjects( listObject );
            return listObject;
        }
    };
} // namespace

/** @brief [ShooterNetSeatTest] 첫 연결은 씬의 몸, 다음 연결은 씬 몸의 설정을 옮긴 새 몸에 앉고, 자리가 다 차면 더 앉히지 않는다 */
SW_TEST_CASE( ShooterNetSeatTest, EachConnectionGetsItsOwnBody )
{
    SW_EXPECT_LOG( "ShooterPlayer.NoWeaponCatalogGameService" ); // 무기 표 · 효과 프리팹이 없다(에셋 없는 테스트)
    SW_EXPECT_LOG( "PrefabPool.PrefabPoolCannotSpawn" );
    SeatTestWorld         world;
    game::ScopedGameScope scopedScope( world._scope );
    world._manager.beginPlay();
    world.advance();
    SW_REQUIRE_TRUE( world._pNetPawn->isServer() );
    SW_EXPECT_FALSE( world._pNetPawn->hasController( *world._pPlayer ) ); // 아무도 없다 — 판을 두는 것은 씬의 몸 하나다
    SW_EXPECT_TRUE( world.collectPlayers() == vector<GameObjectHandle>{ world._pPlayer->getHandle() } );

    world._peers.connect( 3, 11 );
    world.advance();
    SW_EXPECT_TRUE( world._pNetPawn->hasController( *world._pPlayer ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, world.collectPlayers().size() );

    world._peers.connect( 5, 22 );
    world.advance();
    const vector<GameObjectHandle> listSeated = world.collectPlayers();
    SW_REQUIRE_EQUAL( size_t{ 2 }, listSeated.size() );
    SW_EXPECT_TRUE( listSeated[0] == world._pPlayer->getHandle() );
    GameObject* pGuest = world._manager.resolveGameObject( listSeated[1] );
    SW_REQUIRE_NOT_NULL( pGuest );
    const PawnComponent*                pGuestPawn = pGuest->getComponent<PawnComponent>();
    const ShooterBodyMovementComponent* pGuestBody = pGuest->getComponent<ShooterBodyMovementComponent>();
    SW_REQUIRE_TRUE( pGuestPawn != nullptr && pGuestBody != nullptr );
    SW_EXPECT_TRUE( pGuestPawn->getButtonNames() == world._pPlayer->getComponent<PawnComponent>()->getButtonNames() ); // 버튼 비트는 스키마의 차례다
    SW_EXPECT_TRUE( pGuestBody->isNetDriven() );
    SW_EXPECT_NEAR( 4.0f, pGuestBody->getFeetPosition()._value._x, 1.0e-3f );
    SW_EXPECT_NEAR( -16.0f, pGuestBody->getFeetPosition()._value._z, 1.0e-3f );
    SW_EXPECT_TRUE( world._pNetPawn->hasController( *pGuest ) );

    world._peers.connect( 7, 33 );
    world.advance();
    SW_EXPECT_TRUE( world.collectPlayers() == listSeated ); // 자리가 다 찼다
}

/** @brief [ShooterNetSeatTest] 떠난 손님의 몸은 거두고 빈 자리에는 새 몸을 세운다. 같은 연결 id 로 다른 계정이 와도 앞사람의 몸을 물려받지 않고, 씬의 몸에 앉은 사람이 떠나면 남은 사람만 판을 둔다 */
SW_TEST_CASE( ShooterNetSeatTest, LeftSeatsAreCleanedAndNotInherited )
{
    SW_EXPECT_LOG( "ShooterPlayer.NoWeaponCatalogGameService" );
    SW_EXPECT_LOG( "PrefabPool.PrefabPoolCannotSpawn" );
    SeatTestWorld         world;
    game::ScopedGameScope scopedScope( world._scope );
    world._manager.beginPlay();
    world._peers.connect( 3, 11 );
    world._peers.connect( 5, 22 );
    world.advance();
    vector<GameObjectHandle> listSeated = world.collectPlayers();
    SW_REQUIRE_EQUAL( size_t{ 2 }, listSeated.size() );
    const GameObjectHandle firstGuest = listSeated[1];

    // 끊긴 것을 서버가 보기 전에 같은 id 로 다른 계정이 붙었다
    world._peers.drop( 5 );
    world._peers.connect( 5, 44 );
    world.advance();
    listSeated = world.collectPlayers();
    SW_REQUIRE_EQUAL( size_t{ 2 }, listSeated.size() );
    SW_EXPECT_TRUE( listSeated[1] != firstGuest );
    SW_EXPECT_TRUE( world._manager.resolveGameObject( firstGuest ) == nullptr );

    // 씬의 몸에 앉은 사람이 떠난다 — 씬의 몸은 남지만 판을 두는 사람은 아니다
    world._peers.drop( 3 );
    world.advance();
    SW_EXPECT_FALSE( world._pNetPawn->hasController( *world._pPlayer ) );
    SW_EXPECT_TRUE( world.collectPlayers() == vector<GameObjectHandle>{ listSeated[1] } );

    // 다음 사람은 빈 씬의 몸에 앉는다
    world._peers.connect( 9, 55 );
    world.advance();
    SW_EXPECT_TRUE( world._pNetPawn->hasController( *world._pPlayer ) );
    SW_EXPECT_EQUAL( size_t{ 2 }, world.collectPlayers().size() );

    world._peers.drop( 5 );
    world._peers.drop( 9 );
    world.advance();
    SW_EXPECT_TRUE( world.collectPlayers() == vector<GameObjectHandle>{ world._pPlayer->getHandle() } );
}
