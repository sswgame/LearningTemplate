/**
 * @file TestControl.cpp
 * @brief 조종(빙의) — 폰은 의도만 받는다. 플레이어(키 → 입력 맵) · AI 가 같은 의도를 내면 같은 궤적이고, 빙의가 뷰 타깃 · 입력 레이어를 옮긴다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Network/BitStream.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "GameFramework/Base/Camera/CameraManagerComponent.h"
#include "GameFramework/Base/Control/AiControllerComponent.h"
#include "GameFramework/Base/Control/ControlIntent.h"
#include "GameFramework/Base/Control/ControlSystem.h"
#include "GameFramework/Base/Control/PawnComponent.h"
#include "GameFramework/Base/Control/PlayerControllerComponent.h"

#include "TestFramework/TestFramework.h"

#include <string>

namespace sw
{
    /** @brief 시험용 폰 이동 — 의도의 월드 이동 × 4 m/s 로 자리를 옮긴다(물리 없음). 폰은 의도만 읽는다는 것의 가장 작은 모양이다. */
    class ControlTestMoverComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr float32 kSpeed = 4.0f;

        ControlTestMoverComponent() { setCanEverTick( true ); }

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

    inline const TypeInfo* ControlTestMoverComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<ControlTestMoverComponent>, hashed_string( "ControlTestMoverComponent" ),
                                          hashed_string( "sw::ControlTestMoverComponent" ), sizeof( ControlTestMoverComponent ) );
    }
} // namespace sw

using namespace sw;

namespace
{
    struct ControlTestInternal
    {
        static constexpr float32 kDeltaTime = 1.0f / 60.0f;

        /** @brief 자리 · 폰(버튼 "Jump") · 시험 이동을 가진 폰 오브젝트입니다. */
        static GameObject* spawnPawn( GameObjectManager& manager, const utf8* pName, const float3& position )
        {
            GameObject*     pObject = manager.createGameObject( hashed_string( pName ) );
            SceneComponent* pScene  = pObject->addComponent<SceneComponent>();
            pScene->setWorldPosition( position );
            PawnComponent* pPawn = pObject->addComponent<PawnComponent>();
            pPawn->setButtonNames( vector<hashed_string>{ hashed_string( "Jump" ) } );
            pObject->addComponent<ControlTestMoverComponent>();
            return pObject;
        }

        /** @brief 입력 한 프레임과 씬 한 프레임입니다. */
        static void tick( GameObjectManager& manager, InputManager& input )
        {
            input.beginFrame( kDeltaTime );
            manager.tick( kDeltaTime );
            input.endFrame();
        }

        static float3 findPosition( const GameObject& object ) { return object.getPrimarySceneComponent()->getWorldPosition(); }
    };
} // namespace

/**
 * @brief [ControlTest] 의도가 비트로 왕복한다 — quantize 한 값 = write → read 한 값, 바이트는 상한 안, 모자란 바이트는 false 와 0
 */
