#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/Asset/EditorDocumentAutosave.h"
#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorPanelID.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"

#include "ModuleAPI/Service/IModuleStateStore.h"

namespace sw::editor
{
    namespace
    {
        struct EditorAutosaveCommandsInternal
        {
            static constexpr const utf8* kFolderName  = "Autosave";
            static constexpr const utf8* kSceneSuffix = ".scene.xml";
            static constexpr const utf8* kMetaSuffix  = ".meta";
            /** @brief 맡김 칸(`IModuleStateStore`)에 둘 세션 id 의 키와 형식 판입니다. */
            static constexpr string_view kSessionStoreKey = "editor.autosaveSession";
            static constexpr FormatDef   kSessionFormat{ "EDAS" };

            /** @brief 세션 하나의 자동 저장 상태입니다. 에디터 상태 저장소가 소유한다(UI 스레드만). */
            struct State
            {
                vector<EditorAutosaveEntry>         _listRecoverable{};
                vector<EditorDocumentAutosaveEntry> _listRecoverableDocument{}; ///< 문서 패널의 복구 후보(감사 E-1)
                string                              _pendingDocumentPath{};     ///< 복원을 청한 문서(그 문서 패널이 읽으면 사본 글을 적용한다)
                string                              _pendingDocumentText{};     ///< 그 사본 글
                uint32                              _restoredDocumentCount{ 0 };
                Stopwatch                           _sinceSnapshot{};
                AbsolutePath                        _sessionFolder{};         ///< 이 세션의 폴더(`Autosave/<pid>-<시각>`). 비면 세션을 열지 못했다 — 사본을 쓰지 않는다
                string                              _requestedSnapshotID{};   ///< UI 가 복원을 요청한 사본의 id(`requestRestore`). 게임 단계(`tick`)가 연다
                string                              _pendingSnapshotID{};     ///< 복원으로 연 사본의 id(`normalizeAssetID`). 활성 씬이 이 id 로 열리면 소스를 돌린다
                AbsolutePath                        _pendingSlotFolder{};     ///< 그 사본의 씬 칸 폴더. 다 열리면 지운다(씬이 dirty 로 메모리에 있고 다음 사본은 이 세션 폴더에 간다)
                string                              _pendingSourceID{};       ///< 그 원래 id. 비면 새 씬(원본 없음)이다
                uint64                              _pendingGeneration{ 0 };  ///< 복원을 청한 때의 씬 세대
                uint32                              _snapshotEditSerial{ 0 }; ///< 지난 사본 때의 편집 번호(`EditorWorkspace::getSceneEditSerial`)
                uint32                              _writtenCount{ 0 };
                uint32                              _restoredCount{ 0 };
                bool                                _bSessionBegun{ false };
                bool                                _bMounted{ false };
            };

            static State& getState() { return getEditorState<State>(); }

            /** @brief `<에디터 상태 폴더>/Autosave` 의 디스크 경로입니다. */
            static AbsolutePath getFolder() { return EditorPaths::resolveEditorStateFile( kFolderName ); }

            /**
             * @brief 세션을 정합니다 — 같은 프로세스의 앞선 인스턴스(모듈 리로드 · 백엔드 교체)가 맡긴 id 가 있으면 그것을 잇고, 없으면 지금 시각으로 새로 만든다.
             * @details 맡김 칸은 `take` 라 꺼내는 즉시 다시 맡긴다 — 에디터 상태 저장소는 `endSession` 앞에 이미 내려가 그때는 id 를 알 수 없다. 편집기 종료면 맡긴 채로
             *          프로세스와 함께 사라진다. 맡김 칸이 없는 호스트(테스트 · 도구)는 리로드마다 새 세션이 되고 앞 폴더는 넘겨받아 복구 후보가 된다(지우지 않는 쪽으로 틀린다).
             */
            static EditorAutosaveSession takeOrMakeSession( bool& outResumed )
            {
                EditorAutosaveSession session{ Process::getCurrentProcessID(), FileUtil::getCurrentFileWriteTime() };
                outResumed                      = false;
                IModuleStateStore* const pStore = editor::findSubsystem<IModuleStateStore>();
                if ( pStore == nullptr )
                    return session;
                vector<uint8> bytes;
                if ( pStore->take( kSessionStoreKey, bytes ) )
                {
                    Archive archive( bytes.data(), bytes.size() );
                    int32   processID{ 0 };
                    int64   startTicks{ 0 };
                    // 다른 프로세스의 id 는 올 수 없지만(맡김 칸은 호스트 프로세스 안) 판 · pid 가 다르면 잇지 않는다
                    if ( kSessionFormat.check( archive ).has_value() && ( archive >> processID >> startTicks ).isOk() && processID == session._processID )
                    {
                        session._startTicks = startTicks;
                        outResumed          = true;
                    }
                }
                Archive archive;
                kSessionFormat.write( archive );
                archive << session._processID << session._startTicks;
                pStore->store( kSessionStoreKey, vector<uint8>( archive.getData(), archive.getData() + archive.getSize() ) );
                return session;
            }

            /** @brief 폴더 안 사본의 세대 번호를 작은 것부터 모읍니다(`<세대>.scene.xml`). */
            static void collectGenerations( const AbsolutePath& slotFolder, vector<uint32>& outListGeneration )
            {
                outListGeneration.clear();
                vector<AbsolutePath> listFile;
                if ( FileUtil::collectFiles( slotFolder, "", listFile, false ) == false )
                    return; // 폴더가 아직 없다 — 사본이 없다
                for ( const AbsolutePath& file : listFile )
                {
                    const string name = FileUtil::getFileNamePart( file.str() );
                    if ( StringUtil::endsWith( name, kSceneSuffix ) == false )
                        continue;
                    int32 generation = 0;
                    if ( StringUtil::parseInt( string_view( name ).substr( 0, name.size() - string_view( kSceneSuffix ).size() ), generation ) && generation > 0 )
                        outListGeneration.push_back( static_cast<uint32>( generation ) );
                }
                VectorUtil::sort( outListGeneration );
            }

