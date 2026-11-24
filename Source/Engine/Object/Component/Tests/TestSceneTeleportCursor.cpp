// SceneTeleportCursorTest — 순간이동은 아래에 붙은 컴포넌트까지 한 번씩 알려지고, 그냥 옮기기는 알려지지 않는다(nogpu, 헤드리스).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/SceneTeleportCursor.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

using namespace sw;

/** @brief [SceneTeleportCursorTest] 부모가 순간이동하면 자식의 자리도 한 번 알리고, 운동을 지우는 종류를 가린다 — 걸어간 이동은 알리지 않는다 */
SW_TEST_CASE( SceneTeleportCursorTest, TeleportReachesTheChildOnceAndWalkingDoesNot )
{
    GameObjectManager manager;
    GameObject*       pParent = manager.createGameObject( hashed_string( "Parent" ) );
    GameObject*       pChild  = manager.createGameObject( hashed_string( "Child" ) );
    SceneComponent*   pRoot   = pParent->addComponent<SceneComponent>();
    SceneComponent*   pHeld   = pChild->addComponent<SceneComponent>();
    SW_REQUIRE_TRUE( pChild->attachToParent( pParent ) );

    SceneTeleportCursor cursor;
    SW_EXPECT_FALSE( cursor.consume( *pHeld ).has_value() );
    pRoot->setWorldPosition( float3{ 50.0f, 0.0f, 0.0f } );
    SW_EXPECT_FALSE( cursor.consume( *pHeld ).has_value() );

    pRoot->teleportTo( float3{ 1.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( cursor.consume( *pHeld ) == TeleportType::TeleportPhysics );
    SW_EXPECT_FALSE( cursor.consume( *pHeld ).has_value() );

    pRoot->teleportTo( float3{ 2.0f, 0.0f, 0.0f }, TeleportType::ResetPhysics );
    pRoot->teleportTo( float3{ 3.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( cursor.consume( *pHeld ) == TeleportType::ResetPhysics ); // 그사이 한 번이라도 운동을 지웠다
}

/** @brief [SceneTeleportCursorTest] 월드 원점 이동은 좌표만 바꾼다 — 게임의 순간이동으로 알리지 않아, 걷기만 해도 칼 궤적 · 좌석 가속을 잃거나 모습이 다시 피어나지 않는다 */
SW_TEST_CASE( SceneTeleportCursorTest, WorldOriginShiftIsNotATeleport )
{
    Scene           scene( "SceneTeleportCursorTest" );
    GameObject*     pObject = scene.getObjectManager()->createGameObject( hashed_string( "Walker" ) );
    SceneComponent* pRoot   = pObject->addComponent<SceneComponent>();
    scene.getObjectManager()->mergePendingAdds();
    SceneTeleportCursor cursor;
    const uint16        shiftSerial = pRoot->getOriginShiftSerial();
    SW_REQUIRE_TRUE( scene.shiftWorldOrigin( double3{ 1000.0, 0.0, 500.0 } ) );
    SW_EXPECT_NEAR( -1000.0f, pRoot->getWorldPosition()._x, 1.0e-3f );
    SW_EXPECT_EQUAL( static_cast<int32>( shiftSerial ) + 1, static_cast<int32>( pRoot->getOriginShiftSerial() ) );
    SW_EXPECT_FALSE( cursor.consume( *pRoot ).has_value() );
}
