#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Path/AbsolutePath.h"

#include "Editor/Common/Backend/FileDialog.h"
#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Commands/Asset/EditorContentWrite.h"
#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"
#include "Editor/Common/Config/EditorPaths.h"
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
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetPaths.h"
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
            /// @brief 파일 대화상자의 결과를 받습니다. `FileDialog::pollResults` 가 메인 스레드에서 호출합니다.
            static void onSaveSceneDialogResult( const vector<string>& listPath )
            {
                if ( listPath.empty() )
                    return;

                // 필수: 대화상자 결과는 에디터 메인 스레드가 돌릴 때만 온다(종료 때는 `cancelFileDialogResults` 가 비운다).
                EditorNotificationManager&   notifications = editor::getSubsystem<EditorNotificationManager>();
                const optional<AbsolutePath> absPath       = FileUtil::requireAbsolutePath( listPath[0] ); // 대화상자는 절대 경로를 돌려준다
                if ( absPath.has_value() == false || EditorSceneFileCommands::saveActiveSceneAs( *absPath ) == false )
                {
                    notifications.push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaveFailed" ), NotificationKind::Error );
                    return;
                }

                notifications.push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaved" ), NotificationKind::Success );
                if ( editor::getSubsystem<EditorWorkspace>().getPendingSceneAction() != EditorPendingSceneAction::None )
                    EditorSceneFileCommands::applyUnsavedSceneChoice( EditorUnsavedChoice::Discard );
            }

            static bool isPlayStoppedForSceneSwap()
            {
                if ( EditorPlaySession::isStopped() )
                    return true;
                editor::getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifyStopPlayFirst" ),
                                                                        NotificationKind::Warning );
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
    bool EditorSceneFileCommands::loadScene( string_view path )
    {
        // 리소스 id 로만 연다 - 디스크 경로는 받는 쪽(대화상자 · 개발 명령)이 `AssetPaths::toAssetID` 로 바꿔 온다.
        const string loadPath = AssetPaths::normalizeAssetID( path );
        if ( loadPath.empty() )
        {
            SW_LOG_ERROR_ID( "EditorSceneFileCommands.OpenSceneNotScene", "Open Scene: '%#' is not a scene resource id", path );
            return false;
        }

        SceneManager& sceneManager = editor::getSubsystem<SceneManager>(); // 필수

        if ( sceneManager.requestLoadAsync( loadPath, SceneLoadKind::Tool ) == false )
        {
            // 여기서 알린다. 호출하는 쪽이 이 반환값을 읽지 않으므로, 조용히 false 만 반환하면 사용자가 씬을 선택했는데 아무 일도
            // 일어나지 않고 로그에도 남지 않는다.
            SW_LOG_ERROR_ID( "EditorSceneFileCommands.OpenSceneLoadRequest", "Open Scene: load request failed - %#", loadPath );
            editor::getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySceneOpenFailed" ),
                                                                    NotificationKind::Error );
            return false;
        }

        editor::getSubsystem<EditorWorkspace>().clearSelection();

        SW_LOG_INFO_ID( "EditorSceneFileCommands.OpenScene", "Open Scene: %#", loadPath );
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
            SW_LOG_ERROR_ID( "EditorSceneFileCommands.NewSceneSceneManagerCould", "New Scene: SceneManager could not create an empty scene" );
            return false;
        }
        syncAfterSceneGenerationChange();
        editor::getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifyNewScene" ), NotificationKind::Info );
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
                notifications.push( SW_LOCKEY( "EdCommon", "NotifyEditorTitle" ), SW_LOCKEY( "EdCommon", "NotifyDocumentSaveFailed" ), NotificationKind::Error );
                return;
            }
            if ( ws.isSceneDirty() )
            {
                Scene* pScene = editor::getActiveScene();
                if ( pScene != nullptr && pScene->getSourcePath().empty() == false )
                {
                    if ( saveActiveScene() == false )
                    {
                        notifications.push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaveFailed" ), NotificationKind::Error );
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

    bool EditorSceneFileCommands::saveActiveScene()
    {
        SceneManager& sceneManager = editor::getSubsystem<SceneManager>();
        const Scene*  pScene       = sceneManager.getActiveScene();
        const string  sourceID     = pScene != nullptr ? pScene->getSourcePath() : string{};
        // 읽기 전용 검사는 쓰기와 같은 해석으로 푼 자리를 본다(R7). 소스가 없거나 못 풀면 SceneManager 가 이유를 남기고 실패한다.
        const optional<AbsolutePath> targetPath = sourceID.empty() ? nullopt : editor::getSubsystem<AssetPaths>().toDiskPath( sourceID );
        if ( targetPath.has_value() && EditorContentWrite::warnIfReadOnly( targetPath->str(), EditorContentWriteKind::Save ) )
            return false;
        return finishSave( sceneManager.saveActiveScene(), sourceID );
    }

    bool EditorSceneFileCommands::saveActiveSceneAs( const AbsolutePath& absPath )
    {
        if ( EditorContentWrite::warnIfReadOnly( absPath.str(), EditorContentWriteKind::Save ) )
            return false;
        return finishSave( editor::getSubsystem<SceneManager>().saveActiveSceneAs( absPath ), absPath.str() );
    }

    bool EditorSceneFileCommands::finishSave( const bool bSaved, string_view target )
    {
        if ( bSaved == false )
        {
            SW_LOG_ERROR_ID( "EditorSceneFileCommands.SaveSceneSceneManagerCould", "Save Scene: SceneManager could not save '%#'", target );
            return false;
        }
        editor::getSubsystem<EditorWorkspace>().clearSceneDirty();
        // 정상 저장한 씬의 자동 저장 사본은 지운다 — 남겨 두면 다음 비정상 종료 뒤 이미 저장한 일을 복구하라고 묻는다
        const Scene* pSavedScene = editor::getActiveScene();
        if ( pSavedScene != nullptr )
            EditorAutosaveCommands::onSceneSaved( pSavedScene->getSourcePath() );
        return true;
    }

    void EditorSceneFileCommands::saveActiveSceneOrPrompt()
    {
        Scene* pScene = editor::getActiveScene();
        if ( pScene != nullptr && pScene->getSourcePath().empty() == false )
        {
            if ( saveActiveScene() )
                editor::getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaved" ), NotificationKind::Success );
            else
                editor::getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifySceneTitle" ), SW_LOCKEY( "EdCommon", "NotifySaveFailed" ), NotificationKind::Error );
            return;
        }

        FileDialogParams params{};
        params._type               = FileDialogParams::Type::Save;
        params._title              = "Save Scene";
        params._description        = "Scene";
        params._bEnableMultiselect = false;
        // 씬 이름은 쿠커의 규칙 하나를 따른다(`EditorAssetTypeRegistry` → `AssetCookPath`). 맨 `.xml` 은 쿠커가 쿠킹하지 않는 이름이다.
        EditorAssetTypeRegistry::appendSuffixes( EditorAssetType{ SceneDocument::kAssetType }, params._listFilterExtension );
        params._initialDirectory = EditorPaths::getSceneDialogFolderPath();
        FileDialog::open( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, EditorSceneFileCommandsInternal::onSaveSceneDialogResult ) );
    }
} // namespace sw::editor

namespace sw::editor
{
    // 콘솔 명령 — 시나리오 · 콘솔 입력 줄이 부른다(`DevCommandRegistry`). 그 기능 옆에 둔다(언리얼 Exec 명령이 그 시스템 파일에 있는 것과 같다).
    namespace
    {
        struct EditorSceneFileConsoleInternal
        {
            static bool runSaveSceneAs( const vector<string>& listArgument, string& outReply )
            {
                string absolutePath;
                if ( listArgument.size() != 1 || DevCommandRegistry::readDiskPathArgument( listArgument[0], absolutePath, outReply ) == false )
                    return false;
                (void)FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( absolutePath ) );    // 못 만들면 ensure 가 폴더와 이유를 남기고 뒤따르는 쓰기가 실패로 답한다
                const optional<AbsolutePath> scenePath = FileUtil::requireAbsolutePath( absolutePath ); // readDiskPathArgument 가 절대만 받았다
                if ( scenePath.has_value() == false || EditorSceneFileCommands::saveActiveSceneAs( *scenePath ) == false )
                {
                    outReply = "could not save the active scene to '" + absolutePath + "'";
                    return false;
                }
                outReply = "saved " + absolutePath;
                return true;
            }

            static bool runOpenScene( const vector<string>& listArgument, string& outReply )
            {
                string absolutePath;
                if ( listArgument.size() != 1 || EditorAssetFileCommands::readSourceFileArgument( listArgument[0], absolutePath, outReply ) == false )
                    return false;
                // 씬은 리소스 id 로 연다 - 리소스 트리 · 붙인 디스크 도메인 밖의 파일은 씬의 소스가 될 수 없다.
                const string sceneID = editor::getSubsystem<AssetPaths>().toAssetID( absolutePath );
                if ( sceneID.empty() )
                {
                    outReply = "'" + absolutePath + "' is outside the content root and every mounted disk domain";
                    return false;
                }
                // 미저장 씬이 있어도 대화상자를 띄우는 경로(tryOpenScene)를 쓰지 않고 바로 연다. 무인 실행에서는 대화상자를 아무도 닫지 않는다.
                if ( EditorSceneFileCommands::loadScene( sceneID ) == false )
                {
                    outReply = "could not open '" + absolutePath + "'";
                    return false;
                }
                outReply = "opening " + absolutePath;
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( SaveSceneAs, "scene.saveAs", "scene.saveAs <absolute path>", "Save the active scene to a file (an absolute path - scenarios use {out}/...)",
                    &EditorSceneFileConsoleInternal::runSaveSceneAs );
    SW_DEV_COMMAND( OpenScene, "scene.open", "scene.open <path>",
                    "Open a scene file without the unsaved-changes prompt (a resource id, or a path relative to the working folder)",
                    &EditorSceneFileConsoleInternal::runOpenScene );
} // namespace sw::editor
