#include "pch.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserFileActions.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/EditorAssetIndex.h"
#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"

namespace sw::editor
{
    ContentBrowserMoveQueue::ContentBrowserMoveQueue()
        : _listMove{}
    {
    }

    void ContentBrowserMoveQueue::addRename( string_view newName )
    {
        discardRename();
        Move move;
        move._bRename = true;
        move._newName = string( newName );
        _listMove.push_back( std::move( move ) );
    }

    void ContentBrowserMoveQueue::addMove( const AbsolutePath& sourceAbs, const AbsolutePath& folderAbs )
    {
        for ( Move& move : _listMove )
        {
            if ( move._bRename == false && FileUtil::isSameDiskPath( move._sourceAbs.str(), sourceAbs.str() ) )
            {
                move._folderAbs = folderAbs;
                return;
            }
        }
        Move move;
        move._sourceAbs = sourceAbs;
        move._folderAbs = folderAbs;
        _listMove.push_back( std::move( move ) );
    }

    void ContentBrowserMoveQueue::discardRename()
    {
        VectorUtil::removeIf( _listMove, []( const Move& move )
        { return move._bRename; } );
    }

    bool ContentBrowserMoveQueue::takeNext( Move& outMove )
    {
        if ( _listMove.empty() )
            return false;
        outMove = std::move( _listMove.front() );
        _listMove.erase( _listMove.begin() );
        return true;
    }

    ContentBrowserFileActions::ContentBrowserFileActions()
        : _renamingAbs{}
        , _pendingDeleteAbs{}
    {
    }

    bool ContentBrowserFileActions::createFolder( const AbsolutePath& parentAbs, string& outFolderAbs )
    {
        if ( parentAbs.empty() )
            return false;
        return EditorAssetFileCommands::createFolder( parentAbs, outFolderAbs );
    }

    bool ContentBrowserFileActions::createAsset( const AbsolutePath& folderAbs, const EditorNewAssetTemplate& assetTemplate, string& outAssetAbs )
    {
        if ( folderAbs.empty() )
            return false;
        return EditorAssetFileCommands::createAsset( folderAbs, assetTemplate, outAssetAbs );
    }

    bool ContentBrowserFileActions::duplicateAsset( const AbsolutePath& sourceAbs, string& outAssetAbs )
    {
        if ( sourceAbs.empty() )
            return false;
        return EditorAssetFileCommands::duplicateAsset( sourceAbs, outAssetAbs );
    }

    expected<vector<EditorAssetMove>> ContentBrowserFileActions::collectMovesToFolder( const AbsolutePath& sourceAbs, const AbsolutePath& folderAbs )
    {
        if ( FileUtil::isDirectory( sourceAbs ) )
            return EditorAssetFileCommands::collectFolderMoves( sourceAbs, folderAbs );
        const AbsolutePath destinationAbs = folderAbs.join( FileUtil::getFileNamePart( sourceAbs.str() ) );
        if ( FileUtil::isSameDiskPath( destinationAbs.str(), sourceAbs.str() ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "it is already in that folder", FileUtil::getFileNamePart( sourceAbs.str() ) } );
        return vector<EditorAssetMove>{
            EditorAssetMove{ sourceAbs, destinationAbs }
        };
    }

    bool ContentBrowserFileActions::planMoveToFolder( const AbsolutePath& sourceAbs, const AbsolutePath& folderAbs, const EditorAssetIndex& index, EditorAssetMovePlan& outPlan )
    {
        const expected<vector<EditorAssetMove>> listMove = collectMovesToFolder( sourceAbs, folderAbs );
        if ( listMove.has_value() == false )
        {
            outPlan = EditorAssetMovePlan{};
            StringBuilder<constant::buffer::kMax256> failureBuilder;
            failureBuilder.appendFormat( "%# '%#'", listMove.error().message(), listMove.error().detail() );
            outPlan._failure = string( failureBuilder.view() );
            return false;
        }
        return EditorAssetFileCommands::planAssetMoves( *listMove, index, outPlan );
    }

