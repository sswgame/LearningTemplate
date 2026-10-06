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
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/NavMeshTestUtil.h"
#include "EngineTest/TestGameObjectMocks.h"

#include "GameFramework/Base/Control/AiControllerComponent.h"
#include "GameFramework/Base/Control/CharacterPawnMovementComponent.h"
#include "GameFramework/Base/Control/ControlSystem.h"
#include "GameFramework/Base/Control/PawnComponent.h"
#include "GameFramework/Base/Control/PlayerControllerComponent.h"
#include "GameFramework/Base/Vehicle/MountInteractionComponent.h"
#include "GameFramework/Base/Vehicle/MountUtil.h"
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
