/**
 * @file TestControl.cpp
 * @brief 조종(빙의) — 폰은 의도만 받는다. 플레이어(키 → 입력 맵) · AI 가 같은 의도를 내면 같은 궤적이고, 빙의가 뷰 타깃 · 입력 레이어를 옮긴다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Message/NetSendBudget.h"
#include "Core/Network/Replication/NetInputWindow.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Network/Transport/NetTransport.h"

#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshAgentComponent.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"

#include "EngineTest/NavMeshTestUtil.h"
#include "EngineTest/TestGameObjectMocks.h"
#include "EngineTest/UI/UITestWidgets.h"

#include "GameFramework/Base/Actor/Camera/CameraManagerComponent.h"
#include "GameFramework/Base/Actor/Control/ControlSystem.h"
#include "GameFramework/Base/Actor/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Controller/IntentPlayback.h"
#include "GameFramework/Base/Actor/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Base/Actor/Control/FirstPersonCameraComponent.h"
#include "GameFramework/Base/Actor/Control/Intent/ControlIntent.h"
#include "GameFramework/Base/Actor/Control/Intent/ControlIntentHistory.h"
#include "GameFramework/Base/Actor/Control/Pawn/CharacterPawnMovementComponent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"

#include "TestFramework/Harness/TestHostRuntime.h"
#include "TestFramework/TestFramework.h"

#include <string>

namespace sw
{
    /** @brief 테스트용 폰 이동 — 의도의 월드 이동 × 4 m/s 로 자리를 옮긴다(물리 없음). 폰은 의도만 읽는다는 것의 가장 작은 모양이다. */
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

        /** @brief 주입 시계의 지금 시각(마이크로초)입니다 — 발동 시선 테스트가 프레임 창 안의 이벤트 시각을 정한다. */
        static inline int64 s_triggerLookNowMicroseconds{ 0 };

        static int64 readTriggerLookClock() { return s_triggerLookNowMicroseconds; }

        /** @brief 자리 · 폰(버튼 "Jump") · 테스트 이동을 가진 폰 오브젝트입니다. */
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
        static void update( GameObjectManager& manager, InputManager& input )
        {
            input.beginFrame( kDeltaTime );
            manager.update( kDeltaTime );
            input.endFrame();
        }

        /** @brief 입력 → UI 입력(게임 틱 앞) → 씬 → UI 갱신 한 프레임입니다(Engine 와 같은 순서). */
        static void updateWithUI( GameObjectManager& manager, InputManager& input, UISystem& ui )
        {
            input.beginFrame( kDeltaTime );
            ui.processInput( kDeltaTime );
            manager.update( kDeltaTime );
            ui.update( kDeltaTime, UIViewport{
                                       float2{ 1280.0f, 720.0f }
            } );
            input.endFrame();
        }

        static float3 findPosition( const GameObject& object ) { return object.getPrimarySceneComponent()->getWorldPosition(); }

        /** @brief 캐릭터 컨트롤러(발 @p position) · 폰 · 폰 이동(걸음 4 m/s)을 가진 걷는 폰입니다. */
        static GameObject* spawnWalker( GameObjectManager& manager, const utf8* pName, const float3& position )
        {
            GameObject*                   pObject     = manager.createGameObject( hashed_string( pName ) );
            CharacterControllerComponent* pController = pObject->addComponent<CharacterControllerComponent>();
            pController->setLocalPosition( position );
            pObject->addComponent<PawnComponent>();
            CharacterPawnMovementComponent* pMovement = pObject->addComponent<CharacterPawnMovementComponent>();
            pMovement->setWalkSpeed( 4.0f );
            return pObject;
        }

        static float3 findFeet( const GameObject& object ) { return object.getComponent<CharacterControllerComponent>()->getWorldPosition(); }

        static constexpr uint32 kTickCount = 120;

        /** @brief 아날로그 "Throttle" 을 가진 폰입니다. */
        static PawnComponent* spawnAnalogPawn( GameObjectManager& manager, const utf8* pName )
        {
            PawnComponent* pPawn = ControlTestInternal::spawnPawn( manager, pName, float3{} )->getComponent<PawnComponent>();
            pPawn->setAnalogNames( vector<hashed_string>{ hashed_string( "Throttle" ) } );
            return pPawn;
        }

        /** @brief 비스듬한 목적지 · 초점으로 도는 AI 를 쥐어 줍니다 — 이동 축 · 조종 회전이 0 · ±1 이 아닌 값이 된다. */
        static AIControllerComponent* possessWithWanderingAI( GameObjectManager& manager, PawnComponent& pawn )
        {
            auto* pAI = manager.createGameObject( hashed_string( "AI" ) )->addComponent<AIControllerComponent>();
            pAI->possess( pawn );
            pAI->moveTo( float3{ 7.0f, 0.0f, 9.0f } );
            pAI->setFocus( float3{ -3.0f, 1.0f, 5.0f } );
            return pAI;
        }

        /** @brief 매 틱 아날로그를 0.7 · −0.33 으로 번갈아 넣습니다. */
        static void setThrottle( AIControllerComponent& ai, uint32 tick ) { ai.setAnalog( "Throttle", ( tick % 2 ) == 0 ? 0.7f : -0.33f ); }

        /** @brief 보내는 창이 쓴 묶음을 받는 버퍼가 읽습니다(종류 바이트 하나를 앞에 둔 셈으로 예산을 센다). */
        static bool deliver( const NetInputSendWindow& window, NetInputReceiveBuffer& buffer )
        {
            BitWriter     writer;
            NetSendBudget budget( NetConnection::kMaxSingleMessageSize );
            budget.reserveBits( 8 );
            (void)window.write( writer, budget ); // 예산이 모자라 덜 쓴 묶음은 받는 쪽 read 결과로 드러난다
            BitReader reader( writer.getBytes().data(), writer.getByteCount() );
            return buffer.read( reader );
        }

        /** @brief 실행기를 Engine 와 같은 순서로 한 프레임 돌립니다. */
        static AutomationOutcome runFrame( AutomationRunner& runner, GameObjectManager& manager, InputManager& input )
        {
            runner.onFrameBegin( input );
            input.beginFrame( ControlTestInternal::kDeltaTime );
            manager.update( ControlTestInternal::kDeltaTime );
            const AutomationOutcome result = runner.onFrameEnd( input );
            input.endFrame();
            return result;
        }

        /** @brief 카메라 · 폰(시점 액션 "Look") · 1인칭 카메라를 가진 1인칭 폰입니다. */
        static GameObject* spawnFirstPersonPawn( GameObjectManager& manager, bool bLockMouse )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "Viewer" ) );
            pObject->addComponent<CameraComponent>();
            PawnComponent* pPawn = pObject->addComponent<PawnComponent>();
            pPawn->setLockMouse( bLockMouse );
            pObject->addComponent<FirstPersonCameraComponent>();
            return pObject;
        }
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
    intent.setTriggerLook( 4.0f, 0.3f ); // 발동 순간의 시선도 요는 감아서 싣는다

    BitWriter writer;
    SW_ASSERT_TRUE( intent.write( writer ) );
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
    SW_EXPECT_TRUE( decoded._bTriggerLook );
    SW_EXPECT_NEAR_EQUAL( 4.0f - 2.0f * MathUtil::kPi, decoded._triggerYaw, 1.0f / ControlIntent::kAngleStepsPerRadian );
    SW_EXPECT_NEAR_EQUAL( 0.3f, decoded._triggerPitch, 1.0f / ControlIntent::kAngleStepsPerRadian );
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

    // 바이트는 넉넉하지만 첫 칸이 범위 밖(8 비트 255 는 -127 에서 세어 128)이다. 예전에는 127 로 잘라 읽고 true 였다.
    uint8 arrBrokenCell[64] = {};
    arrBrokenCell[0]        = 0xFF;
    BitReader     outOfRangeReader( arrBrokenCell, 64 );
    ControlIntent outOfRange;
    outOfRange._move = float2{ 1.0f, 1.0f };
    SW_EXPECT_FALSE( outOfRange.read( outOfRangeReader ) );
    SW_EXPECT_TRUE( outOfRange == ControlIntent{} );
}

/**
 * @brief [ControlTest] 플레이어(키 W → 입력 맵 Move)와 AI(곧장 앞의 목적지)가 같은 의도를 내면 두 폰의 자리가 매 프레임 비트까지 같다
 */
