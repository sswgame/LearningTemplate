// CreatureTownGameModeTest — 생물 마을 디렉터의 명령(`submitTalk`)은 판이 열린 동안만 받고, 카탈로그가 없으면 판을 열지 않는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/Component/CreatureTownGameModeComponent.h"

using namespace sw;

/** @brief [CreatureTownGameModeTest] 판이 열리기 전의 대화 명령은 거절한다 — 클라이언트가 먼저 돌아도 판을 바꾸지 않는다 */
SW_TEST_CASE( CreatureTownGameModeTest, TalkBeforeTheTownOpensIsRefused )
{
    GameObjectManager              manager;
    CreatureTownGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<CreatureTownGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->submitTalk( "sprout" ) );
    SW_EXPECT_EQUAL( 0u, pGameMode->getTownRevision() ); // 뷰는 맞출 마을이 없다
}

/** @brief [CreatureTownGameModeTest] 씬이 카탈로그 셋(생물 · 부탁 · 호감도)을 적지 않았으면 오류를 남기고 판을 열지 않는다 — C++ 기본값으로 열지 않는다 */
SW_TEST_CASE( CreatureTownGameModeTest, TownWithoutCatalogsDoesNotOpen )
{
    SW_EXPECT_LOG( "GameMode.RequiredAssetMissing" );
    GameObjectManager              manager;
    CreatureTownGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<CreatureTownGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_EXPECT_FALSE( pGameMode->isStarted() );
    SW_EXPECT_FALSE( pGameMode->submitTalk( "sprout" ) );
    SW_EXPECT_EQUAL( 0u, pGameMode->getTownRevision() );
    manager.endPlay();
}
