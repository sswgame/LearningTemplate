/**
 * @file GameState.cpp
 */
#include "GameState.h"

namespace sw
{
	namespace
	{
		GameState s_gameState = GameState::Stopped;
	}

	GameState getGameState()
	{
		return s_gameState;
	}

	void setGameState( GameState state )
	{
		s_gameState = state;
	}
}
