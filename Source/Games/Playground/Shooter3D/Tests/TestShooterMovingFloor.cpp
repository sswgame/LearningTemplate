// ShooterFloorStepTest — 움직이는 바닥의 걸음 계산: 실려 가기(직선 · 회전 · 승강), 올라타기 · 내려서기 · 옮겨 타기 · 사라짐.
// ShooterMovingFloorNetTest — 지연 · 손실 회선의 서버 하나와 클라이언트 둘: 바닥 위 예측이 맞고, 남의 몸이 그 화면의 바닥에 붙어 보이며, 늦게 들어와도 같다.
// ShooterFloorRewindTest — 바닥 위의 사격은 쏜 사람이 본 바닥 위의 자리로 판정한다. 모두 nogpu · 에셋 없음 · 소켓 없음(루프백 망).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/InterpToMovementComponent.h"

#include "Games/Playground/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Playground/Shooter3D/Tests/ShooterServerTestWorld.h"

using namespace sw;

namespace
{
    constexpr float32 kFloorTestStep = 1.0f / 30.0f;

    ShooterFloorStep makeFloorStep( const float3& from, const float3& to, const float3& halfSize, float32 fromYaw = 0.0f, float32 toYaw = 0.0f )
    {
        ShooterFloorStep step;
        step._fromPosition = from;
        step._toPosition   = to;
        step._halfSize     = halfSize;
        step._fromYaw      = fromYaw;
        step._toYaw        = toYaw;
        return step;
    }

    /** @brief 몸 이동 하나로 걸음을 돌립니다(씬 없음 — 걸음 계산은 순수하다). */
    struct FloorWalker
    {
        ShooterBodyMovementComponent _movement{};
        ShooterMoveState             _state{};
        vector<ShooterArenaBox>      _listBox{};

        void step( const vector<ShooterFloorStep>& listFloor, const float3& move = float3{}, bool bJump = false )
        {
            ShooterMoveInput input;
            input._move  = move;
            input._bJump = bJump ? SW_TRUE : SW_FALSE;
            bool bLanded = false;
            _state       = _movement.simulate( _listBox, listFloor, _state, input, kFloorTestStep, bLanded );
        }
    };
} // namespace

/** @brief [ShooterFloorStepTest] 선 몸은 바닥의 이동 · 회전 · 승강만큼 실려 가고, 뜬 몸은 실려 가지 않는다 */
SW_TEST_CASE( ShooterFloorStepTest, StandingBodiesAreCarried )
{
    const float3 halfSize{ 2.0f, 0.25f, 2.0f };
    FloorWalker  walker;
    walker._state._position = float3{ 1.0f, 0.25f, 0.0f };
    walker.step( { makeFloorStep( float3{}, float3{ 0.2f, 0.0f, 0.0f }, halfSize ) } );
    SW_EXPECT_NEAR( 1.2f, walker._state._position._x, 1.0e-4f );
    SW_EXPECT_NEAR( 0.25f, walker._state._position._y, 1.0e-4f );
    SW_EXPECT_TRUE( walker._state._bOnGround == SW_TRUE );

    walker._state._position = float3{ 1.0f, 0.25f, 0.0f };
    walker.step( { makeFloorStep( float3{}, float3{}, halfSize, 0.0f, MathUtil::kHalfPi ) } ); // 요 90° — +X 가 −Z 로 간다
    SW_EXPECT_NEAR( 0.0f, walker._state._position._x, 1.0e-4f );
    SW_EXPECT_NEAR( -1.0f, walker._state._position._z, 1.0e-4f );

    walker._state._position = float3{ 1.0f, 0.25f, 0.0f };
    walker.step( { makeFloorStep( float3{}, float3{ 0.0f, 0.4f, 0.0f }, halfSize ) } ); // 승강 — 한 걸음에 올라설 높이보다 많이 올라도 실려 간다
    SW_EXPECT_NEAR( 0.65f, walker._state._position._y, 1.0e-4f );
    walker.step( { makeFloorStep( float3{ 0.0f, 0.4f, 0.0f }, float3{ 0.0f, 0.1f, 0.0f }, halfSize ) } );
    SW_EXPECT_NEAR( 0.35f, walker._state._position._y, 1.0e-4f );
    SW_EXPECT_TRUE( walker._state._bOnGround == SW_TRUE );

    walker.step( { makeFloorStep( float3{ 0.0f, 0.1f, 0.0f }, float3{ 0.0f, 0.1f, 0.0f }, halfSize ) }, float3{}, true );
    const float32 airborneX = walker._state._position._x;
    walker.step( { makeFloorStep( float3{ 0.0f, 0.1f, 0.0f }, float3{ 0.5f, 0.1f, 0.0f }, halfSize ) } );
    SW_EXPECT_NEAR( airborneX, walker._state._position._x, 1.0e-4f );
    SW_EXPECT_TRUE( walker._state._bOnGround == SW_FALSE );
}

