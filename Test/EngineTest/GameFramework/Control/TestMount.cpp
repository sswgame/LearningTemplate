/**
 * @file TestMount.cpp
 * @brief 탑승 — 타기 = 조종자가 빙의를 탈것으로 옮기는 것. 탑승자는 좌석 소켓에 붙어 따라가고, 내리면 하차 자리에서 다시 걷는다. NPC 도 같은 함수로 탄다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsQuery.h"

#include "EngineTest/NavMeshTestUtil.h"
#include "EngineTest/TestGameObjectMocks.h"

#include "GameFramework/Base/Combat/HealthListenerComponent.h"
#include "GameFramework/Base/Control/AiControllerComponent.h"
#include "GameFramework/Base/Control/CharacterPawnMovementComponent.h"
#include "GameFramework/Base/Control/ControlSystem.h"
#include "GameFramework/Base/Control/PawnComponent.h"
#include "GameFramework/Base/Control/PlayerControllerComponent.h"
#include "GameFramework/Base/Vehicle/MountInteractionComponent.h"
#include "GameFramework/Base/Vehicle/MountUtil.h"
#include "GameFramework/Base/Vehicle/RiderDownWatcherComponent.h"
#include "GameFramework/Base/Vehicle/VehicleExitComponent.h"
#include "GameFramework/Base/Vehicle/VehicleSeatComponent.h"

#include "TestFramework/TestFramework.h"

#include <string>

namespace sw
{
    /** @brief 시험용 탈것 이동 — 의도의 월드 이동 × 10 m/s 로 루트를 옮긴다(물리 없음). 말 · 차 이동 규칙의 자리를 가장 작게 채운다. */
    class MountTestVehicleMoverComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr float32 kSpeed = 10.0f;

        MountTestVehicleMoverComponent() { setCanEverTick( true ); }

        const TypeInfo* getTypeInfo() const override { return StaticType(); }

        void onTick( float32 deltaTime ) override
        {
            Component::onTick( deltaTime );
            GameObject*          pOwner = getOwner();
            const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
            SceneComponent*      pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
            if ( pPawn != nullptr && pScene != nullptr )
                pScene->setWorldPosition( pScene->getWorldPosition() + pPawn->getIntent().computeWorldMove() * ( kSpeed * deltaTime ) );
        }
    };

    inline const TypeInfo* MountTestVehicleMoverComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MountTestVehicleMoverComponent>, hashed_string( "MountTestVehicleMoverComponent" ),
                                          hashed_string( "sw::MountTestVehicleMoverComponent" ), sizeof( MountTestVehicleMoverComponent ) );
    }
} // namespace sw

using namespace sw;

namespace
{
    struct MountTestInternal
    {
        static constexpr float32 kDeltaTime = 1.0f / 60.0f;

        /** @brief 강체 바닥(60 × 60, 윗면 y = 0)입니다. */
        static void spawnFloor( GameObjectManager& manager )
        {
            navtest::spawnStaticBody( manager, "Floor", float3{ 0.0f, -0.5f, 0.0f }, float3{ 30.0f, 0.5f, 30.0f } );
        }

        /** @brief 걷는 탑승자 — 캐릭터 컨트롤러 · 폰(버튼 Interact) · 폰 이동 · 타기 버튼입니다. */
        static GameObject* spawnRider( GameObjectManager& manager, const utf8* pName, const float3& feet )
        {
            GameObject*                   pObject     = manager.createGameObject( hashed_string( pName ) );
            CharacterControllerComponent* pController = pObject->addComponent<CharacterControllerComponent>();
            pController->setLocalPosition( feet );
            PawnComponent* pPawn = pObject->addComponent<PawnComponent>();
            pPawn->setButtonNames( vector<hashed_string>{ hashed_string( "Interact" ) } );
            pObject->addComponent<CharacterPawnMovementComponent>();
            pObject->addComponent<MountInteractionComponent>();
            return pObject;
        }

