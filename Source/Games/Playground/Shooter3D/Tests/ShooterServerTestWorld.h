/**
 * @file ShooterServerTestWorld.h
 * @brief 사격장 매치 서버의 테스트 월드입니다 — 연결 · 계정을 테스트가 정하는 상대, 씬의 몸과 손님 자리 하나, 클라이언트 명령을 서버 라우터에 넣는 클라이언트(nogpu, 에셋 없음).
 */
#pragma once
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Tests/FakeNetPeers.h"
#include "Core/String/StringBuilder.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/Weapon.h"
#include "GameFramework/Runtime/Character/Control/FirstPersonCameraComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "Games/Playground/Shooter3D/ShooterAutoAimControllerComponent.h"
#include "Games/Playground/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Playground/Shooter3D/ShooterGameModeComponent.h"
#include "Games/Playground/Shooter3D/ShooterNetPawnComponent.h"
#include "Games/Playground/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    /** @brief 자리 둘(씬의 몸 + 손님 하나)인 서버 월드입니다. 적은 내지 않는다(개발 스위치). 씬의 몸은 (4.7, 0, -10), 손님은 (4, 0, -16)에 선다. */
    struct ShooterServerTestWorld
    {
        enum class WeaponTable : uint8
        {
            Absent = 0, ///< 무기 표가 없다 — 플레이어의 규칙이 서지 않는다(자리와 몸만 본다)
            Present     ///< 퍼짐 없는 한 방 무기. 맞음 층은 늘 있는 Default 다
        };

        static constexpr int32   kFireButton = 1;            ///< 폰 스키마의 `Fire` 자리
        static constexpr float32 kWalkStep   = 0.5f;         ///< 명령 하나(1/30 초)의 걸음
        static constexpr float32 kFrame      = 1.0f / 20.0f; ///< 고정 틱(30 Hz) 하나보다 길다

        test::FakeNetPeers       _peers;
        NetMessageRouter         _router;
        ShooterAutoPlayOptions   _options;
        WeaponCatalog            _weapons;
        ProjectileCatalog        _projectiles;
        SubsystemScope           _scope;
        GameObjectManager        _manager;
        GameObject*              _pPlayer;
        ShooterNetPawnComponent* _pNetPawn;
        int32                    _enemiesOff;

        /** @brief 퍼짐 없는 한 방 무기(피해 200) 하나의 표를 읽습니다. */
        [[nodiscard]] static bool loadWeapons( WeaponCatalog& outWeapons, ProjectileCatalog& outProjectiles )
        {
            constexpr const utf8* kProjectileXML = R"(<ProjectileCatalog><Projectile id="round" tags="Ammo.Rifle" damage="200" speed="0" range="60"/></ProjectileCatalog>)";
            constexpr const utf8* kWeaponXML     = R"(<WeaponCatalog><Weapon id="rifle" item="scifi_rifle" projectile="round" accepts="Ammo.Rifle" fireInterval="0.01" minSpread="0" maxSpread="0" spreadPerShot="0" recoilPitch="0"/></WeaponCatalog>)";
            return outProjectiles.loadFromXMLText( kProjectileXML, "ShooterServerTest" ) && outWeapons.loadFromXMLText( kWeaponXML, "ShooterServerTest" );
        }

        /** @brief @p eyeHeight 는 플레이어의 눈 높이입니다. @p weaponTable 이 Present 면 무기 표를 올려 두 자리 모두 한 방 무기를 쥡니다. */
        explicit ShooterServerTestWorld( WeaponTable weaponTable, float32 eyeHeight = 1.5f )
            : _peers{ true }
            , _router{}
            , _options{}
            , _weapons{}
            , _projectiles{}
            , _scope{}
            , _manager{}
            , _pPlayer{ nullptr }
            , _pNetPawn{ nullptr }
            , _enemiesOff{ 1 }
        {
            _options._pEnemiesOff = &_enemiesOff;
            SW_REQUIRE_TRUE( _scope.bind<ShooterAutoPlayOptions>( &_options ) );
            if ( weaponTable == WeaponTable::Present )
            {
                SW_REQUIRE_TRUE( loadWeapons( _weapons, _projectiles ) );
                SW_REQUIRE_TRUE( _scope.bind<const WeaponCatalog>( &_weapons ) && _scope.bind<const ProjectileCatalog>( &_projectiles ) );
            }
            NetLinkValue link;
            link._pPeers  = &_peers;
            link._pRouter = &_router;
            link._role    = NetLinkRole::Server;
            SW_REQUIRE_TRUE( _manager.getSubsystem<SceneNetLink>().bind( link, 0 ) );
            GameObject* pGameMode = _manager.createGameObject( hashed_string( "GameMode" ) );
            _pPlayer              = _manager.createGameObject( hashed_string( "Player" ) );
            bindScenePlayer( *pGameMode->addComponent<ShooterGameModeComponent>(), *_pPlayer );
            _pPlayer->addComponent<PawnComponent>()->setButtonNames( { hashed_string( "Jump" ), hashed_string( "Fire" ) } );
            (void)_pPlayer->addComponent<ShooterBodyMovementComponent>();
            (void)_pPlayer->addComponent<FirstPersonCameraComponent>();
            ShooterPlayerComponent*       pShooter  = _pPlayer->addComponent<ShooterPlayerComponent>();
            ShooterBodyMovementComponent* pMovement = _pPlayer->getComponent<ShooterBodyMovementComponent>();
            SW_REQUIRE_TRUE( XMLSerializer::deserialize( pMovement, *ShooterBodyMovementComponent::StaticType(), "<ShooterBodyMovementComponent _walkSpeed=\"15\" _jumpButton=\"Jump\"/>" ) );
            StringBuilder<constant::buffer::kMax512> playerXML;
            playerXML.appendFormat( "<ShooterPlayerComponent _spawnPosition=\"4.7,0,-10\" _hitboxLayer=\"Default\" _eyeHeight=\"%#\">", eyeHeight );
            playerXML.append( "<_listWeaponID><item>rifle</item><item>rifle</item><item>rifle</item></_listWeaponID></ShooterPlayerComponent>" );
            SW_REQUIRE_TRUE( XMLSerializer::deserialize( pShooter, *ShooterPlayerComponent::StaticType(), playerXML.c_str() ) );
            pShooter->bindGameMode( pGameMode->getHandle() );
            _pNetPawn = _pPlayer->addComponent<ShooterNetPawnComponent>();
            SW_REQUIRE_TRUE( XMLSerializer::deserialize( _pNetPawn, *ShooterNetPawnComponent::StaticType(), "<ShooterNetPawnComponent><_listGuestSpawn><item>4,0,-16</item></_listGuestSpawn></ShooterNetPawnComponent>" ) );
        }

        ~ShooterServerTestWorld()
        {
            _manager.endPlay();
            _manager.getSubsystem<SceneNetLink>().unbind();
        }

        /** @brief 씬처럼 감독이 플레이어 오브젝트를 가리키게 합니다 — 플레이어의 규칙이 네트 몸을 이 칸으로 찾는다(없으면 되감기 · 남의 몸 판정 없이 혼자 하는 길을 돈다). */
        static void bindScenePlayer( ShooterGameModeComponent& gameMode, const GameObject& player )
        {
            StringBuilder<constant::buffer::kMax128> gameModeXML;
            gameModeXML.appendFormat( "<ShooterGameModeComponent _player=\"%#\"/>", player.getObjectID() );
            SW_REQUIRE_TRUE( XMLSerializer::deserialize( &gameMode, *ShooterGameModeComponent::StaticType(), gameModeXML.c_str() ) );
            SW_REQUIRE_TRUE( gameMode.getPlayer() == player.getHandle() );
        }

        ShooterServerTestWorld( const ShooterServerTestWorld& )            = delete;
        ShooterServerTestWorld& operator=( const ShooterServerTestWorld& ) = delete;

        /** @brief 앉고 · 떠나고 · 몸을 세우는 일이 모두 끝날 만큼 돕니다. */
        void advance()
        {
            for ( int32 frame = 0; frame < 4; ++frame )
                advanceFrame();
        }

        /** @brief 서버가 지난 넷 틱(30 Hz)입니다. */
        [[nodiscard]] uint32 computeServerTick() { return _router.getWorldClock().getWholeTick( 2u ); }
        /** @brief 한 프레임 돕니다 — 보낸 명령 하나가 걸음 하나다. */
        void advanceFrame( float32 deltaTime = kFrame )
        {
            _manager.update( deltaTime );
            (void)_peers.takeSent(); // 보낸 것은 상대가 모두 받았다 — 쌓아 두면 신뢰 창이 차 연결이 닫힌다
        }
        /** @brief 앉은 차례 @p seatIndex 의 플레이어입니다. */
        [[nodiscard]] ShooterPlayerComponent* findPlayer( size_t seatIndex )
        {
            const vector<GameObjectHandle> listObject = collectPlayers();
            GameObject*                    pObject    = seatIndex < listObject.size() ? _manager.resolveGameObject( listObject[seatIndex] ) : nullptr;
            return pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
        }

        [[nodiscard]] vector<GameObjectHandle> collectPlayers() const
        {
            vector<GameObjectHandle> listObject;
            _pNetPawn->collectPlayerObjects( listObject );
            return listObject;
        }
    };

    /** @brief 연결 하나의 클라이언트입니다 — 실제 `ReplicationClient` 가 쓴 명령 바이트를 서버 월드의 라우터에 그 연결의 것으로 넣습니다. */
    class ShooterServerTestClient final
    {
    public:
        /** @brief 연결 @p connectionID 를 계정 @p accountID 로 붙입니다(서버의 상대 · 라우터 둘 다). */
        ShooterServerTestClient( ShooterServerTestWorld& world, int32 connectionID, uint64 accountID )
            : _world{ world }
            , _upstream{ false }
            , _connectionID{ connectionID }
            , _clock{}
            , _client{}
            , _tick{ 0 }
        {
            world._peers.connectInWorld( connectionID, accountID );
            _upstream.connect( 0 );
            NetEndpointEvent opened;
            opened._kind         = NetEndpointEvent::Kind::Connected;
            opened._connectionID = connectionID;
            world._router.dispatchEvent( opened );
            _upstream.setWorldGeneration( world._peers.getWorldGeneration() );
            SW_REQUIRE_TRUE( _client.initialize( &_upstream, _clock, ReplicationClientConfig{} ) );
        }

        ShooterServerTestClient( const ShooterServerTestClient& )            = delete;
        ShooterServerTestClient& operator=( const ShooterServerTestClient& ) = delete;

        /** @brief 다음 틱의 명령을 보내고 그 틱을 돌려줍니다. */
        uint32 send( const ControlIntent& intent )
        {
            ++_tick;
            SW_EXPECT_TRUE( _client.sendIntent( _tick, intent, {} ) );
            for ( const test::FakeNetPeers::Sent& sent : _upstream.takeSent() )
            {
                NetMessageContext context;
                context._pMessage     = sent._bytes.data();
                context._messageSize  = static_cast<int32>( sent._bytes.size() );
                context._connectionID = _connectionID;
                context._channel      = sent._channel;
                context._kind         = sent._bytes.empty() ? uint8{ 0 } : sent._bytes[0];
                SW_EXPECT_TRUE( _world._router.dispatch( NetPeerRole::Server, context, context._pMessage, context._messageSize ) == NetHandleResult::Handled );
            }
            return _tick;
        }

    private:
        ShooterServerTestWorld& _world;
        test::FakeNetPeers      _upstream;
        int32                   _connectionID;
        NetClock                _clock;
        ReplicationClient       _client;
        uint32                  _tick;
    };
} // namespace sw
