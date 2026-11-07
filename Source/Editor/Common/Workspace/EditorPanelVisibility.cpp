#include "pch.h"

#include "Editor/Common/Workspace/EditorPanelVisibility.h"

#include "Engine/Serialization/Format/JSONSerializer.h"

namespace sw::editor
{
    bool EditorPanelVisibility::saveToFile( string_view absPath ) const
    {
        return JSONSerializer::saveToFile( absPath, this, *StaticType() );
    }

    ConfigReadResult EditorPanelVisibility::loadFromFile( string_view absPath )
    {
        _mapOpen.clear();
        return ConfigManager::readConfigFile( *this, absPath );
    }

    bool EditorPanelVisibility::findOpen( string_view panelID, bool& outOpen ) const
    {
        const auto iter = _mapOpen.find( string( panelID ) );
        if ( iter == _mapOpen.end() )
            return false;
        outOpen = iter->second;
        return true;
    }
} // namespace sw::editor
