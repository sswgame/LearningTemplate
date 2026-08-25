#include "pch.h"

#include "Engine/Input/KeyCodes.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
	Key keyFromName( string_view name )
	{
		return engine::getTypeRegistry().enumFromString<Key>( name );
	}

	const utf8* keyToName( Key key )
	{
		return engine::getTypeRegistry().enumToString( key );
	}

	MouseButton mouseButtonFromName( string_view name )
	{
		return engine::getTypeRegistry().enumFromString<MouseButton>( name );
	}

	const utf8* mouseButtonToName( MouseButton button )
	{
		return engine::getTypeRegistry().enumToString( button );
	}
} // namespace sw
