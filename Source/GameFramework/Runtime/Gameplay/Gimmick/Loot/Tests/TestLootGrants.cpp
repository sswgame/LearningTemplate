// LootGrantTest — 아이템 상자가 받을 가방(IItemReceiver — 폰을 쥔 조종자 쪽 · 테스트 받는 쪽)에 아이템을 넣는지, 받는 쪽이 없거나 거절하면 열리지 않는지 본다(nogpu, 헤드리스).
#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Loot/GatheringNodeComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Loot/ItemBoxComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Loot/Tests/ItemReceiverTestComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/InventoryComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/PickupComponent.h"

using namespace sw;

/**
 * @brief [LootGrantTest] 아이템 상자를 연 폰의 조종자 가방(`InventoryComponent` — 포트로 계약을 내보냄)에 뽑힌 아이템 × 수가 들어가고, 가방 컴포넌트가 없는 몸에는 열리지 않는다(경고)
 */
SW_TEST_CASE( LootGrantTest, ItemBoxFillsThePlayerBag )
{
    SW_EXPECT_LOG( "Control.AxisActionNotInInputMap" ); // 기본 폰의 Look 축이 빈 입력 맵에 없다 — 이 테스트는 가방 길만 본다
    SW_EXPECT_LOG( "GimmickUtil.ItemNotGranted" );      // 받을 가방이 없는 몸
    GameObjectManager          manager;
    GameObject*                pPawnObject = manager.createGameObject( hashed_string( "Pawn" ) );
    PawnComponent*             pPawn       = pPawnObject->addComponent<PawnComponent>();
    GameObject*                pController = manager.createGameObject( hashed_string( "Controller" ) );
    InventoryComponent*        pBag        = pController->addComponent<InventoryComponent>();
    PlayerControllerComponent* pPlayer     = pController->addComponent<PlayerControllerComponent>();
    GameObject*                pStranger   = manager.createGameObject( hashed_string( "Stranger" ) );
    ItemBoxComponent*          pBoxA       = manager.createGameObject( hashed_string( "BoxA" ) )->addComponent<ItemBoxComponent>();
    ItemBoxComponent*          pBoxB       = manager.createGameObject( hashed_string( "BoxB" ) )->addComponent<ItemBoxComponent>(); // 연 상자는 숨는다 — 열 때마다 새 상자
    SW_REQUIRE_NOT_NULL( pBoxA );
    SW_REQUIRE_NOT_NULL( pBoxB );
    const ItemCatalog itemCatalog{};
    InventorySettings settings;
    settings._slotCount = 4;
    SW_REQUIRE_TRUE( pBag->initialize( itemCatalog, settings ) == GameStateInitResult::Fresh );
    manager.beginPlay();
    pPlayer->possess( *pPawn );

    const hashed_string gem( "Gem" );
    ItemBoxChoice       choice;
    choice._item  = gem;
    choice._count = 3;
    pBoxA->setChoices( vector<ItemBoxChoice>{ choice } );
    pBoxB->setChoices( vector<ItemBoxChoice>{ choice } );

    SW_EXPECT_TRUE( pBoxA->open( *pStranger ).empty() ); // 받을 가방이 없으면 열지 않는다 — 아이템을 버리지 않는다
    SW_EXPECT_TRUE( pBoxA->isAvailable() );
    SW_EXPECT_EQUAL( 0, pBoxA->getOpenCount() );

    SW_EXPECT_TRUE( pBoxB->open( *pPawnObject ) == gem );
    SW_EXPECT_EQUAL( 3, pBag->getInventory().getItemCount( gem ) );
}

/**
 * @brief [LootGrantTest] 상자는 계약(`IItemReceiver`)만 본다 — 대상 자신이 내보내는 테스트 받는 쪽이 받고, 자리가 없다고 거절하면 열리지 않는다(경고)
 */
