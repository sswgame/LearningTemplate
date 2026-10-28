#include "pch.h"

#include "Editor/Common/Workspace/EditorLayoutStore.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorPaths.h"

namespace sw::editor
{
    namespace
    {
        bool isAllowedChar( utf8 ch )
        {
            return ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' ) || ( '0' <= ch && ch <= '9' ) || ch == ' ' || ch == '_' || ch == '-';
        }
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
            if ( isAllowedChar( ch ) == false )
                return false;
        }
        outName = string( trimmed );
        return true;
    }

    string EditorLayoutStore::getDefaultFolder()
    {
        const string stateDirectory = EditorPaths::getEditorStateDirectory();
        if ( stateDirectory.empty() )
            return {};
        return FileUtil::joinPath( stateDirectory, kFolderName );
    }

    string EditorLayoutStore::makeImguiINIPath( string_view folder, string_view name )
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

    bool EditorLayoutStore::save( string_view folder, string_view name, string_view imguiINIText, const EditorPanelVisibility& panelVisibility )
    {
        if ( FileUtil::ensureDirectoryExists( folder ) == false )
            return false;
        if ( FileUtil::writeTextFile( makeImguiINIPath( folder, name ), string( imguiINIText ) ) == false )
            return false;
        return panelVisibility.saveFile( makeVisibilityPath( folder, name ) );
    }

    bool EditorLayoutStore::load( string_view folder, string_view name, string& outImguiINIText, EditorPanelVisibility& outPanelVisibility )
    {
        outPanelVisibility._mapOpen.clear();
        const string imguiPath = makeImguiINIPath( folder, name );
        if ( FileUtil::exists( imguiPath ) == false || FileUtil::readTextFile( imguiPath, outImguiINIText ) == false )
            return false;
        const string visibilityPath = makeVisibilityPath( folder, name );
        if ( outPanelVisibility.loadFile( visibilityPath ) == ConfigReadResult::Invalid )
        {
            outPanelVisibility._mapOpen.clear();
            SW_LOG_WARNING( "Layout '%#': could not read %# - panel visibility is left as it is", string( name ).c_str(), visibilityPath.c_str() );
        }
        return true;
    }

    bool EditorLayoutStore::remove( string_view folder, string_view name )
    {
        const bool bImguiRemoved      = FileUtil::removeFile( makeImguiINIPath( folder, name ) );
        const bool bVisibilityRemoved = FileUtil::removeFile( makeVisibilityPath( folder, name ) );
        return bImguiRemoved && bVisibilityRemoved;
    }
} // namespace sw::editor
