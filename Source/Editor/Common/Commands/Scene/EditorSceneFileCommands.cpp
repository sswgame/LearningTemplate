#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Asset/EditorAssetValidation.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSceneGenerationSync.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/IEditorPanelHost.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Window/IWindow.h"

namespace sw::editor
{
    namespace
    {
        struct EditorSceneFileCommandsInternal
        {
            /// @brief 파일 대화상자의 결과를 받습니다. `FileUtil::pumpFileDialogResults` 가 메인 스레드에서 호출합니다.
            static void onSaveSceneDialogResult( const vector<string>& listPath )
            {
                if ( listPath.empty() )
                    return;

                // 컨텍스트는 한 번 받아 검사하고, 그 값만 쓴다.
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;

                if ( EditorSceneFileCommands::saveActiveScene( listPath[0] ) == false )
                {
                    pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaveFailed" ), NotificationType::Error );
                    return;
                }

                pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaved" ), NotificationType::Success );
                if ( pContext->getSubsystem<EditorWorkspace>().getPendingSceneAction() != EditorPendingSceneAction::None )
                    EditorSceneFileCommands::applyUnsavedSceneChoice( EditorUnsavedChoice::Discard );
            }

            static bool isPlayStoppedForSceneSwap()
            {
                if ( EditorPlaySession::isStopped() )
                    return true;
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifyStopPlayFirst" ),
                                                                              NotificationType::Warning );
                return false;
            }

            static void runPendingSceneAction()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;
                EditorWorkspace&               ws     = pContext->getSubsystem<EditorWorkspace>();
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
                    IWindow* pWindow = IWindow::getActiveWindow();
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
        // 리소스 트리 안이면 리소스 id 로 연다. 절대 경로, 프로젝트 기준 경로, id 모두 받는다. 밖이면 받은 경로를 그대로 쓴다.
        string loadPath = ResourcePaths::toResourceID( path );
        if ( loadPath.empty() )
            loadPath = string{ path };

        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
        {
            SW_LOG_ERROR( "Open Scene: SceneManager unavailable" );
            return false;
        }

        if ( pSceneManager->requestLoadAsync( loadPath ) == false )
        {
            // 여기서 알린다. 호출하는 쪽이 이 반환값을 읽지 않으므로, 조용히 false 만 반환하면 사용자가 씬을 선택했는데 아무 일도
            // 일어나지 않고 로그에도 남지 않는다.
            SW_LOG_ERROR( "Open Scene: 로드 요청 실패 — %#", loadPath );
            EditorContext* pFailContext = EditorContext::get();
            if ( pFailContext != nullptr )
            {
                pFailContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySceneOpenFailed" ),
                                                                              NotificationType::Error );
            }
            return false;
        }

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            pContext->getSubsystem<EditorWorkspace>().clearSelection();