            static AbsolutePath makeSnapshotPath( const AbsolutePath& slotFolder, uint32 generation ) { return makeGenerationPath( slotFolder, generation, kSceneSuffix ); }
            static AbsolutePath makeMetaPath( const AbsolutePath& slotFolder, uint32 generation ) { return makeGenerationPath( slotFolder, generation, kMetaSuffix ); }
            static AbsolutePath makeGenerationPath( const AbsolutePath& slotFolder, uint32 generation, string_view suffix )
            {
                fixed_string<constant::buffer::kMax64> fileName;
                formatstring( fileName.data(), fileName.capacity(), "%#%#", generation, suffix );
                return slotFolder.join( fileName.view() );
            }

            /**
             * @brief 새 씬(원본 없음)의 사본 칸 이름 `untitled-<씬 세대>` 입니다 — 언리얼 Untitled 패키지 · 유니티 씬 백업처럼 저장 전 씬도 사본을 둔다.
             * @details 다른 칸(`makeSlotName`)은 `guid_` 로 시작하거나 `.scene.xml` 로 끝나 겹치지 않는다. 세대마다 칸이 달라 새 씬을 다시 만들면 앞 칸과 섞이지 않는다.
             */
            static string makeUntitledSlotName( uint64 sceneGeneration )
            {
                StringBuilder<constant::buffer::kMax64> nameBuilder;
                nameBuilder.appendFormat( "untitled-%#", sceneGeneration );
                return string( nameBuilder.view() );
            }

            /** @brief 세션 폴더 @p listSessionFolder 를 훑어 씬 칸마다 가장 새 사본 하나를 복구 후보로 만듭니다. 다른 프로젝트 · 바뀐 원본은 거절 이유를 단다. */
            static void scanRecoverable( const vector<AbsolutePath>& listSessionFolder, vector<EditorAutosaveEntry>& outListEntry )
            {
                outListEntry.clear();
                vector<AbsolutePath> listSlot;
                for ( const AbsolutePath& sessionFolder : listSessionFolder )
                    (void)FileUtil::collectFolders( sessionFolder, listSlot, false ); // 붙여 모은다. 없는 폴더(그새 지운 세션)는 사본이 없다
                const AbsolutePath projectRoot = ProcessRoots::get()._project;
                for ( const AbsolutePath& slotFolder : listSlot )
                {
                    vector<uint32> listGeneration;
                    collectGenerations( slotFolder, listGeneration );
                    if ( listGeneration.empty() )
                        continue;
                    const uint32 newest = listGeneration.back();
                    string       metaText;
                    if ( FileUtil::readTextFile( makeMetaPath( slotFolder, newest ), metaText ) == false )
                        continue; // 메타 없는 사본은 어디 것인지 모른다 — 복구하지 않는다
                    EditorAutosaveEntry entry{};
                    entry._snapshotAbs = makeSnapshotPath( slotFolder, newest );
                    StringBuilder<constant::buffer::kMax256> snapshotIDBuilder;
                    const string                             sessionName = FileUtil::getFileNamePart( FileUtil::getDirectoryPart( slotFolder.str() ) );
                    snapshotIDBuilder.appendFormat( "%#/%#/%#/%#%#", EditorAutosaveCommands::kDomain, sessionName, FileUtil::getFileNamePart( slotFolder.str() ), newest, kSceneSuffix );
                    entry._snapshotID                           = string( snapshotIDBuilder.view() );
                    entry._sourceReference                      = EditorAutosaveCommands::findMetaValue( metaText, "source" );
                    const expected<AssetReferenceTarget> source = AssetRegistry::readReferenceText( entry._sourceReference );
                    entry._sourceID                             = source.has_value() ? source->_path : entry._sourceReference;
                    entry._projectRoot                          = EditorAutosaveCommands::findMetaValue( metaText, "project" );
                    entry._generationCount                      = static_cast<uint32>( listGeneration.size() );
                    (void)StringUtil::parseInt64( EditorAutosaveCommands::findMetaValue( metaText, "saved" ), entry._savedTicks );
                    (void)StringUtil::parseUint64( EditorAutosaveCommands::findMetaValue( metaText, "sourceHash" ), entry._sourceContentHash ); // 없으면(옛 메타) 0 — 아래에서 바뀐 원본으로 거절한다
                    // 다른 프로젝트 · 바뀐 원본에는 적용하지 않는다 — 사본이 오래된 원본 위에 덮여 남의 일을 지우지 않게.
                    // 바뀜은 내용 해시로 본다 — 쓰기 시각은 체크아웃 · 복사가 내용과 따로 바꾸고, 같은 틱의 다시 쓰기는 시각이 같다.
                    if ( FileUtil::isSameDiskPath( entry._projectRoot, projectRoot.str() ) == false )
                        entry._refusal = "it was written for another project";
                    else if ( source.has_value() == false )
                        entry._refusal = "the original scene is gone";
                    else if ( entry._sourceID.empty() == false ) // 새 씬(원본 없음)은 견줄 원본이 없다 — 프로젝트만 본다
                    {
                        const uint64 currentHash = EditorAutosaveCommands::computeSourceHash( entry._sourceID );
                        if ( currentHash == 0 )
                            entry._refusal = "the original scene is gone";
                        else if ( currentHash != entry._sourceContentHash )
                            entry._refusal = "the original scene changed after this copy";
                    }
                    outListEntry.push_back( std::move( entry ) );
                }
            }