SW_TEST_CASE( ControlTest, PlayerAndAIWithTheSameIntentMoveTheSame )
{
    using Internal = ControlTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getInputMap().bindVector2D( "Move", Key::W, Key::S, Key::A, Key::D );
    input.getInputMap().bind( "Jump", Key::Space );
    SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeKeyDown( Key::W ) ) );

    {
        GameObjectManager manager;
        GameObject*       pPlayerPawn = Internal::spawnPawn( manager, "PlayerPawn", float3{ 0.0f, 0.0f, 0.0f } );
        GameObject*       pAIPawn     = Internal::spawnPawn( manager, "AIPawn", float3{ 10.0f, 0.0f, 0.0f } );
        auto*             pPlayer     = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        auto*             pAI         = manager.createGameObject( hashed_string( "AI" ) )->addComponent<AIControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();
        pPlayer->possess( *pPlayerPawn->getComponent<PawnComponent>() );
        pAI->possess( *pAIPawn->getComponent<PawnComponent>() );
        pAI->moveTo( float3{ 10.0f, 0.0f, 1000.0f } );

        for ( uint32 frame = 0; frame < 60; ++frame )
        {
            Internal::update( manager, input );
            const float3 playerPosition = Internal::findPosition( *pPlayerPawn );
            const float3 aiPosition     = Internal::findPosition( *pAIPawn );
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
    input.getInputMap().bind( "Jump", Key::Space );
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
        Internal::update( manager, input );
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
 * @brief [ControlTest] 빙의할 때 폰이 묻는 축 액션 · 입력 레이어가 맵에 없으면 알린다
 * @details 맵에 없는 액션 질의는 로그 없이 0 이고, `pushLayer` 는 없는 레이어를 빈 레이어로 만든다 — 이름이 틀려도 조작이 조용히 죽는다.
 *          폰의 기본 둘러보기 액션은 `Look` 인데 이 맵에는 없다. 변이: `warnMissingActions` 의 축 액션 검사나 `onPossessed` 의 레이어 검사를 빼면 진다.
 */
SW_TEST_CASE( ControlTest, PossessionWarnsAboutAxisActionsAndLayersTheMapLacks )
{
    using Internal = ControlTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getInputMap().bindVector2D( "Move", Key::W, Key::S, Key::A, Key::D, 0.0f, "OnFoot" );
    input.getInputMap().bind( "Jump", Key::Space );
    {
        GameObjectManager manager;
        GameObject*       pWalker = Internal::spawnPawn( manager, "Walker", float3{} );
        GameObject*       pRider  = Internal::spawnPawn( manager, "Rider", float3{ 5.0f, 0.0f, 0.0f } );
        pWalker->getComponent<PawnComponent>()->setInputLayer( "OnFoot" );
        pRider->getComponent<PawnComponent>()->setInputLayer( "Horse" );
        PlayerControllerComponent* pPlayer = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();

        test::ScopedLogCollector logs;
        pPlayer->possess( *pWalker->getComponent<PawnComponent>() );
        SW_EXPECT_TRUE_MSG( logs.countWithID( "Control.AxisActionNotInInputMap" ) == 1u, logs.joined().c_str() );
        SW_EXPECT_TRUE_MSG( logs.countWithID( "Control.InputLayerNotInInputMap" ) == 0u, logs.joined().c_str() ); // OnFoot 레이어는 맵에 있다
        pPlayer->possess( *pRider->getComponent<PawnComponent>() );
        SW_EXPECT_TRUE_MSG( logs.countWithID( "Control.InputLayerNotInInputMap" ) == 1u, logs.joined().c_str() );
        manager.endPlay();
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
    auto*             pFirst      = manager.createGameObject( hashed_string( "First" ) )->addComponent<AIControllerComponent>();
    GameObject*       pSecondObj  = manager.createGameObject( hashed_string( "Second" ) );
    auto*             pSecond     = pSecondObj->addComponent<AIControllerComponent>();

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
 * @brief [ControlTest] 자동 빙의 — Player0 은 시작 뒤 첫 프레임에 플레이어 0 의 조종자(없으면 세운다)가, AI 는 기본 AI 조종자가 쥐고, None 은 아무도 쥐지 않는다
 */
SW_TEST_CASE( ControlTest, AutoPossessTakesThePawnAtStart )
{
    using Internal = ControlTestInternal;
    GameObjectManager manager;
    PawnComponent*    pHero   = Internal::spawnPawn( manager, "Hero", float3{} )->getComponent<PawnComponent>();
    PawnComponent*    pGuard  = Internal::spawnPawn( manager, "Guard", float3{} )->getComponent<PawnComponent>();
    PawnComponent*    pStatue = Internal::spawnPawn( manager, "Statue", float3{} )->getComponent<PawnComponent>();
    pHero->setAutoPossess( PawnAutoPossess::Player0 );
    pGuard->setAutoPossess( PawnAutoPossess::AI );

    // 플레이 전에는 쥐지 않는다.
    manager.update( Internal::kDeltaTime );
    SW_EXPECT_FALSE( pHero->isPossessed() );

    manager.beginPlay();
    manager.update( Internal::kDeltaTime );
    SW_ASSERT_TRUE( pHero->isPossessed() );
    const ComponentRegistry::View<PlayerControllerComponent> playerView = manager.getComponentRegistry().getAll<PlayerControllerComponent>();
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), playerView.size() );
    SW_EXPECT_TRUE( pHero->getController() == playerView[0]->getHandle() );
    SW_EXPECT_EQUAL( 0u, playerView[0]->getPlayerIndex() );
    SW_EXPECT_TRUE( pGuard->isPossessed() );
    SW_EXPECT_FALSE( pStatue->isPossessed() );

    // 한 번 쥐었으면 다시 세우지 않는다.
    manager.update( Internal::kDeltaTime );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), manager.getComponentRegistry().getAll<PlayerControllerComponent>().size() );
}

/**
 * @brief [ControlTest] 자동 빙의(AI)가 폰을 위해 세운 AI 조종자는 그 폰이 지워질 때 같이 지워지고, 손으로 둔 조종자는 남는다
 * @details 스폰 · 걷기를 되풀이하는 적(슈터의 스켈레톤)마다 조종자 오브젝트가 남으면 판이 길수록 쌓인다.
 */
SW_TEST_CASE( ControlTest, SpawnedAIControllerGoesWithItsPawn )
{
    using Internal = ControlTestInternal;
    GameObjectManager manager;
    GameObject*       pGuardObject  = Internal::spawnPawn( manager, "Guard", float3{} );
    GameObject*       pStatueObject = Internal::spawnPawn( manager, "Statue", float3{} );
    pGuardObject->getComponent<PawnComponent>()->setAutoPossess( PawnAutoPossess::AI );
    AIControllerComponent* pPlaced = manager.createGameObject( hashed_string( "PlacedAI" ) )->addComponent<AIControllerComponent>();
    pPlaced->possess( *pStatueObject->getComponent<PawnComponent>() );

    manager.beginPlay();
    manager.update( Internal::kDeltaTime );
    SW_ASSERT_TRUE( pGuardObject->getComponent<PawnComponent>()->isPossessed() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), manager.getComponentRegistry().getAll<ControllerComponent>().size() );
    SW_EXPECT_FALSE( pPlaced->isSpawnedForPawn() );

    manager.destroyObject( pGuardObject );
    manager.destroyObject( pStatueObject );
    // 조종자 오브젝트는 폰을 지우는 중에 지연 삭제 큐에 든다 — 다음 처리에서 빠진다.
    manager.processDeferredDestruction();
    manager.processDeferredDestruction();
    const ComponentRegistry::View<ControllerComponent> aiView = manager.getComponentRegistry().getAll<ControllerComponent>();
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), aiView.size() );
    SW_EXPECT_TRUE( aiView[0] == pPlaced );
    SW_EXPECT_FALSE( pPlaced->getPawn().isValid() );
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

/**
 * @brief [ControlTest] 캐릭터 컨트롤러 위에서도 플레이어(키 W)와 NPC(곧장 앞의 목적지)가 같은 의도로 같은 속도 곡선 · 같은 거리를 걷는다
 */
SW_TEST_CASE( ControlTest, PlayerAndNpcWalkTheSameOnTheCharacterController )
{
    using Internal = ControlTestInternal;
    using Walk     = ControlTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getInputMap().bindVector2D( "Move", Key::W, Key::S, Key::A, Key::D );
    SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeKeyDown( Key::W ) ) );
    {
        GameObjectManager manager;
        navtest::spawnStaticBody( manager, "Floor", float3{ 0.0f, -0.5f, 0.0f }, float3{ 30.0f, 0.5f, 30.0f } );
        GameObject* pPlayerPawn = Walk::spawnWalker( manager, "PlayerWalker", float3{ -3.0f, 0.05f, 0.0f } );
        GameObject* pNpcPawn    = Walk::spawnWalker( manager, "NpcWalker", float3{ 3.0f, 0.05f, 0.0f } );
        auto*       pPlayer     = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        auto*       pNpc        = manager.createGameObject( hashed_string( "Npc" ) )->addComponent<AIControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();
        pPlayer->possess( *pPlayerPawn->getComponent<PawnComponent>() );
        pNpc->possess( *pNpcPawn->getComponent<PawnComponent>() );
        pNpc->moveTo( float3{ 3.0f, 0.0f, 1000.0f } );

        for ( uint32 frame = 0; frame < 60; ++frame )
        {
            Internal::update( manager, input );
            const float3 playerFeet = Walk::findFeet( *pPlayerPawn );
            const float3 npcFeet    = Walk::findFeet( *pNpcPawn );
            SW_EXPECT_TRUE_MSG( MathUtil::abs( playerFeet._z - npcFeet._z ) <= 1.0e-3f,
                                ( "frame " + std::to_string( frame ) + " player z " + std::to_string( playerFeet._z ) + " npc z " + std::to_string( npcFeet._z ) ).c_str() );
            SW_EXPECT_NEAR_EQUAL( pPlayerPawn->getComponent<CharacterPawnMovementComponent>()->getHorizontalVelocity()._z,
                                  pNpcPawn->getComponent<CharacterPawnMovementComponent>()->getHorizontalVelocity()._z, 1.0e-5f );
        }
        // 1 초 동안 가속(30 m/s²)해 걸음 4 m/s 로 — 4 m 에서 가속 몫을 뺀 만큼 앞에 있다.
        SW_EXPECT_TRUE( Walk::findFeet( *pPlayerPawn )._z > 3.0f );
        SW_EXPECT_NEAR_EQUAL( -3.0f, Walk::findFeet( *pPlayerPawn )._x, 1.0e-3f );
    }
    input.shutdown();
}

