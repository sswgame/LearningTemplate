#include "pch.h"

#include "Editor/Common/Commands/Play/EditorPlayCommands.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/EditorUICommandQueue.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/IEditorPanelSubsystem.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    /**
     * @class EditorPlayLayoutState
     * @brief 플레이가 바꾼 배치의 UI 쪽 상태입니다(`EditorPlayCommands::update`). 에디터 서브시스템이라 컨텍스트와 함께 생기고 사라진다.
     * @details 예전에는 세션 상태(`PlaySessionData`)에 있어 게임 스레드가 도킹 배치를 고쳤다. 배치는 UI 것이라 UI 가 가진다.
     */
    class EditorPlayLayoutState final : public EditorSubsystem
    {
    public:
        uint8 _bOptionsApplied   : 1; ///< 이 세션의 플레이 옵션(최대화)을 적용했다
        uint8 _bMaximizedForPlay : 1; ///< Maximize On Play 가 게임 뷰를 최대화했다. 멈추거나 Eject 하면 배치를 되돌린다
        uint8 _bSeenEjected      : 1; ///< 지난 UI 프레임에 본 Eject 상태(전이를 찾는다)

        static constexpr const utf8* kSubsystemName  = "EditorPlayLayoutState";
        static constexpr int32       kSubsystemOrder = 9610; ///< 창 제목 상태(9600) 다음 — 서로 의존하지 않는다
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        void                         resetToFreshEditor() override
        {
            _bOptionsApplied   = SW_FALSE;
            _bMaximizedForPlay = SW_FALSE;
            _bSeenEjected      = SW_FALSE;
        }

        EditorPlayLayoutState()
            : _bOptionsApplied{ SW_FALSE }
            , _bMaximizedForPlay{ SW_FALSE }
            , _bSeenEjected{ SW_FALSE }
        {
        }
    };

    SW_EDITOR_SUBSYSTEM( EditorPlayLayoutState, EditorPlayLayoutState::kSubsystemOrder );

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

            // 씬은 Stop 이 남긴 상태를 적용할 때 바뀐다(`applyKeptStates` 도 거래를 받는다). 이 호출은 상태를 모으라고 보낼 뿐이지만 같은 형식을 따른다.
            static void runKeepChanges( EditorTransaction& /*transaction*/ ) { EditorPlayCommands::keepSelectedChanges(); }

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
        EditorContext* const         pContext = editor::findSubsystem<EditorContext>();
        EditorPlayLayoutState* const pLayout  = editor::findSubsystem<EditorPlayLayoutState>(); // 이 파일의 SW_EDITOR_SUBSYSTEM 이 컨텍스트와 함께 만든다
        if ( pContext == nullptr || pLayout == nullptr )
            return;
        EditorPlayLayoutState&        layout = *pLayout;
        IEditorPanelSubsystem&        panels = pContext->getSubsystem<IEditorPanelSubsystem>();
        const EditorPlayStateSnapshot play   = EditorPlaySession::getUIState(); // UI 쪽 — 게임 스레드가 넘긴 세션 전이를 본다
        if ( play.isStopped() )
        {
            layout._bOptionsApplied = SW_FALSE;
            layout._bSeenEjected    = SW_FALSE;
            if ( layout._bMaximizedForPlay == SW_TRUE )
            {
                layout._bMaximizedForPlay = SW_FALSE;
                if ( panels.restoreMaximizedLayout() == false )
                    SW_LOG_WARNING_ID( "EditorPlay.LayoutBeforeMaximizePlay", "The layout before Maximize On Play is gone - use Panel > Reset Layout" );
            }
            return;
        }
        if ( layout._bOptionsApplied == SW_FALSE )
        {
            layout._bOptionsApplied = SW_TRUE;
            // Simulate 는 씬 뷰로 지켜보는 세션이라 게임 뷰를 키우지 않는다(유니티 Maximize On Play 도 게임 뷰의 플레이에만).
            if ( getPreferences<EditorPlayPreferences>()._bMaximizeOnPlay && play.isSimulating() == false )
                layout._bMaximizedForPlay = panels.maximizePanel( editorpanel::kGameView ) ? SW_TRUE : SW_FALSE;
        }
        // Eject/Possess 전이 — 세션은 게임 스레드가 바꿨고(`EditorUICommandKind::ToggleEject`), 배치 · 포커스는 여기서 맞춘다.
        const bool bEjected = play.isEjected();
        if ( bEjected == ( layout._bSeenEjected == SW_TRUE ) )
            return;
        layout._bSeenEjected = bEjected ? SW_TRUE : SW_FALSE;
        if ( bEjected )
        {
            // 최대화 중이면 씬 뷰가 닫혀 있으니 배치를 먼저 되돌린다.
            if ( layout._bMaximizedForPlay == SW_TRUE )
            {
                layout._bMaximizedForPlay = SW_FALSE;
                (void)panels.restoreMaximizedLayout(); // 없으면 지금 배치 그대로 씬 뷰를 연다
            }
            (void)panels.focusPanel( editorpanel::kSceneView ); // 패널이 없으면 세션만 바뀐다
            return;
        }
        (void)panels.focusPanel( editorpanel::kGameView ); // 패널이 없으면 세션만 바뀐다
    }

    void EditorPlayCommands::toggleEject()
    {
        EditorUICommandQueue::postToEditor( EditorUICommand::make( EditorUICommandKind::ToggleEject ) );
    }

    void EditorPlayCommands::keepSelectedChanges()
    {
        EditorUICommandQueue::postToEditor( EditorUICommand::make( EditorUICommandKind::KeepSelectedChanges ) );
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
        if ( Process::launchDetached( command ).has_value() == false )
        {
            SW_LOG_WARNING_ID( "EditorPlay.FailedLaunchStandaloneGame", "Failed to launch the standalone game: %#", command.toCommandLine() );
            return false;
        }
        SW_LOG_INFO_ID( "EditorPlay.StandaloneGameLaunched", "Standalone game launched: %#", command.toCommandLine() );
        return true;
    }

    ProcessCommand EditorPlayCommands::makeStandaloneCommand( const CommandLineManager& parent, const AbsolutePath& executablePath, string_view scenePath, string_view extraArguments )
    {
        StringBuilder<constant::buffer::kMax256> sceneArgument;
        sceneArgument.appendFormat( "-gv_firstScene=%#", scenePath );
        vector<string>       listArgument{ string( sceneArgument.view() ) };
        const vector<string> listExtraArgument = StringUtil::splitQuotedTokens( extraArguments );
        listArgument.insert( listArgument.end(), listExtraArgument.begin(), listExtraArgument.end() );
        // 자동화 실행이 띄운 새 창은 사람의 설정 · 세이브를 건드리지 않는다. 경로가 절대 경로가 아니면 빈 명령 - 띄우기가 실패로 알린다
        return ProcessCommand::forEngineChild( parent, executablePath, std::move( listArgument ) );
    }
} // namespace sw::editor