            /**
             * @brief 세션을 엽니다 — 세션 id 를 잇거나 만들고, 자기 폴더 · 잠금을 쓰고, pid 가 죽은 세션을 넘겨받아 복구 후보로 모으고, 사본 루트를 도메인으로 붙인다.
             * @details 죽은 세션만 비정상 종료다. 살아 있는 다른 에디터의 폴더는 보지 않고, 리로드로 이은 자기 폴더는 복구가 아니다.
             */
            static void beginSession()
            {
                State& state              = getState();
                state._bSessionBegun      = true;
                const AbsolutePath folder = getFolder();
                if ( folder.empty() || FileUtil::ensureDirectoryExists( folder ) == false )
                {
                    SW_LOG_ERROR_ID( "EditorAutosaveCommands.NoFolder", "Autosave: the editor state folder is unknown or cannot be created - autosave is off for this session" );
                    return;
                }
                bool                        bResumed = false;
                const EditorAutosaveSession session  = takeOrMakeSession( bResumed );
                vector<AbsolutePath>        listRecoveredFolder;
                if ( session.open( folder, listRecoveredFolder ) == false )
                {
                    SW_LOG_ERROR_ID( "EditorAutosaveCommands.SessionNotOpened", "Autosave: could not write the session folder or lock under '%#' - autosave is off for this session", folder.str() );
                    return;
                }
                state._sessionFolder = session.makeFolder( folder );
                if ( bResumed )
                    SW_LOG_INFO_ID( "EditorAutosaveCommands.SessionResumed", "Autosave: resumed session '%#' after a module reload", FileUtil::getFileNamePart( state._sessionFolder.str() ) );
                if ( listRecoveredFolder.empty() == false )
                {
                    scanRecoverable( listRecoveredFolder, state._listRecoverable );
                    scanRecoverableDocuments( listRecoveredFolder, state._listRecoverableDocument );
                    // 리로드로 이었으면 같은 후보를 앞선 인스턴스가 이미 알렸다 — 창을 다시 열지 않는다
                    if ( ( state._listRecoverable.empty() == false || state._listRecoverableDocument.empty() == false ) && bResumed == false )
                    {
                        SW_LOG_WARNING_ID( "EditorAutosaveCommands.RecoveryOffered",
                                           "Autosave: the last editor session did not close - %# scene copy(ies) and %# document copy(ies) can be recovered (Window > Recovered Work)",
                                           state._listRecoverable.size(), state._listRecoverableDocument.size() );
                        editor::getSubsystem<EditorWorkspace>().requestOpenPanelByID( editorpanel::kRecoveredScene.getView() );
                    }
                }
                state._bMounted = editor::getSubsystem<AssetPaths>().mountDiskDomain( EditorAutosaveCommands::kDomain, folder ); // 거절하면 이유를 로그로 남긴다(복원만 막힌다)
            }

            /** @brief 세션 폴더들의 문서 사본을 복구 후보로 모읍니다(다른 프로젝트 · 바뀐 원본은 거절 이유를 단다). 원본 해시는 `EditorAutosaveCommands::computeDocumentSourceHash` 다. */
            static void scanRecoverableDocuments( const vector<AbsolutePath>& listSessionFolder, vector<EditorDocumentAutosaveEntry>& outListEntry )
            {
                EditorDocumentAutosaveEntry::scan( listSessionFolder, ProcessRoots::get()._project.str(), &EditorAutosaveCommands::computeDocumentSourceHash, outListEntry );
            }

            /** @brief 지금 씬의 사본 폴더에서 @p keepCount 를 넘는 오래된 세대를 지웁니다. */
            static void pruneGenerations( const AbsolutePath& slotFolder, uint32 keepCount )
            {
                vector<uint32> listGeneration;
                collectGenerations( slotFolder, listGeneration );
                while ( listGeneration.size() > keepCount )
                {
                    (void)FileUtil::tryRemoveFile( makeSnapshotPath( slotFolder, listGeneration.front() ) );
                    (void)FileUtil::tryRemoveFile( makeMetaPath( slotFolder, listGeneration.front() ) );
                    listGeneration.erase( listGeneration.begin() );
                }
            }