SW_TEST_CASE( ControlTest, IntentRoundTripsThroughBits )
{
    ControlIntent intent;
    intent._move         = float2{ 0.7f, -0.33f };
    intent._moveUp       = 0.25f;
    intent._controlYaw   = 4.0f; // π 를 넘는 요는 감아서 싣는다
    intent._controlPitch = -0.6f;
    intent._arrAnalog[0] = 0.7f;
    intent._arrAnalog[3] = -1.5f; // 범위 밖은 자른다
    intent.setButton( 0, true, true );
    intent.setButton( 31, true, false );
    intent.setButton( 32, true, true ); // 범위 밖 — 할 일이 없다
    SW_EXPECT_TRUE( intent.isDown( 31 ) );
    SW_EXPECT_FALSE( intent.wasTriggered( 31 ) );

    BitWriter writer;
    intent.write( writer );
    SW_EXPECT_TRUE( writer.getByteCount() <= ControlIntent::kMaxSerializedBytes );
    ControlIntent decoded;
    BitReader     reader( writer.getBytes().data(), writer.getByteCount() );
    SW_ASSERT_TRUE( decoded.read( reader ) );

    ControlIntent quantized = intent;
    quantized.quantize();
    SW_EXPECT_TRUE( decoded == quantized );
    SW_EXPECT_NEAR_EQUAL( 0.7f, decoded._move._x, 1.0f / ControlIntent::kAxisSteps );
    SW_EXPECT_NEAR_EQUAL( 4.0f - 2.0f * MathUtil::kPi, decoded._controlYaw, 1.0f / ControlIntent::kAngleStepsPerRadian );
    SW_EXPECT_NEAR_EQUAL( -1.0f, decoded._arrAnalog[3], 1.0e-6f );
    SW_EXPECT_TRUE( decoded.isDown( 0 ) && decoded.wasTriggered( 0 ) && decoded.isDown( 31 ) );

    // 한 번 더 양자화해도 그대로다(고정점).
    ControlIntent twice = quantized;
    twice.quantize();
    SW_EXPECT_TRUE( twice == quantized );

    const uint8   arrShort[2] = { 0xFF, 0xFF };
    BitReader     shortReader( arrShort, 2 );
    ControlIntent broken;
    broken._move = float2{ 1.0f, 1.0f };
    SW_EXPECT_FALSE( broken.read( shortReader ) );
    SW_EXPECT_TRUE( broken == ControlIntent{} );
}

/**
 * @brief [ControlTest] 플레이어(키 W → 입력 맵 Move)와 AI(곧장 앞의 목적지)가 같은 의도를 내면 두 폰의 자리가 매 프레임 비트까지 같다
 */
SW_TEST_CASE( ControlTest, PlayerAndAiWithTheSameIntentMoveTheSame )
{
    using Internal = ControlTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getInputMap().bindVector2D( "Move", Key::W, Key::S, Key::A, Key::D );
    SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeKeyDown( Key::W ) ) );

    {
        GameObjectManager manager;
        GameObject*       pPlayerPawn = Internal::spawnPawn( manager, "PlayerPawn", float3{ 0.0f, 0.0f, 0.0f } );
        GameObject*       pAiPawn     = Internal::spawnPawn( manager, "AiPawn", float3{ 10.0f, 0.0f, 0.0f } );
        auto*             pPlayer     = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        auto*             pAi         = manager.createGameObject( hashed_string( "Ai" ) )->addComponent<AiControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();
        pPlayer->possess( *pPlayerPawn->getComponent<PawnComponent>() );
        pAi->possess( *pAiPawn->getComponent<PawnComponent>() );
        pAi->moveTo( float3{ 10.0f, 0.0f, 1000.0f } );

        for ( uint32 frame = 0; frame < 60; ++frame )
        {
            Internal::tick( manager, input );
            const float3 playerPosition = Internal::findPosition( *pPlayerPawn );
            const float3 aiPosition     = Internal::findPosition( *pAiPawn );
            SW_EXPECT_TRUE_MSG( playerPosition._x == aiPosition._x - 10.0f && playerPosition._z == aiPosition._z,
                                ( "frame " + std::to_string( frame ) + " player " + std::to_string( playerPosition._x ) + "," + std::to_string( playerPosition._z ) + " ai " + std::to_string( aiPosition._x ) + "," + std::to_string( aiPosition._z ) ).c_str() );
        }
        // 60 프레임 × 4 m/s × 1/60 s = 4 m 앞(의도는 첫 틱부터 든다).
        SW_EXPECT_NEAR_EQUAL( 4.0f, Internal::findPosition( *pPlayerPawn )._z, 1.0e-3f );
        SW_EXPECT_NEAR_EQUAL( 0.0f, Internal::findPosition( *pPlayerPawn )._x, 1.0e-6f );
    }
    input.shutdown();
}

