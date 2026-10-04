#include "pch.h"

#include "Engine/Input/KeyCodeUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    Key KeyCodeUtil::fromName( string_view name )
    {
        if ( engine::areEngineServicesBound() )
            return engine::getTypeRegistry().enumFromString<Key>( name );
        return Key::Unknown;
    }

    const utf8* KeyCodeUtil::toName( Key key )
    {
        if ( engine::areEngineServicesBound() )
            return engine::getTypeRegistry().enumToString( key );
        return "Unknown";
    }

    MouseButton MouseButtonUtil::fromName( string_view name )
    {
        if ( engine::areEngineServicesBound() )
            return engine::getTypeRegistry().enumFromString<MouseButton>( name );
        return MouseButton::Count;
    }

    const utf8* MouseButtonUtil::toName( MouseButton button )
    {
        if ( engine::areEngineServicesBound() )
            return engine::getTypeRegistry().enumToString( button );
        return "Unknown";
    }
} // namespace sw