            /** @brief 지금 씬의 사본 하나를 씁니다. 원본이 없는 새 씬은 `untitled-<세대>` 칸에 쓴다. Play 중 · 저장이 막혔으면 쓰지 않는다. */
            static bool writeSnapshot()
            {
                Scene* pScene = editor::getActiveScene();
                if ( pScene == nullptr || EditorPlaySession::areSceneEditsAllowed() == false )
                    return false;
                const string                           sourceID = pScene->getSourcePath();
                fixed_string<constant::buffer::kMax64> domainPrefix;
                formatstring( domainPrefix.data(), domainPrefix.capacity(), "%#/", EditorAutosaveCommands::kDomain );
                if ( StringUtil::startsWith( sourceID, domainPrefix ) )
                    return false; // 복원 중인 사본 자신이다
                State& state = getState();
                if ( state._sessionFolder.empty() )
                    return false; // 세션을 열지 못했다(이유는 beginSession 이 남겼다)
                const bool         bUntitled  = sourceID.empty();
                const string       slotName   = bUntitled ? makeUntitledSlotName( editor::getSubsystem<SceneManager>().getSceneGeneration() ) : EditorAutosaveCommands::makeSlotName( sourceID );
                const AbsolutePath slotFolder = state._sessionFolder.join( slotName );
                const uint64       sourceHash = bUntitled ? 0 : EditorAutosaveCommands::computeSourceHash( sourceID ); // 새 씬은 견줄 원본이 없다(메타 sourceHash=0)
                if ( ( bUntitled == false && sourceHash == 0 ) || FileUtil::ensureDirectoryExists( slotFolder ) == false )
                    return false;
                vector<uint32> listGeneration;
                collectGenerations( slotFolder, listGeneration );
                const uint32                 generation   = listGeneration.empty() ? 1u : listGeneration.back() + 1u;
                const optional<AbsolutePath> snapshotPath = AbsolutePath::tryMake( makeSnapshotPath( slotFolder, generation ).str() );
                if ( snapshotPath.has_value() == false || editor::getSubsystem<SceneManager>().saveActiveSceneCopy( *snapshotPath ) == false )
                {
                    SW_LOG_WARNING_ID( "EditorAutosaveCommands.CopyFailed", "Autosave: could not write a copy of '%#'", bUntitled ? slotName : sourceID );
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> metaBuilder;
                metaBuilder.appendFormat( "source=%#\nproject=%#\nsaved=%#\nsourceHash=%#\n", AssetRegistry::makeReferenceText( sourceID, AssetRegistry::resolveGUID( sourceID ) ), ProcessRoots::get()._project.str(), FileUtil::getCurrentFileWriteTime(), sourceHash );
                const string meta{ metaBuilder.view() };
                if ( FileUtil::writeTextFile( makeMetaPath( slotFolder, generation ), meta ) == false )
                {
                    (void)FileUtil::tryRemoveFile( *snapshotPath ); // 메타 없는 사본은 복구할 수 없다
                    return false;
                }
                pruneGenerations( slotFolder, getPreferences<EditorAutosavePreferences>()._keepGenerations );
                ++state._writtenCount;
                state._snapshotEditSerial = editor::getSubsystem<EditorWorkspace>().getSceneEditSerial();
                state._sinceSnapshot.restart();
                SW_LOG_INFO_ID( "EditorAutosaveCommands.Written", "Autosave: wrote copy %# of '%#'", generation, bUntitled ? slotName : sourceID );
                return true;
            }

            /**
             * @brief 씬이 바뀐 뒤 그것이 복원을 청한 사본이면 소스를 원래 id 로 돌리고 dirty 로 두고 후보에서 뺀다(원본은 사용자가 저장할 때만 바뀐다).
             * @details 복원은 `tryOpenScene` 을 거쳐 미저장 확인 뒤로 미뤄질 수 있다 — 취소하거나 다른 씬이 열리면 활성 씬이 사본이 아니니 소스를 건드리지 않고 청을 버린다.
             */
            static void finishPendingRestore()
            {
                State& state = getState();
                if ( state._pendingSnapshotID.empty() )
                    return;
                const SceneManager& sceneManager = editor::getSubsystem<SceneManager>();
                if ( sceneManager.isTransitioning() || sceneManager.getSceneGeneration() == state._pendingGeneration )
                    return;
                Scene* pScene = editor::getActiveScene();
                if ( pScene != nullptr && pScene->getSourcePath() == state._pendingSnapshotID )
                {
                    pScene->setSourcePath( state._pendingSourceID );
                    editor::getSubsystem<EditorWorkspace>().markSceneDirty();
                    ++state._restoredCount;
                    if ( FileUtil::isDirectory( state._pendingSlotFolder ) )
                        (void)FileUtil::removeDirectory( state._pendingSlotFolder ); // 씬이 메모리에 dirty 로 있다 — 남기면 리로드 · 다음 기동이 같은 사본을 또 묻는다
                    vector<EditorAutosaveEntry>& listEntry = state._listRecoverable;
                    listEntry.erase( std::remove_if( listEntry.begin(), listEntry.end(), [&state]( const EditorAutosaveEntry& entry )
                    { return AssetPaths::normalizeAssetID( entry._snapshotID ) == state._pendingSnapshotID; } ),
                                     listEntry.end() );
                    SW_LOG_INFO_ID( "EditorAutosaveCommands.Restored", "Autosave: restored '%#' from its copy - save to keep it",
                                    state._pendingSourceID.empty() ? string( "Untitled" ) : state._pendingSourceID );
                }
                else
                    SW_LOG_INFO_ID( "EditorAutosaveCommands.RestoreAbandoned", "Autosave: the copy '%#' was not opened (cancelled or another scene opened) - it stays in Recovered Work",
                                    state._pendingSnapshotID );
                state._pendingSnapshotID.clear();
                state._pendingSourceID.clear();
                state._pendingSlotFolder = {};
            }

            /**
             * @brief 후보 @p index 를 엽니다(게임 단계 — `requestRestore` 가 남긴 요청). 씬 열기와 같은 입구다 — Play 중이면 거절하고, 지금 씬이 dirty 면 미저장 확인 뒤로 미룬다(E-1).
             *        후보는 사본이 실제로 열린 뒤 뺀다(`finishPendingRestore`). 거절 · Play 중이면 false 입니다.
             */
            static bool restoreEntry( uint32 index )
            {
                State& state = getState();
                if ( index >= state._listRecoverable.size() )
                    return false;
                const EditorAutosaveEntry entry = state._listRecoverable[index];
                if ( entry._refusal.empty() == false )
                {
                    SW_LOG_WARNING_ID( "EditorAutosaveCommands.RestoreRefused", "Autosave: '%#' was not restored - %#", entry._sourceID, entry._refusal );
                    return false;
                }
                if ( state._bMounted == false )
                {
                    SW_LOG_ERROR_ID( "EditorAutosaveCommands.DomainNotMounted", "Autosave: the copy folder is not mounted as '%#' - '%#' cannot be opened", EditorAutosaveCommands::kDomain,
                                     entry._sourceID );
                    return false;
                }
                const expected<AssetReferenceTarget> source = AssetRegistry::readReferenceText( entry._sourceReference ); // 훑은 뒤 옮겼으면 지금 자리
                if ( source.has_value() == false )
                {
                    SW_LOG_WARNING_ID( "EditorAutosaveCommands.RestoreRefused", "Autosave: '%#' was not restored - %#", entry._sourceID, "the original scene is gone" );
                    return false;
                }
                if ( EditorSceneFileCommands::tryOpenScene( entry._snapshotID ) == false )
                    return false; // 이유는 tryOpenScene 이 알렸다
                state._pendingSnapshotID = AssetPaths::normalizeAssetID( entry._snapshotID );
                state._pendingSourceID   = source->_path;
                state._pendingSlotFolder = entry._snapshotAbs.getParent();
                state._pendingGeneration = editor::getSubsystem<SceneManager>().getSceneGeneration();
                return true;
            }

            /** @brief 게임 단계 — UI 가 요청한 복원을 엽니다. 그 사이 후보가 지워졌으면 알리고 버린다. */
            static void runRequestedRestore()
            {
                State& state = getState();
                if ( state._requestedSnapshotID.empty() )
                    return;
                const string snapshotID = state._requestedSnapshotID;
                state._requestedSnapshotID.clear();
                uint32 index = 0;
                if ( EditorAutosaveCommands::findRecoverableIndex( state._listRecoverable, snapshotID, index ) == false )
                {
                    SW_LOG_WARNING_ID( "EditorAutosaveCommands.RestoreGone", "Autosave: the copy '%#' is no longer offered - nothing was restored", snapshotID );
                    return;
                }
                (void)restoreEntry( index ); // 거절 이유는 restoreEntry 가 알린다
            }

            /** @brief 게임 단계 — 활성 씬 사본 쓰기 · 복원 마무리는 씬을 읽고 바꾸므로 게임 스레드가 한다. */
            static void runGameStep( EditorContext& /*context*/, EditorPlaySessionWriteKey /*key*/ ) { EditorAutosaveCommands::update(); }

            static bool runNow( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                outReply = EditorAutosaveCommands::writeSnapshotNow() ? "autosave copy written" : "no autosave copy (no saved scene, playing or nothing to write)";
                return true;
            }

            static bool runRescan( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "%# recoverable scene copy(ies)", EditorAutosaveCommands::refreshRecoverable() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            [[nodiscard]] static bool readSnapshotCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( editor::findSubsystem<EditorStateStore>() == nullptr )
                    return false;
                outValue = static_cast<float64>( EditorAutosaveCommands::getWrittenSnapshotCount() );
                return true;
            }

            [[nodiscard]] static bool readRecoverableCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( editor::findSubsystem<EditorStateStore>() == nullptr )
                    return false;
                outValue = static_cast<float64>( EditorAutosaveCommands::getRecoverable().size() );
                return true;
            }