/** @brief [ShooterFloorStepTest] 낮은 바닥에 걸어 올라서고, 옆 바닥으로 옮겨 타고, 끝에서 내려서며, 바닥이 사라지면 떨어져 아레나 바닥에 선다 */
SW_TEST_CASE( ShooterFloorStepTest, BodiesStepOnAcrossAndOff )
{
    const float3                   halfSize{ 1.0f, 0.25f, 1.0f };
    const vector<ShooterFloorStep> listFloor{ makeFloorStep( float3{ 0.0f, 0.0f, 2.0f }, float3{ 0.0f, 0.0f, 2.0f }, halfSize ), makeFloorStep( float3{ 0.0f, 0.0f, 4.0f }, float3{ 0.0f, 0.0f, 4.0f }, halfSize ) };
    FloorWalker                    walker;
    uint8                          highestNumber = 0;
    bool                           bFirst        = false;
    for ( int32 step = 0; step < 20; ++step )
    {
        walker.step( listFloor, float3{ 0.0f, 0.0f, 1.0f } ); // 초당 5.5 m — 걸음마다 0.18 m
        const uint8 number = ShooterFloorStep::findNumber( listFloor, walker._state._position );
        bFirst             = bFirst || number == 1;
        highestNumber      = MathUtil::max( highestNumber, number );
    }
    SW_EXPECT_TRUE( bFirst );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( highestNumber ) );
    SW_EXPECT_NEAR( 0.25f, walker._state._position._y, 1.0e-4f ); // 둘째 바닥 위다

    for ( int32 step = 0; step < 30; ++step )
    {
        walker.step( listFloor, float3{ 0.0f, 0.0f, 1.0f } );
    }
    SW_EXPECT_NEAR( 0.0f, walker._state._position._y, 1.0e-4f ); // 끝에서 내려섰다
    SW_EXPECT_TRUE( walker._state._bOnGround == SW_TRUE );

    walker._state                 = ShooterMoveState{};
    walker._state._position       = float3{ 0.0f, 1.25f, 2.0f };
    vector<ShooterFloorStep> tall = { makeFloorStep( float3{ 0.0f, 1.0f, 2.0f }, float3{ 0.0f, 1.0f, 2.0f }, halfSize ) };
    walker.step( tall );
    SW_EXPECT_NEAR( 1.25f, walker._state._position._y, 1.0e-4f );
    tall.front()._halfSize = float3{}; // 사라졌다 — 번호는 남는다
    walker.step( tall );
    SW_EXPECT_TRUE( walker._state._bOnGround == SW_FALSE );
    for ( int32 step = 0; step < 30; ++step )
    {
        walker.step( tall );
    }
    SW_EXPECT_NEAR( 0.0f, walker._state._position._y, 1.0e-4f );

    walker._state = ShooterMoveState{}; // 걸어 오를 수 없게 높은 바닥은 밑으로 지나간다
    for ( int32 step = 0; step < 12; ++step )
    {
        walker.step( { makeFloorStep( float3{ 0.0f, 1.0f, 2.0f }, float3{ 0.0f, 1.0f, 2.0f }, halfSize ) }, float3{ 0.0f, 0.0f, 1.0f } );
    }
    SW_EXPECT_NEAR( 0.0f, walker._state._position._y, 1.0e-4f );
}

namespace
{
    /** @brief 왕복하는 바닥 하나를 세웁니다(시작 자리에서 @p waypoint 까지 2 초). */
    void addFloor( GameObjectManager& manager, const utf8* pName, const float3& position, const float3& halfSize, const float3& waypoint )
    {
        GameObject*         pFloor = manager.createGameObject( hashed_string( pName ) );
        RigidBodyComponent* pBody  = pFloor->addComponent<RigidBodyComponent>();
        PhysicsShapeDesc3D  slab;
        slab._type        = PhysicsShapeType3D::Box;
        slab._halfExtents = halfSize;
        pBody->setShape( slab );
        pBody->setBodyType( PhysicsBodyType::Kinematic );
        pBody->setLocalPosition( position );
        pFloor->addComponent<InterpToMovementComponent>()->setPath( vector<float3>{ waypoint }, 2.0f, true );
    }

