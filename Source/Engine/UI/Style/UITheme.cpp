#include "pch.h"

#include "Engine/UI/Style/UITheme.h"

#include "Core/Log/Logger.h"

#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "UITheme" );

    bool UIThemeCatalog::loadFromResource( string_view resourcePath )
    {
        if ( XmlSerializer::loadFile( resourcePath, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR( "[UI] UI theme catalog could not be read or holds unknown keys: %#", resourcePath );
            return false;
        }
        return true;
    }

    const UIThemeDesc* UIThemeCatalog::findTheme( const hashed_string& name ) const
    {
        for ( const UIThemeDesc& theme : _listTheme )
        {
            if ( theme._name == name )
                return &theme;
        }
        return nullptr;
    }
} // namespace sw
