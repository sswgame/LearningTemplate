// LootGrantTest — 아이템 상자가 받을 가방(IItemReceiver — 폰을 쥔 조종자 쪽 · 테스트 받는 쪽)에 아이템을 넣는지, 받는 쪽이 없거나 거절하면 열리지 않는지 본다(nogpu, 헤드리스).
#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Loot/ItemBoxComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Loot/Tests/ItemReceiverTestComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/InventoryComponent.h"

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
    InventorySettings settings;
    settings._slotCount = 4;
    SW_REQUIRE_TRUE( pBag->initialize( settings ) == GameStateInitResult::Fresh );
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
