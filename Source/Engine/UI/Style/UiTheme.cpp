#include "pch.h"

#include "Engine/UI/Style/UiTheme.h"

#include "Core/Log/Logger.h"

#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "UiTheme" );

    bool UiThemeCatalog::loadFromResource( string_view resourcePath )
    {
        if ( XMLSerializer::loadFile( resourcePath, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR( "[Ui] UI theme catalog could not be read or holds unknown keys: %#", resourcePath );
            return false;
        }
        return true;
    }

    const UiThemeDesc* UiThemeCatalog::findTheme( const hashed_string& name ) const
    {
        for ( const UiThemeDesc& theme : _listTheme )
        {
            if ( theme._name == name )
                return &theme;
        }
        return nullptr;
    }
} // namespace sw
