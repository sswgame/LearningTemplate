#include "pch.h"

#include "Engine/Utility/Debug/DebugOverlayState.h"

namespace sw
{
	void DebugOverlayState::setFloat( hashed_string key, float32 value )
	{
		_mapFloat[key] = value;
	}

	float32 DebugOverlayState::getFloat( hashed_string key, float32 defaultValue ) const
	{
		const auto it = _mapFloat.find( key );
		if ( it == _mapFloat.end() )
			return defaultValue;
		return it->second;
	}

	void DebugOverlayState::setString( hashed_string key, string_view value )
	{
		_mapString[key] = string( value );
	}

	string DebugOverlayState::getString( hashed_string key ) const
	{
		const auto it = _mapString.find( key );
		if ( it == _mapString.end() )
			return {};
		return it->second;
	}

	void DebugOverlayState::clear()
	{
		_mapFloat.clear();
		_mapString.clear();
	}
} // namespace sw