        /** @brief 상자 차 — 루트 · 폰(버튼 Exit) · 운전석(위 1 m, 하차 왼쪽 2 m) · 내리기 버튼 · 시험 이동(10 m/s)입니다. */
        static GameObject* spawnVehicle( GameObjectManager& manager, const utf8* pName, const float3& position )
        {
            GameObject*     pObject = manager.createGameObject( hashed_string( pName ) );
            SceneComponent* pRoot   = pObject->addComponent<SceneComponent>();
            pRoot->setWorldPosition( position );
            PawnComponent* pPawn = pObject->addComponent<PawnComponent>();
            pPawn->setButtonNames( vector<hashed_string>{ hashed_string( "Exit" ) } );
            VehicleSeatComponent* pSeat = pObject->addComponent<VehicleSeatComponent>();
            pSeat->setSeatOffset( float3{ 0.0f, 1.0f, 0.0f } );
            pSeat->setExitOffset( float3{ -2.0f, 0.0f, 0.0f } );
            pObject->addComponent<VehicleExitComponent>();
            pObject->addComponent<MountTestVehicleMoverComponent>();
            return pObject;
        }

        static void bindKeys( InputManager& input )
        {
            InputMap& inputMap = input.getInputMap();
            inputMap.bindVector2D( "Move", Key::W, Key::S, Key::A, Key::D );
            inputMap.bind( "Interact", Key::E );
            inputMap.bind( "Exit", Key::F );
        }

        static void tick( GameObjectManager& manager, InputManager& input )
        {
            input.beginFrame( kDeltaTime );
            manager.tick( kDeltaTime );
            input.endFrame();
        }

        /** @brief 키를 한 프레임 눌렀다 뗍니다(누른 프레임에 발동). */
        static void tapKey( GameObjectManager& manager, InputManager& input, Key key )
        {
            (void)input.postRawEvent( RawInputEvent::makeKeyDown( key ) );
            tick( manager, input );
            (void)input.postRawEvent( RawInputEvent::makeKeyUp( key ) );
            tick( manager, input );
        }

        static float3 findPosition( const GameObject& object ) { return object.getPrimarySceneComponent()->getWorldPosition(); }
    };
} // namespace

/**
 * @brief [MountTest] 플레이어가 걷는 폰으로 차에 다가가 Interact(E) → 빙의가 차로, 탑승자는 좌석 자리에서 차를 따라가고, Exit(F) → 빙의가 탑승자로, 하차 자리에 서고 차는 선다
 */
SW_TEST_CASE( MountTest, MountAndDismountRoundTrip )
{
    using Internal = MountTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    Internal::bindKeys( input );
    {
        GameObjectManager manager;
        Internal::spawnFloor( manager );
        GameObject* pRider   = Internal::spawnRider( manager, "Rider", float3{ 0.0f, 0.05f, 0.0f } );
        GameObject* pVehicle = Internal::spawnVehicle( manager, "Car", float3{ 1.5f, 0.0f, 0.0f } );
        auto*       pPlayer  = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();
        pPlayer->possess( *pRider->getComponent<PawnComponent>() );
        for ( uint32 frame = 0; frame < 5; ++frame )
            Internal::tick( manager, input );

        Internal::tapKey( manager, input, Key::E );
        SW_ASSERT_TRUE( pPlayer->getPawn() == pVehicle->getComponent<PawnComponent>()->getHandle() );
        SW_EXPECT_FALSE( pRider->getComponent<PawnComponent>()->isPossessed() );
        SW_EXPECT_TRUE( MountUtil::findSeatOf( *pRider->getComponent<PawnComponent>() ) == pVehicle->getComponent<VehicleSeatComponent>() );
        SW_EXPECT_TRUE( pRider->getComponent<CharacterPawnMovementComponent>()->isSuspended() );
        SW_EXPECT_TRUE( pRider->getComponent<SocketBindingComponent>()->getState() == SocketBindingState::Bound );
        const float3 seated = Internal::findPosition( *pRider );
        SW_EXPECT_NEAR_EQUAL( 1.5f, seated._x, 1.0e-3f );
        SW_EXPECT_NEAR_EQUAL( 1.0f, seated._y, 1.0e-3f );

        // 30 프레임 W → 차가 앞으로 5 m, 탑승자도 같이(계층).
        const float3 carStart = Internal::findPosition( *pVehicle );
        (void)input.postRawEvent( RawInputEvent::makeKeyDown( Key::W ) );
        for ( uint32 frame = 0; frame < 30; ++frame )
            Internal::tick( manager, input );
        (void)input.postRawEvent( RawInputEvent::makeKeyUp( Key::W ) );
        Internal::tick( manager, input );
        const float3 carMoved = Internal::findPosition( *pVehicle );
        SW_EXPECT_NEAR_EQUAL( 5.0f, carMoved._z - carStart._z, 1.0e-3f );
        SW_EXPECT_NEAR_EQUAL( carMoved._z, Internal::findPosition( *pRider )._z, 1.0e-3f );

        Internal::tapKey( manager, input, Key::F );
        SW_EXPECT_TRUE( pPlayer->getPawn() == pRider->getComponent<PawnComponent>()->getHandle() );
        SW_EXPECT_FALSE( pVehicle->getComponent<PawnComponent>()->isPossessed() );
        SW_EXPECT_TRUE( pVehicle->getComponent<VehicleSeatComponent>()->isFree() );
        SW_EXPECT_FALSE( pRider->getComponent<CharacterPawnMovementComponent>()->isSuspended() );
        const float3 standing = Internal::findPosition( *pRider );
        SW_EXPECT_TRUE_MSG( MathUtil::abs( standing._x - ( carMoved._x - 2.0f ) ) < 0.1f && MathUtil::abs( standing._z - carMoved._z ) < 0.1f,
                            ( "rider stands at " + std::to_string( standing._x ) + ", " + std::to_string( standing._z ) ).c_str() );
        // 놓인 차는 선다.
        Internal::tick( manager, input );
        const float3 parked = Internal::findPosition( *pVehicle );
        Internal::tick( manager, input );
        SW_EXPECT_NEAR_EQUAL( parked._z, Internal::findPosition( *pVehicle )._z, 1.0e-6f );
    }
    input.shutdown();
}