    /** @brief 서버 하나와 클라이언트 둘의 사격장 월드입니다. 월드마다 같은 바닥 둘(`FloorA` · 바로 옆의 `FloorB`)이 있고, 몸의 의도는 테스트가 낸다. */
    class ShooterNetRig
    {
    public:
        static constexpr int32   kClientCount = 2;
        static constexpr float32 kFrame       = 1.0f / 60.0f;

        struct Side
        {
            NetMessageRouter              _router{};
            GameObjectManager             _manager{};
            GameObject*                   _pPlayer{ nullptr };
            ShooterNetPawnComponent*      _pNetPawn{ nullptr };
            ShooterBodyMovementComponent* _pMovement{ nullptr };
            RemoteControllerComponent*    _pDriver{ nullptr };
            ControlIntent                 _intent{};
            uint32                        _intentTick{ 0 };
            uint8                         _bStarted{ SW_FALSE };
        };

        explicit ShooterNetRig( const float3& halfSize )
            : _cluster{ 61u }
            , _options{}
            , _weapons{}
            , _projectiles{}
            , _scope{}
            , _gameScope{}
            , _arrSide{}
            , _enemiesOff{ 1 }
            , _halfSize{ halfSize }
        {
            _options._pEnemiesOff = &_enemiesOff;
            SW_REQUIRE_TRUE( _scope.bind<ShooterAutoPlayOptions>( &_options ) );
            SW_REQUIRE_TRUE( ShooterServerTestWorld::loadWeapons( _weapons, _projectiles ) );
            SW_REQUIRE_TRUE( _scope.bind<const WeaponCatalog>( &_weapons ) && _scope.bind<const ProjectileCatalog>( &_projectiles ) );
            _gameScope = make_unique<game::ScopedGameScope>( _scope );
            for ( int32 endpointIndex = 0; endpointIndex <= kClientCount; ++endpointIndex )
            {
                SW_REQUIRE_TRUE( _cluster.addEndpoint( static_cast<uint16>( 4800 + endpointIndex ), NetEndpointConfig{} ).has_value() );
            }
            SW_REQUIRE_TRUE( _cluster.connectClients( 60 ) );
            NetEmulationConditions conditions; // 한쪽 60 ms · 흔들림 10 ms · 손실 3 %
            conditions._latency  = 0.06;
            conditions._jitter   = 0.01;
            conditions._lossRate = 0.03f;
            _cluster.setConditions( conditions );
            for ( Side& side : _arrSide )
            {
                build( side );
            }
            start( 0 );
        }

        ~ShooterNetRig()
        {
            for ( Side& side : _arrSide )
            {
                side._manager.endPlay();
                side._manager.getSubsystem<SceneNetLink>().unbind();
            }
        }

        ShooterNetRig( const ShooterNetRig& )            = delete;
        ShooterNetRig& operator=( const ShooterNetRig& ) = delete;

        /** @brief 월드 @p sideIndex(0 은 서버)의 플레이를 시작합니다. 클라이언트는 이때 서버의 월드에 들어온다. */
        void start( int32 sideIndex )
        {
            Side&        side = _arrSide[sideIndex];
            NetLinkValue link;
            link._pPeers  = &_cluster.getEndpoint( sideIndex );
            link._pRouter = &side._router;
            link._role    = sideIndex == 0 ? NetLinkRole::Server : NetLinkRole::Client;
            SW_REQUIRE_TRUE( side._manager.getSubsystem<SceneNetLink>().bind( link, 0 ) );
            if ( sideIndex != 0 )
            {
                side._pDriver = RemoteControllerComponent::spawnForConnection( side._manager, hashed_string( "Driver" ), 0, *side._pPlayer->getComponent<PawnComponent>() );
                SW_REQUIRE_NOT_NULL( side._pDriver );
            }
            side._manager.beginPlay();
            side._bStarted = SW_TRUE;
        }

        Side& getServer() { return _arrSide[0]; }
        Side& getClient( int32 clientIndex ) { return _arrSide[clientIndex + 1]; }

        void run( float32 seconds )
        {
            const int32 frameCount = static_cast<int32>( seconds / kFrame );
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                runFrame();
            }
        }