/**
 * @brief [ControlTest] NPC 는 내비 에이전트(SteerOnly)가 낸 속도를 의도로 받아 폰 이동으로 상자 벽을 돌아 목적지에 닿는다 — 에이전트는 몸을 옮기지 않는다(걸음 속도를 넘지 않는다)
 */
SW_TEST_CASE( ControlTest, NpcRoutesAroundCratesThroughIntent )
{
    using Internal = ControlTestInternal;
    using Walk     = ControlTestInternal;
    GameObjectManager manager;
    navtest::spawnPhysicsCrateScene( manager );
    GameObject*            pWalker = Walk::spawnWalker( manager, "Walker", float3{ -6.0f, 0.05f, 0.0f } );
    NavMeshAgentComponent* pAgent  = pWalker->addComponent<NavMeshAgentComponent>();
    pAgent->setMaxSpeed( 4.0f );
    pAgent->setDriveMode( NavAgentDriveMode::SteerOnly );
    auto* pNpc = manager.createGameObject( hashed_string( "Npc" ) )->addComponent<AIControllerComponent>();

    manager.beginPlay();
    pNpc->possess( *pWalker->getComponent<PawnComponent>() );
    for ( uint32 frame = 0; frame < 10; ++frame )
    {
        manager.update( Internal::kDeltaTime );
    }
    pNpc->moveTo( float3{ 6.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( pNpc->getMoveStatus() == NavMoveStatus::Moving );

    bool    bEnteredCrates = false;
    float32 maxSpeed       = 0.0f;
    float3  previousFeet   = Walk::findFeet( *pWalker );
    for ( uint32 frame = 0; frame < 900 && pNpc->getMoveStatus() == NavMoveStatus::Moving; ++frame )
    {
        manager.update( Internal::kDeltaTime );
        const float3  feet   = Walk::findFeet( *pWalker );
        const float32 deltaX = feet._x - previousFeet._x;
        const float32 deltaZ = feet._z - previousFeet._z;
        maxSpeed             = MathUtil::max( maxSpeed, MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ ) / Internal::kDeltaTime );
        bEnteredCrates       = bEnteredCrates || navtest::isInsideCrates( feet );
        previousFeet         = feet;
    }
    SW_EXPECT_TRUE( pNpc->getMoveStatus() == NavMoveStatus::Arrived );
    SW_EXPECT_FALSE( bEnteredCrates );
    SW_EXPECT_TRUE_MSG( maxSpeed <= 4.0f * 1.05f, ( "max speed " + std::to_string( maxSpeed ) ).c_str() );
    const float3 feet = Walk::findFeet( *pWalker );
    SW_EXPECT_TRUE_MSG( ( float3{ feet._x, 0.0f, feet._z } - float3{ 6.0f, 0.0f, 0.0f } ).getLength() < 0.8f,
                        ( "walker ended at " + std::to_string( feet._x ) + ", " + std::to_string( feet._z ) ).c_str() );
    manager.endPlay();
}

/**
 * @brief [ControlTest] 아날로그를 섞은 AI 조종 120 틱을 기록 → .swintent 바이트 → 같은 장면 새 매니저에서 기록 조종자로 재생하면 매 틱 자리 · 의도가 비트까지 같다
 */
SW_TEST_CASE( ControlTest, RecordedIntentsReplayTheSameTrajectory )
{
    using Internal = ControlTestInternal;
    vector<float3> listRecordedPosition;
    vector<uint8>  fileBytes;
    {
        GameObjectManager manager;
        PawnComponent*    pPawn = Internal::spawnAnalogPawn( manager, "Runner" );
        ControlSystem::ensureFor( manager ).setRecording( true );
        manager.beginPlay();
        AIControllerComponent* pAI = Internal::possessWithWanderingAI( manager, *pPawn );
        for ( uint32 tick = 0; tick < Internal::kTickCount; ++tick )
        {
            Internal::setThrottle( *pAI, tick );
            manager.update( ControlTestInternal::kDeltaTime );
            listRecordedPosition.push_back( ControlTestInternal::findPosition( *pPawn->getOwner() ) );
        }
        const ControlIntentHistory& history = ControlSystem::find( manager )->getHistory();
        SW_ASSERT_EQUAL( 1, history.getTrackCount() );
        SW_EXPECT_EQUAL( Internal::kTickCount, history.getTrack( 0 )._lastTick - history.getTrack( 0 )._firstTick + 1 );
        // 아날로그가 실제로 실렸다(0.7 → 양자화 값).
        SW_EXPECT_NEAR_EQUAL( 0.7f, history.findIntent( 0, history.getTrack( 0 )._firstTick )->_arrAnalog[0], 1.0f / ControlIntent::kAxisSteps );
        BitWriter writer;
        SW_ASSERT_TRUE( history.write( writer ) );
        fileBytes = writer.releaseBytes();
        manager.endPlay();
    }

    ControlIntentHistory loaded;
    string               error;
    BitReader            reader( fileBytes.data(), static_cast<int32>( fileBytes.size() ) );
    SW_ASSERT_TRUE_MSG( loaded.read( reader, error ), error.c_str() );
    {
        GameObjectManager               manager;
        PawnComponent*                  pPawn  = Internal::spawnAnalogPawn( manager, "Runner" );
        IntentTrackControllerComponent* pTrack = manager.createGameObject( hashed_string( "Replay" ) )->addComponent<IntentTrackControllerComponent>();
        manager.beginPlay();
        pTrack->possess( *pPawn );
        SW_ASSERT_TRUE( pTrack->loadTrack( loaded, "Runner" ) );
        SW_ASSERT_EQUAL( Internal::kTickCount, pTrack->getTrackLength() );
        for ( uint32 tick = 0; tick < Internal::kTickCount; ++tick )
        {
            manager.update( ControlTestInternal::kDeltaTime );
            const float3 position = ControlTestInternal::findPosition( *pPawn->getOwner() );
            const float3 recorded = listRecordedPosition[tick];
            SW_EXPECT_TRUE_MSG( position._x == recorded._x && position._y == recorded._y && position._z == recorded._z,
                                ( "tick " + std::to_string( tick ) + " replay " + std::to_string( position._x ) + "," + std::to_string( position._z ) + " recorded " +
                                  std::to_string( recorded._x ) + "," + std::to_string( recorded._z ) )
                                    .c_str() );
            SW_EXPECT_TRUE( pPawn->getIntent() == *loaded.findIntent( 0, tick ) );
        }
        // 끝난 뒤에는 멈춘 채 쥐고 있다(돌려주기를 걸지 않았다).
        SW_EXPECT_FALSE( pTrack->isPlaying() );
        manager.update( ControlTestInternal::kDeltaTime );
        SW_EXPECT_TRUE( pPawn->getController() == pTrack->getHandle() );
        SW_EXPECT_NEAR_EQUAL( 0.0f, pPawn->getIntent()._move._y, 1.0e-6f );
        manager.endPlay();
    }
}

/**
 * @brief [ControlTest] 의도 기록 파일이 왕복한다 — 폰마다 시작 틱이 달라도 같은 의도, 머리는 'SWIN', 판이 다르거나 잘렸으면 거절
 */
SW_TEST_CASE( ControlTest, IntentFileRoundTrips )
{
    // 기록은 타입 핸들(`TComponentHandle<PawnComponent>`)로만 받는다 — 진짜 폰 둘에서 만든다.
    GameObjectManager                     manager;
    const TComponentHandle<PawnComponent> first( ControlTestInternal::spawnAnalogPawn( manager, "Walker" ) );
    const TComponentHandle<PawnComponent> second( ControlTestInternal::spawnAnalogPawn( manager, "Swimmer" ) );
    ControlIntentHistory                  history;
    history.initialize( 64 );
    for ( uint32 tick = 100; tick < 110; ++tick )
    {
        ControlIntent intent;
        intent._move       = float2{ 0.1f * static_cast<float32>( tick - 100 ), -0.5f };
        intent._controlYaw = 0.25f * static_cast<float32>( tick - 100 );
        intent.setButton( 3, ( tick % 2 ) == 0, tick == 100 );
        intent.quantize();
        history.record( tick, first, "Walker", intent );
        if ( tick >= 104 )
        {
            intent._moveUp = 1.0f;
            history.record( tick, second, "Swimmer", intent );
        }
    }

    const string path = test::makeTempPath( "control.swintent" );
    SW_ASSERT_TRUE( history.saveToFile( path ) );
    ControlIntentHistory loaded;
    string               error;
    SW_ASSERT_TRUE_MSG( loaded.loadFromFile( path, error ), error.c_str() );
    (void)FileUtil::tryRemoveFile( path ); // 임시 파일 정리 — 지우지 못해도 테스트 결과와 무관하다

    SW_ASSERT_EQUAL( 2, loaded.getTrackCount() );
    const int32 walker  = loaded.findTrack( "Walker" );
    const int32 swimmer = loaded.findTrack( "Swimmer" );
    SW_ASSERT_TRUE( walker >= 0 && swimmer >= 0 );
    // 가장 이른 시작이 0 — 늦게 연 트랙은 그만큼 뒤에서 시작한다.
    SW_EXPECT_EQUAL( 0u, loaded.getTrack( walker )._firstTick );
    SW_EXPECT_EQUAL( 4u, loaded.getTrack( swimmer )._firstTick );
    for ( uint32 tick = 100; tick < 110; ++tick )
    {
        SW_ASSERT_NOT_NULL( loaded.findIntent( walker, tick - 100 ) );
        SW_EXPECT_TRUE( *loaded.findIntent( walker, tick - 100 ) == *history.findIntent( 0, tick ) );
    }
    vector<ControlIntent> listSwimmer;
    SW_ASSERT_TRUE( loaded.copyTrack( swimmer, listSwimmer ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 6 ), listSwimmer.size() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listSwimmer[0]._moveUp, 1.0e-6f );

    BitWriter writer;
    SW_ASSERT_TRUE( history.write( writer ) );
    vector<uint8> bytes = writer.releaseBytes();
    SW_ASSERT_TRUE( bytes.size() > 8 );
    SW_EXPECT_TRUE( bytes[0] == 'S' && bytes[1] == 'W' && bytes[2] == 'I' && bytes[3] == 'N' );
    SW_EXPECT_EQUAL( static_cast<uint8>( ControlIntentHistory::kVersion ), bytes[4] );

    vector<uint8> otherVersion = bytes;
    otherVersion[4]            = static_cast<uint8>( ControlIntentHistory::kVersion + 1 );
    BitReader versionReader( otherVersion.data(), static_cast<int32>( otherVersion.size() ) );
    SW_EXPECT_FALSE( loaded.read( versionReader, error ) );
    SW_EXPECT_EQUAL( 0, loaded.getTrackCount() );

    BitReader truncatedReader( bytes.data(), static_cast<int32>( bytes.size() ) - 6 );
    SW_EXPECT_FALSE( loaded.read( truncatedReader, error ) );
    SW_EXPECT_EQUAL( 0, loaded.getTrackCount() );
}

/**
 * @brief [ControlTest] 로컬 의도를 입력 창(NetInputSendWindow → 묶음 셋 중 하나 잃음 → NetInputReceiveBuffer)으로 보내면 두 틱 늦게 도는 원격 조종자 폰이
 *        로컬 폰과 매 틱 같은 자리다. 못 받은 틱은 마지막 의도를 되풀이하고 발동 비트는 지운다
 */
SW_TEST_CASE( ControlTest, RemoteControllerReadsWindowBytes )
{
    using Internal                    = ControlTestInternal;
    static constexpr uint32 kLagTicks = 2;
    const NetInputFormat    format{ 0, ControlIntent::kMaxSerializedBytes, 32, SW_FALSE };
    NetInputSendWindow      window;
    NetInputReceiveBuffer   buffer;
    window.initialize( 64, format );
    buffer.initialize( 64, format, NetInputWindowMode::FollowNewest );

    GameObjectManager localManager;
    GameObjectManager remoteManager;
    PawnComponent*    pLocalPawn  = Internal::spawnAnalogPawn( localManager, "Local" );
    PawnComponent*    pRemotePawn = Internal::spawnAnalogPawn( remoteManager, "Remote" );
    auto*             pRemote     = remoteManager.createGameObject( hashed_string( "Remote" ) )->addComponent<RemoteControllerComponent>();
    localManager.beginPlay();
    remoteManager.beginPlay();
    AIControllerComponent* pAI = Internal::possessWithWanderingAI( localManager, *pLocalPawn );
    pRemote->possess( *pRemotePawn );

    vector<float3> listLocalPosition;
    for ( uint32 tick = 0; tick < 90; ++tick )
    {
        Internal::setThrottle( *pAI, tick );
        if ( tick == 10 )
            pAI->pressButton( "Jump" );
        localManager.update( ControlTestInternal::kDeltaTime );
        listLocalPosition.push_back( ControlTestInternal::findPosition( *pLocalPawn->getOwner() ) );
        BitWriter intentWriter;
        SW_ASSERT_TRUE( pLocalPawn->getIntent().write( intentWriter ) );
        SW_ASSERT_TRUE( window.push( tick, intentWriter.getBytes().data(), intentWriter.getByteCount() ) );
        // 묶음 셋 중 하나를 잃는다 — 다음 묶음이 확인 안 된 틱부터 다시 싣는다.
        if ( ( tick % 3 ) != 1 )
        {
            SW_ASSERT_TRUE( Internal::deliver( window, buffer ) );
            window.acknowledge( buffer.getFirstMissingTick() );
            // 키트 쪽 일 — 받은 틱의 바이트를 의도로 읽어 조종자에 사본으로 넣는다(조종자는 버퍼를 빌리지 않는다).
            for ( uint32 receivedTick = pRemote->getNextInputTick(); receivedTick <= tick; ++receivedTick )
            {
                const NetInputEntry* pEntry = buffer.find( receivedTick );
                if ( pEntry == nullptr )
                    continue;
                BitReader     reader( pEntry->_bytes.data(), static_cast<int32>( pEntry->_bytes.size() ) );
                ControlIntent received;
                SW_ASSERT_TRUE( received.read( reader ) );
                pRemote->receiveIntent( receivedTick, received );
            }
        }
        if ( tick < kLagTicks )
            continue;
        const uint32 remoteTick = tick - kLagTicks;
        remoteManager.update( ControlTestInternal::kDeltaTime );
        const float3 remotePosition = ControlTestInternal::findPosition( *pRemotePawn->getOwner() );
        const float3 localPosition  = listLocalPosition[remoteTick];
        SW_EXPECT_TRUE_MSG( remotePosition._x == localPosition._x && remotePosition._z == localPosition._z,
                            ( "tick " + std::to_string( remoteTick ) + " remote " + std::to_string( remotePosition._x ) + "," + std::to_string( remotePosition._z ) +
                              " local " + std::to_string( localPosition._x ) + "," + std::to_string( localPosition._z ) )
                                .c_str() );
        if ( remoteTick == 10 )
            SW_EXPECT_TRUE( pRemotePawn->wasButtonTriggered( 0 ) );
    }
    SW_EXPECT_EQUAL( 0u, pRemote->getMissingTickCount() );

    // 더 받지 못한 틱까지 가면 마지막 의도를 되풀이한다(발동 비트는 지운다).
    while ( pRemote->getNextInputTick() < 90 )
    {
        remoteManager.update( ControlTestInternal::kDeltaTime );
    }
    const ControlIntent lastIntent = pRemotePawn->getIntent();
    remoteManager.update( ControlTestInternal::kDeltaTime );
    SW_EXPECT_EQUAL( 1u, pRemote->getMissingTickCount() );
    SW_EXPECT_TRUE( pRemotePawn->getIntent()._move._x == lastIntent._move._x && pRemotePawn->getIntent()._move._y == lastIntent._move._y );
    SW_EXPECT_EQUAL( 0u, pRemotePawn->getIntent()._buttonTriggered );
    pRemote->clearReceived();
}

/**
 * @brief [ControlTest] 시나리오 `<Intent>` 는 입력 맵 없이 폰을 기록 조종자로 몬다 — 앞으로 가는 AI 와 같은 자리로 30 프레임 걷고(첫 프레임에 Jump 발동),
 *        끝나면 원래 플레이어 조종자에게 돌아가 멈춘다. 모르는 속성은 읽기 오류다
 */
SW_TEST_CASE( ControlTest, ScenarioIntentStepDrivesThePawnLikeTheAI )
{
    using Internal = ControlTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    {
        GameObjectManager manager;
        GameObject*       pHero   = Internal::spawnPawn( manager, "Hero", float3{} );
        GameObject*       pTwin   = Internal::spawnPawn( manager, "Twin", float3{ 10.0f, 0.0f, 0.0f } );
        auto*             pPlayer = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        auto*             pAI     = manager.createGameObject( hashed_string( "AI" ) )->addComponent<AIControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();
        PawnComponent* pHeroPawn = pHero->getComponent<PawnComponent>();
        pPlayer->possess( *pHeroPawn );
        pAI->possess( *pTwin->getComponent<PawnComponent>() );
        pAI->moveTo( float3{ 10.0f, 0.0f, 1000.0f } );

        AutomationRunner runner;
        runner.setObjectManager( &manager );
        SW_ASSERT_TRUE( runner.startFromText( "<Scenario name=\"control.intent\">"
                                              "<At frame=\"0\"><Intent pawn=\"Hero\" move=\"0,1\" yaw=\"0\" buttons=\"Jump\" frames=\"30\"/></At>"
                                              "<At frame=\"40\"><Pass/></At>"
                                              "</Scenario>" ) );
        AutomationOutcome result = AutomationOutcome::Running;
        for ( uint32 frame = 0; frame < 60 && result == AutomationOutcome::Running; ++frame )
        {
            result                  = ControlTestInternal::runFrame( runner, manager, input );
            const float3 heroAt     = Internal::findPosition( *pHero );
            const float3 twinAt     = Internal::findPosition( *pTwin );
            const bool   bIntentRun = frame < 30;
            if ( bIntentRun )
            {
                SW_EXPECT_TRUE_MSG( heroAt._z == twinAt._z && heroAt._x == twinAt._x - 10.0f,
                                    ( "frame " + std::to_string( frame ) + " hero z " + std::to_string( heroAt._z ) + " twin z " + std::to_string( twinAt._z ) ).c_str() );
                SW_EXPECT_TRUE( pHeroPawn->isButtonDown( 0 ) );
                SW_EXPECT_EQUAL( frame == 0, pHeroPawn->wasButtonTriggered( 0 ) );
            }
            else
            {
                SW_EXPECT_TRUE( pHeroPawn->getController() == pPlayer->getHandle() );
                SW_EXPECT_NEAR_EQUAL( 0.0f, pHeroPawn->getIntent()._move._y, 1.0e-6f );
            }
        }
        SW_EXPECT_TRUE( result == AutomationOutcome::Passed );
        // 30 프레임 × 4 m/s × 1/60 s = 2 m 에서 멈췄다.
        SW_EXPECT_NEAR_EQUAL( 2.0f, Internal::findPosition( *pHero )._z, 1.0e-3f );

        AutomationRunner badRunner;
        badRunner.setObjectManager( &manager );
        SW_ASSERT_TRUE( badRunner.startFromText( "<Scenario name=\"control.bad\"><At frame=\"0\"><Intent pawn=\"Hero\" speed=\"1\"/></At></Scenario>" ) );
        SW_EXPECT_TRUE( ControlTestInternal::runFrame( badRunner, manager, input ) == AutomationOutcome::LoadError );
    }
    input.shutdown();
}

/**
 * @brief [ControlTest] 1인칭 카메라는 폰의 조종 회전을 따른다 — 마우스(Look 액션 × 감도) · 코드가 정한 시점(setAngles → 요청) · 반동(오프셋)이 모두 조종자를 거쳐 카메라에 든다
 */
SW_TEST_CASE( ControlTest, FirstPersonCameraFollowsTheControlRotation )
{
    using Internal = ControlTestInternal;
    using View     = ControlTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getInputMap().bindMouseDelta( "Look", 3.0f ); // 배율 3 — 액션 값이 원시 이동량의 세 배다
    {
        GameObjectManager manager;
        GameObject*       pViewer = View::spawnFirstPersonPawn( manager, false );
        auto*             pPlayer = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();
        pPlayer->possess( *pViewer->getComponent<PawnComponent>() );
        FirstPersonCameraComponent* pRig    = pViewer->getComponent<FirstPersonCameraComponent>();
        const PawnComponent*        pPawn   = pViewer->getComponent<PawnComponent>();
        const CameraComponent*      pCamera = pViewer->getComponent<CameraComponent>();
        Internal::update( manager, input );
        SW_EXPECT_NEAR_EQUAL( 0.0f, pRig->getLook().getYaw(), 1.0e-6f );

        // 마우스 오른쪽 10 px — Look 30 × 감도 = 조종 요, 카메라가 같은 틱(PrePhysics)에 그 값을 시점으로 둔다.
        SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeMouseRawDelta( 10.0f, 0.0f ) ) );
        input.beginFrame( Internal::kDeltaTime );
        const float32 lookX = input.getInputMap().getVector2D( "Look" )._x;
        manager.update( Internal::kDeltaTime );
        input.endFrame();
        SW_EXPECT_NEAR_EQUAL( 30.0f, lookX, 1.0e-4f );
        const float32 expectedYaw = lookX * pPlayer->getLookSensitivity();
        SW_EXPECT_NEAR_EQUAL( expectedYaw, pPlayer->getControlYaw(), 1.0e-5f );
        SW_EXPECT_NEAR_EQUAL( pPawn->getIntent()._controlYaw, pRig->getLook().getYaw(), 1.0e-6f );
        SW_EXPECT_NEAR_EQUAL( expectedYaw, pRig->getLook().getYaw(), 1.0f / ControlIntent::kAngleStepsPerRadian );
        SW_EXPECT_NEAR_EQUAL( pRig->getLook().getYaw(), pCamera->getLocalRotation()._y, 1.0e-5f );

        // 코드가 정한 시점은 다음 틱부터 조종자의 값이다 — 카메라가 조종 회전으로 덮어써도 남는다.
        pRig->setAngles( 1.0f, -0.3f );
        Internal::update( manager, input );
        SW_EXPECT_NEAR_EQUAL( 1.0f, pPlayer->getControlYaw(), 1.0e-4f );
        SW_EXPECT_NEAR_EQUAL( -0.3f, pPlayer->getControlPitch(), 1.0e-4f );
        SW_EXPECT_NEAR_EQUAL( -0.3f, pRig->getLook().getPitch(), 1.0e-3f );

        // 반동은 오프셋으로 쌓여 한 번만 든다.
        pRig->addRecoil( 0.1f, 0.0f );
        Internal::update( manager, input );
        Internal::update( manager, input );
        SW_EXPECT_NEAR_EQUAL( -0.2f, pPlayer->getControlPitch(), 1.0e-3f );
        SW_EXPECT_NEAR_EQUAL( -0.2f, pRig->getLook().getPitch(), 1.0e-3f );
        manager.endPlay();
    }
    input.shutdown();
}

