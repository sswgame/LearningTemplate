// ActionPlatformerGameModeTest — 액션 플랫포머 디렉터의 명령(`submitPlayerInput` · `killPlayer`)은 스테이지가 진행 중일 때만 받는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Component/ActionPlatformerGameModeComponent.h"

using namespace sw;

/** @brief [ActionPlatformerGameModeTest] 스테이지가 열리기 전에는 입력 명령도 죽이기 명령도 거절한다 — 클라이언트 · 개발 명령이 먼저 와도 진행을 바꾸지 않는다 */
SW_TEST_CASE( ActionPlatformerGameModeTest, CommandsBeforeTheStageOpensAreRefused )
{
    GameObjectManager                  manager;
    ActionPlatformerGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<ActionPlatformerGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->isAcceptingInput() );
    SW_EXPECT_FALSE( pGameMode->submitPlayerInput( 1.0f, true ) );
    SW_EXPECT_FALSE( pGameMode->killPlayer() );
    SW_EXPECT_EQUAL( 0, pGameMode->getRespawnCount() );
}
