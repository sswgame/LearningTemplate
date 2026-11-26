#include "pch.h"

#include "Engine/Input/Key.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    Key findKeyByName( string_view name )
    {
        return tryParseEnum<Key>( name ).value_or( Key::Unknown );
    }

    const utf8* getKeyName( Key key )
    {
        if ( engine::areEngineSubsystemsBound() )
            return engine::getSubsystem<TypeRegistry>().enumToString( key );
        return "Unknown";
    }

    MouseButton findMouseButtonByName( string_view name )
    {
        return tryParseEnum<MouseButton>( name ).value_or( MouseButton::Count );
    }

    const utf8* getMouseButtonName( MouseButton button )
    {
        if ( engine::areEngineSubsystemsBound() )
            return engine::getSubsystem<TypeRegistry>().enumToString( button );
        return "Unknown";
    }
} // namespace sw
