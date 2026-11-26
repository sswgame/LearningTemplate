#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/GameMode/PrefabPool.h"
#include "GameFramework/Runtime/Foundation/GameMode/Tests/PrefabPoolComponentSpawner.h"

// PrefabPoolTest — 재사용 · 상한 · 미리 만들기 · 다시 내주기 · 조건 돌려받기 · 모두 지우기 · 풀 계약(되돌리기). 인스턴스는 프리팹 대신 코드로 만든 오브젝트다(nogpu, 에셋 없음).

using namespace sw;

namespace
{
    struct PrefabPoolTestUtil
    {
        /** @brief 지워졌거나 지워지는 중이면 true 입니다. */
        static bool isGone( GameObjectManager& manager, GameObjectHandle handle )
        {
            const GameObject* pObject = manager.resolveGameObject( handle );
            return pObject == nullptr || pObject->isPendingDestroy();
        }
    };
    using Util = PrefabPoolTestUtil;
} // namespace

/** @brief [PrefabPoolTest] 돌려받은 인스턴스는 꺼 두었다가 같은 오브젝트로 다시 켜진다 */
SW_TEST_CASE( PrefabPoolTest, ReleasedInstanceIsReusedInsteadOfCreated )
{
    GameObjectManager manager;
    PrefabPool        pool    = PrefabPoolComponentSpawner<>::makePool( 4 );
    GameObject*       pObject = pool.acquire( manager, "Shot" );
    SW_REQUIRE_TRUE( pObject != nullptr );
    const GameObjectHandle handle = pObject->getHandle();
    SW_EXPECT_EQUAL( 1u, pool.getBusyCount() );

    pool.release( manager, handle );
    SW_EXPECT_EQUAL( 0u, pool.getBusyCount() );
    SW_EXPECT_EQUAL( 1u, pool.getIdleCount() );
    SW_EXPECT_FALSE( pObject->isActiveInHierarchy() );
    pool.release( manager, handle ); // 같은 생을 두 번 돌려줘도 두 번 쉬지 않는다
    SW_EXPECT_EQUAL( 1u, pool.getIdleCount() );

    GameObject* pAgain = pool.acquire( manager, "Shot" );
    SW_EXPECT_TRUE( pAgain == pObject );
    SW_EXPECT_TRUE( pAgain->isActiveInHierarchy() );
    SW_EXPECT_EQUAL( uint64{ 1 }, pool.getCreatedCount().get() );
}

/** @brief [PrefabPoolTest] 쉬는 수가 상한이면 돌려받은 인스턴스를 지운다 */
SW_TEST_CASE( PrefabPoolTest, ReleaseBeyondTheIdleLimitDestroys )
{
    GameObjectManager manager;
    PrefabPool        pool   = PrefabPoolComponentSpawner<>::makePool( 1 );
    GameObject*       pFirst = pool.acquire( manager, "Shot" );
    GameObject*       pOther = pool.acquire( manager, "Shot" );
    SW_REQUIRE_TRUE( pFirst != nullptr && pOther != nullptr );
    const GameObjectHandle otherHandle = pOther->getHandle();
    pool.release( manager, pFirst->getHandle() );
    pool.release( manager, otherHandle );
    SW_EXPECT_EQUAL( 1u, pool.getIdleCount() );
    SW_EXPECT_TRUE( Util::isGone( manager, otherHandle ) );
}

/** @brief [PrefabPoolTest] 미리 만든 수는 상한을 넘지 않고, 꺼내도 새로 만들지 않는다 */
SW_TEST_CASE( PrefabPoolTest, PrewarmFillsUpToTheLimit )
{
    GameObjectManager manager;
    PrefabPool        pool = PrefabPoolComponentSpawner<>::makePool( 3 );
    SW_EXPECT_EQUAL( 3u, pool.prewarm( manager, 5, "Shot" ) );
    SW_EXPECT_EQUAL( 3u, pool.getIdleCount() );
    SW_REQUIRE_TRUE( pool.acquire( manager, "Shot" ) != nullptr );
    SW_EXPECT_EQUAL( uint64{ 3 }, pool.getCreatedCount().get() );
}

/** @brief [PrefabPoolTest] 꺼낸 수가 한도면 가장 오래 꺼낸 것을 다시 내주고 새로 만들지 않는다 */
SW_TEST_CASE( PrefabPoolTest, RecyclingHandsBackTheOldestWhenFull )
{
    GameObjectManager manager;
    PrefabPool        pool   = PrefabPoolComponentSpawner<>::makePool( 2 );
    GameObject*       pFirst = pool.acquireRecycling( manager, 2, "Shot" );
    GameObject*       pOther = pool.acquireRecycling( manager, 2, "Shot" );
    SW_REQUIRE_TRUE( pFirst != nullptr && pOther != nullptr && pFirst != pOther );
    SW_EXPECT_TRUE( pool.acquireRecycling( manager, 2, "Shot" ) == pFirst );
    SW_EXPECT_TRUE( pool.acquireRecycling( manager, 2, "Shot" ) == pOther );
    SW_EXPECT_EQUAL( uint64{ 2 }, pool.getCreatedCount().get() );
    SW_EXPECT_EQUAL( 2u, pool.getBusyCount() );
}

