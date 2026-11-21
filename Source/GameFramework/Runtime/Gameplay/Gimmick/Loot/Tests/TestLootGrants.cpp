// LootGrantTest — 아이템 상자가 연 폰을 쥔 플레이어의 가방에 아이템을 넣는지 본다(nogpu, 헤드리스). 받는 쪽이 가방 하나라 이벤트 없이 직접 넣는다.
#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerProgressComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Loot/ItemBoxComponent.h"

using namespace sw;

/**
 * @brief [LootGrantTest] 아이템 상자를 연 폰의 조종자 가방에 뽑힌 아이템 × 수가 들어가고, 쥔 조종자가 없는 몸이 열면 아무 가방에도 들지 않는다
 */
SW_TEST_CASE( LootGrantTest, ItemBoxFillsThePlayerBag )
{
    SW_EXPECT_LOG( "Control.AxisActionNotInInputMap" ); // 기본 폰의 Look 축이 빈 입력 맵에 없다 — 이 테스트는 가방 길만 본다
    GameObjectManager          manager;
    GameObject*                pPawnObject = manager.createGameObject( hashed_string( "Pawn" ) );
    PawnComponent*             pPawn       = pPawnObject->addComponent<PawnComponent>();
    GameObject*                pController = manager.createGameObject( hashed_string( "Controller" ) );
    PlayerProgressComponent*   pProgress   = pController->addComponent<PlayerProgressComponent>();
    PlayerControllerComponent* pPlayer     = pController->addComponent<PlayerControllerComponent>();
    GameObject*                pStranger   = manager.createGameObject( hashed_string( "Stranger" ) );
    ItemBoxComponent*          pBoxA       = manager.createGameObject( hashed_string( "BoxA" ) )->addComponent<ItemBoxComponent>();
    ItemBoxComponent*          pBoxB       = manager.createGameObject( hashed_string( "BoxB" ) )->addComponent<ItemBoxComponent>(); // 연 상자는 숨는다 — 열 때마다 새 상자
    SW_REQUIRE_NOT_NULL( pBoxA );
    SW_REQUIRE_NOT_NULL( pBoxB );
    PlayerProgressSettings settings;
    settings._inventorySlotCount = 4;
    SW_REQUIRE_TRUE( pProgress->initialize( settings ) == GameStateInitResult::Fresh );
    manager.beginPlay();
    pPlayer->possess( *pPawn );

    const hashed_string gem( "Gem" );
    ItemBoxChoice       choice;
    choice._item  = gem;
    choice._count = 3;
    pBoxA->setChoices( vector<ItemBoxChoice>{ choice } );
    pBoxB->setChoices( vector<ItemBoxChoice>{ choice } );

    SW_EXPECT_TRUE( pBoxA->open( *pStranger ) == gem ); // 뽑기는 된다 — 줄 가방이 없을 뿐이다
    SW_EXPECT_EQUAL( 0, pProgress->getInventory().getItemCount( gem ) );

    SW_EXPECT_TRUE( pBoxB->open( *pPawnObject ) == gem );
    SW_EXPECT_EQUAL( 3, pProgress->getInventory().getItemCount( gem ) );
}