    bool ContentBrowserFileActions::moveAssetToFolder( const AbsolutePath& sourceAbs, const AbsolutePath& folderAbs, EditorAssetIndex& inoutIndex, string& outDestinationAbs )
    {
        const expected<vector<EditorAssetMove>> collected = collectMovesToFolder( sourceAbs, folderAbs );
        if ( collected.has_value() == false )
        {
            SW_LOG_WARNING_ID( "ContentBrowserFileActions.NothingMoved", "Nothing was moved - %# '%#'", collected.error().message(), collected.error().detail() );
            return false;
        }
        const vector<EditorAssetMove>& listMove = *collected;
        const bool                     bFolder  = FileUtil::isDirectory( sourceAbs );
        EditorAssetMovePlan            plan;
        if ( EditorAssetFileCommands::applyAssetMoves( listMove, inoutIndex, plan ) == false )
            return false; // 이유와 되돌림은 applyAssetMoves 가 알렸다
        outDestinationAbs = bFolder ? FileUtil::joinPath( folderAbs.str(), FileUtil::getFileNamePart( sourceAbs.str() ) ) : listMove.front()._destinationAbs.str();
        if ( bFolder )
        {
            // 파일은 모두 옮겼다 — 빈 폴더 나무만 남았을 때만 지운다(`removeDirectory` 는 재귀라, 그 사이 누가 넣은 파일이 있으면 지우지 않고 둔다)
            vector<AbsolutePath> listLeftFile;
            const bool           bListed = FileUtil::collectFiles( sourceAbs, "", listLeftFile, true );
            if ( bListed == false || listLeftFile.empty() == false || FileUtil::removeDirectory( sourceAbs ) == false )
                SW_LOG_WARNING_ID( "ContentBrowserFileActions.SourceFolderKept", "Moved every file of '%#' but kept the folder (not empty or could not be removed)",
                                   FileUtil::getFileNamePart( sourceAbs.str() ) );
        }
        return true;
    }

    bool ContentBrowserFileActions::beginRename( const AbsolutePath& absolutePath, string& outStem )
    {
        if ( absolutePath.empty() || FileUtil::isDirectory( absolutePath ) )
            return false;
        string suffix;
        EditorAssetFileCommands::splitAssetName( FileUtil::getFileNamePart( absolutePath.str() ), outStem, suffix );
        _renamingAbs = absolutePath;
        return true;
    }

    bool ContentBrowserFileActions::commitRename( string_view newName, EditorAssetIndex& inoutIndex, AbsolutePath& outDestinationAbs )
    {
        const AbsolutePath renamingAbs = _renamingAbs;
        _renamingAbs                   = {};
        if ( renamingAbs.empty() )
            return false;
        AbsolutePath destinationAbs;
        if ( makeRenameDestination( renamingAbs, newName, destinationAbs ) == false )
            return false;
        uint32 fixedFileCount{ 0 };
        if ( EditorAssetFileCommands::moveAsset( renamingAbs, destinationAbs, inoutIndex, fixedFileCount ) == false )
            return false;
        outDestinationAbs = destinationAbs;
        return true;
    }

    bool ContentBrowserFileActions::makeRenameDestination( const AbsolutePath& sourceAbs, string_view newName, AbsolutePath& outDestinationAbs )
    {
        if ( newName.empty() )
            return false;
        string stem;
        string suffix;
        EditorAssetFileCommands::splitAssetName( FileUtil::getFileNamePart( sourceAbs.str() ), stem, suffix );
        // 확장자는 그대로 둔다. 확장자까지 입력했으면 그것을 쓴다.
        string fileName = FileUtil::normalizePath( newName ).str();
        if ( suffix.empty() == false && StringUtil::endsWith( fileName, suffix ) == false )
            fileName += suffix;
        outDestinationAbs = sourceAbs.getParent().join( fileName );
        return FileUtil::isSameDiskPath( outDestinationAbs.str(), sourceAbs.str() ) == false;
    }

    bool ContentBrowserFileActions::confirmDelete()
    {
        if ( _pendingDeleteAbs.empty() )
            return false;
        const bool bDeleted = EditorAssetFileCommands::moveToTrash( _pendingDeleteAbs );
        _pendingDeleteAbs   = {};
        return bDeleted;
    }
} // namespace sw::editor