/**
 * @brief [ControlTest] 빙의를 옮기면 플레이어 카메라 매니저의 뷰 타깃과 입력 레이어가 따라가고, 놓인 폰은 의도 0 이되 조종 회전은 남는다
 */
SW_TEST_CASE( ControlTest, PossessionMovesViewTargetAndInputLayer )
{
    using Internal = ControlTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    // 걷기 바인딩은 걷는 폰의 레이어에 있다 — 레이어 스택이 비지 않으면 스택에 든 레이어(와 늘 켜진 레이어)만 읽힌다.
    input.getInputMap().bindVector2D( "Move", Key::W, Key::S, Key::A, Key::D, 0.0f, "OnFoot" );
    {
        GameObjectManager manager;
        GameObject*       pCameraObject = manager.createGameObject( hashed_string( "PlayerCamera" ) );
        pCameraObject->addComponent<CameraComponent>();
        CameraManagerComponent* pCameraManager = pCameraObject->addComponent<CameraManagerComponent>();
        SW_ASSERT_NOT_NULL( pCameraManager );

        GameObject* pWalker = Internal::spawnPawn( manager, "Walker", float3{} );
        GameObject* pRider  = Internal::spawnPawn( manager, "Rider", float3{ 5.0f, 0.0f, 0.0f } );
        pWalker->getComponent<PawnComponent>()->setInputLayer( "OnFoot" );
        pRider->getComponent<PawnComponent>()->setInputLayer( "Horse" );
        PlayerControllerComponent* pPlayer = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();

        pPlayer->possess( *pWalker->getComponent<PawnComponent>() );
        SW_EXPECT_TRUE( pCameraManager->getViewTarget() == pWalker->getHandle() );
        SW_EXPECT_TRUE( input.getInputMap().getCurrentTopLayer() == "OnFoot" );

        pPlayer->setControlRotation( 0.5f, 0.1f );
        SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeKeyDown( Key::W ) ) );
        Internal::tick( manager, input );
        const ControlIntent& walkerIntent = pWalker->getComponent<PawnComponent>()->getIntent();
        SW_EXPECT_NEAR_EQUAL( 1.0f, walkerIntent._move._y, 1.0e-6f );
        SW_EXPECT_NEAR_EQUAL( 0.5f, walkerIntent._controlYaw, 1.0f / ControlIntent::kAngleStepsPerRadian );

        pPlayer->possess( *pRider->getComponent<PawnComponent>() );
        SW_EXPECT_TRUE( pCameraManager->getViewTarget() == pRider->getHandle() );
        SW_EXPECT_TRUE( input.getInputMap().getCurrentTopLayer() == "Horse" );
        SW_EXPECT_FALSE( pWalker->getComponent<PawnComponent>()->isPossessed() );
        SW_EXPECT_NEAR_EQUAL( 0.0f, walkerIntent._move._y, 1.0e-6f );
        SW_EXPECT_NEAR_EQUAL( 0.5f, walkerIntent._controlYaw, 1.0f / ControlIntent::kAngleStepsPerRadian );
        // 갈아탄 폰은 조종 회전을 그 폰의 값에서 이어 간다(빙의 순간 시점이 튀지 않게).
        SW_EXPECT_NEAR_EQUAL( 0.0f, pPlayer->getControlYaw(), 1.0e-6f );

        pPlayer->unpossess();
        SW_EXPECT_TRUE( input.getInputMap().getCurrentTopLayer() != "Horse" );
        SW_EXPECT_FALSE( pRider->getComponent<PawnComponent>()->isPossessed() );
    }
    input.shutdown();
}

/**
 * @brief [ControlTest] 남이 쥔 폰을 쥐면 그 조종자가 놓는다 — 폰 하나에 조종자 하나, 조종자를 지우면 폰이 풀린다
 */
