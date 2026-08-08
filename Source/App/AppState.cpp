/**
 * @file AppState.cpp
 */
#include "AppState.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	namespace
	{
		AppState s_appState = AppState::Splash;
	}

	AppState getAppState()
	{
		return s_appState;
	}

	void setAppState( AppState state )
	{
		if ( s_appState == state )
			return;
		SW_LOG_INFO( "[AppState] %# → %#", static_cast<int32>( s_appState ), static_cast<int32>( state ) );
		s_appState = state;
	}
} // namespace sw
