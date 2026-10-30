#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Backend/FileDialog.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/IEditorPanelService.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Play/EditorSceneGenerationSync.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw::editor
{
    namespace
    {
        struct EditorSceneFileCommandsInternal
        {
            /// @brief 파일 대화상자의 결과를 받습니다. `FileDialog::pumpResults` 가 메인 스레드에서 호출합니다.
            static void onSaveSceneDialogResult( const vector<string>& listPath )
            {
                if ( listPath.empty() )
                    return;

                // 필수: 대화상자 결과는 에디터 메인 스레드가 돌릴 때만 온다(종료 때는 `cancelFileDialogResults` 가 비운다).
                EditorNotificationManager& notifications = editor::getSubsystem<EditorNotificationManager>();
                if ( EditorSceneFileCommands::saveActiveScene( listPath[0] ) == false )
                {
                    notifications.push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaveFailed" ), NotificationType::Error );
                    return;
                }

                notifications.push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaved" ), NotificationType::Success );
                if ( editor::getSubsystem<EditorWorkspace>().getPendingSceneAction() != EditorPendingSceneAction::None )
                    EditorSceneFileCommands::applyUnsavedSceneChoice( EditorUnsavedChoice::Discard );
            }

            static bool isPlayStoppedForSceneSwap()
            {
                if ( EditorPlaySession::isStopped() )
                    return true;
                editor::getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifyStopPlayFirst" ),
                                                                        NotificationType::Warning );
                return false;
            }

            static void runPendingSceneAction()
            {
                EditorWorkspace&               ws     = editor::getSubsystem<EditorWorkspace>(); // 필수
                const EditorPendingSceneAction action = ws.getPendingSceneAction();
                const string                   path   = ws.getPendingSceneActionPath();
                ws.clearPendingSceneAction();
                // 실패는 각 함수가 알린다. 씬을 읽지 못했거나 플레이 중일 때가 해당한다.
                if ( action == EditorPendingSceneAction::Load )
                    (void)EditorSceneFileCommands::loadScene( path ); // 실패는 loadScene 이 알린다
                else if ( action == EditorPendingSceneAction::New )
                    (void)EditorSceneFileCommands::tryCreateNewScene(); // 실패는 tryCreateNewScene 이 알린다(플레이 중)
                else if ( action == EditorPendingSceneAction::Quit )
                {
                    // 선택(②): 창 호스트는 헤드리스 실행에 없다. 닫을 창이 없으면 종료 요청도 할 일이 없다.
                    MainWindowSubsystem* pWindowHost = editor::findSubsystem<MainWindowSubsystem>();
                    IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                    if ( pWindow != nullptr )
                        pWindow->requestClose();
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorSceneFileCommands" );

    bool EditorSceneFileCommands::loadScene( string_view path )
    {
        // 리소스 id 로 연다. 절대 경로와 id 를 받는다(붙인 디스크 도메인 아래면 그 도메인의 id). 그 밖의 OS 경로는 열 수 없다.
        const string loadPath = editor::getSubsystem<AssetPaths>().toAssetIDFromPathOrID( path );
        if ( loadPath.empty() )
        {
            SW_LOG_ERROR( "Open Scene: '%#' is outside the content root and every mounted disk domain", path );
            return false;
        }

        SceneManager& sceneManager = editor::getSubsystem<SceneManager>(); // 필수

        if ( sceneManager.requestLoadAsync( loadPath, SceneLoadKind::Tool ) == false )
        {
            // 여기서 알린다. 호출하는 쪽이 이 반환값을 읽지 않으므로, 조용히 false 만 반환하면 사용자가 씬을 선택했는데 아무 일도
            // 일어나지 않고 로그에도 남지 않는다.
            SW_LOG_ERROR( "Open Scene: load request failed - %#", loadPath );
            editor::getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySceneOpenFailed" ),
                                                                    NotificationType::Error );
            return false;
        }

        editor::getSubsystem<EditorWorkspace>().clearSelection();

        SW_LOG_INFO( "Open Scene: %#", loadPath );
        return true;
    }

    bool EditorSceneFileCommands::tryOpenScene( string_view path )
    {
        if ( EditorSceneFileCommandsInternal::isPlayStoppedForSceneSwap() == false )
            return false;

        EditorWorkspace& workspace = editor::getSubsystem<EditorWorkspace>(); // 필수
        if ( EditorSessionPolicy::needsUnsavedPrompt( workspace.isSceneDirty() ) )
        {
            workspace.setPendingSceneAction( EditorPendingSceneAction::Load, path );
            return true;
        }
        return loadScene( path );
    }

    bool EditorSceneFileCommands::tryCreateNewScene()
    {
        if ( EditorSceneFileCommandsInternal::isPlayStoppedForSceneSwap() == false )
            return false;

        EditorWorkspace& workspace = editor::getSubsystem<EditorWorkspace>(); // 필수
        if ( EditorSessionPolicy::needsUnsavedPrompt( workspace.isSceneDirty() ) )
        {
            workspace.setPendingSceneAction( EditorPendingSceneAction::New );
            return true;
        }

        Scene* pScene = editor::getSubsystem<SceneManager>().createEmptyActiveScene( "Untitled" );
        if ( pScene == nullptr )
        {
            SW_LOG_ERROR( "New Scene: SceneManager could not create an empty scene" );
            return false;
        }
        syncAfterSceneGenerationChange();
        editor::getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifyNewScene" ), NotificationType::Info );
        return true;
    }

    void EditorSceneFileCommands::saveFocusedOrScene()
    {
        if ( editor::getSubsystem<IEditorPanelService>().saveFocusedDirtyDocument() )
            return;
        saveActiveSceneOrPrompt();
    }

    void EditorSceneFileCommands::applyUnsavedSceneChoice( EditorUnsavedChoice choice )
    {
        EditorWorkspace& ws = editor::getSubsystem<EditorWorkspace>(); // 필수
        if ( ws.getPendingSceneAction() == EditorPendingSceneAction::None )
            return;

        EditorNotificationManager& notifications = editor::getSubsystem<EditorNotificationManager>();
        if ( EditorSessionPolicy::shouldSaveBeforeAction( choice ) )
        {
            if ( editor::getSubsystem<IEditorPanelService>().saveAllDirtyDocuments() == false )
            {
                notifications.push( SW_LOCKEY( "EdCommon", "NotifyEditorTitle" ), SW_LOCKEY( "EdCommon", "NotifyDocumentSaveFailed" ), NotificationType::Error );
                return;
            }
            if ( ws.isSceneDirty() )
            {
                Scene* pScene = editor::getActiveScene();
                if ( pScene != nullptr && pScene->getSourcePath().empty() == false )
                {
                    if ( saveActiveScene( {} ) == false )
                    {
                        notifications.push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaveFailed" ), NotificationType::Error );
                        return;
                    }
                }
                else
                {
                    saveActiveSceneOrPrompt();
                    return;
                }
            }
        }

        if ( EditorSessionPolicy::shouldClearDirtyWithoutSave( choice ) )
        {
            ws.clearSceneDirty();
            editor::getSubsystem<IEditorPanelService>().discardAllDirtyDocuments();
        }

        if ( EditorSessionPolicy::shouldProceedWithAction( choice ) )
            EditorSceneFileCommandsInternal::runPendingSceneAction();
        else
            ws.clearPendingSceneAction();
    }

    void EditorSceneFileCommands::syncAfterSceneGenerationChange()
    {
        // 이전 씬을 가리키던 상태(선택, dirty, Undo, 프리팹 격리)를 버린다. 이유는 `EditorSceneGenerationSync::apply` 에 있다.
        // 스택은 선택 조회(②): `apply` 가 nullptr 스택을 받는다(스택 없는 구성은 되돌릴 기록이 없다).
        (void)EditorSceneGenerationSync::apply( editor::getSubsystem<EditorWorkspace>(), editor::getSubsystem<SceneManager>().getSceneGeneration(), editor::findSubsystem<CommandStack>() );
    }

    bool EditorSceneFileCommands::tryBeginQuit()
    {
        EditorWorkspace& workspace = editor::getSubsystem<EditorWorkspace>(); // 필수 - 창 닫기 질의는 에디터가 떠 있을 때만 온다
        if ( workspace.getPendingSceneAction() != EditorPendingSceneAction::None )
            return false;

        const bool   bSceneDirty = workspace.isSceneDirty();
        const uint32 dirtyDocs   = editor::getSubsystem<IEditorPanelService>().countDirtyDocuments();
        if ( EditorSessionPolicy::needsQuitPrompt( bSceneDirty, dirtyDocs ) == false )
            return true;

        workspace.setPendingSceneAction( EditorPendingSceneAction::Quit );
        return false;
    }

    void EditorSceneFileCommands::requestExit()
    {
        // 선택(②): 창 호스트는 헤드리스 실행에 없다. 닫을 창이 없으면 할 일이 없다.
        MainWindowSubsystem* pWindowHost = editor::findSubsystem<MainWindowSubsystem>();
        IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( pWindow != nullptr )
            (void)pWindow->tryBeginClose(); // 닫기를 거절해도(저장 확인 중) 여기서 할 일이 없다
    }

    bool EditorSceneFileCommands::saveActiveScene( string_view path )
    {
        SceneManager* pSceneManager = &editor::getSubsystem<SceneManager>(); // 필수

        // 버전 관리가 읽기 전용으로 둔 파일(git LFS lockable, 잠그기 전)에는 쓰지 않고 이유를 알린다. 쓰기 실패 로그만으로는 "잠가야 한다" 는 사실이 드러나지 않는다.
        Scene*       pScene     = pSceneManager->getActiveScene();
        const string targetPath = path.empty() && pScene != nullptr ? pScene->getSourcePath() : string{ path };
        const string targetAbs  = FileUtil::isAbsolutePath( targetPath ) ? targetPath : editor::getSubsystem<AssetPaths>().findLooseFile( targetPath ).getDiskPath();
        if ( targetPath.empty() == false && FileUtil::isReadOnlyFile( targetAbs ) )
        {
            editor::getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySaveTitle" ), SW_LOCKEY( "EdCommon", "NotifySceneReadOnly" ),
                                                                    NotificationType::Warning );
            SW_LOG_WARNING( "Scene '%#' is read-only (not checked out) - not saved", targetAbs.c_str() );
            return false;
        }

        if ( pSceneManager->saveActiveScene( path ) == false )
        {
            SW_LOG_ERROR( "Save Scene: SceneManager could not save '%#'", targetPath.c_str() );
            return false;
        }
        editor::getSubsystem<EditorWorkspace>().clearSceneDirty();
        return true;
    }

    void EditorSceneFileCommands::saveActiveSceneOrPrompt()
    {
        Scene* pScene = editor::getActiveScene();
        if ( pScene != nullptr && pScene->getSourcePath().empty() == false )
        {
            if ( saveActiveScene( {} ) )
                editor::getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaved" ), NotificationType::Success );
            else
                editor::getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaveFailed" ), NotificationType::Error );
            return;
        }

        FileDialogParams params{};
        params._type               = FileDialogParams::Type::Save;
        params._title              = "Save Scene";
        params._description        = "Scene";
        params._bEnableMultiselect = false;
        // 씬 이름은 쿠커의 규칙 하나를 따른다(`EditorAssetTypeRegistry` → `AssetCookPath`). 맨 `.xml` 은 쿠커가 쿠킹하지 않는 이름이다.
        EditorAssetTypeRegistry::appendSuffixes( EditorAssetType{ SceneDocument::kAssetType }, params._listFilterExtension );
        const string mapsDir = editor::getSubsystem<AssetPaths>().findLooseDomainFolder( editor::getSubsystem<const GameConfig>()._packRoot.get(), path::kMapsFolder ).getDiskPath();
        if ( FileUtil::isDirectory( mapsDir ) )
            params._initialDirectory = mapsDir;
        FileDialog::open( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, EditorSceneFileCommandsInternal::onSaveSceneDialogResult ) );
    }
} // namespace sw::editor