SW_TEST_CASE( LootGrantTest, ItemBoxUsesTheItemContract )
{
    SW_EXPECT_LOG( "GimmickUtil.ItemNotGranted" );
    GameObjectManager          manager;
    GameObject*                pCart     = manager.createGameObject( hashed_string( "Cart" ) );
    ItemReceiverTestComponent* pReceiver = pCart->addComponent<ItemReceiverTestComponent>();
    ItemBoxComponent*          pBoxA     = manager.createGameObject( hashed_string( "BoxA" ) )->addComponent<ItemBoxComponent>();
    ItemBoxComponent*          pBoxB     = manager.createGameObject( hashed_string( "BoxB" ) )->addComponent<ItemBoxComponent>();
    SW_REQUIRE_NOT_NULL( pReceiver );
    SW_REQUIRE_NOT_NULL( pBoxA );
    SW_REQUIRE_NOT_NULL( pBoxB );
    const hashed_string ore( "Ore" );
    ItemBoxChoice       choice;
    choice._item  = ore;
    choice._count = 2;
    pBoxA->setChoices( vector<ItemBoxChoice>{ choice } );
    pBoxB->setChoices( vector<ItemBoxChoice>{ choice } );
    pReceiver->_room = 3;

    SW_EXPECT_TRUE( pBoxA->open( *pCart ) == ore );
    SW_EXPECT_EQUAL( 2, pReceiver->_received );

    SW_EXPECT_TRUE( pBoxB->open( *pCart ).empty() ); // 2 개 더는 자리(3)를 넘는다 — 거절, 상자는 닫힌 채
    SW_EXPECT_TRUE( pBoxB->isAvailable() );
    SW_EXPECT_EQUAL( 2, pReceiver->_received );
}

/** @brief [LootGrantTest] 써서 여는 상자(보물 상자)는 상호작용을 끝낸 쪽에 한 번만 열리고 상호작용이 꺼진다 — 끝낸 쪽도 남는다(`getLastInteractor`) */
SW_TEST_CASE( LootGrantTest, ChestOpensOnceForWhoUsesIt )
{
    GameObjectManager          manager;
    GameObject*                pWho      = manager.createGameObject( hashed_string( "Who" ) );
    ItemReceiverTestComponent* pReceiver = pWho->addComponent<ItemReceiverTestComponent>();
    pReceiver->_room                     = 9;
    GameObject*            pChest        = manager.createGameObject( hashed_string( "Chest" ) );
    InteractableComponent* pInteractable = pChest->addComponent<InteractableComponent>();
    ItemBoxComponent*      pBox          = pChest->addComponent<ItemBoxComponent>();
    InteractionDef         def;
    def._id = hashed_string( "open" );
    pInteractable->setDefinition( def );
    ItemBoxChoice choice;
    choice._item  = hashed_string( "Gem" );
    choice._count = 2;
    pBox->setChoices( vector<ItemBoxChoice>{ choice } );
    pBox->setOpenBy( ItemBoxOpenBy::Use );
    pBox->setMaxOpenCount( 1 );
    manager.beginPlay();

    pInteractable->completeInteraction( *pWho );
    SW_EXPECT_EQUAL( 2, pReceiver->_received );
    SW_EXPECT_FALSE( pBox->isAvailable() );
    SW_EXPECT_FALSE( pInteractable->isEnabled() );
    SW_EXPECT_TRUE( pInteractable->getLastInteractor() == pWho->getHandle() );
    pInteractable->completeInteraction( *pWho ); // 꺼졌다 — 다시 주지 않는다
    SW_EXPECT_EQUAL( 2, pReceiver->_received );
}

