/**
 * @file GameState.cpp
 */
#include "GameState.h"

namespace sw
{
	namespace
	{
		GameState g_gameState = GameState::Stopped;
	}

	GameState getGameState()
	{
		return g_gameState;
	}

	void setGameState( GameState state )
	{
		g_gameState = state;
	}
}
