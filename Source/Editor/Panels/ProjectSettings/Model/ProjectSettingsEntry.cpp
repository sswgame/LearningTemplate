#include "pch.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsEntry.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/ConfigManager.h"

namespace sw::editor
{
    bool ProjectSettingsEntry::isEditableLocation( string_view relativePath )
    {
        const bool bUnderConfig   = StringUtil::startsWith( relativePath, "Config/" );
        const bool bUnderResource = StringUtil::startsWith( relativePath, "Content/" );
        return ( bUnderConfig || bUnderResource ) && StringUtil::endsWith( relativePath, ".json" );
    }

    void ProjectSettingsEntry::collectLoaded( const ConfigManager& configManager, string_view projectRoot, vector<ProjectSettingsEntry>& outListEntry )
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

    bool ProjectSettingsEntry::collectTables( string_view projectRoot, const vector<ProjectSettingsEntry>& listTyped, vector<ProjectSettingsEntry>& outListEntry )
    {
        outListEntry.clear();
        const string   configFolder = FileUtil::joinPath( projectRoot, kTableFolder );
        vector<string> listPath;
        if ( FileUtil::collectFiles( configFolder, ".json", listPath, true ) == false )
        {
            SW_LOG_ERROR_ID( "ProjectSettingsPanel.TableFolderUnreadable", "Project settings: %# could not be listed - the table pages are missing", configFolder );
            return false;
        }
        for ( const string& absolutePath : listPath )
        {
            // 반사 형식 페이지가 있는 파일은 그 페이지가 고친다(형식 검사 · 기본값 차이 쓰기) — 같은 파일을 두 페이지가 들지 않는다.
            const bool bTyped = VectorUtil::containsIf( listTyped, [&absolutePath]( const ProjectSettingsEntry& typed )
            { return FileUtil::isSameDiskPath( typed._absolutePath, absolutePath ); } );
            if ( bTyped )
                continue;
            string relativePath;
            if ( FileUtil::makeRelativePath( projectRoot, absolutePath, relativePath ) == false )
                continue;
            ProjectSettingsEntry entry{};
            entry._relativePath = FileUtil::normalizeSeparators( relativePath );
            entry._absolutePath = FileUtil::normalizeSeparators( absolutePath );
            // 목록 이름은 `Config/` 아래 경로에서 `.json` 을 뗀 것이다(`Rules/Lint/NamingWords`) — 줄기 이름만이면 `Game/Empty` 와 `Server/Empty` 가 겹친다.
            entry._page = entry._relativePath.substr( string_view( kTableFolder ).size() + 1 );
            entry._page.resize( entry._page.size() - string_view( ".json" ).size() );
            outListEntry.push_back( std::move( entry ) );
        }
        VectorUtil::sort( outListEntry, []( const ProjectSettingsEntry& lhs, const ProjectSettingsEntry& rhs )
        { return lhs._page < rhs._page; } );
        return true;
    }
} // namespace sw::editor