            [[nodiscard]] static bool readRecoverableDocumentCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( editor::findSubsystem<EditorStateStore>() == nullptr )
                    return false;
                outValue = static_cast<float64>( EditorAutosaveCommands::getRecoverableDocuments().size() );
                return true;
            }

            [[nodiscard]] static bool readRestoredDocumentCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( editor::findSubsystem<EditorStateStore>() == nullptr )
                    return false;
                outValue = static_cast<float64>( EditorAutosaveCommands::getRestoredDocumentCount() );
                return true;
            }

            [[nodiscard]] static bool readRestoredCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( editor::findSubsystem<EditorStateStore>() == nullptr )
                    return false;
                outValue = static_cast<float64>( EditorAutosaveCommands::getRestoredCount() );
                return true;
            }
        };
    } // namespace

    SW_EDITOR_SETTINGS( EditorAutosavePreferences, "autosave", "Editor/Autosave", 150, nullptr );
    // 명령 단계 뒤(이번 프레임의 씬 세션 처리 · 세대 동기화가 끝난 뒤)에 돈다
    SW_EDITOR_GAME_STEP( Autosave, "autosave", EditorGameStepOrder::kAutosave, &EditorAutosaveCommandsInternal::runGameStep );
    SW_DEV_COMMAND( AutosaveNow, "autosave.now", "autosave.now", "Write an autosave copy of the open scene now (ignores the interval)", &EditorAutosaveCommandsInternal::runNow );
    SW_DEV_COMMAND( AutosaveRescan, "autosave.rescan", "autosave.rescan",
                    "Treat the autosave copies on disk as left by a crashed session and offer them for recovery (Window > Recovered Work)", &EditorAutosaveCommandsInternal::runRescan );
    SW_AUTOMATION_WATCH( editorAutosaveSnapshotCount, "Editor.AutosaveSnapshotCount", "Autosave copies written this session", &EditorAutosaveCommandsInternal::readSnapshotCount );
    SW_AUTOMATION_WATCH( editorAutosaveRecoverableCount, "Editor.AutosaveRecoverableCount", "Scene copies offered for recovery (left by a session that did not close)",
                         &EditorAutosaveCommandsInternal::readRecoverableCount );
    SW_AUTOMATION_WATCH( editorAutosaveDocumentRecoverableCount, "Editor.AutosaveDocumentRecoverableCount", "Document copies offered for recovery (left by a session that did not close)",
                         &EditorAutosaveCommandsInternal::readRecoverableDocumentCount );
    SW_AUTOMATION_WATCH( editorAutosaveDocumentRestoredCount, "Editor.AutosaveDocumentRestoredCount", "Document copies restored this session",
                         &EditorAutosaveCommandsInternal::readRestoredDocumentCount );
    SW_AUTOMATION_WATCH( editorAutosaveRestoredCount, "Editor.AutosaveRestoredCount", "Autosave copies restored this session", &EditorAutosaveCommandsInternal::readRestoredCount );

    void EditorAutosaveCommands::update()
    {
        EditorAutosaveCommandsInternal::State& state = EditorAutosaveCommandsInternal::getState();
        if ( state._bSessionBegun == false )
            EditorAutosaveCommandsInternal::beginSession();
        EditorAutosaveCommandsInternal::finishPendingRestore();
        EditorAutosaveCommandsInternal::runRequestedRestore();

        const EditorAutosavePreferences& preferences = getPreferences<EditorAutosavePreferences>();
        const EditorWorkspace&           workspace   = editor::getSubsystem<EditorWorkspace>();
        const bool                       bChanged    = workspace.isSceneDirty() && workspace.getSceneEditSerial() != state._snapshotEditSerial;
        const bool                       bDue        = static_cast<float64>( state._sinceSnapshot.getElapsedMilliseconds() ) >= static_cast<float64>( preferences._intervalSeconds ) * 1000.0;
        if ( preferences._bEnabled && bChanged && bDue )
            (void)EditorAutosaveCommandsInternal::writeSnapshot(); // 쓰지 못한 이유는 경고로 남는다(새 씬 · Play 중은 조용히 건너뛴다)
    }

    void EditorAutosaveCommands::endSession( const EditorAutosaveSessionEnd reason )
    {
        // 에디터 상태 저장소는 이미 내려갔다(에디터 컨텍스트가 먼저 내려간다) — 상태를 읽지 않고 잠금의 pid 로 자기 폴더를 찾는다.
        const AbsolutePath folder = EditorAutosaveCommandsInternal::getFolder();
        // 리로드 · 교체는 세션 id 를 맡김 칸에 맡겨 두었다(beginSession) — 다음 인스턴스가 같은 폴더에 이어 쓴다. 편집기 종료만 지운다:
        // 저장하지 않은 일은 종료 확인에서 사용자가 이미 골랐다(언리얼 FPackageAutoSaver 도 정상 종료 뒤에는 복원 파일을 묻지 않는다).
        if ( reason == EditorAutosaveSessionEnd::EditorExit && folder.empty() == false )
            EditorAutosaveSession::removeOwned( folder, Process::getCurrentProcessID() );
        AssetPaths*                            pAssetPaths = editor::findSubsystem<AssetPaths>();
        fixed_string<constant::buffer::kMax64> sessionID;
        formatstring( sessionID.data(), sessionID.capacity(), "%#/session", kDomain );
        if ( pAssetPaths != nullptr && pAssetPaths->isMountedDiskDomainID( sessionID ) )
            (void)pAssetPaths->unmountDiskDomain( kDomain );
    }

    bool EditorAutosaveCommands::writeSnapshotNow()
    {
        if ( EditorAutosaveCommandsInternal::getState()._bSessionBegun == false )
            EditorAutosaveCommandsInternal::beginSession();
        return EditorAutosaveCommandsInternal::writeSnapshot();
    }

    void EditorAutosaveCommands::onSceneSaved( string_view sourceID )
    {
        EditorAutosaveCommandsInternal::State& state = EditorAutosaveCommandsInternal::getState();
        if ( state._sessionFolder.empty() || sourceID.empty() )
            return;
        // 새 씬을 처음 저장했으면 저장 전 칸(`untitled-<세대>`)도 지운다 — 씬 세대는 저장으로 바뀌지 않는다
        const string arrSlotName[] = { makeSlotName( sourceID ), EditorAutosaveCommandsInternal::makeUntitledSlotName( editor::getSubsystem<SceneManager>().getSceneGeneration() ) };
        for ( const string& slotName : arrSlotName )
        {
            const AbsolutePath slotFolder     = state._sessionFolder.join( slotName );
            const bool         bPendingFolder = state._pendingSnapshotID.empty() == false && FileUtil::isSameDiskPath( slotFolder.str(), state._pendingSlotFolder.str() );
            if ( bPendingFolder == false && FileUtil::isDirectory( slotFolder ) )
                (void)FileUtil::removeDirectory( slotFolder );
        }
        // 이미 저장한 씬은 복구 후보에서도 빼고 그 사본(넘겨받은 세션의 것)도 지운다 — 저장한 원본이 사본보다 새것이다
        vector<EditorAutosaveEntry>& listEntry = state._listRecoverable;
        // 단, 복원을 청한 사본은 남긴다 — dirty 원본에서 복원을 누르고 미저장 확인에서 Save 를 고르면 원본 저장이 먼저 와서, 지우면 곧 열 사본이 없어 씬 읽기가 졌다(감사 E-1).
        // 그 사본은 열린 뒤 `finishPendingRestore` 가 지운다 — 사용자가 고른 것은 사본으로 돌아가는 일이다.
        const string pendingSlotFolder = state._pendingSnapshotID.empty() ? string{} : state._pendingSlotFolder.str();
        const auto   isPendingCopy     = [&pendingSlotFolder]( const EditorAutosaveEntry& entry )
        { return pendingSlotFolder.empty() == false && FileUtil::isSameDiskPath( FileUtil::getDirectoryPart( entry._snapshotAbs.str() ), pendingSlotFolder ); };
        const auto isCopyOfSaved = [&arrSlotName]( const EditorAutosaveEntry& entry ) // 칸 이름이 씬의 정체다 — 훑은 뒤 옮긴 씬도 맞는다
        { return FileUtil::getFileNamePart( entry._snapshotAbs.getParent().str() ) == arrSlotName[0]; };
        for ( const EditorAutosaveEntry& entry : listEntry )
        {
            const AbsolutePath entrySlotFolder = entry._snapshotAbs.getParent();
            if ( isCopyOfSaved( entry ) && isPendingCopy( entry ) == false && FileUtil::isDirectory( entrySlotFolder ) )
                (void)FileUtil::removeDirectory( entrySlotFolder );
        }
        listEntry.erase( std::remove_if( listEntry.begin(), listEntry.end(), [&isCopyOfSaved, &isPendingCopy]( const EditorAutosaveEntry& entry )
        { return isCopyOfSaved( entry ) && isPendingCopy( entry ) == false; } ),
                         listEntry.end() );
    }

    const vector<EditorAutosaveEntry>& EditorAutosaveCommands::getRecoverable()
    {
        return EditorAutosaveCommandsInternal::getState()._listRecoverable;
    }

    uint32 EditorAutosaveCommands::refreshRecoverable()
    {
        // 모든 세션 폴더(이 세션 것 포함)를 비정상 종료가 남긴 것처럼 읽는다 — 개발 명령 `autosave.rescan` 의 시나리오용
        vector<AbsolutePath> listSessionFolder;
        (void)FileUtil::collectFolders( EditorAutosaveCommandsInternal::getFolder(), listSessionFolder, false ); // 폴더가 없으면 후보도 없다
        EditorAutosaveCommandsInternal::State& state = EditorAutosaveCommandsInternal::getState();
        EditorAutosaveCommandsInternal::scanRecoverable( listSessionFolder, state._listRecoverable );
        EditorAutosaveCommandsInternal::scanRecoverableDocuments( listSessionFolder, state._listRecoverableDocument );
        return static_cast<uint32>( state._listRecoverable.size() + state._listRecoverableDocument.size() );
    }

    AbsolutePath EditorAutosaveCommands::getSessionFolder()
    {
        if ( editor::findSubsystem<EditorStateStore>() == nullptr )
            return {};
        return EditorAutosaveCommandsInternal::getState()._sessionFolder;
    }

    const vector<EditorDocumentAutosaveEntry>& EditorAutosaveCommands::getRecoverableDocuments()
    {
        return EditorAutosaveCommandsInternal::getState()._listRecoverableDocument;
    }

    bool EditorAutosaveCommands::restoreDocument( uint32 index )
    {
        EditorAutosaveCommandsInternal::State& state = EditorAutosaveCommandsInternal::getState();
        if ( index >= state._listRecoverableDocument.size() )
            return false;
        const EditorDocumentAutosaveEntry& entry = state._listRecoverableDocument[index];
        // 씬 복원과 같은 입구 규칙이다 — Play 중에는 거절한다(Stop 이 되돌릴 상태 위에 사본을 얹지 않는다, 감사 E-1)
        if ( EditorPlaySession::isStopped() == false )
        {
            SW_LOG_WARNING_ID( "EditorAutosaveCommands.DocumentRestoreDuringPlay", "Autosave: document '%#' was not restored - stop playing first", entry._documentPath );
            return false;
        }
        if ( entry._refusal.empty() == false )
        {
            SW_LOG_WARNING_ID( "EditorAutosaveCommands.DocumentRestoreRefused", "Autosave: document '%#' was not restored - %#", entry._documentPath, entry._refusal );
            return false;
        }
        string text;
        if ( entry.readText( text ) == false )
        {
            SW_LOG_WARNING_ID( "EditorAutosaveCommands.DocumentCopyUnreadable", "Autosave: the copy of document '%#' could not be read", entry._documentPath );
            return false;
        }
        // 문서 열기와 같은 길(워크스페이스 포커스)이다 — 맞는 문서 패널이 받아 읽고, 읽은 직후 이 청을 꺼내 사본 글을 적용한다(takePendingDocumentRestore).
        // 상태 문서는 연 에셋이 없다 — 그 패널을 열면 패널이 자기 파일의 사본을 적용한다(`EditorStateDocumentPanel`).
        // 훑은 뒤 옮긴 문서는 지금 자리를 연다 — 상태 문서의 참조 글은 파일 id 그대로다.
        const expected<AssetReferenceTarget> document = AssetRegistry::readReferenceText( entry._documentReference );
        if ( document.has_value() == false )
        {
            SW_LOG_WARNING_ID( "EditorAutosaveCommands.DocumentRestoreRefused", "Autosave: document '%#' was not restored - %#", entry._documentPath, "the original document is gone" );
            return false;
        }
        state._pendingDocumentPath = document->_path;
        state._pendingDocumentText = std::move( text );
        EditorWorkspace& workspace = editor::getSubsystem<EditorWorkspace>();
        if ( entry._panelID.empty() )
            workspace.setFocusedAssetPath( state._pendingDocumentPath.c_str() );
        else
            workspace.requestOpenPanelByID( entry._panelID );
        return true;
    }

    void EditorAutosaveCommands::discardDocument( uint32 index )
    {
        vector<EditorDocumentAutosaveEntry>& listEntry = EditorAutosaveCommandsInternal::getState()._listRecoverableDocument;
        if ( index >= listEntry.size() )
            return;
        (void)FileUtil::tryRemoveFile( listEntry[index]._textAbs );
        SW_LOG_INFO_ID( "EditorAutosaveCommands.DocumentDiscarded", "Autosave: discarded the copy of document '%#'", listEntry[index]._documentPath );
        listEntry.erase( listEntry.begin() + static_cast<ptrdiff_t>( index ) );
    }

    bool EditorAutosaveCommands::peekPendingDocumentRestore( string_view& outDocumentPath, string_view& outText )
    {
        if ( editor::findSubsystem<EditorStateStore>() == nullptr )
            return false;
        const EditorAutosaveCommandsInternal::State& state = EditorAutosaveCommandsInternal::getState();
        outDocumentPath                                    = state._pendingDocumentPath;
        outText                                            = state._pendingDocumentText;
        return state._pendingDocumentPath.empty() == false;
    }

    void EditorAutosaveCommands::cancelPendingDocumentRestore()
    {
        EditorAutosaveCommandsInternal::State& state = EditorAutosaveCommandsInternal::getState();
        state._pendingDocumentPath.clear();
        state._pendingDocumentText.clear();
    }

    bool EditorAutosaveCommands::takePendingDocumentRestore( string_view documentPath, string& outText )
    {
        if ( editor::findSubsystem<EditorStateStore>() == nullptr )
            return false;
        EditorAutosaveCommandsInternal::State& state = EditorAutosaveCommandsInternal::getState();
        if ( state._pendingDocumentPath.empty() || state._pendingDocumentPath != documentPath )
            return false;
        outText                                        = std::move( state._pendingDocumentText );
        vector<EditorDocumentAutosaveEntry>& listEntry = state._listRecoverableDocument;
        for ( const EditorDocumentAutosaveEntry& entry : listEntry )
        {
            if ( entry.isCopyOf( documentPath ) )
                (void)FileUtil::tryRemoveFile( entry._textAbs ); // 문서가 dirty 로 메모리에 있다 — 남기면 다음 기동이 같은 사본을 또 묻는다
        }
        listEntry.erase( std::remove_if( listEntry.begin(), listEntry.end(), [documentPath]( const EditorDocumentAutosaveEntry& entry )
        { return entry.isCopyOf( documentPath ); } ),
                         listEntry.end() );
        state._pendingDocumentPath.clear();
        state._pendingDocumentText.clear();
        ++state._restoredDocumentCount;
        SW_LOG_INFO_ID( "EditorAutosaveCommands.DocumentRestored", "Autosave: restored document '%#' from its copy - save to keep it", documentPath );
        return true;
    }

    uint32 EditorAutosaveCommands::getRestoredDocumentCount()
    {
        return EditorAutosaveCommandsInternal::getState()._restoredDocumentCount;
    }

    bool EditorAutosaveCommands::requestRestore( uint32 index )
    {
        EditorAutosaveCommandsInternal::State& state = EditorAutosaveCommandsInternal::getState();
        if ( index >= state._listRecoverable.size() )
            return false;
        state._requestedSnapshotID = AssetPaths::normalizeAssetID( state._listRecoverable[index]._snapshotID );
        return true;
    }

    bool EditorAutosaveCommands::findRecoverableIndex( const vector<EditorAutosaveEntry>& listEntry, string_view snapshotID, uint32& outIndex )
    {
        for ( uint32 index = 0; index < static_cast<uint32>( listEntry.size() ); ++index )
        {
            if ( AssetPaths::normalizeAssetID( listEntry[index]._snapshotID ) == snapshotID )
            {
                outIndex = index;
                return true;
            }
        }
        return false;
    }

    void EditorAutosaveCommands::discard( uint32 index )
    {
        vector<EditorAutosaveEntry>& listEntry = EditorAutosaveCommandsInternal::getState()._listRecoverable;
        if ( index >= listEntry.size() )
            return;
        const AbsolutePath slotFolder = listEntry[index]._snapshotAbs.getParent();
        if ( FileUtil::isDirectory( slotFolder ) )
            (void)FileUtil::removeDirectory( slotFolder );
        SW_LOG_INFO_ID( "EditorAutosaveCommands.Discarded", "Autosave: discarded the copies of '%#'", listEntry[index]._sourceID );
        listEntry.erase( listEntry.begin() + static_cast<ptrdiff_t>( index ) );
    }

    uint32 EditorAutosaveCommands::getWrittenSnapshotCount()
    {
        return EditorAutosaveCommandsInternal::getState()._writtenCount;
    }

    uint32 EditorAutosaveCommands::getRestoredCount()
    {
        return EditorAutosaveCommandsInternal::getState()._restoredCount;
    }

    AbsolutePath EditorAutosaveSession::makeFolder( const AbsolutePath& root ) const
    {
        StringBuilder<constant::buffer::kMax64> nameBuilder;
        nameBuilder.appendFormat( "%#-%#", _processID, _startTicks );
        return root.join( nameBuilder.view() );
    }

    bool EditorAutosaveSession::readLock( const AbsolutePath& sessionFolder, EditorAutosaveSession& outSession )
    {
        string lockText;
        if ( FileUtil::readTextFile( sessionFolder.join( kLockFileName ), lockText ) == false )
            return false;
        return StringUtil::parseInt( EditorAutosaveCommands::findMetaValue( lockText, "pid" ), outSession._processID ) &&
               StringUtil::parseInt64( EditorAutosaveCommands::findMetaValue( lockText, "start" ), outSession._startTicks );
    }

    bool EditorAutosaveSession::open( const AbsolutePath& root, vector<AbsolutePath>& outListRecoveredFolder ) const
    {
        outListRecoveredFolder.clear();
        StringBuilder<constant::buffer::kMax64> lockBuilder;
        lockBuilder.appendFormat( "pid=%#\nstart=%#\n", _processID, _startTicks );
        const AbsolutePath   ownFolder = makeFolder( root );
        vector<AbsolutePath> listSessionFolder;
        (void)FileUtil::collectFolders( root, listSessionFolder, false ); // 루트가 없으면 넘겨받을 세션도 없다
        for ( const AbsolutePath& sessionFolder : listSessionFolder )
        {
            EditorAutosaveSession owner{};
            if ( FileUtil::isSameDiskPath( sessionFolder.str(), ownFolder.str() ) || readLock( sessionFolder, owner ) == false )
                continue; // 자기 폴더 · 잠금 없는 폴더(세션 폴더가 아니다)는 보지 않는다
            const bool bSelf = owner._processID == _processID && owner._startTicks == _startTicks;
            // 같은 pid 의 다른 시각은 이 프로세스의 세션이 아니다(OS 가 pid 를 다시 썼다, 또는 맡김 칸 없이 리로드했다) — 죽은 세션으로 본다
            const bool bDead = bSelf == false && ( owner._processID == _processID || Process::isProcessAlive( owner._processID ) == false );
            if ( bDead && FileUtil::writeTextFile( sessionFolder.join( kLockFileName ), lockBuilder.view() ) == false )
                continue; // 넘겨받지 못하면 다음 기동이 다시 묻는다
            if ( bSelf || bDead )
                outListRecoveredFolder.push_back( sessionFolder );
        }
        return FileUtil::ensureDirectoryExists( ownFolder ) && FileUtil::writeTextFile( ownFolder.join( kLockFileName ), lockBuilder.view() );
    }

    void EditorAutosaveSession::removeOwned( const AbsolutePath& root, const int32 processID )
    {
        vector<AbsolutePath> listSessionFolder;
        (void)FileUtil::collectFolders( root, listSessionFolder, false ); // 루트가 없으면 지울 것도 없다
        for ( const AbsolutePath& sessionFolder : listSessionFolder )
        {
            EditorAutosaveSession owner{};
            // 넘겨받은 폴더는 잠금을 이 pid 로 고쳐 썼고, 같은 pid 의 남의 폴더는 open 이 모두 넘겨받았다 — pid 가 같으면 이 세션 것이다
            if ( readLock( sessionFolder, owner ) && owner._processID == processID )
                (void)FileUtil::removeDirectory( sessionFolder );
        }
    }

    string EditorAutosaveCommands::makeSlotName( string_view sourceID )
    {
        // 칸은 자리가 아니라 정체다 — 옮긴 뒤의 사본 · 저장이 같은 칸을 잇고, 옛 자리에 새로 생긴 에셋이 남의 사본을 이어받지 않는다.
        const string identity = AssetRegistry::makeReferenceText( sourceID, AssetRegistry::resolveGUID( sourceID ) );
        string       slot;
        slot.reserve( identity.size() );
        for ( const utf8 ch : identity )
        {
            const utf8 lower    = StringUtil::toLowerChar( ch );
            const bool bAllowed = ( lower >= 'a' && lower <= 'z' ) || ( lower >= '0' && lower <= '9' ) || lower == '_' || lower == '-' || lower == '.';
            slot.push_back( bAllowed ? lower : '_' );
        }
        return slot;
    }

    string EditorAutosaveCommands::findMetaValue( string_view metaText, string_view key )
    {
        size_t lineStart = 0;
        while ( lineStart < metaText.size() )
        {
            size_t lineEnd = metaText.find( '\n', lineStart );
            if ( lineEnd == string_view::npos )
                lineEnd = metaText.size();
            const string_view line = metaText.substr( lineStart, lineEnd - lineStart );
            if ( line.size() > key.size() && line.substr( 0, key.size() ) == key && line[key.size()] == '=' )
                return string( line.substr( key.size() + 1 ) );
            lineStart = lineEnd + 1;
        }
        return string();
    }

    uint64 EditorAutosaveCommands::computeSourceHash( string_view sourceID )
    {
        const optional<AbsolutePath> sourcePath = editor::getSubsystem<AssetPaths>().toDiskPath( sourceID );
        return sourcePath.has_value() ? ContentHash::computeFile( *sourcePath ) : 0;
    }

    uint64 EditorAutosaveCommands::computeDocumentSourceHash( const EditorDocumentAutosaveEntry& entry )
    {
        if ( entry._panelID.empty() )
            return computeSourceHash( entry._documentPath );
        const optional<AbsolutePath> filePath = EditorDocumentAutosaveEntry::resolveFileID( entry._documentPath );
        return filePath.has_value() ? ContentHash::computeFile( *filePath ) : 0;
    }
} // namespace sw::editor
