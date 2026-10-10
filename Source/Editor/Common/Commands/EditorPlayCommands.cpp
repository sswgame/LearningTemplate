#include "pch.h"

#include "Editor/Common/Commands/EditorPlayCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/GUI/EditorDockLayout.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        struct EditorPlayCommandsInternal
        {
            /** @brief 패널을 열고 그 창(탭)을 앞으로 둡니다. */
            static void focusPanel( EditorContext& context, string_view panelID )
            {
                const IEditorPanel* pPanel = context.getPanelManager().findPanel( panelID );
                if ( pPanel != nullptr )
                    context.getWorkspace().requestOpenPanel( pPanel->getPanelTitle() );
            }

            static bool isSessionRunning() { return EditorPlaySession::isStopped() == false; }

            static bool canKeepChanges()
            {
                EditorContext* pContext = EditorContext::get();
                return isSessionRunning() && pContext != nullptr && pContext->getEditorSelection().getSelectedObjectCount() > 0;
            }

            static void runToggleEject() { EditorPlayCommands::toggleEject(); }

            static void runKeepChanges()
            {
                const uint32   keptCount = EditorPlayCommands::keepSelectedChanges();
                EditorContext* pContext  = EditorContext::get();
                if ( pContext != nullptr && keptCount > 0 )
                    pContext->getNotificationManager().push( "Keep Simulation Changes", "The selected objects keep their state after Stop", NotificationType::Info );
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
                       "Leave the player for the scene view while playing, or take it back", ( EditorCommandShortcut{ EditorCommandKey::F8, commandmodifier::kNone } ),
                       &EditorPlayCommandsInternal::runToggleEject, &EditorPlayCommandsInternal::isSessionRunning, "MainMenu/Play" );
    SW_EDITOR_COMMAND( PlayKeepChanges, "play.keepChanges", 4110, "Keep Simulation Changes", editoricon::kBookmark, "Play",
                       "고른 오브젝트의 지금(플레이 중) 상태를 Stop 뒤 편집 씬에 남깁니다(되돌리기 한 번)",
                       "Keep the selected objects' play state after Stop", {}, &EditorPlayCommandsInternal::runKeepChanges, &EditorPlayCommandsInternal::canKeepChanges,
                       "MainMenu/Play" );
    SW_EDITOR_COMMAND( PlayStandalone, "play.standalone", 4120, "Play in New Window", editoricon::kLayout, "Play",
                       "저장된 활성 씬을 에디터 없이 새 창(별도 프로세스)으로 띄웁니다",
                       "Run the saved active scene in a separate game process", {}, &EditorPlayCommandsInternal::runLaunchStandalone, nullptr, "MainMenu/Play" );

    bool EditorPlayCommands::maximizePanel( string_view keepPanelID )
    {
        EditorContext*    pContext = EditorContext::get();
        EditorDockLayout* pDock    = pContext != nullptr ? pContext->findDockLayout() : nullptr;
        if ( pDock == nullptr )
            return false;
        // 직전 배치는 에디터 상태 폴더의 임시 레이아웃으로 둔다(이름 붙인 레이아웃 목록에 섞이지 않게).
        const string folder = FileUtil::joinPath( EditorUtil::getEditorStateDirectory(), "Temp" );
        if ( pDock->saveNamedLayout( kMaximizeLayoutName, folder ) == false )
            return false;
        EditorPanelManager& panelManager = pContext->getPanelManager();
        vector<string>      listCloseID;
        for ( const EditorPanelEntry& entry : panelManager.getPanels() )
        {
            if ( entry._id != keepPanelID && entry._pInstance != nullptr && entry._pInstance->isOpen() )
                listCloseID.push_back( entry._id );
        }
        for ( const string& panelID : listCloseID )
        {
            (void)panelManager.setPanelOpen( panelID, false ); // 이미 닫혔으면 할 일이 없다
        }
        EditorPlayCommandsInternal::focusPanel( *pContext, keepPanelID );
        return true;
    }

    bool EditorPlayCommands::restoreMaximizedLayout()
    {
        EditorContext*    pContext = EditorContext::get();
        EditorDockLayout* pDock    = pContext != nullptr ? pContext->findDockLayout() : nullptr;
        if ( pDock == nullptr )
            return false;
        const string folder = FileUtil::joinPath( EditorUtil::getEditorStateDirectory(), "Temp" );
        return pDock->requestLoadNamedLayout( kMaximizeLayoutName, folder );
    }

    void EditorPlayCommands::tick()
    {
        PlaySessionData* pData = EditorPlaySession::findData();
        if ( pData == nullptr )
            return;
        if ( pData->_state == PlaySessionState::Stopped )
        {
            pData->_bOptionsApplied = SW_FALSE;
            pData->_bEjected        = SW_FALSE;
            if ( pData->_bMaximizedForPlay == SW_TRUE )
            {
                pData->_bMaximizedForPlay = SW_FALSE;
                if ( restoreMaximizedLayout() == false )
                    SW_LOG_WARNING( "The layout before Maximize On Play is gone - use Panel > Reset Layout" );
            }
            return;
        }
        if ( pData->_bOptionsApplied == SW_TRUE )
            return;
        pData->_bOptionsApplied = SW_TRUE;
        // Simulate 는 씬 뷰로 지켜보는 세션이라 게임 뷰를 키우지 않는다(유니티 Maximize On Play 도 게임 뷰의 플레이에만).
        if ( getPreferences<EditorPlayPreferences>()._bMaximizeOnPlay && pData->_bSimulate == SW_FALSE )
            pData->_bMaximizedForPlay = maximizePanel( "game_view" ) ? SW_TRUE : SW_FALSE;
    }

    void EditorPlayCommands::toggleEject()
    {
        EditorContext*   pContext = EditorContext::get();
        PlaySessionData* pData    = EditorPlaySession::findData();
        if ( pContext == nullptr || pData == nullptr || pData->_state == PlaySessionState::Stopped )
            return;
        if ( EditorPlaySession::isPlayerActive( *pData ) )
        {
            // 조종을 놓는다 — 월드는 계속 돌고(Simulate) 에디터 카메라로 둘러본다. 최대화 중이면 씬 뷰가 닫혀 있으니 배치를 먼저 되돌린다.
            if ( pData->_bMaximizedForPlay == SW_TRUE )
            {
                pData->_bMaximizedForPlay = SW_FALSE;
                (void)restoreMaximizedLayout(); // 없으면 지금 배치 그대로 씬 뷰를 연다
            }
            EditorPlaySession::simulate();
            pData->_bEjected = SW_TRUE;
            EditorPlayCommandsInternal::focusPanel( *pContext, "scene_view" );
            return;
        }
        if ( pData->_bEjected == SW_TRUE )
        {
            EditorPlaySession::play();
            pData->_bEjected = SW_FALSE;
            EditorPlayCommandsInternal::focusPanel( *pContext, "game_view" );
        }
    }

    uint32 EditorPlayCommands::keepSelectedChanges()
    {
        EditorContext*   pContext = EditorContext::get();
        PlaySessionData* pData    = EditorPlaySession::findData();
        if ( pContext == nullptr || pData == nullptr || pData->_state == PlaySessionState::Stopped )
            return 0;
        vector<GameObject*> listSelected;
        pContext->getEditorSelection().getSelectedObjects( listSelected );
        uint32 keptCount{ 0 };
        for ( const GameObject* pObject : listSelected )
        {
            keptCount += EditorPlaySession::keepObjectState( *pData, pObject ) ? 1u : 0u;
        }
        return keptCount;
    }

    bool EditorPlayCommands::launchStandalone()
    {
        EditorContext* pContext = EditorContext::get();
        const Scene*   pScene   = editor::getActiveScene();
        if ( pContext == nullptr || pScene == nullptr || pScene->getSourcePath().empty() )
        {
            if ( pContext != nullptr )
                pContext->getNotificationManager().push( "Play in New Window", "Save the scene first - the new window opens the saved file", NotificationType::Warning );
            return false;
        }
        // 새 창은 디스크의 씬을 연다(언리얼 Standalone 처럼) — 저장하지 않은 편집은 들어가지 않는다고 알린다.
        if ( pContext->getWorkspace().isSceneDirty() )
            pContext->getNotificationManager().push( "Play in New Window", "Unsaved scene edits are not in the new window", NotificationType::Warning );
        const string command = makeStandaloneCommand( FileUtil::getExecutablePath(), pScene->getSourcePath(), getPreferences<EditorPlayPreferences>()._standaloneArguments );
        if ( Process::launchDetached( command ) == false )
        {
            SW_LOG_WARNING( "Failed to launch the standalone game: %#", command.c_str() );
            return false;
        }
        SW_LOG_INFO( "Standalone game launched: %#", command.c_str() );
        return true;
    }

    string EditorPlayCommands::makeStandaloneCommand( string_view executablePath, string_view scenePath, string_view extraArguments )
    {
        string command = "\"";
        command += executablePath;
        command += "\" \"-gv_firstScene=";
        command += scenePath;
        command += "\"";
        if ( extraArguments.empty() == false )
        {
            command += " ";
            command += extraArguments;
        }
        return command;
    }
} // namespace sw::editor