SW_TEST_CASE( ControlTest, PossessingATakenPawnReleasesTheOtherController )
{
    using Internal = ControlTestInternal;
    GameObjectManager manager;
    GameObject*       pPawnObject = Internal::spawnPawn( manager, "Pawn", float3{} );
    PawnComponent*    pPawn       = pPawnObject->getComponent<PawnComponent>();
    auto*             pFirst      = manager.createGameObject( hashed_string( "First" ) )->addComponent<AiControllerComponent>();
    GameObject*       pSecondObj  = manager.createGameObject( hashed_string( "Second" ) );
    auto*             pSecond     = pSecondObj->addComponent<AiControllerComponent>();

    pFirst->possess( *pPawn );
    SW_EXPECT_TRUE( pPawn->getController() == pFirst->getHandle() );
    pSecond->possess( *pPawn );
    SW_EXPECT_TRUE( pPawn->getController() == pSecond->getHandle() );
    SW_EXPECT_TRUE( pFirst->findPawn() == nullptr );
    SW_EXPECT_TRUE( pSecond->findPawn() == pPawn );

    manager.destroyObject( pSecondObj );
    manager.processDeferredDestruction();
    SW_EXPECT_FALSE( pPawn->isPossessed() );
}

/**
 * @brief [ControlTest] 자동 빙의 — Player0 은 시작 뒤 첫 프레임에 플레이어 0 의 조종자(없으면 세운다)가, Ai 는 기본 AI 조종자가 쥐고, None 은 아무도 쥐지 않는다
 */
SW_TEST_CASE( ControlTest, AutoPossessTakesThePawnAtStart )
{
    using Internal = ControlTestInternal;
    GameObjectManager manager;
    PawnComponent*    pHero   = Internal::spawnPawn( manager, "Hero", float3{} )->getComponent<PawnComponent>();
    PawnComponent*    pGuard  = Internal::spawnPawn( manager, "Guard", float3{} )->getComponent<PawnComponent>();
    PawnComponent*    pStatue = Internal::spawnPawn( manager, "Statue", float3{} )->getComponent<PawnComponent>();
    pHero->setAutoPossess( PawnAutoPossess::Player0 );
    pGuard->setAutoPossess( PawnAutoPossess::Ai );

    // 플레이 전에는 쥐지 않는다.
    manager.tick( Internal::kDeltaTime );
    SW_EXPECT_FALSE( pHero->isPossessed() );

    manager.beginPlay();
    manager.tick( Internal::kDeltaTime );
    SW_ASSERT_TRUE( pHero->isPossessed() );
    const ComponentRegistry::View<PlayerControllerComponent> playerView = manager.getComponentRegistry().getAll<PlayerControllerComponent>();
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), playerView.size() );
    SW_EXPECT_TRUE( pHero->getController() == playerView[0]->getHandle() );
    SW_EXPECT_EQUAL( 0u, playerView[0]->getPlayerIndex() );
    SW_EXPECT_TRUE( pGuard->isPossessed() );
    SW_EXPECT_FALSE( pStatue->isPossessed() );

    // 한 번 쥐었으면 다시 세우지 않는다.
    manager.tick( Internal::kDeltaTime );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), manager.getComponentRegistry().getAll<PlayerControllerComponent>().size() );
}

/**
 * @brief [ControlTest] 조종 시스템은 첫 폰 · 조종자가 붙을 때 매니저에 붙고, 마지막 것이 빠지면 떨어진다
 */
SW_TEST_CASE( ControlTest, ControlSystemFollowsItsComponents )
{
    GameObjectManager manager;
    SW_EXPECT_TRUE( ControlSystem::find( manager ) == nullptr );
    GameObject* pObject = manager.createGameObject( hashed_string( "Pawn" ) );
    pObject->addComponent<PawnComponent>();
    SW_EXPECT_TRUE( ControlSystem::find( manager ) != nullptr );
    manager.destroyObject( pObject );
    manager.processDeferredDestruction();
    SW_EXPECT_TRUE( ControlSystem::find( manager ) == nullptr );
}
