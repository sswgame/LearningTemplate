#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/World/Lifetime/PrefabPool.h"

// PrefabPoolTest — 재사용 · 상한 · 미리 만들기 · 다시 내주기 · 조건 돌려받기 · 모두 지우기. 인스턴스는 프리팹 대신 빈 오브젝트다(nogpu, 에셋 없음).

using namespace sw;

namespace
{
    struct PrefabPoolTestUtil
    {
        static PrefabPool makePool( uint32 maxIdleCount )
        {
            PrefabPool pool{ string{}, maxIdleCount };
            pool.setSpawner( PrefabPool::SpawnDelegate::create( []( GameObjectManager& manager, const utf8* pName )
            { return manager.createGameObject( hashed_string( pName ) ); } ) );
            return pool;
        }

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
    PrefabPool        pool    = Util::makePool( 4 );
    GameObject*       pObject = pool.acquire( manager, "Shot" );
    SW_REQUIRE_TRUE( pObject != nullptr );
    const GameObjectHandle handle = pObject->getHandle();
    SW_EXPECT_EQUAL( 1u, pool.getBusyCount() );

    pool.release( manager, handle );
    SW_EXPECT_EQUAL( 0u, pool.getBusyCount() );
    SW_EXPECT_EQUAL( 1u, pool.getIdleCount() );
    SW_EXPECT_FALSE( pObject->isActiveInHierarchy() );

    GameObject* pAgain = pool.acquire( manager, "Shot" );
    SW_EXPECT_TRUE( pAgain == pObject );
    SW_EXPECT_TRUE( pAgain->isActiveInHierarchy() );
    SW_EXPECT_EQUAL( 1u, pool.getCreatedCount() );
}

/** @brief [PrefabPoolTest] 쉬는 수가 상한이면 돌려받은 인스턴스를 지운다 */
SW_TEST_CASE( PrefabPoolTest, ReleaseBeyondTheIdleLimitDestroys )
{
    GameObjectManager manager;
    PrefabPool        pool   = Util::makePool( 1 );
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
    PrefabPool        pool = Util::makePool( 3 );
    SW_EXPECT_EQUAL( 3u, pool.prewarm( manager, 5, "Shot" ) );
    SW_EXPECT_EQUAL( 3u, pool.getIdleCount() );
    SW_REQUIRE_TRUE( pool.acquire( manager, "Shot" ) != nullptr );
    SW_EXPECT_EQUAL( 3u, pool.getCreatedCount() );
}

/** @brief [PrefabPoolTest] 꺼낸 수가 한도면 가장 오래 꺼낸 것을 다시 내주고 새로 만들지 않는다 */
SW_TEST_CASE( PrefabPoolTest, RecyclingHandsBackTheOldestWhenFull )
{
    GameObjectManager manager;
    PrefabPool        pool   = Util::makePool( 2 );
    GameObject*       pFirst = pool.acquireRecycling( manager, 2, "Shot" );
    GameObject*       pOther = pool.acquireRecycling( manager, 2, "Shot" );
    SW_REQUIRE_TRUE( pFirst != nullptr && pOther != nullptr && pFirst != pOther );
    SW_EXPECT_TRUE( pool.acquireRecycling( manager, 2, "Shot" ) == pFirst );
    SW_EXPECT_TRUE( pool.acquireRecycling( manager, 2, "Shot" ) == pOther );
    SW_EXPECT_EQUAL( 2u, pool.getCreatedCount() );
    SW_EXPECT_EQUAL( 2u, pool.getBusyCount() );
}

/** @brief [PrefabPoolTest] 조건이 참인 것만 돌려받고 모두 지우기는 꺼낸 것까지 지운다 */
SW_TEST_CASE( PrefabPoolTest, ReleaseBusyIfAndDestroyAll )
{
    GameObjectManager manager;
    PrefabPool        pool   = Util::makePool( 4 );
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