        SW_LOG_INFO( "Open Scene: %#", loadPath );
        return true;
    }

    bool EditorSceneFileCommands::tryOpenScene( string_view path )
    {
        if ( EditorSceneFileCommandsInternal::isPlayStoppedForSceneSwap() == false )
            return false;

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr && EditorSessionPolicy::needsUnsavedPrompt( pContext->getSubsystem<EditorWorkspace>().isSceneDirty() ) )
        {
            pContext->getSubsystem<EditorWorkspace>().setPendingSceneAction( EditorPendingSceneAction::Load, path );
            return true;
        }
        return loadScene( path );
    }

    bool EditorSceneFileCommands::tryCreateNewScene()
    {
        if ( EditorSceneFileCommandsInternal::isPlayStoppedForSceneSwap() == false )
            return false;

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr && EditorSessionPolicy::needsUnsavedPrompt( pContext->getSubsystem<EditorWorkspace>().isSceneDirty() ) )
        {
            pContext->getSubsystem<EditorWorkspace>().setPendingSceneAction( EditorPendingSceneAction::New );
            return true;
        }

        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
            return false;
        Scene* pScene = pSceneManager->createEmptyActiveScene( "Untitled" );
        if ( pScene == nullptr )
            return false;
        syncAfterSceneGenerationChange();
        if ( pContext != nullptr )
            pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifyNewScene" ), NotificationType::Info );
        return true;
    }

    void EditorSceneFileCommands::saveFocusedOrScene()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr && pContext->getSubsystem<IEditorPanelHost>().saveFocusedDirtyDocument() )
            return;
        saveActiveSceneOrPrompt();
    }

    void EditorSceneFileCommands::applyUnsavedSceneChoice( EditorUnsavedChoice choice )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        EditorWorkspace& ws = pContext->getSubsystem<EditorWorkspace>();
        if ( ws.getPendingSceneAction() == EditorPendingSceneAction::None )
            return;

        if ( EditorSessionPolicy::shouldSaveBeforeAction( choice ) )
        {
            if ( pContext->getSubsystem<IEditorPanelHost>().saveAllDirtyDocuments() == false )
            {
                pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyEditorTitle" ), SW_LOCKEY( "EdCommon", "NotifyDocumentSaveFailed" ), NotificationType::Error );
                return;
            }
            if ( ws.isSceneDirty() )
            {
                Scene* pScene = editor::getActiveScene();
                if ( pScene != nullptr && pScene->getSourcePath().empty() == false )
                {
                    if ( saveActiveScene( {} ) == false )
                    {
                        pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaveFailed" ), NotificationType::Error );
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
            pContext->getSubsystem<IEditorPanelHost>().discardAllDirtyDocuments();
        }

        if ( EditorSessionPolicy::shouldProceedWithAction( choice ) )
            EditorSceneFileCommandsInternal::runPendingSceneAction();
        else
            ws.clearPendingSceneAction();
    }

    void EditorSceneFileCommands::syncAfterSceneGenerationChange()
    {
        EditorContext* pContext      = EditorContext::get();
        SceneManager*  pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pContext == nullptr || pSceneManager == nullptr )
            return;

        // 이전 씬을 가리키던 상태(선택, dirty, Undo, 프리팹 격리)를 버린다. 이유는 `EditorSceneGenerationSync::apply` 에 있다.
        (void)EditorSceneGenerationSync::apply( pContext->getSubsystem<EditorWorkspace>(), pSceneManager->getSceneGeneration(), editor::findSubsystem<CommandStack>() );
    }

    bool EditorSceneFileCommands::tryBeginQuit()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return true;
        if ( pContext->getSubsystem<EditorWorkspace>().getPendingSceneAction() != EditorPendingSceneAction::None )
            return false;

        const bool   bSceneDirty = pContext->getSubsystem<EditorWorkspace>().isSceneDirty();
        const uint32 dirtyDocs   = pContext->getSubsystem<IEditorPanelHost>().countDirtyDocuments();
        if ( EditorSessionPolicy::needsQuitPrompt( bSceneDirty, dirtyDocs ) == false )
            return true;

        pContext->getSubsystem<EditorWorkspace>().setPendingSceneAction( EditorPendingSceneAction::Quit );
        return false;
    }

    void EditorSceneFileCommands::requestExit()
    {
        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow != nullptr )
            (void)pWindow->tryBeginClose(); // 닫기를 거절해도(저장 확인 중) 여기서 할 일이 없다
    }

    bool EditorSceneFileCommands::saveActiveScene( string_view path )
    {
        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
            return false;

        // 버전 관리가 읽기 전용으로 둔 파일(git LFS lockable, 잠그기 전)에는 쓰지 않고 이유를 알린다. 쓰기 실패 로그만으로는 "잠가야 한다" 는 사실이 드러나지 않는다.
        Scene*         pScene     = pSceneManager->getActiveScene();
        const string   targetPath = path.empty() && pScene != nullptr ? pScene->getSourcePath() : string{ path };
        const string   targetAbs  = FileUtil::isAbsolutePath( targetPath ) ? targetPath : FileUtil::joinPath( ResourcePaths::getRootFolderPath(), targetPath );
        EditorContext* pContext   = EditorContext::get();
        if ( targetPath.empty() == false && FileUtil::isReadOnlyFile( targetAbs ) )
        {
            if ( pContext != nullptr )
                pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySaveTitle" ), SW_LOCKEY( "EdCommon", "NotifySceneReadOnly" ),
                                                                          NotificationType::Warning );
            SW_LOG_WARNING( "Scene '%#' is read-only (not checked out) - not saved", targetAbs.c_str() );
            return false;
        }

        if ( pSceneManager->saveActiveScene( path ) == false )
            return false;
        if ( pContext != nullptr )
        {
            pContext->getSubsystem<EditorWorkspace>().clearSceneDirty();
            // 저장한 씬에 검증 규칙을 실행한다. 결과는 로그로 알리고 저장을 막지 않는다.
            pScene = pSceneManager->getActiveScene();
            if ( pScene != nullptr )
                pContext->getSubsystem<EditorAssetValidation>().requestValidation( pScene->getSourcePath() );
        }
        return true;
    }

    void EditorSceneFileCommands::saveActiveSceneOrPrompt()
    {
        EditorContext* pContext = EditorContext::get();
        Scene*         pScene   = editor::getActiveScene();
        if ( pScene != nullptr && pScene->getSourcePath().empty() == false )
        {
            if ( saveActiveScene( {} ) )
            {
                if ( pContext != nullptr )
                    pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaved" ), NotificationType::Success );
            }
            else if ( pContext != nullptr )
                pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaveFailed" ), NotificationType::Error );
            return;
        }

        FileDialogParams params{};
        params._type               = FileDialogParams::Type::Save;
        params._title              = "Save Scene";
        params._description        = "Scene";
        params._bEnableMultiselect = false;
        // 씬 이름은 쿠커의 규칙 하나를 따른다(`EditorAssetTypeRegistry` → `AssetCookPath`). 맨 `.xml` 은 쿠커가 쿠킹하지 않는 이름이다.
        EditorAssetTypeRegistry::appendSuffixes( EditorAssetType::Scene, params._listFilterExtension );
        const string mapsDir = ResourcePaths::getDomainFolderPath( GameConfig::getActive()._packRoot, path::kMapsFolder );
        if ( FileUtil::isDirectory( mapsDir ) )
            params._initialDirectory = mapsDir;
        FileUtil::openFileDialog( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, EditorSceneFileCommandsInternal::onSaveSceneDialogResult ) );
    }
} // namespace sw::editor