/**
 * @brief [MountTest] NPC 도 같은 함수로 탄다 — AI 조종자가 탄 차와 플레이어가 탄 차가 같은 의도(곧장 앞)로 매 프레임 같은 자리를 간다
 */
SW_TEST_CASE( MountTest, NpcMountsThroughTheSameFunction )
{
    using Internal = MountTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    Internal::bindKeys( input );
    {
        GameObjectManager manager;
        Internal::spawnFloor( manager );
        GameObject* pPlayerRider = Internal::spawnRider( manager, "PlayerRider", float3{ -5.0f, 0.05f, 0.0f } );
        GameObject* pNpcRider    = Internal::spawnRider( manager, "NpcRider", float3{ 5.0f, 0.05f, 0.0f } );
        GameObject* pPlayerCar   = Internal::spawnVehicle( manager, "PlayerCar", float3{ -4.0f, 0.0f, 0.0f } );
        GameObject* pNpcCar      = Internal::spawnVehicle( manager, "NpcCar", float3{ 6.0f, 0.0f, 0.0f } );
        auto*       pPlayer      = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        auto*       pNpc         = manager.createGameObject( hashed_string( "Npc" ) )->addComponent<AiControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();
        pPlayer->possess( *pPlayerRider->getComponent<PawnComponent>() );
        pNpc->possess( *pNpcRider->getComponent<PawnComponent>() );
        Internal::tick( manager, input );

        SW_ASSERT_TRUE( MountUtil::mount( *pPlayerRider->getComponent<PawnComponent>(), *pPlayerCar->getComponent<VehicleSeatComponent>() ) == MountResult::Mounted );
        SW_ASSERT_TRUE( MountUtil::mount( *pNpcRider->getComponent<PawnComponent>(), *pNpcCar->getComponent<VehicleSeatComponent>() ) == MountResult::Mounted );
        SW_EXPECT_TRUE( pNpc->getPawn() == pNpcCar->getComponent<PawnComponent>()->getHandle() );
        pNpc->moveTo( float3{ 6.0f, 0.0f, 1000.0f } );
        (void)input.postRawEvent( RawInputEvent::makeKeyDown( Key::W ) );
        for ( uint32 frame = 0; frame < 30; ++frame )
        {
            Internal::tick( manager, input );
            const float32 playerZ = Internal::findPosition( *pPlayerCar )._z;
            const float32 npcZ    = Internal::findPosition( *pNpcCar )._z;
            SW_EXPECT_TRUE_MSG( playerZ == npcZ, ( "frame " + std::to_string( frame ) + " player " + std::to_string( playerZ ) + " npc " + std::to_string( npcZ ) ).c_str() );
        }
        SW_EXPECT_NEAR_EQUAL( 5.0f, Internal::findPosition( *pNpcCar )._z, 1.0e-3f );
        SW_EXPECT_NEAR_EQUAL( Internal::findPosition( *pNpcCar )._z, Internal::findPosition( *pNpcRider )._z, 1.0e-3f );
    }
    input.shutdown();
}

