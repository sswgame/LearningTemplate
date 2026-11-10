#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

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
#include "Engine/Content/ContentHash.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    namespace
    {
        struct EditorAutosaveCommandsInternal
        {
            static constexpr const utf8* kFolderName   = "Autosave";
            static constexpr const utf8* kLockFileName = "session.lock";
            static constexpr const utf8* kSceneSuffix  = ".scene.xml";
            static constexpr const utf8* kMetaSuffix   = ".meta";

            /** @brief 세션 하나의 자동 저장 상태입니다. 에디터 상태 저장소가 소유한다(UI 스레드만). */
            struct State
            {
                vector<EditorAutosaveEntry> _listRecoverable{};
                Stopwatch                   _sinceSnapshot{};
                string                      _pendingSnapshotID{};     ///< 복원으로 연 사본의 id. 다 열리면 소스를 돌린다
                string                      _pendingSourceID{};       ///< 그 원래 id
                uint64                      _pendingGeneration{ 0 };  ///< 복원을 청한 때의 씬 세대
                uint32                      _snapshotEditSerial{ 0 }; ///< 지난 사본 때의 편집 번호(`EditorWorkspace::getSceneEditSerial`)
                uint32                      _writtenCount{ 0 };
                uint32                      _restoredCount{ 0 };
                bool                        _bSessionBegun{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
                bool                        _bMounted{ false };      ///< NOLINT(sw-bool-flag): 위와 같다
            };

            static State& getState() { return getEditorState<State>(); }

            /** @brief `<에디터 상태 폴더>/Autosave` 입니다. 상태 폴더를 정하지 못하면 빈 글입니다. */
            static string getFolder() { return EditorPaths::resolveEditorStateFile( kFolderName ); }

            /** @brief 사본 옆 메타 한 줄(`key=value`)을 읽습니다. 없으면 빈 글입니다. */
            static string readMetaValue( string_view metaText, string_view key )
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

            /** @brief 폴더 안 사본의 세대 번호를 작은 것부터 모읍니다(`<세대>.scene.xml`). */
            static void collectGenerations( const string& slotFolder, vector<uint32>& outListGeneration )
            {
                outListGeneration.clear();
                vector<string> listFile;
                if ( FileUtil::collectFiles( slotFolder, "", listFile, false ) == false )
                    return; // 폴더가 아직 없다 — 사본이 없다
                for ( const string& file : listFile )
                {
                    const string name = FileUtil::getFileNamePart( file );
                    if ( StringUtil::endsWith( name, kSceneSuffix ) == false )
                        continue;
                    int32 generation = 0;
                    if ( StringUtil::parseInt( string_view( name ).substr( 0, name.size() - string_view( kSceneSuffix ).size() ), generation ) && generation > 0 )
                        outListGeneration.push_back( static_cast<uint32>( generation ) );
                }
                std::sort( outListGeneration.begin(), outListGeneration.end() );
            }

            static string makeSnapshotPath( const string& slotFolder, uint32 generation ) { return makeGenerationPath( slotFolder, generation, kSceneSuffix ); }
            static string makeMetaPath( const string& slotFolder, uint32 generation ) { return makeGenerationPath( slotFolder, generation, kMetaSuffix ); }
            static string makeGenerationPath( const string& slotFolder, uint32 generation, string_view suffix )
            {
                fixed_string<constant::buffer::kMax64> fileName;
                formatstring( fileName.data(), fileName.capacity(), "%#%#", generation, suffix );
                return FileUtil::joinPath( slotFolder, fileName.view() );
            }

            /** @brief 사본 폴더를 훑어 씬마다 가장 새 사본 하나를 복구 후보로 만듭니다. 다른 프로젝트 · 바뀐 원본은 거절 이유를 단다. */
            static void scanRecoverable( vector<EditorAutosaveEntry>& outListEntry )
            {
                outListEntry.clear();
                vector<string> listSlot;
                if ( FileUtil::collectFolders( getFolder(), listSlot, false ) == false )
                    return;
                const string projectRoot = ProcessRoots::get()._project.str();
                for ( const string& slotFolder : listSlot )
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
                    snapshotIDBuilder.appendFormat( "%#/%#/%#%#", EditorAutosaveCommands::kDomain, FileUtil::getFileNamePart( slotFolder ), newest, kSceneSuffix );
                    entry._snapshotID      = string( snapshotIDBuilder.view() );
                    entry._sourceID        = readMetaValue( metaText, "source" );
                    entry._projectRoot     = readMetaValue( metaText, "project" );
                    entry._generationCount = static_cast<uint32>( listGeneration.size() );
                    (void)StringUtil::parseInt64( readMetaValue( metaText, "saved" ), entry._savedTicks );
                    (void)StringUtil::parseUint64( readMetaValue( metaText, "sourceHash" ), entry._sourceContentHash ); // 없으면(옛 메타) 0 — 아래에서 바뀐 원본으로 거절한다
                    // 다른 프로젝트 · 바뀐 원본에는 적용하지 않는다 — 사본이 오래된 원본 위에 덮여 남의 일을 지우지 않게.
                    // 바뀜은 내용 해시로 본다 — 쓰기 시각은 체크아웃 · 복사가 내용과 따로 바꾸고, 같은 틱의 다시 쓰기는 시각이 같다.
                    const optional<AbsolutePath> sourcePath  = editor::getSubsystem<AssetPaths>().toDiskPath( entry._sourceID );
                    const uint64                 currentHash = sourcePath.has_value() ? ContentHash::computeFile( sourcePath->str() ) : 0;
                    if ( FileUtil::isSameDiskPath( entry._projectRoot, projectRoot ) == false )
                        entry._refusal = "it was written for another project";
                    else if ( currentHash == 0 )
                        entry._refusal = "the original scene is gone";
                    else if ( currentHash != entry._sourceContentHash )
                        entry._refusal = "the original scene changed after this copy";
                    outListEntry.push_back( std::move( entry ) );
                }
            }

            /** @brief 세션을 엽니다 — 지난 잠금이 남았으면 비정상 종료라 복구 후보를 모으고, 새 잠금을 쓰고, 사본 폴더를 도메인으로 붙인다. */
            static void beginSession()
            {
                State& state         = getState();
                state._bSessionBegun = true;
                const string folder  = getFolder();
                if ( folder.empty() || FileUtil::ensureDirectoryExists( folder ) == false )
                {
                    SW_LOG_ERROR_ID( "EditorAutosaveCommands.NoFolder", "Autosave: the editor state folder is unknown or cannot be created - autosave is off for this session" );
                    return;
                }
                const string lockPath = FileUtil::joinPath( folder, kLockFileName );
                if ( FileUtil::exists( lockPath ) )
                {
                    scanRecoverable( state._listRecoverable );
                    if ( state._listRecoverable.empty() == false )
                    {
                        SW_LOG_WARNING_ID( "EditorAutosaveCommands.RecoveryOffered", "Autosave: the last editor session did not close - %# scene copy(ies) can be recovered (Window > Recovered Work)", state._listRecoverable.size() );
                        editor::getSubsystem<EditorWorkspace>().requestOpenPanelByID( editorpanel::kRecoveredWork.getView() );
                    }
                }
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%#\n", Process::getCurrentProcessID() );
                if ( FileUtil::writeTextFile( lockPath, textBuilder.view() ) == false )
                    SW_LOG_WARNING_ID( "EditorAutosaveCommands.LockNotWritten", "Autosave: could not write the session lock - a crash of this session will not offer recovery" );
                state._bMounted = editor::getSubsystem<AssetPaths>().mountDiskDomain( EditorAutosaveCommands::kDomain, folder ); // 거절하면 이유를 로그로 남긴다(복원만 막힌다)
            }

            /** @brief 지금 씬의 사본 폴더에서 @p keepCount 를 넘는 오래된 세대를 지웁니다. */
            static void pruneGenerations( const string& slotFolder, uint32 keepCount )
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

            /** @brief 지금 씬의 사본 하나를 씁니다. 원본이 없는 새 씬 · Play 중 · 저장이 막혔으면 쓰지 않는다. */
            static bool writeSnapshot()
            {
                Scene* pScene = editor::getActiveScene();
                if ( pScene == nullptr || pScene->getSourcePath().empty() || EditorPlaySession::areSceneEditsAllowed() == false )
                    return false;
                const string sourceID = pScene->getSourcePath();
                if ( StringUtil::startsWith( sourceID, string( EditorAutosaveCommands::kDomain ) + "/" ) )
                    return false; // 복원 중인 사본 자신이다
                const string                 slotFolder = FileUtil::joinPath( getFolder(), EditorAutosaveCommands::makeSlotName( sourceID ) );
                const optional<AbsolutePath> sourcePath = editor::getSubsystem<AssetPaths>().toDiskPath( sourceID );
                const uint64                 sourceHash = sourcePath.has_value() ? ContentHash::computeFile( sourcePath->str() ) : 0;
                if ( sourceHash == 0 || FileUtil::ensureDirectoryExists( slotFolder ) == false )
                    return false;
                vector<uint32> listGeneration;
                collectGenerations( slotFolder, listGeneration );
                const uint32                 generation   = listGeneration.empty() ? 1u : listGeneration.back() + 1u;
                const optional<AbsolutePath> snapshotPath = AbsolutePath::tryMake( makeSnapshotPath( slotFolder, generation ) );
                if ( snapshotPath.has_value() == false || editor::getSubsystem<SceneManager>().saveActiveSceneCopy( *snapshotPath ) == false )
                {
                    SW_LOG_WARNING_ID( "EditorAutosaveCommands.CopyFailed", "Autosave: could not write a copy of '%#'", sourceID );
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> metaBuilder;
                metaBuilder.appendFormat( "source=%#\nproject=%#\nsaved=%#\nsourceHash=%#\n", sourceID, ProcessRoots::get()._project.str(), FileUtil::getCurrentFileWriteTime(), sourceHash );
                const string meta{ metaBuilder.view() };
                if ( FileUtil::writeTextFile( makeMetaPath( slotFolder, generation ), meta ) == false )
                {
                    (void)FileUtil::tryRemoveFile( snapshotPath->str() ); // 메타 없는 사본은 복구할 수 없다
                    return false;
                }
                pruneGenerations( slotFolder, getPreferences<EditorAutosavePreferences>()._keepGenerations );
                State& state = getState();
                ++state._writtenCount;
                state._snapshotEditSerial = editor::getSubsystem<EditorWorkspace>().getSceneEditSerial();
                state._sinceSnapshot.restart();
                SW_LOG_INFO_ID( "EditorAutosaveCommands.Written", "Autosave: wrote copy %# of '%#'", generation, sourceID );
                return true;
            }

            /** @brief 복원으로 연 사본이 다 열렸으면 소스를 원래 id 로 돌리고 dirty 로 둔다(원본은 사용자가 저장할 때만 바뀐다). */
            static void finishPendingRestore()
            {
                State& state = getState();
                if ( state._pendingSnapshotID.empty() )
                    return;
                const SceneManager& sceneManager = editor::getSubsystem<SceneManager>();
                if ( sceneManager.isTransitioning() || sceneManager.getSceneGeneration() == state._pendingGeneration )
                    return;
                Scene* pScene = editor::getActiveScene();
                if ( pScene != nullptr )
                {
                    pScene->setSourcePath( state._pendingSourceID );
                    editor::getSubsystem<EditorWorkspace>().markSceneDirty();
                    ++state._restoredCount;
                    SW_LOG_INFO_ID( "EditorAutosaveCommands.Restored", "Autosave: restored '%#' from its copy - save to keep it", state._pendingSourceID );
                }
                state._pendingSnapshotID.clear();
                state._pendingSourceID.clear();
            }

            static void runFrameStep( EditorContext& /*context*/ ) { EditorAutosaveCommands::tick(); }

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
    SW_EDITOR_FRAME_STEP( Autosave, "autosave", EditorFrameStepOrder::kCommands + 50, &EditorAutosaveCommandsInternal::runFrameStep );
    SW_DEV_COMMAND( AutosaveNow, "autosave.now", "autosave.now", "Write an autosave copy of the open scene now (ignores the interval)", &EditorAutosaveCommandsInternal::runNow );
    SW_DEV_COMMAND( AutosaveRescan, "autosave.rescan", "autosave.rescan",
                    "Treat the autosave copies on disk as left by a crashed session and offer them for recovery (Window > Recovered Work)", &EditorAutosaveCommandsInternal::runRescan );
    SW_AUTOMATION_WATCH( editorAutosaveSnapshotCount, "Editor.AutosaveSnapshotCount", "Autosave copies written this session", &EditorAutosaveCommandsInternal::readSnapshotCount );
    SW_AUTOMATION_WATCH( editorAutosaveRecoverableCount, "Editor.AutosaveRecoverableCount", "Scene copies offered for recovery (left by a session that did not close)",
                         &EditorAutosaveCommandsInternal::readRecoverableCount );
    SW_AUTOMATION_WATCH( editorAutosaveRestoredCount, "Editor.AutosaveRestoredCount", "Autosave copies restored this session", &EditorAutosaveCommandsInternal::readRestoredCount );

    void EditorAutosaveCommands::tick()
    {
        EditorAutosaveCommandsInternal::State& state = EditorAutosaveCommandsInternal::getState();
        if ( state._bSessionBegun == false )
            EditorAutosaveCommandsInternal::beginSession();
        EditorAutosaveCommandsInternal::finishPendingRestore();

        const EditorAutosavePreferences& preferences = getPreferences<EditorAutosavePreferences>();
        const EditorWorkspace&           workspace   = editor::getSubsystem<EditorWorkspace>();
        const bool                       bChanged    = workspace.isSceneDirty() && workspace.getSceneEditSerial() != state._snapshotEditSerial;
        const bool                       bDue        = static_cast<float64>( state._sinceSnapshot.getElapsedMilliseconds() ) >= static_cast<float64>( preferences._intervalSeconds ) * 1000.0;
        if ( preferences._bEnabled && bChanged && bDue )
            (void)EditorAutosaveCommandsInternal::writeSnapshot(); // 쓰지 못한 이유는 경고로 남는다(새 씬 · Play 중은 조용히 건너뛴다)
    }

    void EditorAutosaveCommands::endSession()
    {
        const string folder = EditorAutosaveCommandsInternal::getFolder();
        if ( folder.empty() )
            return;
        // 정상 종료 — 저장하지 않은 일은 종료 확인에서 사용자가 이미 골랐다. 사본과 잠금을 지운다(다음 기동이 복구를 묻지 않는다).
        vector<string> listSlot;
        if ( FileUtil::collectFolders( folder, listSlot, false ) )
        {
            for ( const string& slotFolder : listSlot )
                (void)FileUtil::removeDirectory( slotFolder );
        }
        (void)FileUtil::tryRemoveFile( FileUtil::joinPath( folder, EditorAutosaveCommandsInternal::kLockFileName ) );
        AssetPaths* pAssetPaths = editor::findSubsystem<AssetPaths>();
        if ( pAssetPaths != nullptr && pAssetPaths->isMountedDiskDomainID( string( kDomain ) + "/session" ) )
            (void)pAssetPaths->unmountDiskDomain( kDomain );
        // 상태 저장소가 아직 서 있으면(모듈 리로드) 다음 프레임이 세션을 다시 연다 — 잠금 · 도메인을 새로 건다
        if ( editor::findSubsystem<EditorStateStore>() != nullptr )
        {
            EditorAutosaveCommandsInternal::State& state = EditorAutosaveCommandsInternal::getState();
            state._bSessionBegun                         = false;
            state._bMounted                              = false;
        }
    }

    bool EditorAutosaveCommands::writeSnapshotNow()
    {
        if ( EditorAutosaveCommandsInternal::getState()._bSessionBegun == false )
            EditorAutosaveCommandsInternal::beginSession();
        return EditorAutosaveCommandsInternal::writeSnapshot();
    }

    void EditorAutosaveCommands::onSceneSaved( string_view sourceID )
    {
        const string folder = EditorAutosaveCommandsInternal::getFolder();
        if ( folder.empty() || sourceID.empty() )
            return;
        const string slotFolder = FileUtil::joinPath( folder, makeSlotName( sourceID ) );
        if ( FileUtil::isDirectory( slotFolder ) )
            (void)FileUtil::removeDirectory( slotFolder );
        // 이미 저장한 씬은 복구 후보에서도 뺀다
        vector<EditorAutosaveEntry>& listEntry = EditorAutosaveCommandsInternal::getState()._listRecoverable;
        listEntry.erase( std::remove_if( listEntry.begin(), listEntry.end(), [sourceID]( const EditorAutosaveEntry& entry )
        { return entry._sourceID == sourceID; } ),
                         listEntry.end() );
    }

    const vector<EditorAutosaveEntry>& EditorAutosaveCommands::getRecoverable()
    {
        return EditorAutosaveCommandsInternal::getState()._listRecoverable;
    }

    uint32 EditorAutosaveCommands::refreshRecoverable()
    {
        EditorAutosaveCommandsInternal::scanRecoverable( EditorAutosaveCommandsInternal::getState()._listRecoverable );
        return static_cast<uint32>( EditorAutosaveCommandsInternal::getState()._listRecoverable.size() );
    }

    bool EditorAutosaveCommands::restore( uint32 index )
    {
        EditorAutosaveCommandsInternal::State& state = EditorAutosaveCommandsInternal::getState();
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
            SW_LOG_ERROR_ID( "EditorAutosaveCommands.DomainNotMounted", "Autosave: the copy folder is not mounted as '%#' - '%#' cannot be opened", kDomain, entry._sourceID );
            return false;
        }
        if ( EditorSceneFileCommands::loadScene( entry._snapshotID ) == false )
            return false; // 이유는 loadScene 이 알렸다
        state._pendingSnapshotID = entry._snapshotID;
        state._pendingSourceID   = entry._sourceID;
        state._pendingGeneration = editor::getSubsystem<SceneManager>().getSceneGeneration();
        state._listRecoverable.erase( state._listRecoverable.begin() + static_cast<ptrdiff_t>( index ) );
        return true;
    }

    void EditorAutosaveCommands::discard( uint32 index )
    {
        vector<EditorAutosaveEntry>& listEntry = EditorAutosaveCommandsInternal::getState()._listRecoverable;
        if ( index >= listEntry.size() )
            return;
        const string slotFolder = FileUtil::getDirectoryPart( listEntry[index]._snapshotAbs );
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

    string EditorAutosaveCommands::makeSlotName( string_view sourceID )
    {
        string slot;
        slot.reserve( sourceID.size() );
        for ( const utf8 ch : sourceID )
        {
            const utf8 lower    = StringUtil::toLowerChar( ch );
            const bool bAllowed = ( lower >= 'a' && lower <= 'z' ) || ( lower >= '0' && lower <= '9' ) || lower == '_' || lower == '-' || lower == '.';
            slot.push_back( bAllowed ? lower : '_' );
        }
        return slot;
    }
} // namespace sw::editor
