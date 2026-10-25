#include "pch.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Asset/AssetImportPathFilter.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    bool ProjectSettingsCatalog::isEditableLocation( string_view relativePath )
    {
        const bool bUnderConfig   = StringUtil::startsWith( relativePath, "Config/" );
        const bool bUnderResource = StringUtil::startsWith( relativePath, "Resource/" );
        return ( bUnderConfig || bUnderResource ) && StringUtil::endsWith( relativePath, ".json" );
    }

    bool ProjectSettingsCatalog::parseReference( string_view referenceJSON, vector<ProjectSettingsEntry>& outListEntry )
    {
        outListEntry.clear();
        JSONDocument document;
        if ( document.parse( referenceJSON, "ConfigReference.json" ) == false || document.getRoot().isObject() == false )
            return false;
        forEachObjectInArray( document.getRoot(), "files", [&outListEntry]( const JSONValue& file, size_t /*index*/ )
        {
            ProjectSettingsEntry entry{};
            entry._page                  = file.get( "page" ).asString();
            entry._relativePath          = file.get( "path" ).asString();
            entry._typeName              = file.get( "type" ).asString();
            const bool bNotEditableEntry = entry._page.empty() || entry._typeName.empty() || isEditableLocation( entry._relativePath ) == false;
            if ( bNotEditableEntry )
                return;
            outListEntry.push_back( std::move( entry ) );
        } );
        return true;
    }

    void ProjectSettingsCatalog::expandWildcards( string_view projectRoot, vector<ProjectSettingsEntry>& inoutListEntry )
    {
        vector<ProjectSettingsEntry> listExpanded;
        listExpanded.reserve( inoutListEntry.size() );
        for ( ProjectSettingsEntry& entry : inoutListEntry )
        {
            if ( entry._relativePath.find( '*' ) == string::npos )
            {
                listExpanded.push_back( std::move( entry ) );
                continue;
            }
            // 와일드카드는 마지막 구성요소 한 곳만 지원한다(`Config/Game/*.json`). 폴더 위치에 있는 `*` 는 확장하지 않는다.
            const size_t slash = entry._relativePath.rfind( '/' );
            if ( slash == string::npos || entry._relativePath.find( '*' ) < slash )
                continue;
            // 이름 조건은 파이썬 `PathPattern` 과 같은 임포트 규칙 필터로 맞춘다(`*` 는 폴더를 넘지 않는다). 확장자로 모은 목록을 이 필터가 거른다.
            AssetImportPathFilter filter;
            filter._listIncludePattern.push_back( entry._relativePath );
            const string   directory = FileUtil::joinPath( projectRoot, string_view( entry._relativePath ).substr( 0, slash ) );
            const size_t   dot       = entry._relativePath.rfind( '.' );
            const string   extension = dot != string::npos && dot > slash ? entry._relativePath.substr( dot ) : string();
            vector<string> listFile;
            if ( FileUtil::collectFiles( directory, extension, listFile, false ) == false )
                continue;
            std::sort( listFile.begin(), listFile.end() );
            for ( const string& filePath : listFile )
            {
                string fileName = FileUtil::getFileNamePart( filePath );
                if ( filter.matchesPath( entry._relativePath.substr( 0, slash + 1 ) + fileName ) == false )
                    continue;
                if ( fileName.size() > extension.size() )
                    fileName.resize( fileName.size() - extension.size() );
                ProjectSettingsEntry expanded{};
                expanded._page         = entry._page + "/" + fileName;
                expanded._relativePath = entry._relativePath.substr( 0, slash + 1 ) + FileUtil::getFileNamePart( filePath );
                expanded._typeName     = entry._typeName;
                listExpanded.push_back( std::move( expanded ) );
            }
        }
        inoutListEntry = std::move( listExpanded );
    }
} // namespace sw::editor