/**
 * @brief [MountTest] 앉은 좌석 · 먼 좌석 · 조종자 없는 탑승자의 운전석은 거절하고 아무것도 바꾸지 않는다
 */
SW_TEST_CASE( MountTest, SeatTakenAndTooFarAreRefused )
{
    using Internal = MountTestInternal;
    GameObjectManager manager;
    Internal::spawnFloor( manager );
    GameObject* pFirst    = Internal::spawnRider( manager, "First", float3{ 0.0f, 0.05f, 0.0f } );
    GameObject* pSecond   = Internal::spawnRider( manager, "Second", float3{ 0.0f, 0.05f, 2.0f } );
    GameObject* pFar      = Internal::spawnRider( manager, "Far", float3{ 20.0f, 0.05f, 0.0f } );
    GameObject* pVehicle  = Internal::spawnVehicle( manager, "Car", float3{ 1.0f, 0.0f, 0.0f } );
    auto*       pFirstAi  = manager.createGameObject( hashed_string( "FirstAi" ) )->addComponent<AiControllerComponent>();
    auto*       pSecondAi = manager.createGameObject( hashed_string( "SecondAi" ) )->addComponent<AiControllerComponent>();
    auto*       pFarAi    = manager.createGameObject( hashed_string( "FarAi" ) )->addComponent<AiControllerComponent>();
    manager.beginPlay();
    pFirstAi->possess( *pFirst->getComponent<PawnComponent>() );
    pSecondAi->possess( *pSecond->getComponent<PawnComponent>() );
    pFarAi->possess( *pFar->getComponent<PawnComponent>() );
    manager.tick( Internal::kDeltaTime );
    VehicleSeatComponent& seat = *pVehicle->getComponent<VehicleSeatComponent>();

    // 조종자 없는 탑승자는 운전석에 앉지 못한다(넘길 빙의가 없다).
    pFirstAi->unpossess();
    SW_EXPECT_TRUE( MountUtil::mount( *pFirst->getComponent<PawnComponent>(), seat ) == MountResult::NotPossessed );
    SW_EXPECT_TRUE( seat.isFree() );
    pFirstAi->possess( *pFirst->getComponent<PawnComponent>() );

    SW_EXPECT_TRUE( MountUtil::mount( *pFar->getComponent<PawnComponent>(), seat ) == MountResult::TooFar );
    SW_EXPECT_TRUE( MountUtil::findNearestFreeSeat( *pFar->getComponent<PawnComponent>() ) == nullptr );
    SW_EXPECT_TRUE( MountUtil::findNearestFreeSeat( *pFirst->getComponent<PawnComponent>() ) == &seat );
    SW_ASSERT_TRUE( MountUtil::mount( *pFirst->getComponent<PawnComponent>(), seat ) == MountResult::Mounted );
    SW_EXPECT_TRUE( MountUtil::mount( *pSecond->getComponent<PawnComponent>(), seat ) == MountResult::SeatTaken );
    SW_EXPECT_TRUE( pSecondAi->getPawn() == pSecond->getComponent<PawnComponent>()->getHandle() );
    SW_EXPECT_FALSE( MountUtil::dismount( *pSecond->getComponent<PawnComponent>(), false ) );
    SW_EXPECT_TRUE( MountUtil::dismount( *pFirst->getComponent<PawnComponent>(), false ) );
    SW_EXPECT_TRUE( pFirstAi->getPawn() == pFirst->getComponent<PawnComponent>()->getHandle() );
}