/** @brief [LootGrantTest] 월드 아이템은 상호작용을 끝낸 쪽의 가방에 모두 들어가면 사라지고, 자리가 없으면 남는다(경고) */
SW_TEST_CASE( LootGrantTest, WorldItemIsPickedUpByUse )
{
    SW_EXPECT_LOG( "Pickup.NotGranted" );
    GameObjectManager          manager;
    GameObject*                pWho      = manager.createGameObject( hashed_string( "Who" ) );
    ItemReceiverTestComponent* pReceiver = pWho->addComponent<ItemReceiverTestComponent>();
    InventorySlot              stack;
    stack._itemID     = hashed_string( "Ore" );
    stack._count      = 3;
    GameObject* pItem = manager.createGameObject( hashed_string( "Dropped" ) );
    pItem->addComponent<PickupComponent>()->setItem( stack );
    InteractableComponent* pInteractable = pItem->addComponent<InteractableComponent>();
    InteractionDef         def;
    def._id = hashed_string( "pickup" );
    pInteractable->setDefinition( def );
    manager.beginPlay();
    const GameObjectHandle item = pItem->getHandle();

    pReceiver->_room = 2;
    pInteractable->completeInteraction( *pWho );
    SW_EXPECT_EQUAL( 0, pReceiver->_received );
    SW_EXPECT_NOT_NULL( manager.resolveGameObject( item ) );

    pReceiver->_room = 3;
    pInteractable->completeInteraction( *pWho );
    SW_EXPECT_EQUAL( 3, pReceiver->_received );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_NULL( manager.resolveGameObject( item ) );
}

/** @brief [LootGrantTest] 다 벤 나무는 벤 쪽의 반대로 쓰러져 눕는다 — 누운 뒤 위 축이 벤 쪽에서 멀어지는 쪽이다 */
SW_TEST_CASE( LootGrantTest, DepletedTreeFallsAwayFromWhoFelledIt )
{
    GameObjectManager manager;
    GameObject*       pWho = manager.createGameObject( hashed_string( "Who" ) );
    pWho->addComponent<SceneComponent>()->setLocalPosition( float3{ -2.0f, 0.0f, 0.0f } );
    pWho->addComponent<ItemReceiverTestComponent>()->_room = 9;
    GameObject*             pTree                          = manager.createGameObject( hashed_string( "Tree" ) );
    SceneComponent*         pRoot                          = pTree->addComponent<SceneComponent>();
    GatheringNodeComponent* pNode                          = pTree->addComponent<GatheringNodeComponent>();
    pNode->configure( hashed_string( "Log" ), 2, 1 );
    pNode->setFallDuration( 0.5f );
    manager.beginPlay();

    SW_EXPECT_TRUE( pNode->gather( *pWho ) );
    SW_EXPECT_TRUE( pNode->isFalling() );
    for ( int32 step = 0; step < 120 && pNode->isFalling(); ++step )
    {
        pNode->stepOnce();
    }
    SW_EXPECT_FALSE( pNode->isFalling() );
    const float3 up = float3::transformVector( float3{ 0.0f, 1.0f, 0.0f }, pRoot->getWorldMatrix() );
    SW_EXPECT_NEAR( 1.0f, up._x, 1.0e-2f ); // 벤 쪽(-X)의 반대
    SW_EXPECT_NEAR( 0.0f, up._y, 1.0e-2f );
}

/** @brief [LootGrantTest] 채집 지점 · 숨는 곳은 상호작용 완료 알림으로 동작한다(센서 없이) — 끝낸 쪽이 받는다 */
SW_TEST_CASE( LootGrantTest, GatheringRunsOnTheCompletedInteraction )
{
    GameObjectManager          manager;
    GameObject*                pWho       = manager.createGameObject( hashed_string( "Who" ) );
    ItemReceiverTestComponent* pReceiver  = pWho->addComponent<ItemReceiverTestComponent>();
    pReceiver->_room                      = 9;
    GameObject*             pHerb         = manager.createGameObject( hashed_string( "Herb" ) );
    InteractableComponent*  pInteractable = pHerb->addComponent<InteractableComponent>();
    GatheringNodeComponent* pNode         = pHerb->addComponent<GatheringNodeComponent>();
    pNode->configure( hashed_string( "Leaf" ), 2, 2 );
    InteractionDef def;
    def._id = hashed_string( "gather" );
    pInteractable->setDefinition( def );
    manager.beginPlay();

    pInteractable->completeInteraction( *pWho );
    SW_EXPECT_EQUAL( 2, pReceiver->_received );
    SW_EXPECT_EQUAL( 1, pNode->getUsesLeft() );
    pInteractable->completeInteraction( *pWho );
    SW_EXPECT_EQUAL( 4, pReceiver->_received );
    SW_EXPECT_TRUE( pNode->isDepleted() );
    SW_EXPECT_FALSE( pInteractable->isEnabled() );
}
