// AdventureGameModeTest — 액션 어드벤처 디렉터의 명령은 판이 열린 동안만 받는다: 열기 전의 입력 · 전투 명령은 아무것도 바꾸지 않는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Component/AdventureGameModeComponent.h"

using namespace sw;

/** @brief [AdventureGameModeTest] 판이 열리기 전의 입력 명령은 거절하고, 전투를 도는 쪽의 명령도 게이트 · 자리를 바꾸지 않는다 — 클라이언트 · 전투가 먼저 돌아도 판은 그대로다 */
SW_TEST_CASE( AdventureGameModeTest, CommandsBeforeTheGameOpensChangeNothing )
{
    GameObjectManager           manager;
    AdventureGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<AdventureGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->submitPlayerInput( float2{ 1.0f, 0.0f }, true ) );
    SW_EXPECT_TRUE( pGameMode->openEncounter() == nullptr );
    pGameMode->announceBoss();
    pGameMode->completeEncounter();
    SW_EXPECT_FALSE( pGameMode->applyPlayerDamage( 1 ) );
    SW_EXPECT_TRUE( pGameMode->getPendingEncounter().empty() );
    SW_EXPECT_TRUE( pGameMode->getEncounterRegion().empty() );
    SW_EXPECT_EQUAL( 0.0f, pGameMode->getPlayerX() );
    SW_EXPECT_EQUAL( 0, pGameMode->getFallCount() );
}
