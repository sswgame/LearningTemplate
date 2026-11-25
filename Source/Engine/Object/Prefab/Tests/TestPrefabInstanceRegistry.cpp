#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"

using namespace sw;

// 구조 세대가 그대로인 프레임은 표를 훑지 않는다 — 그래도 지운 오브젝트의 항목과 없는 id 로 기록한 항목은 거둬야 한다.
SW_TEST_CASE( PrefabInstanceRegistryTest, PruneFollowsStructureChangesAndNewRecords )
{
    GameObjectManager      manager;
    PrefabInstanceRegistry registry{ manager };
    GameObject*            pOwner = manager.createGameObject( hashed_string( "Owner" ) );
    GameObject*            pPart  = manager.createGameObject( hashed_string( "Part" ) );
    SW_REQUIRE_NOT_NULL( pOwner );
    SW_REQUIRE_NOT_NULL( pPart );
    const uint64 partID = pPart->getObjectID();
    registry.registerNestedPart( partID, pOwner->getObjectID(), "prefabs/part.prefab.xml", "Part" );

    registry.update( 0.0f );
    registry.update( 0.0f ); // 세대가 그대로다 — 훑지 않고 항목도 그대로
    SW_EXPECT_EQUAL( registry.getNestedPartCount(), size_t{ 1 } );

    // 없는 id 로 기록하면 세대가 그대로여도 다음 거두기가 돈다.
    registry.registerNestedPart( partID + 1000000, pOwner->getObjectID(), "prefabs/part.prefab.xml", "Ghost" );
    registry.update( 0.0f );
    SW_EXPECT_EQUAL( registry.getNestedPartCount(), size_t{ 1 } );

    manager.destroyGameObject( pPart );
    manager.update( 1.0f / 60.0f );
    manager.update( 1.0f / 60.0f );
    registry.update( 0.0f );
    SW_EXPECT_EQUAL( registry.getNestedPartCount(), size_t{ 0 } );
    SW_EXPECT_TRUE( registry.findNestedPart( partID ) == nullptr );
}