namespace sw::editor
{
    // 콘솔 명령 — 시나리오 · 콘솔 입력 줄이 부른다(`DevCommandRegistry`). 그 기능 옆에 둔다(언리얼 Exec 명령이 그 시스템 파일에 있는 것과 같다).
    // 콘솔은 게임 스레드에서 돈다 — 플레이 세션 열쇠(`EditorPlaySessionWriteKey`)를 만드는 입구라 이름 있는 형식이다(열쇠가 친구로 부른다).
    /** @struct EditorPlayConsoleCommands @brief 플레이 콘솔 명령(play · simulate · pause · stop · step)의 본문입니다. */
    struct EditorPlayConsoleCommands
    {
        static bool runPlay( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            EditorPlaySession::play( EditorPlaySessionWriteKey{} );
            outReply = "play";
            return true;
        }

        static bool runSimulate( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            EditorPlaySession::simulate( EditorPlaySessionWriteKey{} );
            outReply = "simulate";
            return true;
        }

        static bool runPause( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            EditorPlaySession::pause( EditorPlaySessionWriteKey{} );
            outReply = "pause";
            return true;
        }

        static bool runStop( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            EditorPlaySession::stop( EditorPlaySessionWriteKey{} );
            outReply = "stop";
            return true;
        }

        static bool runStep( const vector<string>& listArgument, string& outReply )
        {
            uint32     frameCount            = 1;
            const bool bInvalidStepArguments = listArgument.size() > 1 || ( listArgument.size() == 1 && DevCommandRegistry::readCountArgument( listArgument[0], frameCount ) == false ) || frameCount == 0;
            if ( bInvalidStepArguments )
                return false;
            EditorPlaySession::stepFrames( EditorPlaySessionWriteKey{}, frameCount );
            StringBuilder<constant::buffer::kMax256> replyBuilder;
            replyBuilder.appendFormat( "step %#", frameCount );
            outReply = string( replyBuilder.view() );
            return true;
        }
    };

    SW_DEV_COMMAND( Play, "play", "play", "Start (or switch to) play-in-editor", &EditorPlayConsoleCommands::runPlay );
    SW_DEV_COMMAND( Simulate, "simulate", "simulate", "Start (or switch to) simulate - world only, no player or game input",
                    &EditorPlayConsoleCommands::runSimulate );
    SW_DEV_COMMAND( Pause, "pause", "pause", "Pause the play session", &EditorPlayConsoleCommands::runPause );
    SW_DEV_COMMAND( Stop, "stop", "stop", "Stop the play session and restore the edited scene", &EditorPlayConsoleCommands::runStop );
    SW_DEV_COMMAND( Step, "step", "step [frames]", "Advance the play session by N frames, then pause", &EditorPlayConsoleCommands::runStep );
} // namespace sw::editor