/**
 * @brief [MountTest] 탑승 중에도 탑승자의 히트박스는 소켓 계층을 따라가 그대로 맞는다(탑승 중 무적 없음) — 차가 움직인 뒤 탑승자 자리로 쏜 광선이 탑승자 히트박스에 닿는다
 */
SW_TEST_CASE( MountTest, RiderStillTakesHitsWhileMounted )
{
    using Internal = MountTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    Internal::bindKeys( input );
    {
        GameObjectManager manager;
        Internal::spawnFloor( manager );
        GameObject* pRider   = Internal::spawnRider( manager, "Rider", float3{ 0.0f, 0.05f, 0.0f } );
        GameObject* pVehicle = Internal::spawnVehicle( manager, "Car", float3{ 1.5f, 0.0f, 0.0f } );
        // 히트박스 — 탑승자 루트 아래 키네마틱 상자(애니메이션을 따르는 히트박스와 같은 종류).
        GameObject*         pHitbox = manager.createGameObject( hashed_string( "RiderHitbox" ) );
        RigidBodyComponent* pBody   = pHitbox->addComponent<RigidBodyComponent>();
        PhysicsShapeDesc3D  box;
        box._halfExtents = float3{ 0.3f, 0.9f, 0.3f };
        pBody->setShape( box );
        pBody->setBodyType( PhysicsBodyType::Kinematic );
        pBody->setLocalPosition( float3{ 0.0f, 0.9f, 0.0f } );
        SW_ASSERT_TRUE( pBody->attachToComponent( pRider->getPrimarySceneComponent() ) );
        auto* pPlayer = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();
        pPlayer->possess( *pRider->getComponent<PawnComponent>() );
        Internal::tick( manager, input );
        SW_ASSERT_TRUE( MountUtil::mount( *pRider->getComponent<PawnComponent>(), *pVehicle->getComponent<VehicleSeatComponent>() ) == MountResult::Mounted );

        (void)input.postRawEvent( RawInputEvent::makeKeyDown( Key::W ) );
        for ( uint32 frame = 0; frame < 30; ++frame )
            Internal::tick( manager, input );
        const float3           feet   = Internal::findPosition( *pRider );
        const IPhysicsScene3D* pScene = manager.getScenePhysics().findScene3D();
        SW_ASSERT_NOT_NULL( pScene );
        IPhysicsScene3D::CastHit hit;
        const bool               bHit = pScene->raycast( float3{ feet._x - 3.0f, feet._y + 0.9f, feet._z }, float3{ 1.0f, 0.0f, 0.0f }, 6.0f, PhysicsQueryFilter{}, hit );
        SW_ASSERT_TRUE( bHit );
        SW_EXPECT_EQUAL( pHitbox->getObjectId(), hit._userData );
        SW_EXPECT_TRUE( feet._z > 4.0f );
    }
    input.shutdown();
}

/**
 * @brief [MountTest] 탑승자가 쓰러지면(체력 신호 Died) 하차 자리를 찾지 않고 지금 자리에서 강제로 내리고, 조종자가 탑승자를 다시 쥔다
 */
