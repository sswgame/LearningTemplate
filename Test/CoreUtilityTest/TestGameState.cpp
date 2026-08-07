/**
 * @file TestGameState.cpp
 * @brief Play-mode GameState transitions
 */
#include "TestFramework.h"
#include "Core/Game/GameState.h"

SW_TEST_CASE( GameStateTest, DefaultIsStopped )
{
	sw::setGameState( sw::GameState::Stopped );
	SW_EXPECT_EQUAL( static_cast<int>( sw::GameState::Stopped ), static_cast<int>( sw::getGameState() ) );
}

SW_TEST_CASE( GameStateTest, TransitionStoppedPlayingPaused )
{
	sw::setGameState( sw::GameState::Stopped );
	SW_ASSERT_EQUAL( static_cast<int>( sw::GameState::Stopped ), static_cast<int>( sw::getGameState() ) );

	sw::setGameState( sw::GameState::Playing );
	SW_EXPECT_EQUAL( static_cast<int>( sw::GameState::Playing ), static_cast<int>( sw::getGameState() ) );

	sw::setGameState( sw::GameState::Paused );
	SW_EXPECT_EQUAL( static_cast<int>( sw::GameState::Paused ), static_cast<int>( sw::getGameState() ) );

	sw::setGameState( sw::GameState::Playing );
	SW_EXPECT_EQUAL( static_cast<int>( sw::GameState::Playing ), static_cast<int>( sw::getGameState() ) );

	sw::setGameState( sw::GameState::Stopped );
	SW_EXPECT_EQUAL( static_cast<int>( sw::GameState::Stopped ), static_cast<int>( sw::getGameState() ) );
}

SW_TEST_CASE( GameStateTest, IdempotentSet )
{
	sw::setGameState( sw::GameState::Playing );
	sw::setGameState( sw::GameState::Playing );
	SW_EXPECT_EQUAL( static_cast<int>( sw::GameState::Playing ), static_cast<int>( sw::getGameState() ) );

	sw::setGameState( sw::GameState::Stopped );
}
