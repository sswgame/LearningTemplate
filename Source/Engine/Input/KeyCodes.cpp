#include "pch.h"

#include "Engine/Input/KeyCodes.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
	Key KeyCodes::fromName( string_view name )
	{
		return engine::getTypeRegistry().enumFromString<Key>( name );
	}

	const utf8* KeyCodes::toName( Key key )
	{
		return engine::getTypeRegistry().enumToString( key );
	}

	MouseButton MouseButtons::fromName( string_view name )
	{
		return engine::getTypeRegistry().enumFromString<MouseButton>( name );
	}

	const utf8* MouseButtons::toName( MouseButton button )
	{
		return engine::getTypeRegistry().enumToString( button );
	}
} // namespace sw