/**
 * @brief [ControlTest] 발동 순간의 시선(서브틱 입력 B4) — 한 프레임 안에서 클릭 앞의 마우스 이동만 의도의 발동 시선에 들고, 프레임 끝의 조종 회전은 클릭 뒤 이동까지 담는다
 * @details 시계를 주입해 프레임 창 안에서 이동 10 px, 클릭, 이동 30 px 를 넣는다(액션 배율 3, 감도 s). 조종 요는 40 × 3 × s, 발동 요는 10 × 3 × s 다.
 *          폰이 발동 버튼을 지정하지 않으면 발동 시선은 비고, 누르지 않은 프레임에도 비며, 의도를 비트로 왕복해도 같다.
 * @details 변이: `PlayerControllerComponent::produceIntent` 가 발동 시선을 프레임 끝의 조종 회전으로 채우면 진다.
 */
SW_TEST_CASE( ControlTest, TriggerLookCarriesTheControlRotationAtTheClick )
{
    using Internal = ControlTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.setClock( &ControlTestInternal::readTriggerLookClock );
    input.getInputMap().bindMouseDelta( "Look", 3.0f );
    input.getInputMap().bind( "Fire", MouseButton::Left );
    {
        GameObjectManager manager;
        GameObject*       pViewer = ControlTestInternal::spawnFirstPersonPawn( manager, false );
        PawnComponent*    pPawn   = pViewer->getComponent<PawnComponent>();
        pPawn->setButtonNames( vector<hashed_string>{ hashed_string( "Fire" ) } );
        auto* pPlayer = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();
        pPlayer->possess( *pPawn );

        ControlTestInternal::s_triggerLookNowMicroseconds = 1000000;
        input.beginFrame( Internal::kDeltaTime );
        input.endFrame();

        const float32 sensitivity   = pPlayer->getLookSensitivity();
        auto          runClickFrame = [&]()
        {
            ControlTestInternal::s_triggerLookNowMicroseconds += 4000;
            SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeMouseRawDelta( 10.0f, 0.0f ) ) );
            ControlTestInternal::s_triggerLookNowMicroseconds += 4000;
            SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeMouseButtonDown( MouseButton::Left ) ) );
            ControlTestInternal::s_triggerLookNowMicroseconds += 4000;
            SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeMouseRawDelta( 30.0f, 0.0f ) ) );
            ControlTestInternal::s_triggerLookNowMicroseconds += 4000;
            input.beginFrame( Internal::kDeltaTime );
            manager.update( Internal::kDeltaTime );
            input.endFrame();
        };

        // 발동 버튼을 지정하지 않았다 — 클릭해도 발동 시선이 비어 있다.
        runClickFrame();
        SW_EXPECT_TRUE( pPawn->wasButtonTriggered( 0 ) );
        SW_EXPECT_FALSE( pPawn->getIntent()._bTriggerLook );
        SW_EXPECT_NEAR_EQUAL( 40.0f * 3.0f * sensitivity, pPlayer->getControlYaw(), 1.0e-4f );

        // 지정하면 클릭 앞의 이동(10 px)만 담는다. 프레임 끝의 조종 회전은 40 px 를 모두 담는다.
        pPawn->setTriggerLookButton( hashed_string( "Fire" ) );
        const float32 yawBefore = pPlayer->getControlYaw();
        SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeMouseButtonUp( MouseButton::Left ) ) );
        ControlTestInternal::s_triggerLookNowMicroseconds += 16000;
        input.beginFrame( Internal::kDeltaTime );
        manager.update( Internal::kDeltaTime );
        input.endFrame();
        runClickFrame();
        const ControlIntent& intent = pPawn->getIntent();
        SW_ASSERT_TRUE( intent._bTriggerLook );
        SW_EXPECT_NEAR_EQUAL( yawBefore + 10.0f * 3.0f * sensitivity, intent._triggerYaw, 2.0f / ControlIntent::kAngleStepsPerRadian );
        SW_EXPECT_NEAR_EQUAL( yawBefore + 40.0f * 3.0f * sensitivity, intent._controlYaw, 2.0f / ControlIntent::kAngleStepsPerRadian );
        SW_EXPECT_TRUE( intent._triggerYaw < intent._controlYaw );

        // 비트로 왕복해도 같고, 누르지 않은 프레임은 비어 있다.
        BitWriter writer;
        SW_ASSERT_TRUE( intent.write( writer ) );
        SW_EXPECT_TRUE( writer.getByteCount() <= ControlIntent::kMaxSerializedBytes );
        ControlIntent decoded;
        BitReader     reader( writer.getBytes().data(), writer.getByteCount() );
        SW_ASSERT_TRUE( decoded.read( reader ) );
        SW_EXPECT_TRUE( decoded == intent );

        ControlTestInternal::s_triggerLookNowMicroseconds += 16000;
        input.beginFrame( Internal::kDeltaTime );
        manager.update( Internal::kDeltaTime );
        input.endFrame();
        SW_EXPECT_FALSE( pPawn->getIntent()._bTriggerLook );
        manager.endPlay();
    }
    input.shutdown();
}

