#include "pch.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Config/ConfigManager.h"

namespace sw::editor
{
    bool ProjectSettingsCatalog::isEditableLocation( string_view relativePath )
    {
        const bool bUnderConfig   = StringUtil::startsWith( relativePath, "Config/" );
        const bool bUnderResource = StringUtil::startsWith( relativePath, "Content/" );
        return ( bUnderConfig || bUnderResource ) && StringUtil::endsWith( relativePath, ".json" );
    }

    void ProjectSettingsCatalog::collectLoaded( const ConfigManager& configManager, string_view projectRoot, vector<ProjectSettingsEntry>& outListEntry )
    {
        outListEntry.clear();
        configManager.forEachFileSource( [projectRoot, &outListEntry]( const hashed_string& typeName, string_view resolvedPath )
        {
            string relativePath;
            if ( FileUtil::makeRelativePath( projectRoot, resolvedPath, relativePath ) == false )
                return;
            relativePath = FileUtil::normalizeSeparators( relativePath );
            if ( isEditableLocation( relativePath ) == false )
                return;
            ProjectSettingsEntry entry{};
            entry._page         = string( typeName.c_str() );
            entry._typeName     = entry._page;
            entry._relativePath = std::move( relativePath );
            entry._absolutePath = FileUtil::normalizeSeparators( resolvedPath );
            outListEntry.push_back( std::move( entry ) );
        } );
        VectorUtil::sort( outListEntry, []( const ProjectSettingsEntry& lhs, const ProjectSettingsEntry& rhs )
        { return lhs._page < rhs._page; } );
    }
} // namespace sw::editor
