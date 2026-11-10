#include "pch.h"

#include "Editor/Common/Commands/Play/EditorPlayCommands.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/EngineChildProcess.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/IEditorPanelService.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        struct EditorPlayCommandsInternal
        {
            static bool isSessionRunning() { return EditorPlaySession::isStopped() == false; }

            static bool canKeepChanges()
            {
                return isSessionRunning() && editor::getMainSceneSelection().getSelectedObjectCount() > 0;
            }

            static void runToggleEject() { EditorPlayCommands::toggleEject(); }

            // 씬은 Stop 이 남긴 상태를 적용할 때 바뀐다(`applyKeptStates` 도 거래를 받는다). 이 호출은 상태를 모을 뿐이지만 같은 형식을 따른다.
            static void runKeepChanges( EditorTransaction& /*transaction*/ )
            {
                const uint32   keptCount     = EditorPlayCommands::keepSelectedChanges();
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                if ( keptCount > 0 )
                    editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyKeepSimulationTitle" ), SW_LOCKEY( "EdCommon", "NotifyKeepSimulation" ), NotificationKind::Info );
            }

            static void runLaunchStandalone() { (void)EditorPlayCommands::launchStandalone(); } // 실패는 알림 · 로그가 알린다
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_COMMAND( PlayEject, "play.eject", &EditorPlayCommandsInternal::runToggleEject, &EditorPlayCommandsInternal::isSessionRunning, NoScene );
    SW_EDITOR_COMMAND( PlayKeepChanges, "play.keepChanges", &EditorPlayCommandsInternal::runKeepChanges, &EditorPlayCommandsInternal::canKeepChanges, SceneDuringPlay );
    SW_EDITOR_COMMAND( PlayStandalone, "play.standalone", &EditorPlayCommandsInternal::runLaunchStandalone, nullptr, NoScene );

    void EditorPlayCommands::update()
    {
        PlaySessionData* pData         = EditorPlaySession::findData();
        EditorContext&   editorContext = editor::getSubsystem<EditorContext>();
        if ( pData == nullptr )
            return;
        if ( pData->_state == PlaySessionState::Stopped )
        {
            pData->_bOptionsApplied = SW_FALSE;
            pData->_bEjected        = SW_FALSE;
            if ( pData->_bMaximizedForPlay == SW_TRUE )
            {
                pData->_bMaximizedForPlay = SW_FALSE;
                if ( editorContext.getSubsystem<IEditorPanelService>().restoreMaximizedLayout() == false )
                    SW_LOG_WARNING_ID( "EditorPlay.LayoutBeforeMaximizePlay", "The layout before Maximize On Play is gone - use Panel > Reset Layout" );
            }
            return;
        }
        if ( pData->_bOptionsApplied == SW_TRUE )
            return;
        pData->_bOptionsApplied = SW_TRUE;
        // Simulate 는 씬 뷰로 지켜보는 세션이라 게임 뷰를 키우지 않는다(유니티 Maximize On Play 도 게임 뷰의 플레이에만).
        if ( getPreferences<EditorPlayPreferences>()._bMaximizeOnPlay && pData->_bSimulate == SW_FALSE )
            pData->_bMaximizedForPlay = editorContext.getSubsystem<IEditorPanelService>().maximizePanel( editorpanel::kGameView ) ? SW_TRUE : SW_FALSE;
    }

    void EditorPlayCommands::toggleEject()
    {
        EditorContext&   editorContext        = editor::getSubsystem<EditorContext>();
        PlaySessionData* pData                = EditorPlaySession::findData();
        const bool       bNoActivePlaySession = pData == nullptr || pData->_state == PlaySessionState::Stopped;
        if ( bNoActivePlaySession )
            return;
        if ( EditorPlaySession::isPlayerActive( *pData ) )
        {
            // 조종을 놓는다 — 월드는 계속 돌고(Simulate) 에디터 카메라로 둘러본다. 최대화 중이면 씬 뷰가 닫혀 있으니 배치를 먼저 되돌린다.
            if ( pData->_bMaximizedForPlay == SW_TRUE )
            {
                pData->_bMaximizedForPlay = SW_FALSE;
                (void)editorContext.getSubsystem<IEditorPanelService>().restoreMaximizedLayout(); // 없으면 지금 배치 그대로 씬 뷰를 연다
            }
            EditorPlaySession::simulate();
            pData->_bEjected = SW_TRUE;
            (void)editorContext.getSubsystem<IEditorPanelService>().focusPanel( editorpanel::kSceneView ); // 패널이 없으면 세션만 바뀐다
            return;
        }
        if ( pData->_bEjected == SW_TRUE )
        {
            EditorPlaySession::play();
            pData->_bEjected = SW_FALSE;
            (void)editorContext.getSubsystem<IEditorPanelService>().focusPanel( editorpanel::kGameView ); // 패널이 없으면 세션만 바뀐다
        }
    }

    uint32 EditorPlayCommands::keepSelectedChanges()
    {
        // 플레이 세션은 메인 월드에서 돈다 — 남길 변경은 메인 씬의 선택이다.
        PlaySessionData* pData                = EditorPlaySession::findData();
        const bool       bNoActivePlaySession = pData == nullptr || pData->_state == PlaySessionState::Stopped;
        if ( bNoActivePlaySession )
            return 0;
        vector<GameObject*> listSelected;
        editor::getMainSceneSelection().getSelectedObjects( listSelected );
        uint32 keptCount{ 0 };
        for ( const GameObject* pObject : listSelected )
        {
            keptCount += EditorPlaySession::keepObjectState( *pData, pObject ) ? 1u : 0u;
        }
        return keptCount;
    }

    bool EditorPlayCommands::launchStandalone()
    {
        EditorContext& editorContext  = editor::getSubsystem<EditorContext>();
        const Scene*   pScene         = editor::getActiveScene();
        const bool     bSceneNotSaved = pScene == nullptr || pScene->getSourcePath().empty();
        if ( bSceneNotSaved )
        {
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPlayNewWindowTitle" ), SW_LOCKEY( "EdCommon", "NotifyPlayNewWindowSaveFirst" ), NotificationKind::Warning );
            return false;
        }
        // 새 창은 디스크의 씬을 연다(언리얼 Standalone 처럼) — 저장하지 않은 편집은 들어가지 않는다고 알린다.
        if ( editorContext.getSubsystem<EditorWorkspace>().isSceneDirty() )
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPlayNewWindowTitle" ), SW_LOCKEY( "EdCommon", "NotifyPlayNewWindowUnsaved" ), NotificationKind::Warning );
        const ProcessCommand command = makeStandaloneCommand( editor::getSubsystem<CommandLineManager>(), FileUtil::getExecutablePath(), pScene->getSourcePath(),
                                                              getPreferences<EditorPlayPreferences>()._standaloneArguments );
        if ( Process::launchDetached( command ) == false )
        {
            SW_LOG_WARNING_ID( "EditorPlay.FailedLaunchStandaloneGame", "Failed to launch the standalone game: %#", command.toCommandLine() );
            return false;
        }
        SW_LOG_INFO_ID( "EditorPlay.StandaloneGameLaunched", "Standalone game launched: %#", command.toCommandLine() );
        return true;
    }

    ProcessCommand EditorPlayCommands::makeStandaloneCommand( const CommandLineManager& parent, string_view executablePath, string_view scenePath, string_view extraArguments )
    {
        vector<string>       listArgument{ "-gv_firstScene=" + string( scenePath ) };
        const vector<string> listExtraArgument = StringUtil::splitQuotedTokens( extraArguments );
        listArgument.insert( listArgument.end(), listExtraArgument.begin(), listExtraArgument.end() );
        // 자동화 실행이 띄운 새 창은 사람의 설정 · 세이브를 건드리지 않는다. 경로가 절대 경로가 아니면 빈 명령 - 띄우기가 실패로 알린다
        return EngineChildProcess::makeCommand( parent, executablePath, std::move( listArgument ) );
    }
} // namespace sw::editor