        void runFrame()
        {
            _cluster.step( static_cast<float64>( kFrame ) );
            for ( int32 sideIndex = 0; sideIndex <= kClientCount; ++sideIndex )
            {
                Side& side = _arrSide[sideIndex];
                (void)side._router.poll( _cluster.getEndpoint( sideIndex ) );
                if ( side._bStarted == SW_FALSE )
                    continue;
                if ( side._pDriver != nullptr )
                    side._pDriver->receiveIntent( side._intentTick++, side._intent );
                side._manager.update( kFrame );
            }
        }

        /** @brief 서버에서 자리 @p seatIndex 의 몸의 발입니다. */
        float3 getServerFeet( size_t seatIndex )
        {
            vector<GameObjectHandle> listObject;
            getServer()._pNetPawn->collectPlayerObjects( listObject );
            GameObject* pObject = seatIndex < listObject.size() ? getServer()._manager.resolveGameObject( listObject[seatIndex] ) : nullptr;
            return pObject != nullptr ? pObject->getComponent<ShooterBodyMovementComponent>()->getFeetPosition()._value : float3{};
        }

    private:
        void build( Side& side )
        {
            GameObject*               pGameMode   = side._manager.createGameObject( hashed_string( "GameMode" ) );
            ShooterGameModeComponent* pArenaRules = pGameMode->addComponent<ShooterGameModeComponent>();
            addFloor( side._manager, "FloorA", float3{}, _halfSize, float3{ 6.0f, 0.0f, 0.0f } );
            addFloor( side._manager, "FloorB", float3{ 0.0f, 0.0f, _halfSize._z * 2.0f }, _halfSize, float3{ 6.0f, 0.0f, 0.0f } );
            side._pPlayer = side._manager.createGameObject( hashed_string( "Player" ) );
            ShooterServerTestWorld::bindScenePlayer( *pArenaRules, *side._pPlayer );
            PawnComponent* pPawn = side._pPlayer->addComponent<PawnComponent>();
            pPawn->setButtonNames( { hashed_string( "Jump" ), hashed_string( "Fire" ) } );
            pPawn->setAutoPossess( PawnAutoPossess::None );
            side._pMovement = side._pPlayer->addComponent<ShooterBodyMovementComponent>();
            SW_REQUIRE_TRUE( XMLSerializer::deserialize( side._pMovement, *ShooterBodyMovementComponent::StaticType(), "<ShooterBodyMovementComponent _jumpButton=\"Jump\"/>" ) );
            (void)side._pPlayer->addComponent<FirstPersonCameraComponent>();
            ShooterPlayerComponent* pShooter   = side._pPlayer->addComponent<ShooterPlayerComponent>();
            constexpr const utf8*   kPlayerXML = "<ShooterPlayerComponent _spawnPosition=\"0,0.25,0\" _hitboxLayer=\"Default\"><_listWeaponID><item>rifle</item><item>rifle</item><item>rifle</item></_listWeaponID></ShooterPlayerComponent>";
            SW_REQUIRE_TRUE( XMLSerializer::deserialize( pShooter, *ShooterPlayerComponent::StaticType(), kPlayerXML ) );
            pShooter->bindGameMode( pGameMode->getHandle() );
            side._pNetPawn = side._pPlayer->addComponent<ShooterNetPawnComponent>();
            SW_REQUIRE_TRUE( XMLSerializer::deserialize( side._pNetPawn, *ShooterNetPawnComponent::StaticType(), "<ShooterNetPawnComponent><_listGuestSpawn><item>0,0.25,1.5</item></_listGuestSpawn></ShooterNetPawnComponent>" ) );
        }

    private:
        test::LoopbackCluster             _cluster;
        ShooterAutoPlayOptions            _options;
        WeaponCatalog                     _weapons;
        ProjectileCatalog                 _projectiles;
        SubsystemScope                    _scope;
        unique_ptr<game::ScopedGameScope> _gameScope;
        Side                              _arrSide[kClientCount + 1];
        int32                             _enemiesOff;
        float3                            _halfSize;
    };
} // namespace

