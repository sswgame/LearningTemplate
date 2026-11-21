// ComponentTickTimeTest — 프레임의 컴포넌트 틱 시간을 잰다(감시 값 `Scene.ComponentTickShare` 의 재료, 틱 주기 등급 판정의 재는 장치)(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tests/TickModeTestComponents.h"

using namespace sw;

/**
 * @brief [ComponentTickTimeTest] 두 틱 단계가 끝난 프레임의 그룹 디스패치 시간이 게시된다 — 프레임을 돌기 전에는 0 이다
 * @details 변이: 첫 단계의 되돌림이나 마지막 단계의 게시를 빼면 첫 프레임 뒤 값이 0 으로 남아 단언이 진다.
 */
SW_TEST_CASE( ComponentTickTimeTest, FrameTickTimeIsPublishedAfterBothPhases )
{
    GameObjectManager manager;
    SW_EXPECT_EQUAL( 0, static_cast<int32>( manager.getLastFrameComponentTickNanoseconds() ) );
    for ( uint32 index = 0; index < 64; ++index )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( "Mover" ) );
        SW_REQUIRE_NOT_NULL( pObject );
        SW_REQUIRE_NOT_NULL( pObject->addComponent<TickModeMoverTestComponent>() );
    }
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_TRUE( manager.getLastFrameComponentTickNanoseconds() > 0 );
}
