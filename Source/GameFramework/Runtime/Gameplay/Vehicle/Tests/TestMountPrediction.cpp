// MountPredictionTest — 컨트롤러 없는 탈것의 걸음은 (상태, 의도)만의 순수 함수다: 되감아 다시 돌리면 같고, 혼자 도는 틱과 같은 궤적이다(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "GameFramework/Runtime/Character/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/MountMovementComponent.h"

using namespace sw;

namespace
{
    constexpr float32 kMountTestTick = 1.0f / 30.0f;

    MountMovementComponent* addHorse( GameObjectManager& manager )
    {
        GameObject* pHorse = manager.createGameObject( hashed_string( "Horse" ) );
        (void)pHorse->addComponent<SceneComponent>();
        pHorse->addComponent<PawnComponent>()->setAutoPossess( PawnAutoPossess::None );
        return pHorse->addComponent<MountMovementComponent>();
    }

    vector<ControlIntent> makeRideIntents()
    {
        vector<ControlIntent> listIntent( 90 );
        for ( size_t tick = 0; tick < listIntent.size(); ++tick )
        {
            listIntent[tick]._move       = tick < 70 ? float2{ 0.0f, 1.0f } : float2{};
            listIntent[tick]._controlYaw = tick < 30 ? 0.0f : 1.2f;
            listIntent[tick].quantize();
        }
        return listIntent;
    }
} // namespace

/** @brief [MountPredictionTest] 되감아 다시 돌린 말은 이어 돌린 말과 비트까지 같고, 걸음새 상한은 그 상태의 값을 쓴다 */
SW_TEST_CASE( MountPredictionTest, ReplayEqualsContinuing )
{
    GameObjectManager       manager;
    MountMovementComponent* pMount = addHorse( manager );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pMount->setNetDriven( true ) );
    const vector<ControlIntent> listIntent = makeRideIntents();
    vector<MountMoveState>      listState( 1, pMount->getState() );
    for ( const ControlIntent& intent : listIntent )
    {
        listState.push_back( pMount->simulate( listState.back(), intent, kMountTestTick ) );
    }
    MountMoveState state = listState[25];
    for ( size_t tick = 25; tick < listIntent.size(); ++tick )
    {
        state = pMount->simulate( state, listIntent[tick], kMountTestTick );
        SW_REQUIRE_TRUE( state._position == listState[tick + 1u]._position && state._facingYaw == listState[tick + 1u]._facingYaw && state._forwardSpeed == listState[tick + 1u]._forwardSpeed );
    }
    SW_EXPECT_TRUE( listState.back()._position._z > 5.0f );

    MountMoveState capped = listState[60];
    capped._maxGait       = static_cast<uint8>( MountGait::Walk );
    SW_EXPECT_TRUE( pMount->simulate( capped, listIntent[60], kMountTestTick ).getGait() == MountGait::Walk );
    SW_EXPECT_TRUE( pMount->simulate( listState[60], listIntent[60], kMountTestTick ).getGait() == MountGait::Canter );
}

/** @brief [MountPredictionTest] 같은 의도 열이면 네트 걸음과 혼자 도는 틱이 같은 궤적이다 — 걸음 계산이 하나다 */
SW_TEST_CASE( MountPredictionTest, NetStepMatchesTheTick )
{
    GameObjectManager               manager;
    MountMovementComponent*         pTicked = addHorse( manager );
    IntentTrackControllerComponent* pTrack  = manager.createGameObject( hashed_string( "Track" ) )->addComponent<IntentTrackControllerComponent>();
    manager.beginPlay();
    const vector<ControlIntent> listIntent = makeRideIntents();
    pTrack->play( *pTicked->getOwner()->getComponent<PawnComponent>(), vector<ControlIntent>( listIntent ), false );
    MountMoveState state = pTicked->getState();
    for ( const ControlIntent& intent : listIntent )
    {
        manager.update( kMountTestTick );
        state = pTicked->simulate( state, intent, kMountTestTick );
    }
    const float3 ticked = pTicked->getState()._position;
    SW_EXPECT_NEAR( state._position._x, ticked._x, 1.0e-3f );
    SW_EXPECT_NEAR( state._position._z, ticked._z, 1.0e-3f );
}

/** @brief [MountPredictionTest] 네트가 움직이는 말은 틱이 옮기지 않고, 그릴 자리와 시뮬레이션 자리가 따로다 */
SW_TEST_CASE( MountPredictionTest, NetDrivenMountIgnoresTheTick )
{
    GameObjectManager       manager;
    MountMovementComponent* pMount = addHorse( manager );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pMount->setNetDriven( true ) );
    MountMoveState state = pMount->getState();
    state._position      = float3{ 3.0f, 0.0f, 4.0f };
    state._forwardSpeed  = 5.0f;
    pMount->setState( state, float3{ 3.5f, 0.0f, 4.0f } );
    manager.update( kMountTestTick );
    SW_EXPECT_NEAR( 3.0f, pMount->getState()._position._x, 1.0e-5f );
    SW_EXPECT_NEAR( 3.5f, pMount->getOwner()->getPrimarySceneComponent()->getWorldPosition()._x, 1.0e-5f );
}

/** @brief [MountPredictionTest] 물리 컨트롤러로 가는 탈것은 예측을 켤 수 없다(오류) — 조용히 다른 길로 돌지 않는다 */
SW_TEST_CASE( MountPredictionTest, ControllerMountRefusesPrediction )
{
    GameObjectManager       manager;
    MountMovementComponent* pMount = addHorse( manager );
    (void)pMount->getOwner()->addComponent<CharacterControllerComponent>();
    manager.beginPlay();
    SW_EXPECT_LOG( "MountMovement.NotPredictable" );
    SW_EXPECT_FALSE( pMount->setNetDriven( true ) );
    SW_EXPECT_FALSE( pMount->isNetDriven() );
}

/** @brief [MountPredictionTest] 네트가 움직이는 탈것의 시뮬레이션 자리는 월드 원점과 함께 옮겨진다 — 남으면 다음 걸음이 옛 원점의 자리에서 시작한다 */
SW_TEST_CASE( MountPredictionTest, NetPositionFollowsTheWorldOrigin )
{
    GameObjectManager manager;
    GameObject* const pHorse = manager.createGameObject( hashed_string( "Horse" ) );
    pHorse->addComponent<SceneComponent>()->setLocalPosition( float3{ 100.0f, 0.0f, 0.0f } );
    MountMovementComponent* const pMount = pHorse->addComponent<MountMovementComponent>();
    manager.beginPlay();
    SW_REQUIRE_TRUE( pMount->setNetDriven( true ) );
    pMount->onWorldOriginShifted( float3{ -100.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR( 0.0f, pMount->getState()._position._x, 1.0e-4f );
}