/** @brief [PrefabPoolTest] 결함: 풀이 돌려받은 오브젝트는 id 가 같아 앞 생을 겨누던 핸들(잠금 조준 · 포탑 · 킬캠)이 다시 꺼낸 생을 가리켰다 — 돌려받기 · 다시 내주기가 핸들을 끊는다 */
SW_TEST_CASE( PrefabPoolTest, HandleOfAnEarlierLifeDoesNotSeeTheNext )
{
    GameObjectManager manager;
    PrefabPool        pool   = PrefabPoolComponentSpawner<>::makePool( 2 );
    GameObject*       pEnemy = pool.acquire( manager, "Enemy" );
    SW_REQUIRE_TRUE( pEnemy != nullptr );
    const GameObjectHandle firstLife = pEnemy->getHandle();
    SW_EXPECT_TRUE( manager.resolveGameObject( firstLife ) == pEnemy );
    pool.release( manager, firstLife );
    SW_EXPECT_TRUE( manager.resolveGameObject( firstLife ) == nullptr );

    SW_REQUIRE_TRUE( pool.acquire( manager, "Enemy" ) == pEnemy );
    const GameObjectHandle secondLife = pEnemy->getHandle();
    SW_EXPECT_TRUE( secondLife != firstLife );
    SW_EXPECT_EQUAL( firstLife.objectID(), secondLife.objectID() );
    SW_EXPECT_TRUE( manager.resolveGameObject( firstLife ) == nullptr );
    SW_EXPECT_TRUE( manager.resolveGameObject( secondLife ) == pEnemy );

    SW_REQUIRE_TRUE( pool.acquireRecycling( manager, 1, "Enemy" ) == pEnemy ); // 쓰던 것을 빼앗아 다시 내준다
    SW_EXPECT_TRUE( manager.resolveGameObject( secondLife ) == nullptr );
    SW_EXPECT_EQUAL( 1u, pool.getBusyCount() );
    pool.destroyAll( manager );
    SW_EXPECT_TRUE( manager.resolveGameObject( pEnemy->getHandle() ) == nullptr );
}

/** @brief [PrefabPoolTest] 조건이 참인 것만 돌려받고 모두 지우기는 꺼낸 것까지 지운다 */
SW_TEST_CASE( PrefabPoolTest, ReleaseBusyIfAndDestroyAll )
{
    GameObjectManager manager;
    PrefabPool        pool   = PrefabPoolComponentSpawner<>::makePool( 4 );
    GameObject*       pFirst = pool.acquire( manager, "First" );
    GameObject*       pOther = pool.acquire( manager, "Other" );
    SW_REQUIRE_TRUE( pFirst != nullptr && pOther != nullptr );
    const GameObjectHandle firstHandle = pFirst->getHandle();
    const GameObjectHandle otherHandle = pOther->getHandle();
    pool.releaseBusyIf( manager, [firstHandle]( const GameObject& object )
    { return object.getHandle() == firstHandle; } );
    SW_EXPECT_EQUAL( 1u, pool.getIdleCount() );
    SW_EXPECT_EQUAL( 1u, pool.getBusyCount() );

    pool.destroyAll( manager );
    SW_EXPECT_EQUAL( 0u, pool.getIdleCount() );
    SW_EXPECT_EQUAL( 0u, pool.getBusyCount() );
    SW_EXPECT_TRUE( Util::isGone( manager, firstHandle ) );
    SW_EXPECT_TRUE( Util::isGone( manager, otherHandle ) );
}

/** @brief [PrefabPoolTest] 되돌리기를 구현하지 않은 컴포넌트가 붙은 프리팹은 처음 만들 때 오류로 풀이 막히고, 다음 꺼내기도 새로 만들지 않는다(매번 스폰으로 물러서지 않는다) */
SW_TEST_CASE( PrefabPoolTest, ComponentWithoutSpawnStateResetRefusesThePool )
{
    SW_EXPECT_LOG( "PrefabPool.ComponentNotPoolable" ); // 계약 위반 오류가 이 테스트가 보려는 동작이다
    GameObjectManager manager;
    PrefabPool        pool = PrefabPoolComponentSpawner<SceneComponent>::makePool( 4 ); // 되돌리기를 구현하지 않은 컴포넌트
    SW_EXPECT_TRUE( pool.acquire( manager, "Bare" ) == nullptr );
    SW_EXPECT_TRUE( pool.acquire( manager, "Bare" ) == nullptr );
    SW_EXPECT_EQUAL( uint64{ 0 }, pool.getCreatedCount().get() );
    SW_EXPECT_EQUAL( 0u, pool.getBusyCount() );
    SW_EXPECT_EQUAL( 0u, pool.prewarm( manager, 2, "Bare" ) );
}