/**
 * @brief [ControlTest] 시나리오 `<Possess>` 는 조종자의 빙의를 옮기고(앞 폰은 풀린다), pawn 이 없으면 놓게 한다
 */
SW_TEST_CASE( ControlTest, ScenarioPossessStepMovesPossession )
{
    using Internal = ControlTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    {
        GameObjectManager manager;
        PawnComponent*    pHero   = Internal::spawnPawn( manager, "Hero", float3{} )->getComponent<PawnComponent>();
        PawnComponent*    pHorse  = Internal::spawnPawn( manager, "Horse", float3{} )->getComponent<PawnComponent>();
        auto*             pPlayer = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();
        pPlayer->possess( *pHero );

        AutomationRunner runner;
        runner.setObjectManager( &manager );
        SW_ASSERT_TRUE( runner.startFromText( "<Scenario name=\"control.possess\">"
                                              "<At frame=\"5\"><Possess controller=\"Player\" pawn=\"Horse\"/></At>"
                                              "<At frame=\"8\"><Possess controller=\"Player\"/></At>"
                                              // 입력 맵이 빈 테스트라 Horse 에 빙의할 때 맵에 없는 액션마다 한 번 경고한다(버튼 Jump, 축 Move · Look) — 시나리오가 기대한 줄로 선언한다
                                              "<At frame=\"10\"><ExpectLog contains=\"Pawn 'Horse' intent button 'Jump' has no InputMap action\" count=\"1\"/>"
                                              "<ExpectLog contains=\"Pawn 'Horse' axis action\" count=\"2\"/>"
                                              "<ExpectLog contains=\"has no InputMap action\" count=\"3\"/><Pass/></At>"
                                              "</Scenario>" ) );
        AutomationOutcome result = AutomationOutcome::Running;
        for ( uint32 frame = 0; frame < 30 && result == AutomationOutcome::Running; ++frame )
        {
            result = ControlTestInternal::runFrame( runner, manager, input );
            if ( frame == 4 )
                SW_EXPECT_TRUE( pHero->getController() == pPlayer->getHandle() );
            if ( frame == 5 )
            {
                SW_EXPECT_TRUE( pHorse->getController() == pPlayer->getHandle() );
                SW_EXPECT_FALSE( pHero->isPossessed() );
            }
        }
        SW_EXPECT_TRUE( result == AutomationOutcome::Passed );
        SW_EXPECT_FALSE( pHorse->isPossessed() );
        SW_EXPECT_TRUE( pPlayer->findPawn() == nullptr );
    }
    input.shutdown();
}

