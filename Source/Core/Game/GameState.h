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

	SW_API GameState getGameState();
	SW_API void		 setGameState( GameState state );
} // namespace sw
