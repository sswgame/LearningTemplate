// MinimapTest — 표시가 놓일 자리(보는 쪽이 위, 범위 밖은 가장자리)를 본다(nogpu, 화면 없음).
#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/Math/MathUtil.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Client/HUD/HUDControllerComponent.h"
#include "GameFramework/Client/HUD/MinimapComponent.h"
#include "GameFramework/Client/HUD/MinimapMarkerComponent.h"
#include "GameFramework/Runtime/Character/Control/ControlEvents.h"
#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

using namespace sw;

/** @brief [MinimapTest] 요 0 이면 +Z 가 위(-Y), +X 가 오른쪽이고, 요 90° 면 +X 가 위다 */
SW_TEST_CASE( MinimapTest, ForwardIsUp )
{
    const float2           half{ 100.0f, 100.0f };
    const float3           center{ 5.0f, 0.0f, 5.0f };
    const MinimapPlacement ahead = MinimapComponent::computePlacement( center, 0.0f, 20.0f, half, float3{ 5.0f, 3.0f, 15.0f } );
    SW_EXPECT_TRUE( ahead._bInRange == SW_TRUE );
    SW_EXPECT_NEAR( 0.0f, ahead._offset._x, 1.0e-3f );
    SW_EXPECT_NEAR( -50.0f, ahead._offset._y, 1.0e-3f );

    const MinimapPlacement right = MinimapComponent::computePlacement( center, 0.0f, 20.0f, half, float3{ 15.0f, 0.0f, 5.0f } );
    SW_EXPECT_NEAR( 50.0f, right._offset._x, 1.0e-3f );
    SW_EXPECT_NEAR( 0.0f, right._offset._y, 1.0e-3f );

    const MinimapPlacement turned = MinimapComponent::computePlacement( center, MathUtil::kHalfPi, 20.0f, half, float3{ 15.0f, 0.0f, 5.0f } );
    SW_EXPECT_NEAR( 0.0f, turned._offset._x, 1.0e-3f );
    SW_EXPECT_NEAR( -50.0f, turned._offset._y, 1.0e-3f );
}

/** @brief [MinimapTest] 범위 밖은 같은 방향의 가장자리 점이고 범위 밖으로 표시된다 */
SW_TEST_CASE( MinimapTest, OutOfRangeSticksToTheEdge )
{
    const MinimapPlacement placement = MinimapComponent::computePlacement( float3{}, 0.0f, 10.0f, float2{ 60.0f, 60.0f }, float3{ 30.0f, 0.0f, 40.0f } );
    SW_EXPECT_TRUE( placement._bInRange == SW_FALSE );
    SW_EXPECT_NEAR( 36.0f, placement._offset._x, 1.0e-3f );
    SW_EXPECT_NEAR( -48.0f, placement._offset._y, 1.0e-3f );
}

/** @brief [MinimapTest] 빙의를 따라간다 — 다른 폰에 빙의하면 그 폰이 가운데이고, 남의 빙의는 보지 않으며, 놓으면 제 오브젝트로 돌아온다 */
SW_TEST_CASE( MinimapTest, FollowsThePossessedPawn )
{
    EventDispatcher dispatcher;
    SubsystemScope  scope;
    SW_REQUIRE_TRUE( scope.bind<EventDispatcher>( &dispatcher ) );
    game::ScopedGameScope scopedScope( scope );
    GameObjectManager     manager;

    const auto createAt = [&manager]( const utf8* pName, float32 x )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
        pObject->addComponent<SceneComponent>()->setWorldPosition( float3{ x, 0.0f, 0.0f } );
        return pObject;
    };
    GameObject* const pPlayer = createAt( "Player", 0.0f );
    pPlayer->addComponent<HUDControllerComponent>();
    MinimapComponent* const       pMinimap = pPlayer->addComponent<MinimapComponent>();
    GameObject* const             pVehicle = createAt( "Vehicle", 15.0f );
    GameObject* const             pOther   = createAt( "Other", -15.0f );
    MinimapMarkerComponent* const pMarker  = createAt( "Marker", 15.0f )->addComponent<MinimapMarkerComponent>();
    manager.beginPlay();
    SW_EXPECT_NEAR( 0.5f, pMinimap->computeMarkerPlacement( *pMarker )._offset._x, 1.0e-3f ); // 기본 범위 30 m

    PossessionChangedEvent event;
    event._previousPawn = pOther->getHandle();
    event._pawn         = pVehicle->getHandle();
    GameEventUtil::send( event ); // 다른 플레이어가 탈것에 탔다
    SW_EXPECT_NEAR( 0.5f, pMinimap->computeMarkerPlacement( *pMarker )._offset._x, 1.0e-3f );

    event._previousPawn = pPlayer->getHandle();
    GameEventUtil::send( event );
    SW_EXPECT_NEAR( 0.0f, pMinimap->computeMarkerPlacement( *pMarker )._offset._x, 1.0e-3f );

    event._previousPawn = pVehicle->getHandle();
    event._pawn         = GameObjectHandle{};
    GameEventUtil::send( event );
    SW_EXPECT_NEAR( 0.5f, pMinimap->computeMarkerPlacement( *pMarker )._offset._x, 1.0e-3f );
    manager.endPlay();
}
