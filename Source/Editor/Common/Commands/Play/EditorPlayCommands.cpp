#include "pch.h"

#include "Editor/Common/Commands/Play/EditorPlayCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/IEditorPanelService.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

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

            static void runKeepChanges()
            {
                const uint32   keptCount     = EditorPlayCommands::keepSelectedChanges();
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                if ( keptCount > 0 )
                    editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyKeepSimulationTitle" ), SW_LOCKEY( "EdCommon", "NotifyKeepSimulation" ), NotificationType::Info );
            }

            static void runLaunchStandalone() { (void)EditorPlayCommands::launchStandalone(); } // 실패는 알림 · 로그가 알린다
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorPlay" );

    SW_EDITOR_COMMAND( PlayEject, "play.eject", 4100, "Eject / Possess", editoricon::kController, "Play",
                       "플레이 중 플레이어 조종을 놓고 씬 뷰 에디터 카메라로 빠져나옵니다(다시 누르면 조종으로 돌아갑니다)",
                       "Leave the player for the scene view while playing, or take it back", "play.eject",
                       &EditorPlayCommandsInternal::runToggleEject, &EditorPlayCommandsInternal::isSessionRunning, NoScene, "MainMenu/Play", None, 0 );
    SW_EDITOR_COMMAND( PlayKeepChanges, "play.keepChanges", 4110, "Keep Simulation Changes", editoricon::kBookmark, "Play",
                       "고른 오브젝트의 지금(플레이 중) 상태를 Stop 뒤 편집 씬에 남깁니다(되돌리기 한 번)",
                       "Keep the selected objects' play state after Stop", nullptr, &EditorPlayCommandsInternal::runKeepChanges, &EditorPlayCommandsInternal::canKeepChanges, NoScene,
                       "MainMenu/Play", None, 0 );
    SW_EDITOR_COMMAND( PlayStandalone, "play.standalone", 4120, "Play in New Window", editoricon::kLayout, "Play",
                       "저장된 활성 씬을 에디터 없이 새 창(별도 프로세스)으로 띄웁니다",
                       "Run the saved active scene in a separate game process", nullptr, &EditorPlayCommandsInternal::runLaunchStandalone, nullptr, NoScene, "MainMenu/Play", None, 0 );

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
                    SW_LOG_WARNING( "The layout before Maximize On Play is gone - use Panel > Reset Layout" );
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
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPlayNewWindowTitle" ), SW_LOCKEY( "EdCommon", "NotifyPlayNewWindowSaveFirst" ), NotificationType::Warning );
            return false;
        }
        // 새 창은 디스크의 씬을 연다(언리얼 Standalone 처럼) — 저장하지 않은 편집은 들어가지 않는다고 알린다.
        if ( editorContext.getSubsystem<EditorWorkspace>().isSceneDirty() )
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPlayNewWindowTitle" ), SW_LOCKEY( "EdCommon", "NotifyPlayNewWindowUnsaved" ), NotificationType::Warning );
        const ProcessCommand command =
            makeStandaloneCommand( FileUtil::getExecutablePath(), pScene->getSourcePath(), getPreferences<EditorPlayPreferences>()._standaloneArguments );
        if ( Process::launchDetached( command ) == false )
        {
            SW_LOG_WARNING( "Failed to launch the standalone game: %#", command.toCommandLine() );
            return false;
        }
        SW_LOG_INFO( "Standalone game launched: %#", command.toCommandLine() );
        return true;
    }

    ProcessCommand EditorPlayCommands::makeStandaloneCommand( string_view executablePath, string_view scenePath, string_view extraArguments )
    {
        const std::optional<AbsolutePath> executable = AbsolutePath::tryMake( executablePath );
        if ( executable.has_value() == false )
            return {}; // 빈 명령 - 띄우기가 실패로 알린다
        ProcessCommand       command           = ProcessCommand::at( *executable, { "-gv_firstScene=" + string( scenePath ) } );
        const vector<string> listExtraArgument = ProcessCommand::splitArgumentLine( extraArguments );
        command._listArgument.insert( command._listArgument.end(), listExtraArgument.begin(), listExtraArgument.end() );
        return command;
    }
} // namespace sw::editor