SW_TEST_CASE( MountTest, ForcedDismountWhenTheRiderGoesDown )
{
    using Internal = MountTestInternal;
    GameObjectManager manager;
    Internal::spawnFloor( manager );
    GameObject* pRider   = Internal::spawnRider( manager, "Rider", float3{ 0.0f, 0.05f, 0.0f } );
    GameObject* pVehicle = Internal::spawnVehicle( manager, "Horse", float3{ 1.5f, 0.0f, 0.0f } );
    auto*       pAi      = manager.createGameObject( hashed_string( "Ai" ) )->addComponent<AiControllerComponent>();
    manager.beginPlay();
    pAi->possess( *pRider->getComponent<PawnComponent>() );
    manager.tick( Internal::kDeltaTime );
    SW_ASSERT_TRUE( MountUtil::mount( *pRider->getComponent<PawnComponent>(), *pVehicle->getComponent<VehicleSeatComponent>() ) == MountResult::Mounted );
    SW_EXPECT_NOT_NULL( pRider->getComponent<RiderDownWatcherComponent>() );
    manager.tick( Internal::kDeltaTime );
    const float3 seated = Internal::findPosition( *pRider );

    HealthChangedEvent hurt;
    hurt._ratio = 0.4f;
    HealthListenerComponent::broadcast( *pRider, hurt );
    SW_EXPECT_FALSE( pVehicle->getComponent<VehicleSeatComponent>()->isFree() ); // 다친 것만으로는 내리지 않는다

    HealthChangedEvent down;
    down._ratio = 0.0f;
    down._kind  = HealthChangeKind::Died;
    HealthListenerComponent::broadcast( *pRider, down );
    SW_EXPECT_TRUE( pVehicle->getComponent<VehicleSeatComponent>()->isFree() );
    SW_EXPECT_TRUE( pAi->getPawn() == pRider->getComponent<PawnComponent>()->getHandle() );
    const float3 dropped = Internal::findPosition( *pRider );
    SW_EXPECT_NEAR_EQUAL( seated._x, dropped._x, 1.0e-3f ); // 하차 자리(왼쪽 2 m)가 아니라 그 자리
    SW_EXPECT_NEAR_EQUAL( seated._z, dropped._z, 1.0e-3f );
}

/**
 * @brief [MountTest] 폰의 연결은 빙의를 따라간다 — 운전석에 타면 탈것의 의도 연결이 운전석 조종자의 것이 되고, 승객은 빙의를 옮기지 않으며, 내리면 돌아온다
 */
SW_TEST_CASE( MountTest, InputPeerFollowsTheDriver )
{
    using Internal = MountTestInternal;
    GameObjectManager manager;
    Internal::spawnFloor( manager );
    GameObject*           pDriver    = Internal::spawnRider( manager, "Driver", float3{ 0.0f, 0.05f, 0.0f } );
    GameObject*           pPassenger = Internal::spawnRider( manager, "Passenger", float3{ 0.0f, 0.05f, 1.0f } );
    GameObject*           pVehicle   = Internal::spawnVehicle( manager, "Car", float3{ 1.5f, 0.0f, 0.0f } );
    VehicleSeatComponent* pBackSeat  = pVehicle->addComponent<VehicleSeatComponent>();
    pBackSeat->setDriverSeat( false );
    pBackSeat->setSeatOffset( float3{ 0.0f, 1.0f, -1.0f } );
    auto* pDriverRemote    = manager.createGameObject( hashed_string( "DriverRemote" ) )->addComponent<AiControllerComponent>();
    auto* pPassengerRemote = manager.createGameObject( hashed_string( "PassengerRemote" ) )->addComponent<AiControllerComponent>();
    pDriverRemote->setInputPeer( 7 );
    pPassengerRemote->setInputPeer( 9 );
    manager.beginPlay();
    PawnComponent& driverPawn    = *pDriver->getComponent<PawnComponent>();
    PawnComponent& passengerPawn = *pPassenger->getComponent<PawnComponent>();
    PawnComponent& vehiclePawn   = *pVehicle->getComponent<PawnComponent>();
    pDriverRemote->possess( driverPawn );
    pPassengerRemote->possess( passengerPawn );
    SW_EXPECT_EQUAL( 7u, driverPawn.getInputPeer() );
    SW_EXPECT_EQUAL( 0u, vehiclePawn.getInputPeer() );

    SW_ASSERT_TRUE( MountUtil::mount( driverPawn, *pVehicle->getComponent<VehicleSeatComponent>() ) == MountResult::Mounted );
    SW_EXPECT_EQUAL( 7u, vehiclePawn.getInputPeer() );
    SW_EXPECT_EQUAL( 0u, driverPawn.getInputPeer() );
    SW_ASSERT_TRUE( MountUtil::mount( passengerPawn, *pBackSeat ) == MountResult::Mounted );
    SW_EXPECT_EQUAL( 7u, vehiclePawn.getInputPeer() );
    SW_EXPECT_EQUAL( 9u, passengerPawn.getInputPeer() );

    SW_EXPECT_TRUE( MountUtil::dismount( driverPawn, false ) );
    SW_EXPECT_EQUAL( 0u, vehiclePawn.getInputPeer() );
    SW_EXPECT_EQUAL( 7u, driverPawn.getInputPeer() );
}
