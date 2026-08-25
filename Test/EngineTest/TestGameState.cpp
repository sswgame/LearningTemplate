#include "pch.h"

#include "Engine/Game/GameState.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) GameStateTest — Stopped/Playing/Paused
// ------------------------------------------------------------------------------
/**
 * @brief [GameStateTest] 기본 상태는 Stopped
 */
SW_TEST_CASE( GameStateTest, DefaultIsStopped )
{
	sw::setGameState( sw::GameState::Stopped );
	SW_EXPECT_EQUAL( static_cast<int32>( sw::GameState::Stopped ), static_cast<int32>( sw::getGameState() ) );
}

/**
 * @brief [GameStateTest] Stopped/Playing/Paused 전이
 */
SW_TEST_CASE( GameStateTest, TransitionStoppedPlayingPaused )
{
	sw::setGameState( sw::GameState::Stopped );
	SW_ASSERT_EQUAL( static_cast<int32>( sw::GameState::Stopped ), static_cast<int32>( sw::getGameState() ) );

	sw::setGameState( sw::GameState::Playing );
	SW_EXPECT_EQUAL( static_cast<int32>( sw::GameState::Playing ), static_cast<int32>( sw::getGameState() ) );

	sw::setGameState( sw::GameState::Paused );
	SW_EXPECT_EQUAL( static_cast<int32>( sw::GameState::Paused ), static_cast<int32>( sw::getGameState() ) );

	sw::setGameState( sw::GameState::Playing );
	SW_EXPECT_EQUAL( static_cast<int32>( sw::GameState::Playing ), static_cast<int32>( sw::getGameState() ) );

	sw::setGameState( sw::GameState::Stopped );
	SW_EXPECT_EQUAL( static_cast<int32>( sw::GameState::Stopped ), static_cast<int32>( sw::getGameState() ) );
}

/**
 * @brief [GameStateTest] 동일 상태 재설정은 멱등
 */
SW_TEST_CASE( GameStateTest, IdempotentSet )
{
	sw::setGameState( sw::GameState::Playing );
	sw::setGameState( sw::GameState::Playing );
	SW_EXPECT_EQUAL( static_cast<int32>( sw::GameState::Playing ), static_cast<int32>( sw::getGameState() ) );

	sw::setGameState( sw::GameState::Stopped );
}

/**
 * @brief [GameStateTest] GameStartMode 설정 및 1회성 소비(consume) 검증
 */
SW_TEST_CASE( GameStateTest, GameStartModeOneShotConsumption )
{
	// 기본값은 NewGame
	SW_EXPECT_TRUE( sw::consumeGameStartMode() == sw::GameStartMode::NewGame );

	// Continue 설정 후 소비 시 Continue 반환되고 다시 NewGame으로 리셋
	sw::setGameStartMode( sw::GameStartMode::Continue );
	SW_EXPECT_TRUE( sw::consumeGameStartMode() == sw::GameStartMode::Continue );
	SW_EXPECT_TRUE( sw::consumeGameStartMode() == sw::GameStartMode::NewGame );
}