/** @brief [ShooterMovingFloorNetTest] 지연 · 손실에서도 바닥 위에 선 자기 몸은 보정 없이 실려 가고, 두 화면에서 남의 몸이 서버와 같은 간격으로 붙어 보인다 — 늦게 들어온 쪽도 같다 */
SW_TEST_CASE( ShooterMovingFloorNetTest, RidersStayTogetherOnEveryScreen, ._timeoutSeconds = 120 )
{
    SW_EXPECT_LOG( "Control.IntentButtonNotInInputMap" ); // 클라이언트의 감독이 플레이어 조종자에게 폰을 쥐이는데 테스트에는 입력 맵이 없다
    SW_EXPECT_LOG( "Control.AxisActionNotInInputMap" );
    SW_EXPECT_LOG( "PrefabPool.PrefabPoolCannotSpawn" );
    ShooterNetRig rig{
        float3{ 12.0f, 0.25f, 12.0f }
    };
    rig.start( 1 );
    rig.run( 2.0f );
    rig.start( 2 ); // 늦은 참가
    rig.run( 3.0f );
    SW_REQUIRE_EQUAL( 1u, rig.getClient( 0 )._pNetPawn->getRemotePawnCount() );
    SW_REQUIRE_EQUAL( 1u, rig.getClient( 1 )._pNetPawn->getRemotePawnCount() );
    const uint32 firstCount  = rig.getClient( 0 )._pNetPawn->getCorrectionCount();
    const uint32 secondCount = rig.getClient( 1 )._pNetPawn->getCorrectionCount();
    float3       firstGap{};
    float32      lowestX   = 1000.0f;
    float32      highestX  = -1000.0f;
    float32      worstGap  = 0.0f; // 두 화면이 본 두 사람의 간격 차
    float32      worstSlip = 0.0f; // 한 화면에서 간격이 흔들린 크기
    for ( int32 frame = 0; frame < 240; ++frame )
    {
        rig.runFrame();
        float3 arrGap[ShooterNetRig::kClientCount]{}; // 손님 − 씬의 몸
        for ( int32 clientIndex = 0; clientIndex < ShooterNetRig::kClientCount; ++clientIndex )
        {
            ShooterNetRig::Side&             client = rig.getClient( clientIndex );
            const AbsolutePosition           own    = client._pMovement->getFeetPosition();
            const optional<AbsolutePosition> other  = client._pNetPawn->findNearestRemotePawn( own );
            SW_REQUIRE_TRUE( other.has_value() );
            arrGap[clientIndex] = clientIndex == 0 ? other->_value - own._value : own._value - other->_value;
            SW_EXPECT_NEAR( 0.25f, own._value._y, 0.02f );
        }
        if ( frame == 0 )
            firstGap = arrGap[0];
        worstGap  = MathUtil::max( worstGap, ( arrGap[0] - arrGap[1] ).getLength() );
        worstSlip = MathUtil::max( worstSlip, ( arrGap[0] - firstGap ).getLength() );
        lowestX   = MathUtil::min( lowestX, rig.getClient( 0 )._pMovement->getFeetPosition()._value._x );
        highestX  = MathUtil::max( highestX, rig.getClient( 0 )._pMovement->getFeetPosition()._value._x );
    }
    SW_EXPECT_TRUE( highestX - lowestX > 4.0f ); // 바닥이 오가는 만큼 실려 갔다
    SW_EXPECT_TRUE( worstGap < 0.1f );           // 절대 자리로 보내면 보간 지연만큼(3 m/s x 0.2 초 = 0.6 m) 서로 다르게 본다
    SW_EXPECT_TRUE( worstSlip < 0.1f );
    SW_EXPECT_TRUE( rig.getClient( 0 )._pNetPawn->getCorrectionCount() <= firstCount + 1u );
    SW_EXPECT_TRUE( rig.getClient( 1 )._pNetPawn->getCorrectionCount() <= secondCount + 1u );
}

