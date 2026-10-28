#include "pch.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserFileActions.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Commands/Asset/EditorReferenceIndex.h"

namespace sw::editor
{
    ContentBrowserFileActions::ContentBrowserFileActions()
        : _renamingAbs{}
        , _pendingDeleteAbs{}
    {
    }

    bool ContentBrowserFileActions::createFolder( string_view parentAbs, string& outFolderAbs )
    {
        if ( parentAbs.empty() )
            return false;
        return EditorAssetFileCommands::createFolder( parentAbs, outFolderAbs );
    }

    bool ContentBrowserFileActions::createAsset( string_view folderAbs, const EditorNewAssetTemplate& assetTemplate, string& outAssetAbs )
    {
        if ( folderAbs.empty() )
            return false;
        return EditorAssetFileCommands::createAsset( folderAbs, assetTemplate, outAssetAbs );
    }

    bool ContentBrowserFileActions::duplicateAsset( string_view sourceAbs, string& outAssetAbs )
    {
        if ( sourceAbs.empty() )
            return false;
        return EditorAssetFileCommands::duplicateAsset( sourceAbs, outAssetAbs );
    }

    bool ContentBrowserFileActions::moveAssetToFolder( string_view sourceAbs, string_view folderAbs, EditorReferenceIndex& inoutIndex, string& outDestinationAbs )
    {
        const string destinationAbs = FileUtil::joinPath( folderAbs, FileUtil::getFileNamePart( sourceAbs ) );
        if ( FileUtil::pathsEqualNormalized( destinationAbs, sourceAbs ) )
            return false;
        uint32 fixedFileCount{ 0 };
        if ( EditorAssetFileCommands::moveAsset( sourceAbs, destinationAbs, inoutIndex, fixedFileCount ) == false )
            return false;
        outDestinationAbs = destinationAbs;
        return true;
    }

    bool ContentBrowserFileActions::beginRename( string_view absolutePath, string& outStem )
    {
        if ( absolutePath.empty() || FileUtil::isDirectory( absolutePath ) )
            return false;
        string suffix;
        EditorAssetFileCommands::splitAssetName( FileUtil::getFileNamePart( absolutePath ), outStem, suffix );
        _renamingAbs = FileUtil::normalizeSeparators( absolutePath );
        return true;
    }

    bool ContentBrowserFileActions::commitRename( string_view newName, EditorReferenceIndex& inoutIndex, string& outDestinationAbs )
    {
        const string renamingAbs = _renamingAbs;
        _renamingAbs.clear();
        if ( renamingAbs.empty() )
            return false;
        string destinationAbs;
        if ( makeRenameDestination( renamingAbs, newName, destinationAbs ) == false )
            return false;
        uint32 fixedFileCount{ 0 };
        if ( EditorAssetFileCommands::moveAsset( renamingAbs, destinationAbs, inoutIndex, fixedFileCount ) == false )
            return false;
        outDestinationAbs = destinationAbs;
        return true;
    }

    bool ContentBrowserFileActions::makeRenameDestination( string_view sourceAbs, string_view newName, string& outDestinationAbs )
    {
        if ( newName.empty() )
            return false;
        string stem;
        string suffix;
        EditorAssetFileCommands::splitAssetName( FileUtil::getFileNamePart( sourceAbs ), stem, suffix );
        // 확장자는 그대로 둔다. 확장자까지 입력했으면 그것을 쓴다.
        string fileName = FileUtil::normalizePath( newName );
        if ( suffix.empty() == false && StringUtil::endsWith( fileName, suffix ) == false )
            fileName += suffix;
        outDestinationAbs = FileUtil::joinPath( FileUtil::getDirectoryPart( sourceAbs ), fileName );
        return FileUtil::pathsEqualNormalized( outDestinationAbs, sourceAbs ) == false;
    }

    bool ContentBrowserFileActions::confirmDelete()
    {
        if ( _pendingDeleteAbs.empty() )
            return false;
        const bool bDeleted = EditorAssetFileCommands::moveToTrash( _pendingDeleteAbs );
        _pendingDeleteAbs.clear();
        return bDeleted;
    }
} // namespace sw::editor
