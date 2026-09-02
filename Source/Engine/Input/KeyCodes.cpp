#include "pch.h"

#include "Engine/Input/KeyCodes.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
	Key KeyCodes::fromName( string_view name )
	{
		if ( engine::areEngineServicesBound() )
			return engine::getTypeRegistry().enumFromString<Key>( name );
		return Key::Unknown;
	}

	const utf8* KeyCodes::toName( Key key )
	{
		if ( engine::areEngineServicesBound() )
			return engine::getTypeRegistry().enumToString( key );
		return "Unknown";
	}

	MouseButton MouseButtons::fromName( string_view name )
	{
		if ( engine::areEngineServicesBound() )
			return engine::getTypeRegistry().enumFromString<MouseButton>( name );
		return MouseButton::Count;
	}

	const utf8* MouseButtons::toName( MouseButton button )
	{
		if ( engine::areEngineServicesBound() )
			return engine::getTypeRegistry().enumToString( button );
		return "Unknown";
	}
} // namespace sw