/** @brief [ShooterMovingFloorNetTest] 바닥에서 옆 바닥으로 옮겨 타고 땅으로 뛰어내렸다가 다시 뛰어 올라타도 예측이 서버와 맞는다 */
SW_TEST_CASE( ShooterMovingFloorNetTest, CrossingAndJumpingStayPredicted, ._timeoutSeconds = 120 )
{
    SW_EXPECT_LOG( "Control.IntentButtonNotInInputMap" ); // 클라이언트의 감독이 플레이어 조종자에게 폰을 쥐이는데 테스트에는 입력 맵이 없다
    SW_EXPECT_LOG( "Control.AxisActionNotInInputMap" );
    SW_EXPECT_LOG( "PrefabPool.PrefabPoolCannotSpawn" );
    ShooterNetRig rig{
        float3{ 8.0f, 0.25f, 2.0f }
    };
    rig.start( 1 );
    rig.run( 2.0f );
    ShooterNetRig::Side& client      = rig.getClient( 0 );
    const uint32         beforeCount = client._pNetPawn->getCorrectionCount();
    const auto           walk        = [&rig, &client]( float32 forward, bool bJump, float32 seconds )
    {
        client._intent       = ControlIntent{};
        client._intent._move = float2{ 0.0f, forward };
        if ( bJump )
        {
            client._intent.addTrigger( 0, 0.0f, 0.0f, ControlViewTick::makePresent() );
            rig.runFrame();
            client._intent._listTrigger.clear();
        }
        rig.run( seconds );
    };
    walk( 1.0f, false, 0.9f ); // FloorB 로 옮겨 탄다(초당 5.5 m)
    SW_EXPECT_NEAR( 0.25f, client._pMovement->getFeetPosition()._value._y, 0.02f );
    walk( 0.0f, false, 0.6f );
    walk( 1.0f, true, 0.8f ); // 뛰어내린다
    walk( 0.0f, false, 1.0f );
    SW_EXPECT_NEAR( 0.0f, client._pMovement->getFeetPosition()._value._y, 0.02f );
    walk( -1.0f, true, 1.1f ); // 다시 뛰어 올라탄다
    walk( 0.0f, false, 1.5f );

    const float3 own    = client._pMovement->getFeetPosition()._value;
    const float3 server = rig.getServerFeet( 0 );
    SW_EXPECT_NEAR( 0.25f, own._y, 0.02f ); // 자리(x)는 바닥을 따라 움직인다 — 서버는 회선 지연만큼 앞의 명령을 풀었다
    SW_EXPECT_NEAR( own._y, server._y, 0.02f );
    SW_EXPECT_NEAR( own._z, server._z, 0.02f );
    SW_EXPECT_TRUE( client._pNetPawn->getCorrectionCount() <= beforeCount + 3u ); // 손실에 몰린 입력이 바닥 틱 차이를 다시 맞출 수 있다
}

/** @brief [ShooterFloorRewindTest] 빠른 바닥 위의 두 사람 — 과거를 보고 쏜 탄도 쏜 사람이 본 바닥 위의 자리를 맞힌다(절대 자리로 되감으면 바닥이 간 만큼 빗나간다) */
SW_TEST_CASE( ShooterFloorRewindTest, ShotsHitTheBodyWhereTheShooterSawItOnTheFloor )
{
    ShooterServerTestWorld::expectHitSound();
    SW_EXPECT_LOG( "PrefabPool.PrefabPoolCannotSpawn" );
    ShooterServerTestWorld world{ ShooterServerTestWorld::WeaponTable::Present };
    game::ScopedGameScope  scopedScope( world._scope );
    addFloor( world._manager, "Floor", float3{ 4.0f, -0.25f, -13.0f }, float3{ 20.0f, 0.25f, 20.0f }, float3{ 8.0f, 0.0f, 0.0f } ); // 윗면이 땅 높이다 — 초당 4 m
    world._manager.beginPlay();
    ShooterServerTestClient target{ world, 3, 11 };
    ShooterServerTestClient shooter{ world, 5, 22 };
    constexpr int32         kFrameCount = 20;
    constexpr float64       kViewLag    = 8.0; // 여덟 틱(약 0.27 초) 앞을 봤다 — 바닥은 그 사이 1 m 갔다
    const auto              makeIntent  = [&world]()
    {
        ControlIntent intent;
        intent._viewTick = ControlViewTick::makeSeen( static_cast<float64>( world.computeServerTick() ) - kViewLag );
        return intent;
    };
    for ( int32 frame = 0; frame < kFrameCount; ++frame )
    {
        (void)target.send( makeIntent() );
        (void)shooter.send( makeIntent() );
        world.advanceFrame( kFloorTestStep );
    }
    ShooterPlayerComponent* pTarget  = world.findPlayer( 0 );
    ShooterPlayerComponent* pShooter = world.findPlayer( 1 );
    SW_REQUIRE_TRUE( pTarget != nullptr && pShooter != nullptr );
    const float3 offset = pTarget->getFeetPosition()._value - pShooter->getFeetPosition()._value; // 둘 다 바닥에 실려 가 간격은 그대로다
    SW_REQUIRE_TRUE( offset._z > 3.0f );
    ControlIntent fire = makeIntent();
    fire.addTrigger( ShooterServerTestWorld::kFireButton, MathUtil::atan2( offset._x, offset._z ), 0.0f, fire._viewTick );
    (void)target.send( makeIntent() );
    (void)shooter.send( fire );
    for ( int32 frame = 0; frame < 3; ++frame )
    {
        world.advanceFrame( kFloorTestStep );
    }
    SW_EXPECT_EQUAL( 1u, pShooter->getShotCount() );
    SW_EXPECT_EQUAL( 1u, pShooter->getHitCount() );
}
