// ActionRoomHUDTest — 룸 이벤트(문 잠김 · 방 비움 · 쓰러짐)가 HUD 뷰모델의 보임과 카메라 흔들림 요청이 되는지 본다(nogpu, 화면 없음).
#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/CameraShakeChannel.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionCombatEvents.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Client/ActionRoomHUDComponent.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Client/ActionRoomHUDViewModel.h"
#include "GameFramework/Runtime/Character/Combat/Damage/DamageEvents.h"
#include "GameFramework/Runtime/Foundation/Service/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

using namespace sw;

/** @brief [ActionRoomHUDTest] 문이 잠긴 동안 잠김 표시가, 방을 비우면 알림과 카메라 흔들림(보스는 두 배)이 나고 알림은 시간이 지나면 사라진다 */
SW_TEST_CASE( ActionRoomHUDTest, RoomEventsShowOnTheHUDAndShakeTheCamera )
{
    EventDispatcher dispatcher;
    SubsystemScope  scope;
    SW_REQUIRE_TRUE( scope.bind<EventDispatcher>( &dispatcher ) );
    game::ScopedGameScope   scopedScope( scope );
    GameObjectManager       manager;
    ActionRoomHUDComponent* pHUD = manager.createGameObject( hashed_string( "RoomHUD" ) )->addComponent<ActionRoomHUDComponent>();
    vector<float32>         listShake;
    const Subscription      shakeSubscription = manager.getCameraRegistry().getShakeChannel().add(
        SW_DELEGATE_LAMBDA( CameraShakeRequestCallback, [&listShake]( const CameraShakeRequest& request )
         { listShake.push_back( request._amplitude ); } ) );
    manager.beginPlay();
    const ActionRoomHUDViewModel* pViewModel = pHUD->findViewModel();
    SW_REQUIRE_NOT_NULL( pViewModel );
    SW_EXPECT_TRUE( pViewModel->getGateLockedVisibility() == WidgetVisibility::Collapsed );

    ClearGateStateChangedEvent gate;
    gate._bLocked = SW_TRUE;
    GameEventUtil::send( gate );
    manager.update( 0.1f );
    SW_EXPECT_TRUE( pViewModel->getGateLockedVisibility() == WidgetVisibility::HitTestInvisible );
    SW_EXPECT_TRUE( listShake.empty() );

    RoomClearedEvent cleared;
    GameEventUtil::send( cleared );
    cleared._bBossDefeated = SW_TRUE;
    GameEventUtil::send( cleared );
    gate._bLocked = SW_FALSE;
    GameEventUtil::send( gate );
    manager.update( 0.1f );
    SW_EXPECT_TRUE( pViewModel->getGateLockedVisibility() == WidgetVisibility::Collapsed );
    SW_EXPECT_TRUE( pViewModel->getClearedVisibility() == WidgetVisibility::HitTestInvisible );
    SW_REQUIRE_EQUAL( size_t( 2 ), listShake.size() );
    SW_EXPECT_NEAR( listShake[0] * 2.0f, listShake[1], 1.0e-4f );

    GameEventUtil::send( PlayerDefeatedInRoomEvent{} );
    manager.update( 0.1f );
    SW_EXPECT_TRUE( pViewModel->getClearedVisibility() == WidgetVisibility::Collapsed );
    SW_EXPECT_TRUE( pViewModel->getDefeatedVisibility() == WidgetVisibility::HitTestInvisible );
    manager.update( 10.0f );
    SW_EXPECT_TRUE( pViewModel->getDefeatedVisibility() == WidgetVisibility::Collapsed );
    SW_EXPECT_EQUAL( size_t( 3 ), listShake.size() );
    manager.endPlay();
    SW_EXPECT_TRUE( pHUD->findViewModel() == nullptr );
}

/** @brief [ActionRoomHUDTest] 이 씬의 유닛이 맞으면 맞은 자리에서 거리로 약해지는 흔들림이 나고, 이 씬에 없는 대상의 피해는 아무 일도 없다 */
SW_TEST_CASE( ActionRoomHUDTest, DamageShakesTheCameraAtTheTarget )
{
    EventDispatcher dispatcher;
    SubsystemScope  scope;
    SW_REQUIRE_TRUE( scope.bind<EventDispatcher>( &dispatcher ) );
    game::ScopedGameScope      scopedScope( scope );
    GameObjectManager          manager;
    vector<CameraShakeRequest> listShake;
    const Subscription         shakeSubscription = manager.getCameraRegistry().getShakeChannel().add(
        SW_DELEGATE_LAMBDA( CameraShakeRequestCallback, [&listShake]( const CameraShakeRequest& request )
            { listShake.push_back( request ); } ) );
    (void)manager.createGameObject( hashed_string( "RoomHUD" ) )->addComponent<ActionRoomHUDComponent>();
    GameObject* const pTarget = manager.createGameObject( hashed_string( "Target" ) );
    pTarget->addComponent<SceneComponent>()->setLocalPosition( float3{ 3.0f, 0.0f, 1.0f } );
    manager.beginPlay();

    GameEventUtil::send( DamageAppliedEvent{} );
    SW_EXPECT_TRUE( listShake.empty() );
    DamageAppliedEvent damage;
    damage._target = pTarget->getHandle();
    damage._amount = 5;
    GameEventUtil::send( damage );
    SW_REQUIRE_EQUAL( size_t( 1 ), listShake.size() );
    SW_EXPECT_NEAR( 3.0f, listShake[0]._origin._x, 1.0e-4f );
    SW_EXPECT_TRUE( listShake[0]._amplitude > 0.0f && listShake[0]._falloffRadius > 0.0f );
    SW_EXPECT_EQUAL( pTarget->getObjectID(), listShake[0]._sourceObjectID );
    manager.endPlay();
}
