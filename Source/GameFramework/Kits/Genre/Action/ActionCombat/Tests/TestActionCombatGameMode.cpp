// ActionCombatGameModeTest — 액션 컴뱃 디렉터의 입력 명령(`submitPlayerInput`)은 판이 열린 동안만 받는다: 열기 전에는 거절하고, 판을 열면 받는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/ActionCombatGameModeComponent.h"

using namespace sw;

/** @brief [ActionCombatGameModeTest] 플레이를 시작하기 전(판이 열리기 전)의 입력 명령은 거절한다 — 클라이언트가 먼저 돌아도 판을 바꾸지 않는다 */
SW_TEST_CASE( ActionCombatGameModeTest, InputBeforeTheRunOpensIsRefused )
{
    GameObjectManager              manager;
    ActionCombatGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<ActionCombatGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->isAcceptingInput() );
    SW_EXPECT_FALSE( pGameMode->submitPlayerInput( float2{ 1.0f, 0.0f }, true, false ) );
    SW_EXPECT_EQUAL( 0.0f, pGameMode->getPlayerX() );
}

/** @brief [ActionCombatGameModeTest] 판이 열리면(홀 단계) 입력 명령을 받는다 — 걸음 전이라 자리는 그대로다(명령은 다음 걸음이 쓴다) */
SW_TEST_CASE( ActionCombatGameModeTest, InputAfterTheRunOpensIsAccepted )
{
    SW_EXPECT_LOG( "Action.RoomLayoutNoSpawns" ); // 씬 데이터 없이 연 홀 — 적 없이 시작한다
    GameObjectManager              manager;
    ActionCombatGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<ActionCombatGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pGameMode->isStarted() );
    SW_EXPECT_EQUAL( static_cast<int32>( ActionCombatPhase::Hall ), pGameMode->getPhase() );
    SW_EXPECT_TRUE( pGameMode->isAcceptingInput() );
    SW_EXPECT_TRUE( pGameMode->submitPlayerInput( float2{ 1.0f, 0.0f }, true, true ) );
    SW_EXPECT_EQUAL( 0.0f, pGameMode->getPlayerX() );
    manager.endPlay();
}
