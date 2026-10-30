#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Asset/EditorReferenceIndex.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineDefines.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorAssetFileCommands" );

    namespace
    {
        struct EditorAssetFileCommandsInternal
        {
            /** @brief 탐색기 명령의 기록입니다. 에디터 범위의 상태 저장소가 소유한다. */
            struct ExplorerState
            {
                bool   _bDryRun{ false };
                uint32 _launchCount{ 0 };
                string _lastCommand;
            };

            [[nodiscard]] static bool readExplorerLaunchCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( editor::findSubsystem<EditorStateStore>() == nullptr )
                    return false;
                outValue = static_cast<float64>( getEditorState<ExplorerState>()._launchCount );
                return true;
            }

            [[nodiscard]] static bool readExplorerCommandWellFormed( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( editor::findSubsystem<EditorStateStore>() == nullptr )
                    return false;
                const string_view command{ getEditorState<ExplorerState>()._lastCommand };
#if defined( SW_PLATFORM_WINDOWS )
                constexpr string_view kPrefix = "explorer.exe /select,\"";
                // 경로 부분에 슬래시가 남으면 explorer 가 선택하지 않고 내 문서를 연다(`makeFileExplorerCommand` 의 함정).
                const bool bPrefixed   = command.size() > kPrefix.size() + 1 && command.substr( 0, kPrefix.size() ) == kPrefix && command.back() == '"';
                const bool bWellFormed = bPrefixed && command.find( '/', kPrefix.size() ) == string_view::npos;
#else
                constexpr string_view kPrefix     = "xdg-open \"";
                const bool            bWellFormed = command.size() > kPrefix.size() && command.substr( 0, kPrefix.size() ) == kPrefix && command.back() == '"';
#endif
                outValue = bWellFormed ? 1.0 : 0.0;
                return true;
            }

            static bool runExplorerDryRun( const vector<string>& listArgument, string& outReply )
            {
                bool       bDryRun       = false;
                const bool bReadArgument = listArgument.size() == 1 && StringUtil::tryParseBool( listArgument[0], bDryRun );
                if ( bReadArgument == false || editor::findSubsystem<EditorStateStore>() == nullptr )
                    return false;
                getEditorState<ExplorerState>()._bDryRun = bDryRun;
                outReply                                 = "explorer dry run " + listArgument[0];
                return true;
            }

            static bool runNotifyReveal( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( listArgument.size() != 1 || pContext == nullptr )
                    return false;
                pContext->getSubsystem<EditorNotificationManager>().pushRevealable( "Reveal", listArgument[0], NotificationType::Success, listArgument[0] );
                outReply = "toast with Show in Explorer pushed";
                return true;
            }

            /** @brief `_2` 부터 세다 멈추는 상한입니다. */
            static constexpr uint32 kMaxUniqueSuffix = 10000;

            static bool isNameChar( utf8 ch ) { return ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '-' || ch == '.'; }

            static bool isPathChar( utf8 ch )
            {
                const utf8 lower = StringUtil::toLowerChar( ch );
                return isNameChar( lower ) || lower == '/';
            }

            /** @brief 파일 하나를 옮깁니다(복사한 뒤 원본을 지운다). 실패하면 대상을 지워 원본만 남긴다. */
            [[nodiscard]] static bool moveFile( const string& sourceAbs, const string& destinationAbs )
            {
                if ( FileUtil::copyFile( sourceAbs, destinationAbs ) == false )
                    return false;
                if ( FileUtil::removeFile( sourceAbs ) )
                    return true;
                (void)FileUtil::tryRemoveFile( destinationAbs ); // 원본이 남았으니 사본을 회수한다 — 실패해도 원본은 그대로다
                return false;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool EditorAssetFileCommands::isValidAssetName( string_view name )
    {
        if ( name.empty() || name.front() == '.' )
            return false;
        for ( const utf8 ch : name )
        {
            if ( EditorAssetFileCommandsInternal::isNameChar( ch ) == false )
                return false;
        }
        return true;
    }

    void EditorAssetFileCommands::splitAssetName( string_view fileName, string& outStem, string& outSuffix )
    {
        const size_t dot = fileName.find( '.' );
        if ( dot == string_view::npos || dot == 0 )
        {
            outStem = string{ fileName };
            outSuffix.clear();
            return;
        }
        outStem   = string{ fileName.substr( 0, dot ) };
        outSuffix = string{ fileName.substr( dot ) };
    }

    string EditorAssetFileCommands::makeUniquePath( string_view folderAbs, string_view stem, string_view suffix )
    {
        // 만들기가 곧 자리 잡기다(`reserveUniquePath`) — "있나 보고 고르기" 는 번호를 다 쓰면 있는 파일을 돌려줘 덮어썼다. 접미사가 빈 글이면 폴더다.
        return FileUtil::reserveUniquePath( FileUtil::joinPath( folderAbs, stem ), suffix, suffix.empty(), EditorAssetFileCommandsInternal::kMaxUniqueSuffix, "_" );
    }

    uint32 EditorAssetFileCommands::replaceReference( string& inoutText, string_view oldID, string_view newID )
    {
        if ( oldID.empty() )
            return 0;
        uint32 replaceCount{ 0 };
        size_t offset{ 0 };
        while ( offset + oldID.size() <= inoutText.size() )
        {
            const string_view window{ inoutText.data() + offset, oldID.size() };
            const bool        bMatched          = StringUtil::startsWith( window, oldID, true );
            const bool        bBefore           = offset == 0 || EditorAssetFileCommandsInternal::isPathChar( inoutText[offset - 1] ) == false;
            const size_t      endIndex          = offset + oldID.size();
            const bool        bAfter            = endIndex == inoutText.size() || EditorAssetFileCommandsInternal::isPathChar( inoutText[endIndex] ) == false;
            const bool        bReplaceReference = bMatched && bBefore && bAfter;
            if ( bReplaceReference )
            {
                inoutText.replace( offset, oldID.size(), newID.data(), newID.size() );
                offset += newID.size();
                ++replaceCount;
                continue;
            }
            ++offset;
        }
        return replaceCount;
    }

    bool EditorAssetFileCommands::createFolder( string_view parentAbs, string& outFolderAbs )
    {
        outFolderAbs = makeUniquePath( parentAbs, "new_folder", "" );
        if ( FileUtil::ensureDirectoryExists( outFolderAbs ) )
            return true;
        SW_LOG_ERROR( "Could not create the folder '%#'", outFolderAbs );
        return false;
    }

    bool EditorAssetFileCommands::createAsset( string_view folderAbs, const EditorNewAssetTemplate& assetTemplate, string& outAssetAbs )
    {
        string text;
        if ( assetTemplate._pfnSourcePath == nullptr )
        {
            text = assetTemplate._pText;
        }
        else
        {
            const EngineDefaultAssets* pAssets = editor::findSubsystem<const EngineDefaultAssets>();
            if ( pAssets == nullptr )
            {
                SW_LOG_ERROR( "Engine default assets are not loaded - the new asset template has no source" );
                return false;
            }
            const DataPath& sourcePath = assetTemplate._pfnSourcePath( *pAssets );
            if ( AssetIO::readTextAsset( AssetID{ sourcePath.get() }, text ) == false )
            {
                SW_LOG_ERROR( "Could not read the new asset template '%#'", sourcePath.c_str() );
                return false;
            }
        }
        outAssetAbs = makeUniquePath( folderAbs, assetTemplate._pStem, assetTemplate._pSuffix );
        if ( assetTemplate._pNameAttribute != nullptr )
        {
            string uniqueStem;
            string uniqueSuffix;
            splitAssetName( FileUtil::getFileNamePart( outAssetAbs ), uniqueStem, uniqueSuffix );
            (void)replaceReference( text, assetTemplate._pNameAttribute, "name=\"" + uniqueStem + "\"" ); // 이름 속성이 없으면 템플릿 이름 그대로다
        }
        if ( FileUtil::writeTextFile( outAssetAbs, text ) )
        {
            SW_LOG_INFO( "Created '%#'", editor::getSubsystem<AssetPaths>().toAssetID( outAssetAbs ) );
            return true;
        }
        SW_LOG_ERROR( "Could not write the new asset '%#'", outAssetAbs );
        return false;
    }

    bool EditorAssetFileCommands::duplicateAsset( string_view sourceAbs, string& outAssetAbs )
    {
        string stem;
        string suffix;
        splitAssetName( FileUtil::getFileNamePart( sourceAbs ), stem, suffix );
        outAssetAbs = makeUniquePath( FileUtil::getDirectoryPart( sourceAbs ), stem, suffix );
        return FileUtil::copyFile( sourceAbs, outAssetAbs );
    }

    bool EditorAssetFileCommands::moveAsset( string_view sourceAbs, string_view destinationAbs, EditorReferenceIndex& inoutIndex, uint32& outFixedFileCount )
    {
        outFixedFileCount             = 0;
        const string source           = FileUtil::normalizeSeparators( sourceAbs );
        const string destination      = FileUtil::normalizeSeparators( destinationAbs );
        const string oldID            = editor::getSubsystem<AssetPaths>().toAssetID( source );
        const string newID            = editor::getSubsystem<AssetPaths>().toAssetID( destination );
        const bool   bInvalidAssetIDs = oldID.empty() || newID.empty() || oldID == newID;
        if ( bInvalidAssetIDs )
            return false;
        if ( isValidAssetName( FileUtil::getFileNamePart( destination ) ) == false )
        {
            SW_LOG_WARNING( "'%#' is not a resource name - use lowercase letters, digits, '_', '-' and '.'", FileUtil::getFileNamePart( destination ) );
            return false;
        }
        if ( FileUtil::exists( destination ) )
        {
            SW_LOG_WARNING( "'%#' already exists - nothing was moved", newID );
            return false;
        }
        if ( inoutIndex.isReady() == false )
        {
            SW_LOG_WARNING( "The reference index is still building - try again in a moment" );
            return false;
        }

        vector<EditorAssetReference> listReference;
        inoutIndex.findReferrers( oldID, listReference );
        if ( EditorAssetFileCommandsInternal::moveFile( source, destination ) == false )
            return false;

        // 짝 .meta 는 GUID 가 이어지게 같이 옮기고, 안의 sourcePath 도 새 id 로 고친다.
        const string sourceMeta = AssetDatabase::metaPathFor( source );
        string       metaText;
        if ( FileUtil::readOptionalTextFile( sourceMeta, metaText ) == OptionalFileRead::Read ) // .meta 가 없는 파일도 있다
        {
            (void)replaceReference( metaText, oldID, newID ); // 경로를 적지 않은 .meta 는 그대로 옮긴다
            if ( FileUtil::writeTextFile( AssetDatabase::metaPathFor( destination ), metaText ) == false || FileUtil::removeFile( sourceMeta ) == false )
                SW_LOG_WARNING( "Moved '%#' but its .meta could not follow", newID );
        }

        const string& resourceRoot = editor::getSubsystem<AssetPaths>().getRootFolderPath();
        inoutIndex.refreshFile( resourceRoot, oldID );
        inoutIndex.refreshFile( resourceRoot, newID );

        // 참조자는 파일마다 한 번 고친다(한 파일이 여러 줄에서 적을 수 있다).
        const string* pPreviousReferrer = nullptr;
        for ( const EditorAssetReference& reference : listReference )
        {
            if ( pPreviousReferrer != nullptr && *pPreviousReferrer == reference._referrerPath )
                continue;
            pPreviousReferrer        = &reference._referrerPath;
            const string referrerID  = reference._referrerPath == oldID ? newID : reference._referrerPath;
            const string referrerAbs = FileUtil::joinPath( resourceRoot, referrerID );
            string       referrerText;
            if ( FileUtil::readTextFile( referrerAbs, referrerText ) == false )
                continue;
            if ( replaceReference( referrerText, oldID, newID ) == 0 )
                continue;
            if ( FileUtil::writeTextFile( referrerAbs, referrerText ) == false )
            {
                SW_LOG_WARNING( "Could not fix the reference in '%#' - it still names '%#'", referrerID, oldID );
                continue;
            }
            ++outFixedFileCount;
            inoutIndex.refreshFile( resourceRoot, referrerID );
        }
        SW_LOG_INFO( "Moved '%#' to '%#' and fixed %# referencing file(s)", oldID, newID, outFixedFileCount );
        return true;
    }

    bool EditorAssetFileCommands::moveToTrash( string_view absolutePath )
    {
        // 휴지통이 없는 플랫폼(Linux)은 지운다.
        if ( PlatformFileUtil::hasRecycleBin() == false )
            return deleteAsset( absolutePath );

        // 에셋과 짝 .meta 를 한 번에 보낸다.
        const string   assetPath = FileUtil::normalizeSeparators( absolutePath );
        const string   metaPath  = AssetDatabase::metaPathFor( assetPath );
        vector<string> listPath{ assetPath };
        if ( FileUtil::exists( metaPath ) )
            listPath.push_back( metaPath );
        if ( FileUtil::moveToRecycleBin( listPath ) )
        {
            SW_LOG_INFO( "Moved '%#' to the recycle bin", editor::getSubsystem<AssetPaths>().toAssetID( assetPath ) );
            return true;
        }
        SW_LOG_ERROR( "Could not move '%#' to the recycle bin - it is kept", assetPath );
        return false;
    }

    bool EditorAssetFileCommands::deleteAsset( string_view absolutePath )
    {
        // `.meta` 는 **에셋이 실제로 지워진 뒤에만** 지운다 — 에셋 삭제가 실패했는데(잠긴 파일) `.meta` 를 지우면 남은 에셋이 다음에 새 GUID 를 받아
        // 그 GUID 로 가리키던 참조(씬의 프리팹 · 머티리얼)가 끊긴다.
        if ( absolutePath.empty() )
            return false;
        const string abs{ absolutePath };
        if ( FileUtil::removeFile( abs ) == false )
        {
            SW_LOG_ERROR( "Could not delete '%#' - it and its .meta are kept", abs );
            return false;
        }
        const string metaPath = AssetDatabase::metaPathFor( abs );
        if ( FileUtil::exists( metaPath ) && FileUtil::removeFile( metaPath ) == false )
            SW_LOG_WARNING( "Deleted '%#' but its .meta could not be removed", abs );
        return true;
    }

    uint32 EditorAssetFileCommands::importFiles( string_view destFolderAbs, const vector<string>& listSourcePath )
    {
        if ( destFolderAbs.empty() )
        {
            SW_LOG_WARNING( "Import cancelled — no destination folder." );
            return 0;
        }

        uint32 copied{ 0 };
        for ( const string& sourcePath : listSourcePath )
        {
            if ( FileUtil::exists( sourcePath ) == false )
            {
                SW_LOG_WARNING( "Import skipped (missing): %#", sourcePath.c_str() );
                continue;
            }

            // 리소스 id 는 소문자 규칙이라 파일 이름을 소문자로 담는다.
            const string fileName  = FileUtil::normalizePath( FileUtil::getFileNamePart( sourcePath ) );
            const string plainPath = FileUtil::joinPath( destFolderAbs, fileName );
            if ( FileUtil::pathsEqualNormalized( sourcePath, plainPath ) )
            {
                SW_LOG_TRACE( "Already in folder: %#", fileName.c_str() );
                continue;
            }

            // 이미 있는 애셋을 덮어쓰지 않는다. 같은 이름의 파일을 드롭해 폴더에 있던 것을 덮으면 아무 말 없이 사라지고,
            // 에디터의 임포트에는 되돌리기가 없으므로 그대로 잃는다. 빈 이름은 원자적으로 잡는다(`reserveUniquePath` - 고유 이름의 입구 하나).
            const string destPath = FileUtil::reserveUniquePath( FileUtil::removeExtension( plainPath ), FileUtil::getExtension( plainPath ), false, 10000, "_" );
            if ( destPath.empty() || FileUtil::copyFile( sourcePath, destPath ) == false )
            {
                SW_LOG_ERROR( "Failed to import: %#", sourcePath.c_str() );
                continue;
            }

            ++copied;
            const string rel = AssetDatabase::toRootAssetID( destPath );
            if ( rel.empty() == false )
                editor::getSubsystem<AssetManager>().getAssetDatabase().ensureMeta( rel, true );
            SW_LOG_TRACE( "Imported: %# -> %#", sourcePath.c_str(), destPath.c_str() );
        }
        return copied;
    }

    string EditorAssetFileCommands::makeFileExplorerCommand( string_view absolutePath )
    {
        if ( absolutePath.empty() )
            return {};

        const string path = FileUtil::normalizeSeparators( absolutePath );
        string       command;
#if defined( SW_PLATFORM_WINDOWS )
        // explorer 는 백슬래시만 받는다. 슬래시를 주면 선택이 안 되고 내 문서를 연다.
        string windowsPath = path;
        for ( utf8& ch : windowsPath )
        {
            if ( ch == '/' )
                ch = '\\';
        }
        command = "explorer.exe /select,\"" + windowsPath + "\"";
#else
        // 리눅스 파일 관리자에는 "선택한 채로 열기" 표준이 없다. 그래서 폴더까지만 연다.
        command = "xdg-open \"" + FileUtil::getDirectoryPart( path ) + "\"";
#endif
        return command;
    }

    bool EditorAssetFileCommands::showInFileExplorer( string_view absolutePath )
    {
        const string command = makeFileExplorerCommand( absolutePath );
        if ( command.empty() )
            return false;

        EditorAssetFileCommandsInternal::ExplorerState& state = getEditorState<EditorAssetFileCommandsInternal::ExplorerState>();
        ++state._launchCount;
        state._lastCommand = command;
        if ( state._bDryRun )
            return true;
        // 기다리지 않는다(Windows 는 셸 API, 리눅스는 분리 실행한 xdg-open). 알 수 있는 실패는 "열지 못했다" 뿐이다. `command` 는 기록 · 단언용 글이고 실행하지 않는다.
        if ( Process::showInFileManager( FileUtil::normalizeSeparators( absolutePath ) ) == false )
        {
            SW_LOG_WARNING( "Failed to open file explorer: %#", command.c_str() );
            return false;
        }
        return true;
    }

    void EditorAssetFileCommands::setFileExplorerDryRun( bool bDryRun )
    {
        getEditorState<EditorAssetFileCommandsInternal::ExplorerState>()._bDryRun = bDryRun;
    }

    uint32 EditorAssetFileCommands::getFileExplorerLaunchCount()
    {
        return getEditorState<EditorAssetFileCommandsInternal::ExplorerState>()._launchCount;
    }

    string EditorAssetFileCommands::getLastFileExplorerCommand()
    {
        return getEditorState<EditorAssetFileCommandsInternal::ExplorerState>()._lastCommand;
    }

    SW_AUTOMATION_WATCH( editorExplorerLaunchCount, "Editor.ExplorerLaunchCount", "Times the editor built a file explorer command (Show in Explorer, dry run included)",
                         &EditorAssetFileCommandsInternal::readExplorerLaunchCount );
    SW_AUTOMATION_WATCH( editorExplorerCommandWellFormed, "Editor.ExplorerCommandWellFormed",
                         "1 when the last file explorer command selects a quoted path using the platform separator (Windows: backslashes only)",
                         &EditorAssetFileCommandsInternal::readExplorerCommandWellFormed );
    SW_DEV_COMMAND( ExplorerDryRun, "explorer.dryRun", "explorer.dryRun <on|off>", "Record Show in Explorer commands without starting the file explorer",
                    &EditorAssetFileCommandsInternal::runExplorerDryRun );
    SW_DEV_COMMAND( NotifyReveal, "notify.reveal", "notify.reveal <absolute path>", "Push a toast with a Show in Explorer button for the path",
                    &EditorAssetFileCommandsInternal::runNotifyReveal );
} // namespace sw::editor
