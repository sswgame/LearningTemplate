#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/Reference/EditorReferenceIndex.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Asset/Reference/AssetReferenceFields.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineDefines.h"

namespace sw::editor
{
    namespace
    {
        struct EditorAssetFileCommandsInternal
        {
            /** @brief 탐색기 명령의 기록입니다. 에디터 범위의 상태 저장소가 소유한다. */
            struct ExplorerState
            {
                bool   _bDryRun{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
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
                pContext->getSubsystem<EditorNotificationManager>().pushRevealable( "Reveal", listArgument[0], NotificationKind::Success, listArgument[0] );
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

            /** @brief 참조 파일 하나에 쓸 글입니다. 적용 전에 모두 만들고, 쓰기가 하나라도 지면 `_originalText` 로 되돌린다. */
            struct PreparedRewrite
            {
                string _referrerID;  ///< 옮긴 뒤의 리소스 id(참조자도 함께 옮기면 새 id)
                string _referrerAbs; ///< 옮긴 뒤의 디스크 경로
                string _originalText;
                string _rewrittenText;
            };

            /** @brief 리소스 id 의 도메인입니다(`game/<팩>` · `common` · `engine` …). 옮기기 미리보기가 바깥 참조를 도메인별로 묶는다. */
            static string getDomainOf( string_view assetID )
            {
                const size_t firstSlash = assetID.find( '/' );
                if ( firstSlash == string_view::npos )
                    return string( assetID );
                const string_view head = assetID.substr( 0, firstSlash );
                if ( head != "game" && head != "dlc" )
                    return string( head );
                const size_t secondSlash = assetID.find( '/', firstSlash + 1 );
                return string( assetID.substr( 0, secondSlash == string_view::npos ? assetID.size() : secondSlash ) );
            }

            static void appendUnique( vector<string>& inoutList, string_view value )
            {
                for ( const string& existing : inoutList )
                {
                    if ( existing == value )
                        return;
                }
                inoutList.push_back( string( value ) );
            }

            /** @brief @p id 가 옮기는 원본이면 그 새 id, 아니면 그대로입니다. */
            static string mapMovedID( const EditorAssetMovePlan& plan, string_view id )
            {
                for ( size_t index = 0; index < plan._listOldID.size(); ++index )
                {
                    if ( plan._listOldID[index] == id )
                        return plan._listNewID[index];
                }
                return string( id );
            }

            /**
             * @brief 참조 파일마다 옮긴 뒤 쓸 글을 만듭니다. 경로 칸 밖에서 옮기는 에셋을 적은 곳(고칠 수 없는 참조)은 @p inoutPlan 의 목록에 남긴다.
             * @return 읽지 못한 참조 파일이 있으면 false 입니다(`_failure` 에 이유).
             */
            [[nodiscard]] static bool prepareRewrites( const AssetReferenceFields& params, const EditorReferenceIndex& index, EditorAssetMovePlan& inoutPlan,
                                                       vector<PreparedRewrite>& outListRewrite )
            {
                vector<AssetReferenceRewriteRule> listRule;
                for ( size_t moveIndex = 0; moveIndex < inoutPlan._listOldID.size(); ++moveIndex )
                {
                    const vector<AssetReferenceRewriteRule> listMoveRule = params.makeRewriteRules( inoutPlan._listOldID[moveIndex], inoutPlan._listNewID[moveIndex], false );
                    listRule.insert( listRule.end(), listMoveRule.begin(), listMoveRule.end() );
                }
                // 참조자 파일 목록(지금 id) — 파일 하나가 여러 원본을 적어도 한 번 고친다
                vector<string> listReferrerID;
                for ( const string& oldID : inoutPlan._listOldID )
                {
                    vector<EditorAssetReference> listReference;
                    index.findReferrers( oldID, listReference );
                    for ( const EditorAssetReference& reference : listReference )
                        appendUnique( listReferrerID, reference._referrerPath );
                }
                for ( const string& referrerID : listReferrerID )
                {
                    const optional<AbsolutePath> referrerFile = editor::getSubsystem<AssetPaths>().findLooseFile( referrerID ); // 해석 하나(붙인 도메인 · 대소문자 규칙)
                    PreparedRewrite              rewrite{};
                    if ( referrerFile.has_value() == false || FileUtil::readTextFile( referrerFile->str(), rewrite._originalText ) == false )
                    {
                        StringBuilder<constant::buffer::kMax256> failureBuilder;
                        failureBuilder.appendFormat( "could not read the referencing file '%#'", referrerID );
                        inoutPlan._failure = string( failureBuilder.view() );
                        return false;
                    }
                    rewrite._referrerID    = mapMovedID( inoutPlan, referrerID );
                    const bool bMovedToo   = rewrite._referrerID != referrerID;
                    rewrite._referrerAbs   = referrerFile->str();
                    rewrite._rewrittenText = rewrite._originalText;
                    if ( bMovedToo )
                    {
                        for ( const EditorAssetMove& move : inoutPlan._listMove )
                        {
                            if ( FileUtil::isSameDiskPath( move._sourceAbs, rewrite._referrerAbs ) )
                                rewrite._referrerAbs = move._destinationAbs;
                        }
                    }
                    else
                    {
                        appendUnique( inoutPlan._listOutsideDomain, getDomainOf( referrerID ) );
                    }
                    if ( params.rewrite( rewrite._rewrittenText, AssetReferenceFields::syntaxOf( referrerID ), listRule ) == 0 )
                    {
                        // 역색인은 글에서 경로 모양을 찾고, 고치기는 경로 칸만 본다 — 칸 밖(주석)이거나 목록에 없는 칸이다. 고치지 못한 참조를 남기고 옮기지 않는다.
                        inoutPlan._listUnfixedReferrer.push_back( referrerID );
                        continue;
                    }
                    inoutPlan._listReferrerID.push_back( referrerID );
                    outListRewrite.push_back( std::move( rewrite ) );
                }
                return true;
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
        SW_LOG_ERROR_ID( "EditorAssetFileCommands.CouldNotCreateFolder", "Could not create the folder '%#'", outFolderAbs );
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
                SW_LOG_ERROR_ID( "EditorAssetFileCommands.EngineDefaultAssetsNot", "Engine default assets are not loaded - the new asset template has no source" );
                return false;
            }
            const DataPath& sourcePath = assetTemplate._pfnSourcePath( *pAssets );
            if ( AssetIO::readTextAsset( AssetID{ sourcePath.get() }, text ) == false )
            {
                SW_LOG_ERROR_ID( "EditorAssetFileCommands.CouldNotReadNew", "Could not read the new asset template '%#'", sourcePath.c_str() );
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
            SW_LOG_INFO_ID( "Asset.Created", "Created '%#'", editor::getSubsystem<AssetPaths>().toAssetID( outAssetAbs ) );
            return true;
        }
        SW_LOG_ERROR_ID( "EditorAssetFileCommands.CouldNotWriteNew", "Could not write the new asset '%#'", outAssetAbs );
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

    bool EditorAssetFileCommands::planAssetMoves( const vector<EditorAssetMove>& listMove, const EditorReferenceIndex& index, EditorAssetMovePlan& outPlan )
    {
        outPlan = EditorAssetMovePlan{};
        if ( listMove.empty() )
        {
            outPlan._failure = "nothing to move";
            return false;
        }
        if ( index.isReady() == false )
        {
            outPlan._failure = "the reference index is still building - try again in a moment";
            return false;
        }
        const AssetPaths& assetPaths = editor::getSubsystem<AssetPaths>();
        for ( const EditorAssetMove& move : listMove )
        {
            EditorAssetMove normalized{ FileUtil::normalizeSeparators( move._sourceAbs ), FileUtil::normalizeSeparators( move._destinationAbs ) };
            const string    oldID = assetPaths.toAssetID( normalized._sourceAbs );
            const string    newID = assetPaths.toAssetID( normalized._destinationAbs );
            if ( oldID.empty() || newID.empty() || oldID == newID )
            {
                StringBuilder<constant::buffer::kMax256> failureBuilder;
                failureBuilder.appendFormat( "'%#' cannot move to '%#' (outside the resource tree or the same place)", normalized._sourceAbs, normalized._destinationAbs );
                outPlan._failure = string( failureBuilder.view() );
                return false;
            }
            if ( isValidAssetName( FileUtil::getFileNamePart( normalized._destinationAbs ) ) == false )
            {
                StringBuilder<constant::buffer::kMax256> failureBuilder;
                failureBuilder.appendFormat( "'%#' is not a resource name - use lowercase letters, digits, '_', '-' and '.'", FileUtil::getFileNamePart( normalized._destinationAbs ) );
                outPlan._failure = string( failureBuilder.view() );
                return false;
            }
            if ( FileUtil::exists( normalized._destinationAbs ) || EditorAssetFileCommandsInternal::mapMovedID( outPlan, oldID ) != oldID )
            {
                StringBuilder<constant::buffer::kMax256> failureBuilder;
                failureBuilder.appendFormat( "'%#' already exists or is moved twice - nothing was moved", newID );
                outPlan._failure = string( failureBuilder.view() );
                return false;
            }
            for ( const string& plannedNewID : outPlan._listNewID )
            {
                if ( plannedNewID == newID )
                {
                    StringBuilder<constant::buffer::kMax256> failureBuilder;
                    failureBuilder.appendFormat( "two assets would move to '%#' - nothing was moved", newID );
                    outPlan._failure = string( failureBuilder.view() );
                    return false;
                }
            }
            outPlan._listMove.push_back( std::move( normalized ) );
            outPlan._listOldID.push_back( oldID );
            outPlan._listNewID.push_back( newID );
        }
        // 고치는 규칙은 커밋 전 검사 · move-asset · 참조 검사와 같은 칸 목록이다 — 글을 찾아 바꾸지 않고 경로 칸의 값만 바꾼다. 목록이 없으면 옮기지 않는다(고치지 못한 참조를 남기지 않게).
        AssetReferenceFields params;
        if ( params.load( ProcessRoots::get()._project.str() ) == false )
        {
            StringBuilder<constant::buffer::kMax256> failureBuilder;
            failureBuilder.appendFormat( "could not read %#", AssetReferenceFields::kConfigFile );
            outPlan._failure = string( failureBuilder.view() );
            return false;
        }
        vector<EditorAssetFileCommandsInternal::PreparedRewrite> listRewrite;
        if ( EditorAssetFileCommandsInternal::prepareRewrites( params, index, outPlan, listRewrite ) == false )
            return false;
        if ( outPlan._listUnfixedReferrer.empty() == false )
        {
            StringBuilder<constant::buffer::kMax256> failureBuilder;
            failureBuilder.appendFormat( "%# file(s) name a moved asset outside a path field of %# (first: '%#') - fix them or declare the field as an AssetPath PROPERTY, nothing was moved",
                                         outPlan._listUnfixedReferrer.size(), AssetReferenceFields::kGeneratedFile, outPlan._listUnfixedReferrer.front() );
            outPlan._failure = string( failureBuilder.view() );
            return false;
        }
        return true;
    }

    bool EditorAssetFileCommands::applyAssetMoves( const vector<EditorAssetMove>& listMove, EditorReferenceIndex& inoutIndex, EditorAssetMovePlan& outPlan )
    {
        // 계획을 다시 세운다 — 미리보기와 적용 사이(확인 대화상자)에 파일이 바뀌었을 수 있다. 쓸 글은 모두 미리 만들고 나서 디스크를 바꾼다.
        if ( planAssetMoves( listMove, inoutIndex, outPlan ) == false )
        {
            SW_LOG_WARNING_ID( "EditorAssetFileCommands.NothingMoved", "Nothing was moved - %#", outPlan._failure );
            return false;
        }
        AssetReferenceFields params;
        if ( params.load( ProcessRoots::get()._project.str() ) == false )
            return false;
        EditorAssetMovePlan                                      rewritePlan = outPlan;
        vector<EditorAssetFileCommandsInternal::PreparedRewrite> listRewrite;
        rewritePlan._listReferrerID.clear();
        rewritePlan._listOutsideDomain.clear();
        if ( EditorAssetFileCommandsInternal::prepareRewrites( params, inoutIndex, rewritePlan, listRewrite ) == false || rewritePlan._listUnfixedReferrer.empty() == false )
        {
            outPlan._failure = rewritePlan._failure.empty() ? string( "a referencing file changed while moving" ) : rewritePlan._failure;
            SW_LOG_WARNING_ID( "EditorAssetFileCommands.NothingMoved", "Nothing was moved - %#", outPlan._failure );
            return false;
        }

        // ① 파일을 옮긴다. 하나라도 지면 옮긴 것을 거꾸로 되돌린다.
        size_t movedCount = 0;
        for ( ; movedCount < outPlan._listMove.size(); ++movedCount )
        {
            const EditorAssetMove& move = outPlan._listMove[movedCount];
            if ( FileUtil::ensureParentDirectoryExists( move._destinationAbs ) == false || EditorAssetFileCommandsInternal::moveFile( move._sourceAbs, move._destinationAbs ) == false )
                break;
        }
        bool bFailed = movedCount < outPlan._listMove.size();
        if ( bFailed )
        {
            StringBuilder<constant::buffer::kMax256> failureBuilder2;
            failureBuilder2.appendFormat( "could not move '%#' (locked or read-only?)", outPlan._listOldID[movedCount] );
            outPlan._failure = string( failureBuilder2.view() );
        }

        // ② 참조 파일을 쓴다. 하나라도 지면 쓴 것을 원래 글로 되돌린다.
        size_t writtenCount = 0;
        if ( bFailed == false )
        {
            for ( ; writtenCount < listRewrite.size(); ++writtenCount )
            {
                const EditorAssetFileCommandsInternal::PreparedRewrite& rewrite = listRewrite[writtenCount];
                if ( FileUtil::writeTextFile( rewrite._referrerAbs, rewrite._rewrittenText ) == false )
                {
                    bFailed = true;
                    StringBuilder<constant::buffer::kMax256> failureBuilder;
                    failureBuilder.appendFormat( "could not write the referencing file '%#' (locked or read-only?)", rewrite._referrerID );
                    outPlan._failure = string( failureBuilder.view() );
                    break;
                }
            }
        }

        if ( bFailed )
        {
            // 되돌리기: 글 먼저(옮긴 참조자는 새 자리에 있다), 그다음 파일을 거꾸로. 되돌리지 못한 것은 이름을 남긴다 — 반쯤 고친 상태를 말없이 두지 않는다.
            vector<string> listStuck;
            for ( size_t index = 0; index < writtenCount; ++index )
            {
                if ( FileUtil::writeTextFile( listRewrite[index]._referrerAbs, listRewrite[index]._originalText ) == false )
                    listStuck.push_back( listRewrite[index]._referrerID );
            }
            for ( size_t index = movedCount; index > 0; --index )
            {
                const EditorAssetMove& move = outPlan._listMove[index - 1];
                if ( EditorAssetFileCommandsInternal::moveFile( move._destinationAbs, move._sourceAbs ) == false )
                    listStuck.push_back( outPlan._listNewID[index - 1] );
            }
            if ( listStuck.empty() )
            {
                SW_LOG_WARNING_ID( "EditorAssetFileCommands.MoveRolledBack", "Move rolled back - %#. Nothing changed on disk", outPlan._failure );
            }
            else
            {
                string stuckText;
                for ( const string& stuck : listStuck )
                    stuckText += ( stuckText.empty() ? "" : ", " ) + stuck;
                SW_LOG_ERROR_ID( "EditorAssetFileCommands.MoveIncomplete", "Move failed (%#) and %# file(s) could not be put back - fix by hand: %#", outPlan._failure, listStuck.size(),
                                 stuckText );
            }
            EditorContext* pContext = editor::findSubsystem<EditorContext>();
            if ( pContext != nullptr )
                pContext->getSubsystem<EditorNotificationManager>().push( "Move failed", outPlan._failure, listStuck.empty() ? NotificationKind::Warning : NotificationKind::Error );
            const string& resourceRoot = editor::getSubsystem<AssetPaths>().getRootFolderPath();
            for ( size_t index = 0; index < outPlan._listOldID.size(); ++index )
            {
                inoutIndex.refreshFile( resourceRoot, outPlan._listOldID[index] );
                inoutIndex.refreshFile( resourceRoot, outPlan._listNewID[index] );
            }
            return false;
        }

        const string& resourceRoot = editor::getSubsystem<AssetPaths>().getRootFolderPath();
        for ( size_t index = 0; index < outPlan._listOldID.size(); ++index )
        {
            inoutIndex.refreshFile( resourceRoot, outPlan._listOldID[index] );
            inoutIndex.refreshFile( resourceRoot, outPlan._listNewID[index] );
        }
        for ( const EditorAssetFileCommandsInternal::PreparedRewrite& rewrite : listRewrite )
            inoutIndex.refreshFile( resourceRoot, rewrite._referrerID );
        outPlan._fixedFileCount = static_cast<uint32>( listRewrite.size() );
        if ( outPlan._listMove.size() == 1 )
            SW_LOG_INFO_ID( "Asset.Moved", "Moved '%#' to '%#' and fixed %# referencing file(s)", outPlan._listOldID.front(), outPlan._listNewID.front(), outPlan._fixedFileCount );
        else
            SW_LOG_INFO_ID( "Asset.MovedSet", "Moved %# asset(s) (first '%#' to '%#') and fixed %# referencing file(s)", outPlan._listMove.size(), outPlan._listOldID.front(),
                            outPlan._listNewID.front(), outPlan._fixedFileCount );
        return true;
    }

    expected<vector<EditorAssetMove>> EditorAssetFileCommands::collectFolderMoves( string_view sourceFolderAbs, string_view destinationParentAbs )
    {
        const string source      = FileUtil::normalizeSeparators( sourceFolderAbs );
        const string destination = FileUtil::joinPath( destinationParentAbs, FileUtil::getFileNamePart( source ) );
        if ( FileUtil::isDirectory( source ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "not a folder", source } );
        // 폴더를 자기 안으로 옮기면 끝없이 깊어진다
        if ( FileUtil::isPathAtOrUnderRoot( source, destination ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "a folder cannot move into itself", FileUtil::getFileNamePart( source ) } );
        if ( FileUtil::exists( destination ) )
            return unexpected( Error{ ErrorKind::AlreadyExists, "the destination already exists - nothing was moved", destination } );
        vector<string> listFile;
        if ( FileUtil::collectFiles( source, "", listFile, true ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "could not list the folder", source } );
        vector<EditorAssetMove> listMove;
        for ( const string& fileAbs : listFile )
        {
            const string normalized = FileUtil::normalizeSeparators( fileAbs );
            listMove.push_back( EditorAssetMove{ normalized, destination + normalized.substr( source.size() ) } );
        }
        if ( listMove.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the folder is empty - nothing to move", FileUtil::getFileNamePart( source ) } );
        return listMove;
    }

    bool EditorAssetFileCommands::moveAsset( string_view sourceAbs, string_view destinationAbs, EditorReferenceIndex& inoutIndex, uint32& outFixedFileCount )
    {
        outFixedFileCount = 0;
        EditorAssetMovePlan plan;
        if ( applyAssetMoves( {
                                  EditorAssetMove{ string( sourceAbs ), string( destinationAbs ) }
        },
                              inoutIndex, plan ) == false )
            return false; // 이유는 applyAssetMoves 가 알렸다(옮기기 전 거절 · 되돌림 · 되돌리지 못한 목록)
        outFixedFileCount = plan._fixedFileCount;
        return true;
    }

    bool EditorAssetFileCommands::moveToTrash( string_view absolutePath )
    {
        // 휴지통을 쓸 수 없는 곳(Linux 에서 홈을 모를 때)은 지운다.
        if ( PlatformFileUtil::hasRecycleBin() == false )
            return deleteAsset( absolutePath );

        const string assetPath = FileUtil::normalizeSeparators( absolutePath );
        if ( FileUtil::moveToRecycleBin( vector<string>{ assetPath } ) )
        {
            SW_LOG_INFO_ID( "Asset.Recycled", "Moved '%#' to the recycle bin", editor::getSubsystem<AssetPaths>().toAssetID( assetPath ) );
            return true;
        }
        SW_LOG_ERROR_ID( "EditorAssetFileCommands.CouldNotMoveRecycle", "Could not move '%#' to the recycle bin - it is kept", assetPath );
        return false;
    }

    bool EditorAssetFileCommands::deleteAsset( string_view absolutePath )
    {
        if ( absolutePath.empty() )
            return false;
        const string abs{ absolutePath };
        if ( FileUtil::removeFile( abs ) == false )
        {
            SW_LOG_ERROR_ID( "EditorAssetFileCommands.CouldNotDelete", "Could not delete '%#' - it is kept", abs );
            return false;
        }
        return true;
    }

    bool EditorAssetFileCommands::readSourceFileArgument( const string& argument, string& outAbsolutePath, string& outReply )
    {
        if ( AbsolutePath::isStrictAbsolute( argument ) )
            return DevCommandRegistry::readDiskPathArgument( argument, outAbsolutePath, outReply );
        const optional<AbsolutePath> diskPath = editor::getSubsystem<AssetPaths>().findLooseFile( argument );
        if ( diskPath.has_value() == false || FileUtil::isRegularFile( diskPath->str() ) == false )
        {
            outReply = "no resource '" + argument + "'";
            return false;
        }
        outAbsolutePath = diskPath->str();
        return true;
    }

    uint32 EditorAssetFileCommands::importFiles( string_view destFolderAbs, const vector<string>& listSourcePath )
    {
        if ( destFolderAbs.empty() )
        {
            SW_LOG_WARNING_ID( "EditorAssetFileCommands.ImportCancelledNoDestination", "Import cancelled — no destination folder." );
            return 0;
        }

        uint32 copied{ 0 };
        for ( const string& sourcePath : listSourcePath )
        {
            if ( FileUtil::exists( sourcePath ) == false )
            {
                SW_LOG_WARNING_ID( "EditorAssetFileCommands.ImportSkippedMissing", "Import skipped (missing): %#", sourcePath.c_str() );
                continue;
            }

            // 리소스 id 는 소문자 규칙이라 파일 이름을 소문자로 담는다.
            const string fileName  = FileUtil::normalizePath( FileUtil::getFileNamePart( sourcePath ) );
            const string plainPath = FileUtil::joinPath( destFolderAbs, fileName );
            if ( FileUtil::isSameDiskPath( sourcePath, plainPath ) )
            {
                SW_LOG_TRACE_ID( "EditorAssetFileCommands.AlreadyFolder", "Already in folder: %#", fileName.c_str() );
                continue;
            }

            // 이미 있는 애셋을 덮어쓰지 않는다. 같은 이름의 파일을 드롭해 폴더에 있던 것을 덮으면 아무 말 없이 사라지고,
            // 에디터의 임포트에는 되돌리기가 없으므로 그대로 잃는다. 빈 이름은 원자적으로 잡는다(`reserveUniquePath` - 고유 이름의 입구 하나).
            const string destPath = FileUtil::reserveUniquePath( FileUtil::removeExtension( plainPath ), FileUtil::getExtension( plainPath ), false, 10000, "_" );
            if ( destPath.empty() || FileUtil::copyFile( sourcePath, destPath ) == false )
            {
                SW_LOG_ERROR_ID( "EditorAssetFileCommands.FailedImport", "Failed to import: %#", sourcePath.c_str() );
                continue;
            }

            ++copied;
            SW_LOG_TRACE_ID( "EditorAssetFileCommands.Imported", "Imported: %# -> %#", sourcePath.c_str(), destPath.c_str() );
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
            SW_LOG_WARNING_ID( "EditorAssetFileCommands.FailedOpenFileExplorer", "Failed to open file explorer: %#", command.c_str() );
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
