#include "pch.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/World/Lifetime/PrefabPool.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestScopes.h"

// PrefabPoolTest — 돌려받은 인스턴스를 꺼 두었다가 다시 켜서 내주는 풀(재사용 · 상한 · 지워진 핸들 · 빈 경로).

using namespace sw;

/**
 * @brief [PrefabPoolTest] 돌려준 인스턴스는 꺼져 쉬다가 다음 `acquire` 에 켜져 그대로 돌아온다 — 새로 만들지 않는다
 * @details 상한이 하나면 둘째로 돌려준 것은 지운다. 씬이 지운 쉬는 인스턴스는 건너뛴다. 프리팹 경로가 비면 새로 만들지 못하고 nullptr 이다.
 */
SW_TEST_CASE( PrefabPoolTest, ReleasedInstancesAreReusedUpToTheIdleLimit )
{
    GameObjectManager manager;
    PrefabPool        pool( string{}, 1u );
    GameObject*       pFirst  = manager.createGameObject( hashed_string( "Bullet0" ) );
    GameObject*       pSecond = manager.createGameObject( hashed_string( "Bullet1" ) );
    SW_ASSERT_NOT_NULL( pFirst );
    SW_ASSERT_NOT_NULL( pSecond );
    const GameObjectHandle first  = pFirst->getHandle();
    const GameObjectHandle second = pSecond->getHandle();

    pool.release( manager, first );
    SW_EXPECT_EQUAL( 1u, pool.getIdleCount() );
    SW_EXPECT_FALSE( pFirst->isActive() );
    pool.release( manager, second ); // 상한 1 — 지운다
    SW_EXPECT_EQUAL( 1u, pool.getIdleCount() );
    SW_EXPECT_TRUE( pSecond->isPendingDestroy() );

    GameObject* pReused = pool.acquire( manager, "Bullet" );
    SW_EXPECT_TRUE( pReused == pFirst );
    SW_EXPECT_TRUE( pReused->isActive() );
    SW_EXPECT_EQUAL( 0u, pool.getIdleCount() );
    SW_EXPECT_EQUAL( 0u, pool.getCreatedCount() );

    // 쉬는 동안 씬이 지운 인스턴스는 건너뛰고, 경로가 비어 새로 만들지 못한다.
    pool.release( manager, first );
    manager.destroyObject( pFirst );
    test::ScopedLogSuppressor suppressor; // 빈 경로 오류 한 줄
    SW_EXPECT_NULL( pool.acquire( manager, "Bullet" ) );
    SW_EXPECT_EQUAL( 0u, pool.getIdleCount() );
}