/**
 * @brief [ControlTest] 잠금을 바라는 폰을 쥐면 플레이어 조종자가 커서를 잠그고, 잠금 토글 액션(Esc)이 풀고 다시 걸며, 놓으면 푼다 — 잠금이 쉬는 동안(개발 콘솔)은 시선이 쌓이지 않는다
 */
SW_TEST_CASE( ControlTest, PlayerControllerTogglesTheMouseLockOfItsPawn )
{
    using Internal = ControlTestInternal;
    using View     = ControlTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getInputMap().bindMouseDelta( "Look" );
    input.getInputMap().bind( "ToggleMouseLock", Key::Escape );
    SW_ASSERT_NOT_NULL( input.getMouse() );
    {
        GameObjectManager manager;
        GameObject*       pViewer = View::spawnFirstPersonPawn( manager, true );
        auto*             pPlayer = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        ControlSystem::ensureFor( manager ).setInputManager( &input );
        manager.beginPlay();
        pPlayer->possess( *pViewer->getComponent<PawnComponent>() );
        SW_EXPECT_TRUE( pPlayer->isMouseLockRequested() );
        SW_EXPECT_TRUE( input.getMouse()->getLockMode() == MouseLockMode::LockedInCenter );
        SW_EXPECT_FALSE( input.getMouse()->isCursorVisible() );

        // 잠금이 걸린 동안은 시선이 쌓인다.
        const float32 step = 40.0f * pPlayer->getLookSensitivity();
        SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeMouseRawDelta( 40.0f, 0.0f ) ) );
        Internal::update( manager, input );
        SW_EXPECT_NEAR_EQUAL( step, pPlayer->getControlYaw(), 1.0e-5f );
        // 개발 콘솔이 키보드를 쥐었다 — 잠금은 요청돼 있지만 쉬고 있으니 마우스를 움직여도 시점이 돌지 않는다(포커스 밖 · Alt 도 같은 조건).
        input.setKeyboardFocus( InputKeyboardFocus::DevConsole );
        SW_EXPECT_FALSE( input.isMouseLockActive() );
        SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeMouseRawDelta( 40.0f, 0.0f ) ) );
        Internal::update( manager, input );
        SW_EXPECT_NEAR_EQUAL( step, pPlayer->getControlYaw(), 1.0e-5f );
        input.setKeyboardFocus( InputKeyboardFocus::Game );

        // Esc — 풀린다. 한 번 더 — 다시 잠긴다.
        SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeKeyDown( Key::Escape ) ) );
        Internal::update( manager, input );
        SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeKeyUp( Key::Escape ) ) );
        Internal::update( manager, input );
        SW_EXPECT_FALSE( pPlayer->isMouseLockRequested() );
        SW_EXPECT_TRUE( input.getMouse()->getLockMode() == MouseLockMode::None );
        SW_EXPECT_TRUE( input.getMouse()->isCursorVisible() );
        SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeKeyDown( Key::Escape ) ) );
        Internal::update( manager, input );
        SW_ASSERT_TRUE( input.postRawEvent( RawInputEvent::makeKeyUp( Key::Escape ) ) );
        Internal::update( manager, input );
        SW_EXPECT_TRUE( pPlayer->isMouseLockRequested() );
        SW_EXPECT_TRUE( input.getMouse()->getLockMode() == MouseLockMode::LockedInCenter );

        // 배타 가상 입력(시나리오)은 OS 포인터를 쥐지 않는다 — 잠금 요청만 보고 시선을 쌓는다.
        VirtualInputScript script;
        script.addMouseDelta( 1, 40.0f, 0.0f );
        input.attachVirtualInput( &script, VirtualInputMode::Exclusive );
        Internal::update( manager, input );
        Internal::update( manager, input );
        input.detachVirtualInput();
        SW_EXPECT_NEAR_EQUAL( 2.0f * step, pPlayer->getControlYaw(), 1.0e-5f );

        // 놓으면 건 잠금을 푼다.
        pPlayer->unpossess();
        SW_EXPECT_FALSE( pPlayer->isMouseLockRequested() );
        SW_EXPECT_TRUE( input.getMouse()->getLockMode() == MouseLockMode::None );
        SW_EXPECT_TRUE( input.getMouse()->isCursorVisible() );
        manager.endPlay();
    }
    input.shutdown();
}

