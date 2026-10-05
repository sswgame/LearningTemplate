#include "pch.h"

#include "Editor/Common/Workspace/EditorLayoutStore.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/EditorUtil.h"

namespace sw::editor
{
    namespace
    {
        struct EditorLayoutStoreInternal
        {
            static bool isAllowedChar( utf8 ch )
            {
                return ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' ) || ( '0' <= ch && ch <= '9' ) || ch == ' ' || ch == '_' || ch == '-';
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorLayoutStore" );

    bool EditorLayoutStore::sanitizeName( string_view name, string& outName )
    {
        const string_view trimmed = StringUtil::trim( name );
        if ( trimmed.empty() || trimmed.size() > kMaxNameLength )
            return false;
        for ( const utf8 ch : trimmed )
        {
            if ( EditorLayoutStoreInternal::isAllowedChar( ch ) == false )
                return false;
        }
        outName = string( trimmed );
        return true;
    }

    string EditorLayoutStore::getDefaultFolder()
    {
        const string configDirectory = EditorUtil::getEditorConfigDirectory();
        if ( configDirectory.empty() )
            return {};
        return FileUtil::joinPath( configDirectory, kFolderName );
    }

    string EditorLayoutStore::makeImguiIniPath( string_view folder, string_view name )
    {
        return FileUtil::joinPath( folder, string( name ) + kImguiSuffix );
    }

    string EditorLayoutStore::makeVisibilityPath( string_view folder, string_view name )
    {
        return FileUtil::joinPath( folder, string( name ) + kVisibilitySuffix );
    }

    void EditorLayoutStore::collectNames( string_view folder, vector<string>& outListName )
    {
        outListName.clear();
        vector<string> listFile;
        if ( FileUtil::isDirectory( folder ) == false || FileUtil::collectFiles( folder, ".ini", listFile, false ) == false )
            return;
        const string_view suffix{ kImguiSuffix };
        for ( const string& filePath : listFile )
        {
            const string fileName = FileUtil::getFileNamePart( filePath );
            if ( fileName.size() <= suffix.size() || StringUtil::endsWith( fileName, suffix ) == false )
                continue;
            outListName.push_back( fileName.substr( 0, fileName.size() - suffix.size() ) );
        }
        std::sort( outListName.begin(), outListName.end() );
    }

    bool EditorLayoutStore::save( string_view folder, string_view name, string_view imguiIniText, const KeyValueMap& panelVisibility )
    {
        if ( FileUtil::ensureDirectoryExists( folder ) == false )
            return false;
        if ( FileUtil::writeTextFile( makeImguiIniPath( folder, name ), string( imguiIniText ) ) == false )
            return false;
        return KeyValueFile::saveFile( makeVisibilityPath( folder, name ), panelVisibility, "Editor panel visibility (1=open, 0=closed)", "WindowVisibility" );
    }

    bool EditorLayoutStore::load( string_view folder, string_view name, string& outImguiIniText, KeyValueMap& outPanelVisibility )
    {
        outPanelVisibility.clear();
        const string imguiPath = makeImguiIniPath( folder, name );
        if ( FileUtil::exists( imguiPath ) == false || FileUtil::readTextFile( imguiPath, outImguiIniText ) == false )
            return false;
        const string visibilityPath = makeVisibilityPath( folder, name );
        if ( FileUtil::exists( visibilityPath ) && KeyValueFile::loadFile( visibilityPath, outPanelVisibility ) == false )
            SW_LOG_WARNING( "Layout '%#': could not read %# - panel visibility is left as it is", string( name ).c_str(), visibilityPath.c_str() );
        return true;
    }

    bool EditorLayoutStore::remove( string_view folder, string_view name )
    {
        const bool bImguiRemoved      = FileUtil::removeFile( makeImguiIniPath( folder, name ) );
        const bool bVisibilityRemoved = FileUtil::removeFile( makeVisibilityPath( folder, name ) );
        return bImguiRemoved && bVisibilityRemoved;
    }
} // namespace sw::editor
