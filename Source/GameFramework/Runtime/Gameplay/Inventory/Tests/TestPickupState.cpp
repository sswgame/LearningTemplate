// PickupStateTest — 바닥의 아이템이 개체 상태(내구도)까지 상태 코덱으로 실리는지 본다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Gameplay/Inventory/PickupComponent.h"

using namespace sw;

/** @brief [PickupStateTest] 닳은 아이템을 떠서 다시 지은 픽업에 되돌리면 닳은 채다 — 시작해도 씬 데이터의 새것으로 바뀌지 않는다 */
SW_TEST_CASE( PickupStateTest, DroppedItemKeepsItsDurability )
{
    GameObjectManager manager;
    PickupComponent*  pDropped = manager.createGameObject( hashed_string( "Dropped" ) )->addComponent<PickupComponent>();
    SW_REQUIRE_NOT_NULL( pDropped );
    InventorySlot item;
    item._itemID     = hashed_string( "sword" );
    item._count      = 1;
    item._durability = 12.0f;
    pDropped->setItem( item );
    Archive written;
    pDropped->writeState( written );
    vector<uint8> bytes;
    written.writeData( bytes );

    PickupComponent* pRebuilt = manager.createGameObject( hashed_string( "Rebuilt" ) )->addComponent<PickupComponent>();
    SW_REQUIRE_NOT_NULL( pRebuilt );
    pRebuilt->restoreState( std::move( bytes ) );
    manager.beginPlay();
    SW_EXPECT_TRUE( pRebuilt->getItem()._itemID == hashed_string( "sword" ) );
    SW_EXPECT_NEAR( 12.0f, pRebuilt->getItem()._durability, 1.0e-6f );
    manager.endPlay();
}