namespace sw::editor
{
    // 콘솔 명령 — 시나리오 · 콘솔 입력 줄이 부른다(`DevCommandRegistry`). 그 기능 옆에 둔다(언리얼 Exec 명령이 그 시스템 파일에 있는 것과 같다).
    namespace
    {
        struct EditorPlayConsoleInternal
        {
            static bool runPlay( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                EditorPlaySession::play();
                outReply = "play";
                return true;
            }

            static bool runSimulate( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                EditorPlaySession::simulate();
                outReply = "simulate";
                return true;
            }

            static bool runPause( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                EditorPlaySession::pause();
                outReply = "pause";
                return true;
            }

            static bool runStop( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                EditorPlaySession::stop();
                outReply = "stop";
                return true;
            }

            static bool runStep( const vector<string>& listArgument, string& outReply )
            {
                uint32     frameCount            = 1;
                const bool bInvalidStepArguments = listArgument.size() > 1 || ( listArgument.size() == 1 && DevCommandRegistry::readCountArgument( listArgument[0], frameCount ) == false ) || frameCount == 0;
                if ( bInvalidStepArguments )
                    return false;
                EditorPlaySession::stepFrames( frameCount );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "step %#", frameCount );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( Play, "play", "play", "Start (or switch to) play-in-editor", &EditorPlayConsoleInternal::runPlay );
    SW_DEV_COMMAND( Simulate, "simulate", "simulate", "Start (or switch to) simulate - world only, no player or game input",
                    &EditorPlayConsoleInternal::runSimulate );
    SW_DEV_COMMAND( Pause, "pause", "pause", "Pause the play session", &EditorPlayConsoleInternal::runPause );
    SW_DEV_COMMAND( Stop, "stop", "stop", "Stop the play session and restore the edited scene", &EditorPlayConsoleInternal::runStop );
    SW_DEV_COMMAND( Step, "step", "step [frames]", "Advance the play session by N frames, then pause", &EditorPlayConsoleInternal::runStep );
} // namespace sw::editor
