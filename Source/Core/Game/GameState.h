#pragma once

/**
 * @file GameState.h
 * @brief 앱/에디터 Play 상태
 */

#include "Core/Common/CommonMacros.h"

namespace sw
{
	enum class GameState
	{
		Stopped,
		Playing,
		Paused
	};

	/** @brief Title → Playing handoff (New Game vs Continue). */
	enum class GameStartMode : uint8
	{
		NewGame = 0,
		Continue
	};

	SW_API GameState getGameState();
	SW_API void		 setGameState( GameState state );

	SW_API void		  setGameStartMode( GameStartMode mode );
	SW_API GameStartMode consumeGameStartMode();
} // namespace sw
