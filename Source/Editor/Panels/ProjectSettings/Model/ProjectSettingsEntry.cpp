#include "pch.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsEntry.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/ConfigManager.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    bool ProjectSettingsEntry::isEditableLocation( string_view relativePath )
    {
        const bool bUnderConfig   = StringUtil::startsWith( relativePath, "Config/" );
        const bool bUnderResource = StringUtil::startsWith( relativePath, "Content/" );
        return ( bUnderConfig || bUnderResource ) && StringUtil::endsWith( relativePath, ".json" );
    }

    void ProjectSettingsEntry::collectLoaded( const ConfigManager& configManager, const DiskPath& projectRoot, vector<ProjectSettingsEntry>& outListEntry )
    {
        outListEntry.clear();
        configManager.forEachFileSource( [projectRoot, &outListEntry]( const hashed_string& typeName, string_view resolvedPath )
        {
            string relativePath;
            if ( FileUtil::makeRelativePath( projectRoot.str(), resolvedPath, relativePath ) == false )
                return;
            relativePath = FileUtil::normalizeSeparators( relativePath );
            if ( isEditableLocation( relativePath ) == false )
                return;
            ProjectSettingsEntry entry{};
            entry._page         = string( typeName.c_str() );
            entry._typeName     = entry._page;
            entry._relativePath = std::move( relativePath );
            entry._absolutePath = FileUtil::toDiskPath( FileUtil::normalizeSeparators( resolvedPath ) );
            outListEntry.push_back( std::move( entry ) );
        } );
        addOtherGamePresets( projectRoot, outListEntry );
        VectorUtil::sort( outListEntry, []( const ProjectSettingsEntry& lhs, const ProjectSettingsEntry& rhs )
        { return lhs._page < rhs._page; } );
    }

    void ProjectSettingsEntry::addOtherGamePresets( const DiskPath& projectRoot, vector<ProjectSettingsEntry>& inoutListEntry )
    {
        for ( const utf8* pPresetFolder : { config::kDirRuntimeGamePreset, config::kDirRuntimeServerPreset } )
        {
            const string presetFolder = FileUtil::normalizeSeparators( pPresetFolder );
            // 고른 게임의 프리셋이 읽혀 있으면 그 형식이 이 폴더 파일의 형식이다
            const auto isInFolder = [&presetFolder]( const ProjectSettingsEntry& entry )
            { return FileUtil::normalizeSeparators( FileUtil::getDirectoryPart( entry._relativePath ) ) == presetFolder; };
            ProjectSettingsEntry* const pLoaded = VectorUtil::findIf( inoutListEntry, isInFolder );
            if ( pLoaded == nullptr )
                continue;
            const ProjectSettingsEntry active = *pLoaded; // 복사 — 아래 push_back 이 배열을 옮긴다
            pLoaded->_gameName                = FileUtil::removeExtension( FileUtil::getFileNamePart( active._relativePath ) );
            vector<DiskPath> listPath;
            if ( FileUtil::collectFiles( projectRoot.join( presetFolder ), ".json", listPath, false ) == false )
            {
                SW_LOG_ERROR_ID( "ProjectSettingsPanel.PresetFolderUnreadable", "Project settings: %# could not be listed - the other games' presets are missing", presetFolder );
                continue;
            }
            for ( const DiskPath& absolutePath : listPath )
            {
                if ( FileUtil::isSameDiskPath( absolutePath.str(), active._absolutePath.str() ) )
                    continue;
                ProjectSettingsEntry entry{};
                entry._gameName = FileUtil::removeExtension( FileUtil::getFileNamePart( absolutePath.str() ) );
                entry._typeName = active._typeName;
                StringBuilder<constant::buffer::kMax256> pageBuilder;
                pageBuilder.appendFormat( "%#/%#", active._typeName, entry._gameName );
                entry._page         = string( pageBuilder.view() );
                entry._relativePath = FileUtil::joinPath( presetFolder, FileUtil::getFileNamePart( absolutePath.str() ) );
                entry._absolutePath = absolutePath;
                inoutListEntry.push_back( std::move( entry ) );
            }
        }
    }

    bool ProjectSettingsEntry::collectTables( const DiskPath& projectRoot, const vector<ProjectSettingsEntry>& listTyped, vector<ProjectSettingsEntry>& outListEntry )
    {
        outListEntry.clear();
        const DiskPath   configFolder = projectRoot.join( kTableFolder );
        vector<DiskPath> listPath;
        if ( FileUtil::collectFiles( configFolder, ".json", listPath, true ) == false )
        {
            SW_LOG_ERROR_ID( "ProjectSettingsPanel.TableFolderUnreadable", "Project settings: %# could not be listed - the table pages are missing", configFolder.str() );
            return false;
        }
        for ( const DiskPath& absolutePath : listPath )
        {
            // 반사 형식 페이지가 있는 파일은 그 페이지가 고친다(형식 검사 · 기본값 차이 쓰기) — 같은 파일을 두 페이지가 들지 않는다.
            const bool bTyped = VectorUtil::containsIf( listTyped, [&absolutePath]( const ProjectSettingsEntry& typed )
            { return FileUtil::isSameDiskPath( typed._absolutePath.str(), absolutePath.str() ); } );
            if ( bTyped )
                continue;
            string relativePath;
            if ( FileUtil::makeRelativePath( projectRoot.str(), absolutePath.str(), relativePath ) == false )
                continue;
            ProjectSettingsEntry entry{};
            entry._relativePath = FileUtil::normalizeSeparators( relativePath );
            entry._absolutePath = absolutePath;
            // 목록 이름은 `Config/` 아래 경로에서 `.json` 을 뗀 것이다(`Rules/Lint/NamingWords`) — 줄기 이름만이면 `Game/Playground` 와 `Server/Playground` 가 겹친다.
            entry._page = entry._relativePath.substr( string_view( kTableFolder ).size() + 1 );
            entry._page.resize( entry._page.size() - string_view( ".json" ).size() );
            outListEntry.push_back( std::move( entry ) );
        }
        VectorUtil::sort( outListEntry, []( const ProjectSettingsEntry& lhs, const ProjectSettingsEntry& rhs )
        { return lhs._page < rhs._page; } );
        return true;
    }
} // namespace sw::editor
