// GameModeTest — 디렉터(`GameModeComponent`)가 판을 열 때 상태 칸(`PROPERTY( RuntimeState )`)을 기본값으로 되돌리고, 복원 바이트는 그 뒤에 적용한다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Framework/Tests/GameModeTestComponent.h"

using namespace sw;

/** @brief [GameModeTest] 두 번 열면 앞 판의 값이 남지 않는다 — 더하기만 하는 칸도, 조건이 맞을 때만 적는 칸(수 · 비트필드 · 목록)도 */
SW_TEST_CASE( GameModeTest, ReopenedGameStartsFromTheDefaultState )
{
    GameObjectManager      manager;
    GameModeTestComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<GameModeTestComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 1, pGameMode->_roundCount );
    SW_EXPECT_EQUAL( 1, pGameMode->_winner );
    SW_EXPECT_EQUAL( size_t{ 1 }, pGameMode->_listScore.size() );
    SW_EXPECT_TRUE( pGameMode->_bBonus == SW_TRUE );
    manager.endPlay();

    pGameMode->_bWins = SW_FALSE;
    manager.beginPlay();
    SW_REQUIRE_TRUE( pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 1, pGameMode->_roundCount );
    SW_EXPECT_EQUAL( -1, pGameMode->_winner );
    SW_EXPECT_TRUE( pGameMode->_listScore.empty() );
    SW_EXPECT_TRUE( pGameMode->_bBonus == SW_FALSE );
    SW_EXPECT_TRUE( pGameMode->_bWins == SW_FALSE ); // 같은 바이트의 상태 아닌 비트는 그대로다
    manager.endPlay();
}

/** @brief [GameModeTest] 시작 전에 받은 복원 바이트는 되돌림 뒤에 적용된다 — 저장한 판의 값이 이긴다 */
SW_TEST_CASE( GameModeTest, RestoredStateIsAppliedAfterTheReset )
{
    GameObjectManager      manager;
    GameModeTestComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<GameModeTestComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    pGameMode->_roundCount = 7;
    pGameMode->_winner     = 3;
    Archive saved;
    pGameMode->writeState( saved );
    vector<uint8> bytes;
    saved.writeData( bytes );
    pGameMode->restoreState( std::move( bytes ) );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 7, pGameMode->_roundCount );
    SW_EXPECT_EQUAL( 3, pGameMode->_winner );
    manager.endPlay();
}
