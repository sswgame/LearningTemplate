#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameStateRefs.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerProgressComponent.h"

// PlayerProgressTest — 플레이어 진행(PlayerProgressComponent — 조종자 오브젝트의 지갑 · 가방 · 일지 · 평판): 저장 왕복, 폰에서 찾기, 씬 하나를 읽는 쪽의 "하나뿐" 규칙,
// 월드 상태와 묶어 키트에 빌려 주기(makeRefs)를 본다(nogpu, 헤드리스).

using namespace sw;

/** @brief [PlayerProgressTest] 지갑 · 가방이 상태 저장 왕복을 넘고, 열기 전에 받은 복원 바이트는 `initialize` 가 적용해 복원한 판(`Restored`)으로 연다 */
SW_TEST_CASE( PlayerProgressTest, SaveRoundTripsWalletAndBag )
{
    GameObjectManager        manager;
    PlayerProgressComponent* pProgress = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerProgressComponent>();
    SW_REQUIRE_NOT_NULL( pProgress );
    PlayerProgressSettings settings;
    settings._inventorySlotCount = 4;
    SW_REQUIRE_TRUE( pProgress->initialize( settings ) == GameStateInitResult::Fresh );
    const hashed_string seedItem( "seed" );
    pProgress->getWallet().add( Wallet::getDefaultCurrency(), 120 );
    SW_REQUIRE_EQUAL( 2, pProgress->getInventory().addItem( seedItem, 2 ) );

    Archive written;
    pProgress->writeState( written );

    PlayerProgressComponent* pRestored = manager.createGameObject( hashed_string( "Restored" ) )->addComponent<PlayerProgressComponent>();
    SW_REQUIRE_NOT_NULL( pRestored );
    pRestored->restoreState( vector<uint8>( written.getData(), written.getData() + written.getSize() ) ); // 열기 전 — 보관한다
    SW_EXPECT_TRUE( pRestored->initialize( settings ) == GameStateInitResult::Restored );
    SW_EXPECT_FALSE( pRestored->isFreshGame() );
    SW_EXPECT_EQUAL( int64{ 120 }, pRestored->getWallet().getBalance( Wallet::getDefaultCurrency() ) );
    SW_EXPECT_EQUAL( 2, pRestored->getInventory().getItemCount( seedItem ) );
}

/** @brief [PlayerProgressTest] 플레이어 진행은 폰을 쥔 조종자 오브젝트에서 찾힌다(형제 `PlayerStateComponent` 와 같은 길) */
SW_TEST_CASE( PlayerProgressTest, FoundFromThePawn )
{
    SW_EXPECT_LOG( "Control.AxisActionNotInInputMap" ); // 기본 폰의 Look 축이 빈 입력 맵에 없다 — 이 테스트는 찾는 길만 본다
    GameObjectManager          manager;
    GameObject*                pPawnObject = manager.createGameObject( hashed_string( "Pawn" ) );
    PawnComponent*             pPawn       = pPawnObject->addComponent<PawnComponent>();
    GameObject*                pController = manager.createGameObject( hashed_string( "Controller" ) );
    PlayerProgressComponent*   pProgress   = pController->addComponent<PlayerProgressComponent>();
    PlayerControllerComponent* pPlayer     = pController->addComponent<PlayerControllerComponent>();
    manager.beginPlay();

    SW_EXPECT_NULL( PlayerProgressComponent::findForPawnObject( manager, *pPawnObject ) ); // 아직 쥐지 않았다
    pPlayer->possess( *pPawn );
    SW_EXPECT_TRUE( PlayerProgressComponent::findForPawnObject( manager, *pPawnObject ) == pProgress );
}

/** @brief [PlayerProgressTest] 씬 하나를 읽는 쪽(`findOpen` — 감시 값 · 치트 · 목표 표시)은 열린 플레이어 진행이 둘이면 오류로 멈춘다 — 어느 플레이어인지 등록 순서로 고르지 않는다 */
SW_TEST_CASE( PlayerProgressTest, SecondOpenProgressStopsTheSingleReader )
{
    SW_EXPECT_LOG( "PlayerProgress.SeveralOpen" );
    GameObjectManager        manager;
    PlayerProgressComponent* pFirst  = manager.createGameObject( hashed_string( "PlayerA" ) )->addComponent<PlayerProgressComponent>();
    PlayerProgressComponent* pSecond = manager.createGameObject( hashed_string( "PlayerB" ) )->addComponent<PlayerProgressComponent>();
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_NOT_NULL( pSecond );
    SW_REQUIRE_TRUE( pFirst->initialize( PlayerProgressSettings{} ) == GameStateInitResult::Fresh );
    SW_EXPECT_TRUE( PlayerProgressComponent::findOpen( &manager ) == pFirst );
    SW_REQUIRE_TRUE( pSecond->initialize( PlayerProgressSettings{} ) == GameStateInitResult::Fresh );
    SW_EXPECT_NULL( PlayerProgressComponent::findOpen( &manager ) );
}

/** @brief [PlayerProgressTest] 키트에 빌려 주는 묶음은 지갑 · 일지 · 평판을 플레이어 진행에서, 시계 · 플래그를 월드 상태에서 채우고, 슬롯 0 이면 가방이 없다 */
SW_TEST_CASE( PlayerProgressTest, RefsBorrowFromBothScopes )
{
    GameObjectManager        manager;
    GameObject*              pDirector = manager.createGameObject( hashed_string( "Director" ) );
    GameStateComponent*      pWorld    = pDirector->addComponent<GameStateComponent>();
    PlayerProgressComponent* pProgress = pDirector->addComponent<PlayerProgressComponent>();
    SW_REQUIRE_TRUE( pWorld->initialize( GameStateSettings{} ) == GameStateInitResult::Fresh );
    SW_REQUIRE_TRUE( pProgress->initialize( PlayerProgressSettings{} ) == GameStateInitResult::Fresh );

    const GameStateRefs refs = pWorld->makeRefs( *pProgress );
    SW_EXPECT_TRUE( refs._pWallet == &pProgress->getWallet() );
    SW_EXPECT_TRUE( refs._pQuestLog == &pProgress->getQuestLog() );
    SW_EXPECT_TRUE( refs._pReputation == &pProgress->getReputation() );
    SW_EXPECT_TRUE( refs._pFlags == &pWorld->getFlags() );
    SW_EXPECT_TRUE( refs._pClock == &pWorld->getClock() );
    SW_EXPECT_NULL( refs._pInventory );
}
