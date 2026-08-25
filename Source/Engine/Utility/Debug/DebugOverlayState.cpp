#include "pch.h"

#include "Engine/Utility/Debug/DebugOverlayState.h"

namespace sw
{
	void DebugOverlayState::setFloat( hashed_string key, float32 value )
	{
		_mapFloats[key] = value;
	}

	float32 DebugOverlayState::getFloat( hashed_string key, float32 defaultValue ) const
	{
		const auto it = _mapFloats.find( key );
		if ( it == _mapFloats.end() )
			return defaultValue;
		return it->second;
	}

	void DebugOverlayState::setString( hashed_string key, string_view value )
	{
		_mapStrings[key] = string( value );
	}

	string DebugOverlayState::getString( hashed_string key ) const
	{
		const auto it = _mapStrings.find( key );
		if ( it == _mapStrings.end() )
			return {};
		return it->second;
	}

	void DebugOverlayState::clear()
	{
		_mapFloats.clear();
		_mapStrings.clear();
	}
} // namespace sw
