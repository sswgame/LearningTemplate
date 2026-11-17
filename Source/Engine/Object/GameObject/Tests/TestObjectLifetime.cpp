#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tests/TickModeTestComponents.h"

// ObjectLifetimeTest — 오브젝트 수명의 두 불변식: 씬을 비우는 중(`clear`) 해체 콜백이 만든 오브젝트도 함께 거두고,
// 삭제 대기 오브젝트에서 뺀 컴포넌트는 지연 파괴로 가 틱 레지스트리가 사라진 컴포넌트를 가리키지 않는다(nogpu).

using namespace sw;

namespace
{
    constexpr float32 kFrameSeconds = 1.0f / 60.0f;
} // namespace

/**
 * @brief [ObjectLifetimeTest] 비우는 중 해체 콜백이 만든 오브젝트도 비운 뒤에는 남지 않는다
 * @details 예전에는 한 번만 거둬, 해체 콜백이 만든 오브젝트가 대기 목록 · 이름 색인에 남은 채 풀이 비워져 풀린 메모리를 가리켰다(이름으로 찾으면 매달린 포인터).
 */
SW_TEST_CASE( ObjectLifetimeTest, ClearAlsoReapsObjectsCreatedByTeardownCallbacks )
{
    GameObjectManager                          manager;
    uint32                                     spawnCount      = 0;
    GameObject*                                pSpawner        = manager.createGameObject( hashed_string( "Spawner" ) );
    LifetimeSpawnOnDestroyTestComponent* const pSpawnComponent = pSpawner->addComponent<LifetimeSpawnOnDestroyTestComponent>();
    pSpawnComponent->_pSpawnCount                              = &spawnCount;
    manager.beginPlay();
    manager.update( kFrameSeconds );

    manager.clear();
    SW_EXPECT_EQUAL( 1u, spawnCount ); // 해체 콜백이 정말 만들었다
    SW_EXPECT_TRUE( manager.findGameObjectByName( hashed_string( LifetimeSpawnOnDestroyTestComponent::kSpawnedName ) ) == nullptr );
    manager.update( kFrameSeconds ); // 대기 목록이 비었으니 합치기가 풀린 메모리를 읽지 않는다
    SW_EXPECT_TRUE( manager.findGameObjectByName( hashed_string( "Spawner" ) ) == nullptr );
}

/**
 * @brief [ObjectLifetimeTest] 삭제 대기 오브젝트에서 뺀 틱 컴포넌트는 지연 파괴로 가고, 그 사이 프레임이 돌아도 사라진 컴포넌트를 틱하지 않는다
 * @details 예전에는 바로 해체했는데 삭제 대기 오브젝트는 틱 레지스트리에 자기를 다시 알리지 않아, 틱 파도가 해체된 컴포넌트를 가리킨 채 남았다.
 */
SW_TEST_CASE( ObjectLifetimeTest, ComponentRemovedFromADyingObjectIsDeferred )
{
    GameObjectManager          manager;
    GameObject*                pObject        = manager.createGameObject( hashed_string( "Dying" ) );
    TickModeMainTestComponent* pTickComponent = pObject->addComponent<TickModeMainTestComponent>();
    manager.beginPlay();
    manager.update( kFrameSeconds );
    const GameObjectHandle handle = pObject->getHandle();

    manager.destroyGameObject( pObject );
    SW_REQUIRE_TRUE( pObject->removeComponent( pTickComponent ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), pObject->getComponentCount() ); // 아직 해체하지 않았다 — 지연 파괴가 오브젝트와 함께 거둔다
    SW_EXPECT_TRUE( pTickComponent->isPendingDestroy() );

    manager.update( kFrameSeconds );
    manager.update( kFrameSeconds );
    SW_EXPECT_TRUE( manager.resolveGameObject( handle ) == nullptr );
}
