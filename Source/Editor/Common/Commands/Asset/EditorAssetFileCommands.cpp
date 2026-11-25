#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Asset/EditorAssetIndex.h"
#include "Editor/Common/Commands/Asset/EditorContentWrite.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/IEditorPanelSubsystem.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Asset/Reference/AssetReferenceFields.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Inherit/AssetRedirect.h"

namespace sw::editor
{
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
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "explorer dry run %#", listArgument[0] );
                outReply = string( replyBuilder.view() );
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
            [[nodiscard]] static bool moveFile( const AbsolutePath& sourceAbs, const AbsolutePath& destinationAbs )
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
                string       _referrerID;  ///< 옮긴 뒤의 리소스 id(참조자도 함께 옮기면 새 id)
                AbsolutePath _referrerAbs; ///< 옮긴 뒤의 디스크 경로
                string       _originalText;
                string       _rewrittenText;
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

            /**
             * @brief 참조 파일마다 옮긴 뒤 쓸 글을 만듭니다. 경로 칸 밖에서 옮기는 에셋을 적은 곳(고칠 수 없는 참조)은 @p inoutPlan 의 목록에 남긴다.
             * @return 읽지 못한 참조 파일이 있으면 false 입니다(`_failure` 에 이유).
             */
            [[nodiscard]] static bool prepareRewrites( const AssetReferenceFields& params, const EditorAssetIndex& index, EditorAssetMovePlan& inoutPlan,
                                                       vector<PreparedRewrite>& outListRewrite )
            {
                vector<AssetReferenceRewriteRule> listRule;
                for ( size_t moveIndex = 0; moveIndex < inoutPlan._listOldID.size(); ++moveIndex )
                {
                    const vector<AssetReferenceRewriteRule> listMoveRule = params.makeRewriteRules( inoutPlan._listOldID[moveIndex], inoutPlan._listNewID[moveIndex], false );
                    listRule.insert( listRule.end(), listMoveRule.begin(), listMoveRule.end() );
                }
                // 폴더를 옮기면 옮기는 수 · 참조 파일 수가 함께 는다 — 참조 파일마다 옮기기 목록을 훑지 않게 옛 id 로 찾는다.
                unordered_map<string, size_t> mapMoveIndex;
                mapMoveIndex.reserve( inoutPlan._listOldID.size() );
                for ( size_t moveIndex = 0; moveIndex < inoutPlan._listOldID.size(); ++moveIndex )
                    mapMoveIndex.emplace( inoutPlan._listOldID[moveIndex], moveIndex );
                // 참조자 파일 목록(지금 id) — 파일 하나가 여러 원본을 적어도 한 번 고친다
                vector<string>        listReferrerID;
                unordered_set<string> uniqueReferrerID;
                for ( const string& oldID : inoutPlan._listOldID )
                {
                    vector<EditorAssetReference> listReference;
                    index.findReferrers( oldID, listReference );
                    for ( const EditorAssetReference& reference : listReference )
                    {
                        if ( reference._bByGUID == false && uniqueReferrerID.insert( reference._referrerPath ).second ) // GUID 로 적은 참조는 옮겨도 그대로 맞다 — 고칠 글이 없다
                            listReferrerID.push_back( reference._referrerPath );
                    }
                }
                for ( const string& referrerID : listReferrerID )
                {
                    if ( AssetRedirect::isTable( referrerID ) )
                        continue;                                                                                               // 리다이렉트 표의 옛 경로 줄은 고칠 참조가 아니다
                    const optional<AbsolutePath> referrerFile = editor::getSubsystem<AssetPaths>().findLooseFile( referrerID ); // 해석 하나(붙인 도메인 · 대소문자 규칙)
                    PreparedRewrite              rewrite{};
                    if ( referrerFile.has_value() == false || FileUtil::readTextFile( *referrerFile, rewrite._originalText ) == false )
                    {
                        StringBuilder<constant::buffer::kMax256> failureBuilder;
                        failureBuilder.appendFormat( "could not read the referencing file '%#'", referrerID );
                        inoutPlan._failure = string( failureBuilder.view() );
                        return false;
                    }
                    rewrite._rewrittenText = rewrite._originalText;
                    const auto itMove      = mapMoveIndex.find( referrerID );
                    if ( itMove != mapMoveIndex.end() )
                    {
                        rewrite._referrerID  = inoutPlan._listNewID[itMove->second];
                        rewrite._referrerAbs = inoutPlan._listMove[itMove->second]._destinationAbs; // 옮기기 · 옛 id · 새 id 목록은 같은 차례다(planAssetMoves)
                    }
                    else
                    {
                        rewrite._referrerID  = referrerID;
                        rewrite._referrerAbs = *referrerFile;
                        VectorUtil::addUnique( inoutPlan._listOutsideDomain, getDomainOf( referrerID ) );
                    }
                    if ( params.rewrite( rewrite._rewrittenText, AssetReferenceFields::getSyntaxOf( referrerID ), listRule ) == 0 )
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

            /** @brief 글 @p text 가 옮기는 에셋을 경로나 GUID 로 적었으면 true 입니다. */
            static bool hasMovedReference( const string& text, const EditorAssetMovePlan& plan )
            {
                if ( text.empty() )
                    return false;
                string scratch = text;
                for ( const string& oldID : plan._listOldID )
                {
                    const optional<AssetGUID> guid = AssetRegistry::resolveGUID( oldID );
                    if ( EditorAssetFileCommands::replaceReference( scratch, oldID, oldID ) > 0 || ( guid.has_value() && text.find( AssetRegistry::makeReferenceText( *guid ) ) != string::npos ) )
                        return true;
                }
                return false;
            }

            /** @brief @p path 가 원본 곁의 정체성 서술(`<파일>.meta`)이면 true 입니다. */
            static bool isSidecar( string_view path ) { return StringUtil::endsWith( path, AssetMeta::kExtension, true ); }

            /**
             * @brief 옮기기 목록에 원본 곁 `.meta` 의 옮기기를 더한 목록입니다 — 정체성은 원본과 함께 간다(유니티가 .meta 를 함께 옮기는 것과 같다).
             * @details 폴더 옮기기(`collectFolderMoves`)는 안의 `.meta` 를 이미 들고 있으므로 겹치지 않게 더하지 않는다.
             */
            static vector<EditorAssetMove> appendSidecarMoves( const vector<EditorAssetMove>& listMove )
            {
                vector<EditorAssetMove> listResult = listMove;
                // 옮기기마다 목록을 훑지 않는다(폴더를 옮기면 옮기는 파일 수의 제곱) — 곁 파일 경로는 원본 경로에 접미를 붙인 것이라 구분자만 맞추면 같은 글이다.
                unordered_set<string> uniqueSource;
                for ( const EditorAssetMove& move : listMove )
                    uniqueSource.insert( move._sourceAbs.str() );
                for ( const EditorAssetMove& move : listMove )
                {
                    const AbsolutePath sidecar = AssetMeta::makePath( move._sourceAbs );
                    if ( isSidecar( move._sourceAbs.str() ) || FileUtil::isRegularFile( sidecar ) == false )
                        continue;
                    if ( uniqueSource.count( sidecar.str() ) == 0 )
                        listResult.push_back( EditorAssetMove{ sidecar, AssetMeta::makePath( move._destinationAbs ) } );
                }
                return listResult;
            }

            /**
             * @brief 옮기는 `.meta` 마다 원본 이름(`AssetMeta::renameSource`)을 새 이름으로 쓴 글을 @p inoutListRewrite 끝에 더합니다(GUID · 내용 주소 · 임포트 예외는 그대로). 이름이 같으면 더하지 않는다.
             * @return `.meta` 를 읽지 못하면 false 입니다(`_failure` 에 이유 — 아무것도 옮기지 않는다).
             */
            [[nodiscard]] static bool prepareSidecarRewrites( EditorAssetMovePlan& inoutPlan, vector<PreparedRewrite>& inoutListRewrite )
            {
                for ( size_t moveIndex = 0; moveIndex < inoutPlan._listMove.size(); ++moveIndex )
                {
                    const EditorAssetMove& move = inoutPlan._listMove[moveIndex];
                    if ( isSidecar( move._sourceAbs.str() ) == false )
                        continue;
                    PreparedRewrite     rewrite{};
                    AssetMeta           meta;
                    optional<AssetGUID> guid;
                    if ( FileUtil::readTextFile( move._sourceAbs, rewrite._originalText ) )
                        guid = meta.parseXML( rewrite._originalText, inoutPlan._listOldID[moveIndex] );
                    if ( guid.has_value() == false )
                    {
                        StringBuilder<constant::buffer::kMax512> failureBuilder;
                        failureBuilder.appendFormat( "could not read the asset identity '%#' - nothing was moved", inoutPlan._listOldID[moveIndex] );
                        inoutPlan._failure = string( failureBuilder.view() );
                        return false;
                    }
                    const string_view newSourceAbs = string_view{ move._destinationAbs.str() }.substr( 0, move._destinationAbs.str().size() - AssetMeta::kExtension.size() );
                    meta.renameSource( newSourceAbs );
                    rewrite._rewrittenText = meta.toXML();
                    if ( rewrite._rewrittenText == rewrite._originalText )
                        continue;
                    rewrite._referrerID  = inoutPlan._listNewID[moveIndex];
                    rewrite._referrerAbs = move._destinationAbs;
                    inoutListRewrite.push_back( std::move( rewrite ) );
                }
                return true;
            }

            /** @brief 정체성 규칙(`Config/Dev/AssetIdentity.json`)을 읽습니다. 읽지 못하면 오류를 남기고 false 입니다(규칙 없이 정체성을 쓰지 않는다). */
            [[nodiscard]] static bool loadIdentityRules( AssetIdentityRules& outRules ) { return outRules.load( ProcessRoots::get()._project ); }

            /** @brief 막 생긴 파일(새로 만들기 · 복제 · 가져오기)에 새 GUID 를 줍니다. 리소스 트리 밖이면 정체성이 없다(할 일 없음). */
            [[nodiscard]] static bool assignNewIdentity( const AbsolutePath& absPath )
            {
                const string       assetID = editor::getSubsystem<AssetPaths>().toAssetID( absPath );
                AssetIdentityRules rules;
                if ( assetID.empty() )
                    return true;
                return loadIdentityRules( rules ) && rules.assignNewIdentity( absPath, assetID );
            }

            /** @brief 손으로 적는 글을 임포트 산출물 폴더에 만들려 하면(`AssetIdentityRules::isMisplacedInDerivedFolder`) 이유를 알리고 true 입니다. 규칙을 읽지 못해도 만들지 않는다. */
            [[nodiscard]] static bool refuseDerivedFolder( const AbsolutePath& folderAbs, string_view stem, string_view suffix )
            {
                // 이름을 잡기(`makeUniquePath` — 빈 파일을 만든다) 전에 묻는다. 겹쳐서 번호가 붙어도 폴더와 접미사는 같다.
                StringBuilder<constant::buffer::kMax256> fileName;
                fileName.appendFormat( "%#%#", stem, suffix );
                const string       assetID = editor::getSubsystem<AssetPaths>().toAssetID( folderAbs.join( fileName.view() ) );
                AssetIdentityRules rules;
                if ( assetID.empty() )
                    return false;
                if ( loadIdentityRules( rules ) && rules.isMisplacedInDerivedFolder( assetID ) == false )
                    return false;
                SW_LOG_ERROR_ID( "EditorAssetFileCommands.DerivedFolder", "Not created - '%#' is in an import output folder that is deleted and rebuilt, use another folder (data, prefabs)", assetID );
                return true;
            }

            /** @brief @p absolutePath 가 읽기 전용(`EditorContentWrite::findReadOnlyAtOrUnder`)이면 계획의 실패 이유를 적고 true 입니다 — 옮기기 계획은 알림 대신 `_failure` 로 이유를 돌려준다. */
            [[nodiscard]] static bool failOnReadOnly( const AbsolutePath& absolutePath, EditorAssetMovePlan& inoutPlan )
            {
                const optional<string> readOnlyPath = EditorContentWrite::findReadOnlyAtOrUnder( absolutePath );
                if ( readOnlyPath.has_value() == false )
                    return false;
                StringBuilder<constant::buffer::kMax256> failureBuilder;
                failureBuilder.appendFormat( "'%#' is read-only (not checked out) - lock it first, nothing was moved", *readOnlyPath );
                inoutPlan._failure = string( failureBuilder.view() );
                return true;
            }

            /** @brief 지우기 전 확인 둘이다 — 열린 문서(`refuseOpenDocumentDelete`)와 읽기 전용(`EditorContentWrite::warnIfReadOnly`, 저장 · 옮기기와 같은 확인). 거절하면 이유를 알리고 true 입니다. */
            [[nodiscard]] static bool refuseDelete( const AbsolutePath& absolutePath )
            {
                return refuseOpenDocumentDelete( absolutePath ) || EditorContentWrite::warnIfReadOnly( absolutePath, EditorContentWriteKind::Delete );
            }

            /**
             * @brief @p absolutePath(파일, 또는 폴더면 그 아래)에 열린 문서가 있으면 지우기를 거절하고 이유를 알린 뒤 true 입니다(감사 P0-4).
             * @details 연 문서를 지우면 다음 저장(Ctrl+S)이 파일을 되살린다. 유니티 · Godot 는 GUID · UID 로 연 문서를 끊고, 이 엔진은 경로가 정체라 거절이 그 자리다.
             */
            [[nodiscard]] static bool refuseOpenDocumentDelete( const AbsolutePath& absolutePath )
            {
                const string deletedID = editor::getSubsystem<AssetPaths>().toAssetID( absolutePath );
                if ( deletedID.empty() )
                    return false; // 리소스 트리 밖 파일은 문서로 열 수 없다
                vector<EditorOpenDocument> listOpen;
                EditorAssetFileCommands::collectOpenDocuments( listOpen );
                const optional<string> openDocument = EditorAssetFileCommands::findOpenDocumentAtOrUnder( deletedID, listOpen );
                if ( openDocument.has_value() == false )
                    return false;
                SW_LOG_WARNING_ID( "EditorAssetFileCommands.DeleteRefusedOpenDocument", "Not deleted - '%#' is open in an editor, close it first", *openDocument );
                EditorContext* const pContext = editor::findSubsystem<EditorContext>();
                if ( pContext != nullptr )
                    pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "DeleteRefusedTitle" ),
                                                                              SW_LOCKEYFORMAT( "EdCommon", "OpenDocumentNotDeleted", TextArgumentList().addText( "path", *openDocument ) ),
                                                                              NotificationKind::Warning );
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

    AbsolutePath EditorAssetFileCommands::makeUniquePath( const AbsolutePath& folderAbs, string_view stem, string_view suffix )
    {
        // 만들기가 곧 자리 잡기다(`reserveUniquePath`) — "있나 보고 고르기" 는 번호를 다 쓰면 있는 파일을 돌려줘 덮어썼다. 접미사가 빈 글이면 폴더다.
        return FileUtil::reserveUniquePath( folderAbs.join( stem ), suffix, suffix.empty(), EditorAssetFileCommandsInternal::kMaxUniqueSuffix, "_" );
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

    bool EditorAssetFileCommands::createFolder( const AbsolutePath& parentAbs, AbsolutePath& outFolderAbs )
    {
        outFolderAbs = makeUniquePath( parentAbs, "new_folder", "" );
        if ( FileUtil::ensureDirectoryExists( outFolderAbs ) )
            return true;
        SW_LOG_ERROR_ID( "EditorAssetFileCommands.CouldNotCreateFolder", "Could not create the folder '%#'", outFolderAbs.str() );
        return false;
    }

    bool EditorAssetFileCommands::createAsset( const AbsolutePath& folderAbs, const EditorNewAssetTemplate& assetTemplate, AbsolutePath& outAssetAbs )
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
        if ( EditorAssetFileCommandsInternal::refuseDerivedFolder( folderAbs, assetTemplate._pStem, assetTemplate._pSuffix ) )
            return false;
        outAssetAbs = makeUniquePath( folderAbs, assetTemplate._pStem, assetTemplate._pSuffix );
        if ( assetTemplate._pNameAttribute != nullptr )
        {
            string uniqueStem;
            string uniqueSuffix;
            splitAssetName( FileUtil::getFileNamePart( outAssetAbs.str() ), uniqueStem, uniqueSuffix );
            fixed_string<constant::buffer::kMax256> nameAttribute;
            formatstring( nameAttribute.data(), nameAttribute.capacity(), "name=\"%#\"", uniqueStem );
            (void)replaceReference( text, assetTemplate._pNameAttribute, nameAttribute ); // 이름 속성이 없으면 템플릿 이름 그대로다
        }
        // 틀은 그 자신의 정체성(머리 GUID)을 든 에셋이다 — 새 에셋은 새 GUID 를 받는다.
        if ( FileUtil::writeTextFile( outAssetAbs, text ) && EditorAssetFileCommandsInternal::assignNewIdentity( outAssetAbs ) )
        {
            SW_LOG_INFO_ID( "Asset.Created", "Created '%#'", editor::getSubsystem<AssetPaths>().toAssetID( outAssetAbs ) );
            return true;
        }
        SW_LOG_ERROR_ID( "EditorAssetFileCommands.CouldNotWriteNew", "Could not write the new asset '%#'", outAssetAbs.str() );
        return false;
    }

    bool EditorAssetFileCommands::duplicateAsset( const AbsolutePath& sourceAbs, AbsolutePath& outAssetAbs )
    {
        string stem;
        string suffix;
        splitAssetName( FileUtil::getFileNamePart( sourceAbs.str() ), stem, suffix );
        if ( EditorAssetFileCommandsInternal::refuseDerivedFolder( sourceAbs.getParent(), stem, suffix ) )
            return false;
        outAssetAbs = makeUniquePath( sourceAbs.getParent(), stem, suffix );
        // 복제는 새 에셋이다 — 베낀 머리 GUID 는 새 것으로 바꾸고, 원본이면 곁의 .meta 를 베끼지 않고 새로 쓴다(겹친 GUID 는 참조를 엉뚱한 쪽으로 푼다).
        return FileUtil::copyFile( sourceAbs, outAssetAbs ) && EditorAssetFileCommandsInternal::assignNewIdentity( outAssetAbs );
    }

    bool EditorAssetFileCommands::planAssetMoves( const vector<EditorAssetMove>& listMove, const EditorAssetIndex& index, EditorAssetMovePlan& outPlan )
    {
        outPlan = EditorAssetMovePlan{};
        if ( listMove.empty() )
        {
            outPlan._failure = "nothing to move";
            return false;
        }
        // 플레이 중에는 옮기지 않는다 — 편집 씬은 롤백 스냅샷(바이트 · 조립 기록)에 있어 새 id 로 바꿔 끼울 수 없고, Stop 이 옛 id 를 되돌려 다음 저장이 그것을 쓴다.
        if ( EditorPlaySession::isStopped() == false )
        {
            outPlan._failure = "stop playing first - the edited scene is in the play snapshot";
            return false;
        }
        if ( index.isReady() == false )
        {
            outPlan._failure = "the reference index is still building - try again in a moment";
            return false;
        }
        AssetIdentityRules identityRules;
        if ( EditorAssetFileCommandsInternal::loadIdentityRules( identityRules ) == false )
        {
            StringBuilder<constant::buffer::kMax256> failureBuilder;
            failureBuilder.appendFormat( "could not read %#", AssetIdentityRules::kConfigFile );
            outPlan._failure = string( failureBuilder.view() );
            return false;
        }
        const AssetPaths& assetPaths = editor::getSubsystem<AssetPaths>();
        // 옮기기마다 계획한 id 목록을 훑지 않는다(폴더를 옮기면 옮기는 에셋 수의 제곱). 계획 목록의 순서는 그대로다.
        unordered_set<string>         uniqueOldID;
        unordered_set<string>         uniqueNewID;
        const vector<EditorAssetMove> listExpanded = EditorAssetFileCommandsInternal::appendSidecarMoves( listMove );
        for ( const EditorAssetMove& move : listExpanded )
        {
            EditorAssetMove normalized{ move };
            const string    oldID = assetPaths.toAssetID( normalized._sourceAbs );
            const string    newID = assetPaths.toAssetID( normalized._destinationAbs );
            if ( oldID.empty() || newID.empty() || oldID == newID )
            {
                StringBuilder<constant::buffer::kMax256> failureBuilder;
                failureBuilder.appendFormat( "'%#' cannot move to '%#' (outside the resource tree or the same place)", normalized._sourceAbs.str(), normalized._destinationAbs.str() );
                outPlan._failure = string( failureBuilder.view() );
                return false;
            }
            if ( isValidAssetName( FileUtil::getFileNamePart( normalized._destinationAbs.str() ) ) == false )
            {
                StringBuilder<constant::buffer::kMax256> failureBuilder;
                failureBuilder.appendFormat( "'%#' is not a resource name - use lowercase letters, digits, '_', '-' and '.'", FileUtil::getFileNamePart( normalized._destinationAbs.str() ) );
                outPlan._failure = string( failureBuilder.view() );
                return false;
            }
            if ( identityRules.isMisplacedInDerivedFolder( newID ) && identityRules.isUnderDerivedFolder( oldID ) == false )
            {
                StringBuilder<constant::buffer::kMax512> failureBuilder;
                failureBuilder.appendFormat( "'%#' is in an import output folder that is deleted and rebuilt - nothing was moved", newID );
                outPlan._failure = string( failureBuilder.view() );
                return false;
            }
            if ( FileUtil::exists( normalized._destinationAbs ) || uniqueOldID.insert( oldID ).second == false )
            {
                StringBuilder<constant::buffer::kMax256> failureBuilder;
                failureBuilder.appendFormat( "'%#' already exists or is moved twice - nothing was moved", newID );
                outPlan._failure = string( failureBuilder.view() );
                return false;
            }
            if ( uniqueNewID.insert( newID ).second == false )
            {
                StringBuilder<constant::buffer::kMax256> failureBuilder;
                failureBuilder.appendFormat( "two assets would move to '%#' - nothing was moved", newID );
                outPlan._failure = string( failureBuilder.view() );
                return false;
            }
            outPlan._listMove.push_back( std::move( normalized ) );
            outPlan._listOldID.push_back( oldID );
            outPlan._listNewID.push_back( newID );
        }
        // 고치는 규칙은 커밋 전 검사 · move-asset · 참조 검사와 같은 칸 목록이다 — 글을 찾아 바꾸지 않고 경로 칸의 값만 바꾼다. 목록이 없으면 옮기지 않는다(고치지 못한 참조를 남기지 않게).
        AssetReferenceFields params;
        if ( params.load( ProcessRoots::get()._project ) == false )
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
        // 옮기는 파일과 고칠 참조 파일이 읽기 전용(잠그지 않음)이면 옮기지 않는다 — 저장 · 삭제와 같은 확인이다. 옮기는 참조자의 새 자리는 아직 없고 그 원본을 옮기는 쪽에서 본다.
        for ( const EditorAssetMove& move : outPlan._listMove )
        {
            if ( EditorAssetFileCommandsInternal::failOnReadOnly( move._sourceAbs, outPlan ) )
                return false;
        }
        for ( const EditorAssetFileCommandsInternal::PreparedRewrite& rewrite : listRewrite )
        {
            if ( EditorAssetFileCommandsInternal::failOnReadOnly( rewrite._referrerAbs, outPlan ) )
                return false;
        }
        // 미저장 참조 문서는 옛 id 를 들고 있다 — 고친 디스크 글을 다음 저장이 덮지 않게 먼저 저장을 요구한다(감사 P0-4).
        vector<EditorOpenDocument> listOpen;
        collectOpenDocuments( listOpen );
        const optional<string> dirtyReferrer = findDirtyReferrerDocument( outPlan, listOpen );
        if ( dirtyReferrer.has_value() )
        {
            StringBuilder<constant::buffer::kMax256> failureBuilder;
            failureBuilder.appendFormat( "'%#' has unsaved edits and names a moved asset - save it first, nothing was moved", *dirtyReferrer );
            outPlan._failure = string( failureBuilder.view() );
            return false;
        }
        return true;
    }

    void EditorAssetFileCommands::collectOpenDocuments( vector<EditorOpenDocument>& outList )
    {
        const IEditorPanelSubsystem* const pPanelSubsystem = editor::findSubsystem<IEditorPanelSubsystem>(); // 셸 없는 테스트 호스트에는 패널이 없다
        if ( pPanelSubsystem != nullptr )
            pPanelSubsystem->collectOpenDocuments( outList );
        const Scene* const pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getSourcePath().empty() )
            return;
        const EditorWorkspace* const pWorkspace = editor::findSubsystem<EditorWorkspace>();
        outList.push_back( EditorOpenDocument{ pScene->getSourcePath(), pWorkspace != nullptr && pWorkspace->isSceneDirty(), {} } ); // 씬의 새 참조는 옮긴 뒤 메모리에서 바꿔 끼운다(`remapScene`)
    }

    optional<string> EditorAssetFileCommands::findDirtyReferrerDocument( const EditorAssetMovePlan& plan, const vector<EditorOpenDocument>& listOpen )
    {
        for ( const EditorOpenDocument& document : listOpen )
        {
            const bool bReferrer    = VectorUtil::contains( plan._listReferrerID, document._assetID ) || VectorUtil::contains( plan._listUnfixedReferrer, document._assetID );
            const bool bStaysBehind = document._bFollowsMove == false && VectorUtil::contains( plan._listOldID, document._assetID );
            if ( document._bDirty && ( bReferrer || bStaysBehind || EditorAssetFileCommandsInternal::hasMovedReference( document._unsavedText, plan ) ) )
                return document._assetID;
        }
        return nullopt;
    }

    optional<string> EditorAssetFileCommands::findOpenDocumentAtOrUnder( string_view deletedID, const vector<EditorOpenDocument>& listOpen )
    {
        SW_ASSERT( deletedID.empty() == false );
        for ( const EditorOpenDocument& document : listOpen )
        {
            const string_view documentID = document._assetID;
            // 같은 id 이거나 지우는 폴더 아래(`<폴더 id>/…`)다 — `a/b` 가 `a/bc` 를 덮지 않게 경계의 `/` 를 본다.
            const bool bAtOrUnder = documentID == deletedID || ( documentID.size() > deletedID.size() && StringUtil::startsWith( documentID, deletedID ) && documentID[deletedID.size()] == '/' );
            if ( bAtOrUnder )
                return document._assetID;
        }
        return nullopt;
    }

    bool EditorAssetFileCommands::applyAssetMoves( const vector<EditorAssetMove>& listMove, EditorAssetIndex& inoutIndex, EditorAssetMovePlan& outPlan )
    {
        // 계획을 다시 세운다 — 미리보기와 적용 사이(확인 대화상자)에 파일이 바뀌었을 수 있다. 쓸 글은 모두 미리 만들고 나서 디스크를 바꾼다.
        if ( planAssetMoves( listMove, inoutIndex, outPlan ) == false )
        {
            SW_LOG_WARNING_ID( "EditorAssetFileCommands.NothingMoved", "Nothing was moved - %#", outPlan._failure );
            return false;
        }
        AssetReferenceFields params;
        if ( params.load( ProcessRoots::get()._project ) == false )
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
        // 원본 곁 `.meta` 도 같은 트랜잭션으로 쓴다(참조 글 뒤에 붙인다 — 고친 참조 파일 수에는 세지 않는다).
        const size_t referrerRewriteCount = listRewrite.size();
        if ( EditorAssetFileCommandsInternal::prepareSidecarRewrites( outPlan, listRewrite ) == false )
        {
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
                const EditorAssetFileCommandsInternal::PreparedRewrite& rewrite = listRewrite[index];
                if ( FileUtil::writeTextFile( rewrite._referrerAbs, rewrite._originalText ) == false )
                    listStuck.push_back( rewrite._referrerID );
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
                StringBuilder<constant::buffer::kMax1024> stuckText;
                for ( const string& stuck : listStuck )
                    stuckText.appendFormat( "%#%#", stuckText.size() == 0 ? "" : ", ", stuck );
                SW_LOG_ERROR_ID( "EditorAssetFileCommands.MoveIncomplete", "Move failed (%#) and %# file(s) could not be put back - fix by hand: %#", outPlan._failure, listStuck.size(),
                                 stuckText.view() );
            }
            EditorContext* pContext = editor::findSubsystem<EditorContext>();
            if ( pContext != nullptr )
                pContext->getSubsystem<EditorNotificationManager>().push( "Move failed", outPlan._failure, listStuck.empty() ? NotificationKind::Warning : NotificationKind::Error );
            vector<string> listChanged = outPlan._listOldID;
            listChanged.insert( listChanged.end(), outPlan._listNewID.begin(), outPlan._listNewID.end() );
            inoutIndex.applyChanges( editor::getSubsystem<AssetPaths>().getRootFolderPath(), listChanged );
            return false;
        }

        // 옮긴 파일(옛 id 빠짐 · 새 id 생김)과 글을 고친 참조자만 다시 읽는다 — 다른 파일은 그대로다(색인이 대상의 있음을 물을 때 본다).
        vector<string> listChanged = outPlan._listOldID;
        listChanged.insert( listChanged.end(), outPlan._listNewID.begin(), outPlan._listNewID.end() );
        for ( const EditorAssetFileCommandsInternal::PreparedRewrite& rewrite : listRewrite )
            listChanged.push_back( rewrite._referrerID );
        inoutIndex.applyChanges( editor::getSubsystem<AssetPaths>().getRootFolderPath(), listChanged );
        // 실행 중 GUID 색인도 새 자리를 안다 — 살아 있는 참조(`AssetRef<T>`)를 새 경로로 다시 넣을 때 GUID 를 잃지 않는다(`.meta` 는 색인의 줄이 아니라 false 다).
        for ( size_t index = 0; index < outPlan._listOldID.size(); ++index )
            (void)AssetRegistry::move( outPlan._listOldID[index], outPlan._listNewID[index] );
        outPlan._fixedFileCount           = static_cast<uint32>( referrerRewriteCount );
        EditorWorkspace* const pWorkspace = editor::findSubsystem<EditorWorkspace>();
        if ( pWorkspace != nullptr )
        {
            // 열린 문서 · 씬 · 포커스 · 선택이 새 id 를 따르고, 깨끗한 참조 문서는 고친 글을 다시 읽는다. `.meta`(`referrerRewriteCount` 뒤)는 문서가 아니다.
            EditorAssetMoveEvent event{ outPlan._listOldID, outPlan._listNewID, {} };
            event._listRewrittenID.reserve( referrerRewriteCount );
            for ( size_t index = 0; index < referrerRewriteCount; ++index )
                event._listRewrittenID.push_back( listRewrite[index]._referrerID );
            pWorkspace->notifyAssetsMoved( event );
        }
        if ( outPlan._listMove.size() == 1 )
            SW_LOG_INFO_ID( "Asset.Moved", "Moved '%#' to '%#' and fixed %# referencing file(s)", outPlan._listOldID.front(), outPlan._listNewID.front(), outPlan._fixedFileCount );
        else
            SW_LOG_INFO_ID( "Asset.MovedSet", "Moved %# asset(s) (first '%#' to '%#') and fixed %# referencing file(s)", outPlan._listMove.size(), outPlan._listOldID.front(),
                            outPlan._listNewID.front(), outPlan._fixedFileCount );
        return true;
    }

    expected<vector<EditorAssetMove>> EditorAssetFileCommands::collectFolderMoves( const AbsolutePath& sourceFolderAbs, const AbsolutePath& destinationParentAbs )
    {
        const AbsolutePath source      = sourceFolderAbs;
        const AbsolutePath destination = destinationParentAbs.join( FileUtil::getFileNamePart( source.str() ) );
        if ( FileUtil::isDirectory( source ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "not a folder", source.str() } );
        // 폴더를 자기 안으로 옮기면 끝없이 깊어진다
        if ( FileUtil::isPathAtOrUnderRoot( source.str(), destination.str() ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "a folder cannot move into itself", FileUtil::getFileNamePart( source.str() ) } );
        if ( FileUtil::exists( destination ) )
            return unexpected( Error{ ErrorKind::AlreadyExists, "the destination already exists - nothing was moved", destination.str() } );
        vector<AbsolutePath> listFile;
        if ( FileUtil::collectFiles( source, "", listFile, true ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "could not list the folder", source.str() } );
        vector<EditorAssetMove> listMove;
        for ( const AbsolutePath& fileAbs : listFile )
        {
            listMove.push_back( EditorAssetMove{ fileAbs, destination.join( string_view( fileAbs.str() ).substr( source.str().size() ) ) } );
        }
        if ( listMove.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the folder is empty - nothing to move", FileUtil::getFileNamePart( source.str() ) } );
        return listMove;
    }

    bool EditorAssetFileCommands::moveAsset( const AbsolutePath& sourceAbs, const AbsolutePath& destinationAbs, EditorAssetIndex& inoutIndex, uint32& outFixedFileCount )
    {
        outFixedFileCount = 0;
        EditorAssetMovePlan plan;
        if ( applyAssetMoves( {
                                  EditorAssetMove{ sourceAbs, destinationAbs }
        },
                              inoutIndex, plan ) == false )
            return false; // 이유는 applyAssetMoves 가 알렸다(옮기기 전 거절 · 되돌림 · 되돌리지 못한 목록)
        outFixedFileCount = plan._fixedFileCount;
        return true;
    }

    bool EditorAssetFileCommands::moveToTrash( const AbsolutePath& absolutePath )
    {
        if ( EditorAssetFileCommandsInternal::refuseDelete( absolutePath ) )
            return false;
        // 휴지통을 쓸 수 없는 곳(Linux 에서 홈을 모를 때)은 지운다.
        if ( PlatformFileUtil::hasRecycleBin() == false )
            return deleteAsset( absolutePath );

        // 에셋과 곁의 정체성 서술(.meta)을 한 번에 보낸다 — 휴지통에서 되살리면 둘이 함께 온다.
        const AbsolutePath   assetPath = absolutePath;
        const AbsolutePath   metaPath  = AssetMeta::makePath( assetPath );
        vector<AbsolutePath> listPath{ assetPath };
        if ( FileUtil::isRegularFile( metaPath ) )
            listPath.push_back( metaPath );
        if ( FileUtil::moveToRecycleBin( listPath ) )
        {
            SW_LOG_INFO_ID( "Asset.Recycled", "Moved '%#' to the recycle bin", editor::getSubsystem<AssetPaths>().toAssetID( assetPath ) );
            return true;
        }
        SW_LOG_ERROR_ID( "EditorAssetFileCommands.CouldNotMoveRecycle", "Could not move '%#' to the recycle bin - it is kept", assetPath.str() );
        return false;
    }

    bool EditorAssetFileCommands::deleteAsset( const AbsolutePath& absolutePath )
    {
        if ( absolutePath.empty() || EditorAssetFileCommandsInternal::refuseDelete( absolutePath ) )
            return false;
        const AbsolutePath abs{ absolutePath };
        if ( FileUtil::removeFile( abs ) == false )
        {
            SW_LOG_ERROR_ID( "EditorAssetFileCommands.CouldNotDelete", "Could not delete '%#' - it is kept", abs.str() );
            return false;
        }
        // .meta 는 에셋이 실제로 지워진 뒤에만 지운다 — 남은 에셋에서 정체성을 떼면 다음에 새 GUID 를 받아 가리키던 참조가 끊긴다.
        const AbsolutePath metaPath = AssetMeta::makePath( abs );
        if ( FileUtil::isRegularFile( metaPath ) && FileUtil::removeFile( metaPath ) == false )
            SW_LOG_WARNING_ID( "EditorAssetFileCommands.DeletedButMetaKept", "Deleted '%#' but its .meta could not be removed - delete it too (asset-ids reports it)", abs.str() );
        return true;
    }

    bool EditorAssetFileCommands::replaceAssetContent( const AbsolutePath& targetAbs, const AbsolutePath& newContentAbs )
    {
        const string       assetID = editor::getSubsystem<AssetPaths>().toAssetID( targetAbs );
        AssetIdentityRules rules;
        if ( assetID.empty() )
        {
            SW_LOG_ERROR_ID( "EditorAssetFileCommands.ReplaceOutsideResources", "'%#' is outside the resource tree - only an asset has an identity to keep", string( targetAbs.str() ) );
            return false;
        }
        if ( EditorAssetFileCommandsInternal::loadIdentityRules( rules ) == false || rules.replaceContentKeepingIdentity( targetAbs, assetID, newContentAbs ) == false )
            return false;
        SW_LOG_INFO_ID( "Asset.ContentReplaced", "Replaced the content of '%#' and kept its asset GUID", assetID );
        return true;
    }

    bool EditorAssetFileCommands::readSourceFileArgument( const string& argument, AbsolutePath& outAbsolutePath, string& outReply )
    {
        if ( AbsolutePath::isStrictAbsolute( argument ) )
            return DevCommandRegistry::readDiskPathArgument( argument, outAbsolutePath, outReply );
        const optional<AbsolutePath> diskPath = editor::getSubsystem<AssetPaths>().findLooseFile( argument );
        if ( diskPath.has_value() == false || FileUtil::isRegularFile( *diskPath ) == false )
        {
            StringBuilder<constant::buffer::kMax256> replyBuilder;
            replyBuilder.appendFormat( "no resource '%#'", argument );
            outReply = string( replyBuilder.view() );
            return false;
        }
        outAbsolutePath = *diskPath;
        return true;
    }

    uint32 EditorAssetFileCommands::importFiles( const AbsolutePath& destFolderAbs, const vector<AbsolutePath>& listSourcePath )
    {
        if ( destFolderAbs.empty() )
        {
            SW_LOG_WARNING_ID( "EditorAssetFileCommands.ImportCancelledNoDestination", "Import cancelled — no destination folder." );
            return 0;
        }

        uint32 copied{ 0 };
        for ( const AbsolutePath& sourcePath : listSourcePath )
        {
            if ( FileUtil::exists( sourcePath ) == false )
            {
                SW_LOG_WARNING_ID( "EditorAssetFileCommands.ImportSkippedMissing", "Import skipped (missing): %#", sourcePath.c_str() );
                continue;
            }

            // 리소스 id 는 소문자 규칙이라 파일 이름을 소문자로 담는다.
            const string       fileName  = FileUtil::normalizePath( FileUtil::getFileNamePart( sourcePath.str() ) ).str();
            const AbsolutePath plainPath = destFolderAbs.join( fileName );
            if ( FileUtil::isSameDiskPath( sourcePath.str(), plainPath.str() ) )
            {
                SW_LOG_TRACE_ID( "EditorAssetFileCommands.AlreadyFolder", "Already in folder: %#", fileName.c_str() );
                continue;
            }

            // 이미 있는 애셋을 덮어쓰지 않는다. 같은 이름의 파일을 드롭해 폴더에 있던 것을 덮으면 아무 말 없이 사라지고,
            // 에디터의 임포트에는 되돌리기가 없으므로 그대로 잃는다. 빈 이름은 원자적으로 잡는다(`reserveUniquePath` - 고유 이름의 입구 하나).
            const AbsolutePath destPath = FileUtil::reserveUniquePath( plainPath.removeExtension(), FileUtil::getExtension( fileName ), false, 10000, "_" );
            if ( destPath.empty() || FileUtil::copyFile( sourcePath, destPath ) == false )
            {
                SW_LOG_ERROR_ID( "EditorAssetFileCommands.FailedImport", "Failed to import: %#", sourcePath.c_str() );
                continue;
            }

            ++copied;
            (void)EditorAssetFileCommandsInternal::assignNewIdentity( destPath ); // 가져온 파일은 새 에셋이다(밖에서 든 머리는 이 프로젝트의 정체성이 아니다). 지면 오류가 남고 asset-ids 가 잡는다
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
        StringBuilder<constant::buffer::kMax512> commandBuilder;
        commandBuilder.appendFormat( "explorer.exe /select,\"%#\"", windowsPath );
        command = string( commandBuilder.view() );
#else
        // 리눅스 파일 관리자에는 "선택한 채로 열기" 표준이 없다. 그래서 폴더까지만 연다.
        StringBuilder<constant::buffer::kMax512> commandBuilder;
        commandBuilder.appendFormat( "xdg-open \"%#\"", FileUtil::getDirectoryPart( path ) );
        command = string( commandBuilder.view() );
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
