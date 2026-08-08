#pragma once
/**
 * @file AppState.h
 * @brief High-level client app flow (Splash → Title → Playing / Editor)
 */

#include "Core/Common/Types.h"

namespace sw
{
	enum class AppState : uint8
	{
		Splash = 0,
		Title,
		Playing,
		Editor
	};

	AppState getAppState();
	void	 setAppState( AppState state );
} // namespace sw