/**
 * @brief [ControlTest] UI 가 먹은 입력은 폰의 의도에 들지 않는다 — 메뉴 버튼에서 누른 패드 A 는 클릭이고 Jump 비트는 꺼져 있으며, 메뉴를 닫고 다시 누르면 켜진다.
 *        모달이 떠 있는 동안은 이동 의도도 0 이다(플레이어 조종자가 UI 를 보는 한 자리)
 * @details 변이: `PlayerControllerComponent::isActionDownForGame` · `wasActionTriggeredForGame` 이 UI 를 보지 않으면 진다.
 */
SW_TEST_CASE( ControlTest, UIConsumedInputDoesNotReachThePawn )
{
    using Internal = ControlTestInternal;
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getInputMap().bind( "Jump", GamepadButton::A, ActionTrigger::Down );
    input.getInputMap().bindVector2D( "Move", Key::W, Key::S, Key::A, Key::D );
    VirtualInputScript script;
    script.addEvent( 0, RawInputEvent::makeGamepadConnection( 0, true ) );
    SW_ASSERT_TRUE( script.addSlot( 1, InputSlot::fromGamepadButton( GamepadButton::A ), true ) );
    SW_ASSERT_TRUE( script.addSlot( 3, InputSlot::fromGamepadButton( GamepadButton::A ), false ) );
    SW_ASSERT_TRUE( script.addSlot( 5, InputSlot::fromGamepadButton( GamepadButton::A ), true ) );
    SW_ASSERT_TRUE( script.addSlot( 7, InputSlot::fromKey( Key::W ), true ) );
    {
        UISystem ui;
        SW_ASSERT_TRUE( ui.initialize( input, nullptr, ::test::getEngineDefaultAssets()._uiInputMap.c_str() ) );
        auto                 root    = sw::make_unique<uitest::TestPanelWidget>( "menuRoot" );
        auto*                pButton = static_cast<uitest::TestBoxWidget*>( root->addChild( sw::make_unique<uitest::TestBoxWidget>( "play", true ) ) );
        const UIScreenHandle menu    = ui.pushScreen( sw::make_unique<UIScreen>( UIScreenDesc{}, std::move( root ) ) );
        SW_ASSERT_TRUE( ui.getInputRouter().getFocusManager().setFocus( ui.findScreen( menu )->getTree(), pButton->getID() ) );

        GameObjectManager manager;
        GameObject*       pPawnObject = Internal::spawnPawn( manager, "Hero", float3{ 0.0f, 0.0f, 0.0f } );
        auto*             pPlayer     = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
        ControlSystem&    control     = ControlSystem::ensureFor( manager );
        control.setInputManager( &input );
        control.setUISystem( &ui );
        manager.beginPlay();
        pPlayer->possess( *pPawnObject->getComponent<PawnComponent>() );
        const PawnComponent* pPawn = pPawnObject->getComponent<PawnComponent>();
        input.attachVirtualInput( &script, VirtualInputMode::Exclusive );

        Internal::updateWithUI( manager, input, ui ); // 0 — 패드 연결
        Internal::updateWithUI( manager, input, ui ); // 1 — A: 메뉴 클릭
        SW_EXPECT_EQUAL( 1u, pButton->_clickCount );
        SW_EXPECT_TRUE( input.getInputMap().isActionDown( "Jump" ) );
        SW_EXPECT_FALSE( pPawn->getIntent().isDown( 0 ) );
        Internal::updateWithUI( manager, input, ui ); // 2 — 누른 채
        SW_EXPECT_FALSE( pPawn->getIntent().isDown( 0 ) );
        Internal::updateWithUI( manager, input, ui ); // 3 — 뗌
        ui.closeScreen( menu );
        Internal::updateWithUI( manager, input, ui ); // 4 — 메뉴 닫힘
        Internal::updateWithUI( manager, input, ui ); // 5 — 다시 누름: 폰의 것
        SW_EXPECT_TRUE( pPawn->getIntent().isDown( 0 ) );

        // 모달이 뜨면 이동 의도가 0 이다(W 를 누르고 있어도).
        UIScreenDesc modalDesc{};
        modalDesc._layer  = UILayer::Modal;
        modalDesc._bModal = true;
        (void)ui.pushScreen( sw::make_unique<UIScreen>( modalDesc, sw::make_unique<uitest::TestPanelWidget>( "modalRoot" ) ) );
        Internal::updateWithUI( manager, input, ui ); // 6
        Internal::updateWithUI( manager, input, ui ); // 7 — W
        SW_EXPECT_TRUE( input.getInputMap().getVector2D( "Move" )._y > 0.0f );
        SW_EXPECT_EQUAL( 0.0f, pPawn->getIntent()._move._y );
        SW_EXPECT_FALSE( pPawn->getIntent().isDown( 0 ) );

        input.detachVirtualInput();
        manager.endPlay();
        ui.shutdown();
    }
    input.shutdown();
}

/**
 * @brief [ControlTest] 재생 진입점 — 기록 파일을 `IntentPlayback::startFromFile` 로 열면 폰마다 기록 조종자가 서서 기록과 같은 궤적을 따라가고,
 *        씬에 폰이 없는 트랙은 건너뛰며, 파일이 없으면 오류를 채우고 false 이다
 */
SW_TEST_CASE( ControlTest, PlaybackEntryReplaysAFileAndSkipsMissingPawns )
{
    using Internal = ControlTestInternal;
    vector<float3> listRecordedPosition;
    const string   path = test::makeTempPath( "playback.swintent" );
    {
        GameObjectManager manager;
        PawnComponent*    pPawn = Internal::spawnAnalogPawn( manager, "Runner" );
        PawnComponent*    pGone = Internal::spawnAnalogPawn( manager, "Gone" );
        ControlSystem::ensureFor( manager ).setRecording( true );
        manager.beginPlay();
        AIControllerComponent* pAI     = Internal::possessWithWanderingAI( manager, *pPawn );
        AIControllerComponent* pAIGone = Internal::possessWithWanderingAI( manager, *pGone );
        for ( uint32 tick = 0; tick < Internal::kTickCount; ++tick )
        {
            Internal::setThrottle( *pAI, tick );
            Internal::setThrottle( *pAIGone, tick );
            manager.update( ControlTestInternal::kDeltaTime );
            listRecordedPosition.push_back( ControlTestInternal::findPosition( *pPawn->getOwner() ) );
        }
        SW_ASSERT_TRUE( ControlSystem::find( manager )->getHistory().saveToFile( path ) );
        manager.endPlay();
    }

    {
        GameObjectManager manager; // "Gone" 폰이 없는 씬
        PawnComponent*    pPawn = Internal::spawnAnalogPawn( manager, "Runner" );
        manager.beginPlay();
        string error;
        uint32 started = 99;
        SW_ASSERT_TRUE_MSG( IntentPlayback::startFromFile( manager, path, false, error, &started ), error.c_str() );
        SW_EXPECT_EQUAL( 1u, started );
        GameObject* pTrackObject = manager.findGameObjectByName( "IntentTrack.Runner" );
        SW_ASSERT_NOT_NULL( pTrackObject );
        SW_EXPECT_TRUE( pTrackObject->getComponent<IntentTrackControllerComponent>() != nullptr );
        for ( uint32 tick = 0; tick < Internal::kTickCount; ++tick )
        {
            manager.update( ControlTestInternal::kDeltaTime );
            const float3 position = ControlTestInternal::findPosition( *pPawn->getOwner() );
            const float3 recorded = listRecordedPosition[tick];
            SW_EXPECT_TRUE( position._x == recorded._x && position._y == recorded._y && position._z == recorded._z );
        }
        manager.endPlay();
    }
    (void)FileUtil::tryRemoveFile( path ); // 임시 파일 정리 — 지우지 못해도 테스트 결과와 무관하다

    GameObjectManager manager;
    string            error;
    uint32            started = 99;
    SW_EXPECT_FALSE( IntentPlayback::startFromFile( manager, path, false, error, &started ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_EQUAL( 0u, started );
}

/**
 * @brief [ControlTest] 서버 루프가 복제 키트의 의도로 원격 조종 폰을 몬다 — 클라이언트 폰의 의도를 `sendIntent` 로 보내고 서버가 `popIntent` → `receiveIntent` 로 넣으면 같은 궤적이다
 * @details 클라이언트는 떠도는 AI 가 아날로그 · 점프를 내는 폰을 틱마다 갱신하고 그 의도를 보낸다. 서버는 의도가 도착한 뒤부터 틱마다 `popIntent` 로 꺼낸 의도를 원격 조종자에게
 *          넣고 폰을 갱신한다(`ShooterNetPawnComponent` 와 같은 모양의 서버 루프). 서버의 폰은 같은 입력 틱의 클라이언트 폰과 같은 자리여야 한다.
 */
SW_TEST_CASE( ControlTest, ServerLoopDrivesRemotePawnFromReplicationIntents )
{
    using Internal                                = ControlTestInternal;
    static constexpr uint16 kServerPort           = 4400;
    static constexpr uint16 kClientPort           = 5400;
    static constexpr uint32 kServerInputDelayTick = 12; // 지연 50 ms(3 틱)에 여유를 더해, 꺼낼 틱이 늘 도착해 있게 한다
    static constexpr int32  kFrameCount           = 200;

    LoopbackNetwork        network;
    NetEmulationTransport  serverTransport( network.createEndpoint( kServerPort ), 31u );
    NetEmulationTransport  clientTransport( network.createEndpoint( kClientPort ), 32u );
    NetEmulationConditions conditions;
    conditions._latency = 0.05;
    serverTransport.setDefaultConditions( conditions );
    clientTransport.setDefaultConditions( conditions );
    NetHostConfig hostSettings;
    hostSettings._sendInterval = 1.0 / 60.0;
    NetHost serverHost;
    NetHost clientHost;
    serverHost.initialize( &serverTransport, hostSettings );
    clientHost.initialize( &clientTransport, hostSettings );
    SW_ASSERT_TRUE( serverHost.listen() );
    SW_ASSERT_TRUE( clientHost.connect( NetAddress::makeLoopback( kServerPort ) ) );
    ReplicationServerConfig serverSettings;
    serverSettings._tickInterval = 1.0f / 60.0f;
    ReplicationClientConfig clientSettings;
    clientSettings._tickInterval = 1.0f / 60.0f;
    ReplicationServer server;
    ReplicationClient client;
    SW_ASSERT_TRUE( server.initialize( &serverHost, serverSettings, nullptr ) );
    SW_ASSERT_TRUE( client.initialize( &clientHost, clientSettings ) );

    GameObjectManager clientManager;
    GameObjectManager serverManager;
    PawnComponent*    pClientPawn = Internal::spawnAnalogPawn( clientManager, "Runner" );
    PawnComponent*    pServerPawn = Internal::spawnAnalogPawn( serverManager, "Runner" );
    auto*             pRemote     = serverManager.createGameObject( hashed_string( "Remote" ) )->addComponent<RemoteControllerComponent>();
    clientManager.beginPlay();
    serverManager.beginPlay();
    AIControllerComponent* pAI = Internal::possessWithWanderingAI( clientManager, *pClientPawn );
    pRemote->possess( *pServerPawn );
    pRemote->setNextInputTick( 1 );

    float64        time          = 0.0;
    uint32         clientTick    = 0;
    uint32         serverFrame   = 0;
    uint32         nextInputTick = 1;
    int32          compareCount  = 0;
    int32          exactCount    = 0;
    vector<float3> listClientPosition( 1, float3{} ); // 입력 틱이 인덱스(0 은 비워 둔다)
    vector<uint8>  buffer;
    for ( int32 frame = 0; frame < kFrameCount; ++frame )
    {
        time += 1.0 / 60.0;
        serverTransport.update( time );
        clientTransport.update( time );
        serverHost.update( time );
        clientHost.update( time );
        int32          connectionID = -1;
        NetChannelType channel      = NetChannelType::Unreliable;
        while ( serverHost.receiveMessage( connectionID, channel, buffer ) )
        {
            SW_EXPECT_TRUE( NetHandleResult::Handled == server.handleMessage( connectionID, buffer ) );
        }
        while ( clientHost.receiveMessage( connectionID, channel, buffer ) )
        {
            SW_EXPECT_TRUE( NetHandleResult::Handled == client.handleMessage( 0, buffer ) );
        }
        if ( clientHost.getConnectionState( 0 ) != NetConnectionState::Connected )
            continue;

        // 클라이언트 틱 — 폰을 한 틱 돌리고 그 틱의 의도를 보낸다(입력 틱은 1 부터).
        ++clientTick;
        Internal::setThrottle( *pAI, clientTick );
        if ( clientTick == 10 )
            pAI->pressButton( "Jump" );
        clientManager.update( ControlTestInternal::kDeltaTime );
        listClientPosition.push_back( ControlTestInternal::findPosition( *pClientPawn->getOwner() ) );
        SW_ASSERT_TRUE( client.sendIntent( clientTick, pClientPawn->getIntent() ) );
        client.update( ControlTestInternal::kDeltaTime );

        // 서버 틱 — 입력이 도착해 있을 때부터 틱마다 의도 하나를 꺼내 원격 조종자에게 넣고 폰을 돌린다.
        ++serverFrame;
        if ( serverFrame <= kServerInputDelayTick )
            continue;
        ControlIntent intent;
        bool          bExact = false;
        SW_ASSERT_TRUE( server.popIntent( 0, nextInputTick, intent, bExact ) );
        exactCount += bExact ? 1 : 0;
        pRemote->receiveIntent( nextInputTick, intent );
        serverManager.update( ControlTestInternal::kDeltaTime );
        const float3 serverPosition = ControlTestInternal::findPosition( *pServerPawn->getOwner() );
        const float3 clientPosition = listClientPosition[nextInputTick];
        SW_EXPECT_TRUE_MSG( serverPosition._x == clientPosition._x && serverPosition._z == clientPosition._z, ( "input tick " + std::to_string( nextInputTick ) ).c_str() );
        if ( nextInputTick == 10 )
            SW_EXPECT_TRUE( pServerPawn->wasButtonTriggered( 0 ) );
        ++nextInputTick;
        ++compareCount;
    }
    SW_EXPECT_TRUE( compareCount > 100 );
    SW_EXPECT_EQUAL( compareCount, exactCount ); // 늘 도착해 있어 되풀이한 틱이 없다
    SW_EXPECT_EQUAL( 0u, pRemote->getMissingTickCount() );
    clientManager.endPlay();
    serverManager.endPlay();
}

/**
 * @brief [ControlTest] 기록을 키 틱 번째부터 틀면 그 번째 이전은 건너뛰고, 이미 끝난 트랙은 건드리지 않는다 — 되감기가 키 틱에서 이어 돌 때의 재생 시작
 */
SW_TEST_CASE( ControlTest, PlaybackStartsFromAGivenTickOfTheTrack )
{
    using Internal = ControlTestInternal;
    ControlIntentHistory history;
    vector<float3>       listPosition;
    {
        GameObjectManager manager;
        PawnComponent*    pPawn = Internal::spawnAnalogPawn( manager, "Runner" );
        ControlSystem::ensureFor( manager ).setRecording( true );
        manager.beginPlay();
        AIControllerComponent* pAI = Internal::possessWithWanderingAI( manager, *pPawn );
        for ( uint32 tick = 0; tick < 40; ++tick )
        {
            Internal::setThrottle( *pAI, tick );
            manager.update( ControlTestInternal::kDeltaTime );
            listPosition.push_back( ControlTestInternal::findPosition( *pPawn->getOwner() ) );
        }
        const ControlIntentHistory& recorded = ControlSystem::find( manager )->getHistory();
        SW_ASSERT_EQUAL( 1, recorded.getTrackCount() );
        BitWriter writer;
        SW_ASSERT_TRUE( recorded.write( writer ) );
        BitReader reader( writer.getBytes().data(), static_cast<int32>( writer.getBytes().size() ) );
        string    error;
        SW_ASSERT_TRUE_MSG( history.read( reader, error ), error.c_str() );
        manager.endPlay();
    }

    // 틱 25 에서 이어 틀기 — 다른 씬에서 폰을 틱 24 뒤의 자리에 두고, 트랙의 25 번째부터 틀면 이후 궤적이 같다.
    GameObjectManager manager;
    PawnComponent*    pPawn = Internal::spawnAnalogPawn( manager, "Runner" );
    manager.beginPlay();
    pPawn->getOwner()->getPrimarySceneComponent()->setWorldPosition( listPosition[24] );
    uint32 skipped = 99;
    SW_EXPECT_EQUAL( 1u, IntentPlayback::start( manager, history, false, 25, &skipped ) );
    SW_EXPECT_EQUAL( 0u, skipped );
    for ( uint32 tick = 25; tick < 40; ++tick )
    {
        manager.update( ControlTestInternal::kDeltaTime );
        const float3 position = ControlTestInternal::findPosition( *pPawn->getOwner() );
        SW_EXPECT_TRUE( position._x == listPosition[tick]._x && position._z == listPosition[tick]._z );
    }
    manager.endPlay();

    // 트랙 길이 이상이면 이미 끝난 트랙이라 시작하지도 건너뛰지도 않는다.
    GameObjectManager doneManager;
    Internal::spawnAnalogPawn( doneManager, "Runner" );
    doneManager.beginPlay();
    uint32 doneSkipped = 99;
    SW_EXPECT_EQUAL( 0u, IntentPlayback::start( doneManager, history, false, 40, &doneSkipped ) );
    SW_EXPECT_EQUAL( 0u, doneSkipped );
    SW_EXPECT_TRUE( doneManager.findGameObjectByName( "IntentTrack.Runner" ) == nullptr );
    doneManager.endPlay();
}
