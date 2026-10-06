#include "pch.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/VectorMath.h"
#include "Core/Process/Process.h"
#include "Core/String/StringUtil.h"

#include "Editor/AssetActions/EditorAssetTypeActions.h"
#include "Editor/Common/Asset/EditorAssetValidation.h"
#include "Editor/Common/Commands/EditorInspectorCommands.h"
#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Gui/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSceneGenerationSync.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Window/IWindow.h"

namespace sw::editor
{
    namespace
    {
        struct EditorAssetCommandsInternal
        {
            /// @brief 파일 대화 상자의 결과를 받습니다. `FileUtil::pumpFileDialogResults` 가 **메인 스레드에서** 부릅니다.
            static void onSaveSceneDialogResult( const vector<string>& listPath )
            {
                if ( listPath.empty() )
                    return;

                // 컨텍스트는 한 번 받아 검사하고 그것만 쓴다.
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;

                if ( EditorAssetCommands::saveActiveScene( listPath[0] ) == false )
                {
                    pContext->getNotificationManager().push( "Scene", "Save failed", NotificationType::Error );
                    return;
                }

                pContext->getNotificationManager().push( "Scene", "Saved", NotificationType::Success );
                if ( pContext->getWorkspace().getPendingSceneAction() != EditorPendingSceneAction::None )
                    EditorAssetCommands::applyUnsavedSceneChoice( EditorUnsavedChoice::Discard );
            }

            static bool isPlayStoppedForSceneSwap()
            {
                if ( EditorPlaySession::isStopped() )
                    return true;
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    pContext->getNotificationManager().push( "Scene", "Stop play before opening or creating a scene",
                                                             NotificationType::Warning );
                return false;
            }

            static void runPendingSceneAction()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;
                EditorWorkspace&               ws     = pContext->getWorkspace();
                const EditorPendingSceneAction action = ws.getPendingSceneAction();
                const string                   path   = ws.getPendingSceneActionPath();
                ws.clearPendingSceneAction();
                // 실패는 각 함수가 알린다(읽지 못한 씬 · 플레이 중).
                if ( action == EditorPendingSceneAction::Load )
                    (void)EditorAssetCommands::loadScene( path ); // 실패는 loadScene 이 알린다
                else if ( action == EditorPendingSceneAction::New )
                    (void)EditorAssetCommands::tryCreateNewScene(); // 실패는 tryCreateNewScene 이 알린다(플레이 중)
                else if ( action == EditorPendingSceneAction::Quit )
                {
                    IWindow* pWindow = IWindow::getActiveWindow();
                    if ( pWindow != nullptr )
                        pWindow->requestClose();
                }
            }

            static void appendFolderListingEntry( vector<EditorFolderListingEntry>& outList, const string& path, bool bIsDirectory,
                                                  const string& rootNorm )
            {
                EditorFolderListingEntry item;
                item._absolutePath = FileUtil::normalizeSeparators( path );
                item._name         = FileUtil::getFileNamePart( item._absolutePath );
                item._bIsDirectory = bIsDirectory;
                if ( item._bIsDirectory == false )
                {
                    item._extension = FileUtil::getExtension( item._name );
                    item._bReadOnly = FileUtil::isReadOnlyFile( item._absolutePath );
                }

                if ( rootNorm.empty() == false )
                {
                    const string absNorm = FileUtil::normalizePath( item._absolutePath );
                    if ( FileUtil::startsWithPathComponent( absNorm, rootNorm ) )
                        item._relativePath = FileUtil::suffixAfterPathComponent( absNorm, rootNorm );
                }
                if ( item._relativePath.empty() )
                    item._relativePath = FileUtil::normalizePath( item._name );

                if ( item._bIsDirectory == false && FileUtil::hasExtension( item._name, path::kMetaExtension ) )
                    return;

                outList.push_back( std::move( item ) );
            }

            static bool matchesPrefabPath( const EditorWorkspace& ws, const GameObject* pObject, string_view prefabPath )
            {
                if ( pObject == nullptr || prefabPath.empty() )
                    return false;
                const string& mapped = ws.getGameObjectPrefabPath( pObject->getObjectId() );
                if ( mapped.empty() == false && FileUtil::pathsEqualNormalized( mapped, prefabPath ) )
                    return true;
                return false;
            }

            static GameObject* findPrefabInstance( GameObjectManager* pManager, EditorWorkspace& ws, string_view prefabPath,
                                                   GameObject* pUnderRoot )
            {
                if ( pManager == nullptr )
                    return nullptr;

                GameObject* pPrimary = ws.getSelectedObject();
                if ( pPrimary != nullptr && matchesPrefabPath( ws, pPrimary, prefabPath ) )
                {
                    if ( pUnderRoot == nullptr || pPrimary->isDescendantOf( pUnderRoot ) )
                        return pPrimary;
                }

                GameObject*               pFallback  = nullptr;
                const vector<GameObject*> listObject = pManager->getAllGameObjects();
                for ( GameObject* pObject : listObject )
                {
                    if ( pObject == nullptr || pObject->isPendingDestroy() )
                        continue;
                    if ( matchesPrefabPath( ws, pObject, prefabPath ) == false )
                        continue;
                    if ( pUnderRoot != nullptr && pObject->isDescendantOf( pUnderRoot ) == false )
                        continue;
                    if ( pUnderRoot != nullptr )
                        return pObject;
                    if ( pFallback == nullptr )
                        pFallback = pObject;
                }
                return pFallback;
            }

            static void hideObjectsOutsideIsolation( GameObjectManager* pManager, GameObject* pRoot,
                                                     vector<PrefabIsolationHiddenObject>& outHidden )
            {
                outHidden.clear();
                if ( pManager == nullptr || pRoot == nullptr )
                    return;

                const vector<GameObject*> listObject = pManager->getAllGameObjects();
                for ( GameObject* pObject : listObject )
                {
                    if ( pObject == nullptr || pObject->isPendingDestroy() )
                        continue;
                    if ( pObject->isDescendantOf( pRoot ) )
                        continue;
                    if ( pRoot->isDescendantOf( pObject ) )
                        continue;

                    PrefabIsolationHiddenObject entry{};
                    entry._objectId   = pObject->getObjectId();
                    entry._bWasActive = pObject->isActive() ? SW_TRUE : SW_FALSE;
                    outHidden.push_back( entry );
                    pObject->setActive( false );
                }
            }

            static void restoreIsolationHidden( GameObjectManager* pManager, const vector<PrefabIsolationHiddenObject>& listHidden )
            {
                if ( pManager == nullptr )
                    return;
                for ( const PrefabIsolationHiddenObject& entry : listHidden )
                {
                    GameObject* pObject = pManager->findGameObjectById( entry._objectId );
                    if ( pObject == nullptr )
                        continue;
                    pObject->setActive( entry._bWasActive == SW_TRUE );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorAssetCommands" );

    bool EditorAssetCommands::openPath( string_view relativePath )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return false;

        // 전용 도구 패널이 먼저다(종류 표의 패널 제목). 패널이 없는 종류는 그 종류의 동작이 연다(씬).
        const string_view panelTitle = EditorAssetTypeRegistry::findPanelTitleForPath( relativePath );
        if ( panelTitle.empty() == false )
        {
            pContext->getWorkspace().setFocusedAssetPath( string{ relativePath }.c_str() );
            pContext->getWorkspace().requestOpenPanel( string{ panelTitle }.c_str() );
            return true;
        }

        const IEditorAssetTypeActions* pActions = EditorAssetTypeActionsRegistry::findActionsForPath( relativePath );
        return pActions != nullptr && pActions->open( relativePath );
    }

    bool EditorAssetCommands::loadScene( string_view path )
    {
        // 리소스 트리 안이면 리소스 id 로 연다(절대 경로 · 프로젝트 기준 경로 · id 모두). 밖이면 받은 경로 그대로다.
        string loadPath = ResourceUtil::toResourceId( path );
        if ( loadPath.empty() )
            loadPath = string{ path };

        SceneManager* pSceneManager = editor::getService<SceneManager>();
        if ( pSceneManager == nullptr )
        {
            SW_LOG_ERROR( "Open Scene: SceneManager unavailable" );
            return false;
        }

        if ( pSceneManager->requestLoadAsync( loadPath ) == false )
        {
            // 여기서 알린다 — 호출부는 이 반환값을 읽지 않으므로, 조용히 false 만 반환하면 사용자가 씬을 골랐는데 아무 일도
            // 일어나지 않고 로그에도 남지 않는다.
            SW_LOG_ERROR( "Open Scene: 로드 요청 실패 — %#", loadPath );
            EditorContext* pFailContext = EditorContext::get();
            if ( pFailContext != nullptr )
            {
                pFailContext->getNotificationManager().push( "Scene", "Failed to open the scene",
                                                             NotificationType::Error );
            }
            return false;
        }

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            pContext->getWorkspace().clearSelection();

        SW_LOG_INFO( "Open Scene: %#", loadPath );
        return true;
    }

    bool EditorAssetCommands::tryOpenScene( string_view path )
    {
        if ( EditorAssetCommandsInternal::isPlayStoppedForSceneSwap() == false )
            return false;

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr && EditorSessionPolicy::needsUnsavedPrompt( pContext->getWorkspace().isSceneDirty() ) )
        {
            pContext->getWorkspace().setPendingSceneAction( EditorPendingSceneAction::Load, path );
            return true;
        }
        return loadScene( path );
    }

    bool EditorAssetCommands::tryCreateNewScene()
    {
        if ( EditorAssetCommandsInternal::isPlayStoppedForSceneSwap() == false )
            return false;

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr && EditorSessionPolicy::needsUnsavedPrompt( pContext->getWorkspace().isSceneDirty() ) )
        {
            pContext->getWorkspace().setPendingSceneAction( EditorPendingSceneAction::New );
            return true;
        }

        SceneManager* pSceneManager = editor::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return false;
        Scene* pScene = pSceneManager->createEmptyActiveScene( "Untitled" );
        if ( pScene == nullptr )
            return false;
        syncAfterSceneGenerationChange();
        if ( pContext != nullptr )
            pContext->getNotificationManager().push( "Scene", "New scene", NotificationType::Info );
        return true;
    }

    void EditorAssetCommands::saveFocusedOrScene()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr && pContext->getPanelManager().saveFocusedDirtyDocument() )
            return;
        saveActiveSceneOrPrompt();
    }

    void EditorAssetCommands::applyUnsavedSceneChoice( EditorUnsavedChoice choice )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        EditorWorkspace& ws = pContext->getWorkspace();
        if ( ws.getPendingSceneAction() == EditorPendingSceneAction::None )
            return;

        if ( EditorSessionPolicy::shouldSaveBeforeAction( choice ) )
        {
            if ( pContext->getPanelManager().saveAllDirtyDocuments() == false )
            {
                pContext->getNotificationManager().push( "Editor", "Document save failed", NotificationType::Error );
                return;
            }
            if ( ws.isSceneDirty() )
            {
                Scene* pScene = editor::getActiveScene();
                if ( pScene != nullptr && pScene->getSourcePath().empty() == false )
                {
                    if ( saveActiveScene( {} ) == false )
                    {
                        pContext->getNotificationManager().push( "Scene", "Save failed", NotificationType::Error );
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
            pContext->getPanelManager().discardAllDirtyDocuments();
        }

        if ( EditorSessionPolicy::shouldProceedWithAction( choice ) )
            EditorAssetCommandsInternal::runPendingSceneAction();
        else
            ws.clearPendingSceneAction();
    }

    void EditorAssetCommands::syncAfterSceneGenerationChange()
    {
        EditorContext* pContext      = EditorContext::get();
        SceneManager*  pSceneManager = editor::getService<SceneManager>();
        if ( pContext == nullptr || pSceneManager == nullptr )
            return;

        // 옛 씬을 가리키던 상태(선택 · dirty · Undo · 프리팹 격리)를 버린다 — 이유는 `EditorSceneGenerationSync::apply`.
        (void)EditorSceneGenerationSync::apply( pContext->getWorkspace(), pSceneManager->getSceneGeneration(), editor::getService<CommandStack>() );
    }

    bool EditorAssetCommands::tryBeginQuit()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return true;
        if ( pContext->getWorkspace().getPendingSceneAction() != EditorPendingSceneAction::None )
            return false;

        const bool   bSceneDirty = pContext->getWorkspace().isSceneDirty();
        const uint32 dirtyDocs   = pContext->getPanelManager().countDirtyDocuments();
        if ( EditorSessionPolicy::needsQuitPrompt( bSceneDirty, dirtyDocs ) == false )
            return true;

        pContext->getWorkspace().setPendingSceneAction( EditorPendingSceneAction::Quit );
        return false;
    }

    void EditorAssetCommands::requestExit()
    {
        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow != nullptr )
            (void)pWindow->tryBeginClose(); // 닫기를 거절해도(저장 확인 중) 여기서 할 일이 없다
    }

    void EditorAssetCommands::focusPath( string_view relativePath )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        pContext->getWorkspace().setFocusedAssetPath( string{ relativePath }.c_str() );
    }

    GameObject* EditorAssetCommands::spawnPrefab( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent,
                                                  const utf8* pUndoLabel )
    {
        if ( EditorUtil::areSceneEditsAllowed() == false )
            return nullptr;
        GameObject* pSpawned = EditorUtil::spawnPrefabFromAssetPath( pManager, pPath, pParent );
        if ( pSpawned == nullptr )
            return nullptr;

        const utf8* pLabel = ( pUndoLabel != nullptr ) ? pUndoLabel : "Spawn Prefab";
        EditorTransaction::recordCreation( pSpawned, pLabel );
        EditorSceneCommands::select( pSpawned, SelectionMode::Replace );
        return pSpawned;
    }

    GameObject* EditorAssetCommands::spawnSprite( GameObjectManager* pManager, const utf8* pPath, const float3& worldPos )
    {
        if ( EditorUtil::areSceneEditsAllowed() == false )
            return nullptr;
        if ( pManager == nullptr || StringUtil::isNullOrEmpty( pPath ) )
            return nullptr;

        string filename = FileUtil::getFileNamePart( pPath );
        filename        = FileUtil::removeExtension( filename );

        GameObject* pSpawned = pManager->createGameObject( hashed_string( filename.c_str() ) );
        if ( pSpawned == nullptr )
            return nullptr;

        SceneComponent* pSc = pSpawned->addComponent<SceneComponent>();
        if ( pSc != nullptr )
            pSc->setLocalPosition( worldPos );

        SpriteComponent* pSprite = pSpawned->addComponent<SpriteComponent>();
        if ( pSprite != nullptr )
        {
            // 끌어 놓은 것은 리소스 id 다(콘텐츠 브라우저). 주의: 그것을 프로젝트 루트 기준으로 다시 `makeRelativePath` 하면 경로가 깨져
            // 스프라이트가 텍스처를 찾지 못하고 흰 사각형으로 그려진다.
            const string textureId = ResourceUtil::toResourceId( pPath );
            if ( textureId.empty() )
                SW_LOG_WARNING( "Sprite drop: '%#' is not inside the resource tree - the sprite has no texture", pPath );
            pSprite->setTextureName( textureId );
            // 편집 모드에서는 시작(onBeginPlay)이 없다 — 떨군 자리에서 바로 그려지게 렌더 에셋을 푼다.
            pSprite->resolveRenderAssets();
        }

        EditorTransaction::recordCreation( pSpawned, "Spawn Sprite in Viewport" );
        EditorSceneCommands::select( pSpawned, SelectionMode::Replace );
        return pSpawned;
    }

    void EditorAssetCommands::dropAt( GameObjectManager* pManager, const utf8* pPath, const float3& spawnPos )
    {
        if ( pManager == nullptr || pPath == nullptr )
            return;

        // 스폰 · 로드는 종류의 동작이 한다(프리팹 · 텍스처 · 씬). 처리하지 않은 것은 연다.
        const IEditorAssetTypeActions* pActions = EditorAssetTypeActionsRegistry::findActionsForPath( pPath );
        if ( pActions != nullptr && pActions->dropInViewport( pManager, pPath, spawnPos ) )
            return;
        (void)openPath( pPath ); // 실패는 openPath 가 알린다
    }

    bool EditorAssetCommands::saveActiveScene( string_view path )
    {
        SceneManager* pSceneManager = editor::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return false;

        // 버전 관리가 읽기 전용으로 둔 파일(git LFS lockable — 잠그기 전)에는 쓰지 않고 이유를 말한다. 쓰기 실패 로그만으로는 "잠가야 한다" 가 안 보인다.
        Scene*         pScene     = pSceneManager->getActiveScene();
        const string   targetPath = path.empty() && pScene != nullptr ? pScene->getSourcePath() : string{ path };
        const string   targetAbs  = FileUtil::isAbsolutePath( targetPath ) ? targetPath : FileUtil::joinPath( ResourceUtil::getRootFolderPath(), targetPath );
        EditorContext* pContext   = EditorContext::get();
        if ( targetPath.empty() == false && FileUtil::isReadOnlyFile( targetAbs ) )
        {
            if ( pContext != nullptr )
                pContext->getNotificationManager().push( "Save", "The scene file is read-only - Check Out (lock) it in the Content Browser first",
                                                         NotificationType::Warning );
            SW_LOG_WARNING( "Scene '%#' is read-only (not checked out) - not saved", targetAbs.c_str() );
            return false;
        }

        if ( pSceneManager->saveActiveScene( path ) == false )
            return false;
        if ( pContext != nullptr )
        {
            pContext->getWorkspace().clearSceneDirty();
            // 저장한 씬에 검증 규칙을 돌린다(결과는 로그로 — 저장을 막지 않는다).
            pScene = pSceneManager->getActiveScene();
            if ( pScene != nullptr )
                pContext->getAssetValidation().requestValidation( pScene->getSourcePath() );
        }
        return true;
    }

    void EditorAssetCommands::saveActiveSceneOrPrompt()
    {
        EditorContext* pContext = EditorContext::get();
        Scene*         pScene   = editor::getActiveScene();
        if ( pScene != nullptr && pScene->getSourcePath().empty() == false )
        {
            if ( saveActiveScene( {} ) )
            {
                if ( pContext != nullptr )
                    pContext->getNotificationManager().push( "Scene", "Saved", NotificationType::Success );
            }
            else if ( pContext != nullptr )
                pContext->getNotificationManager().push( "Scene", "Save failed", NotificationType::Error );
            return;
        }

        FileDialogParams params{};
        params._type               = FileDialogParams::Type::Save;
        params._title              = "Save Scene";
        params._description        = "Scene";
        params._bEnableMultiselect = false;
        // 씬 이름은 쿠커의 규칙 하나다(`EditorAssetTypeRegistry` → `AssetCookPath`). 맨 `.xml` 은 쿠커가 쿠킹하지 않는 이름이다.
        EditorAssetTypeRegistry::appendSuffixes( EditorAssetType::Scene, params._listFilterExtension );
        const string mapsDir = ResourceUtil::getDomainFolderPath( GameConfig::getActive()._packRoot, path::kMapsFolder );
        if ( FileUtil::isDirectory( mapsDir ) )
            params._initialDirectory = mapsDir;
        FileUtil::openFileDialog( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, EditorAssetCommandsInternal::onSaveSceneDialogResult ) );
    }

    uint32 EditorAssetCommands::importFiles( string_view destFolderAbs, const vector<string>& listSourcePath )
    {
        if ( destFolderAbs.empty() )
        {
            SW_LOG_WARNING( "Import cancelled — no destination folder." );
            return 0;
        }

        uint32 copied{ 0 };
        for ( const string& sourcePath : listSourcePath )
        {
            if ( FileUtil::exists( sourcePath ) == false )
            {
                SW_LOG_WARNING( "Import skipped (missing): %#", sourcePath.c_str() );
                continue;
            }

            const string fileName = FileUtil::getFileNamePart( sourcePath );

            if ( FileUtil::pathsEqualNormalized( sourcePath, ResourceUtil::makeSavePath( destFolderAbs, fileName ) ) )
            {
                SW_LOG_TRACE( "Already in folder: %#", fileName.c_str() );
                continue;
            }

            // **이미 있는 애셋을 덮어쓰지 않는다.** 같은 이름의 파일을 끌어다 놓아 폴더에 있던 것을 덮으면 **아무 말 없이 사라지고**,
            // 에디터의 임포트에는 되돌리기가 없으므로 그대로 잃는다.
            // "원래 있던 것과 같은 파일인가" 는 바로 위에서 경로로 이미 걸렀으므로, 여기까지 온 것은 **다른 파일인데 이름만 같은**
            // 경우다.
            const string destPath = ResourceUtil::makeUniqueSavePath( destFolderAbs, fileName );

            FileUtil::ensureParentDirectoryExists( destPath );
            if ( FileUtil::copyFile( sourcePath, destPath ) == false )
            {
                SW_LOG_ERROR( "Failed to import: %#", sourcePath.c_str() );
                continue;
            }

            ++copied;
            const string rel = AssetDatabase::toRelativePath( destPath );
            if ( rel.empty() == false )
            {
                AssetManager* pResources = editor::getService<AssetManager>();
                if ( pResources != nullptr )
                    pResources->getAssetDatabase().ensureMeta( rel, true );
            }
            SW_LOG_TRACE( "Imported: %# -> %#", sourcePath.c_str(), destPath.c_str() );
        }
        return copied;
    }

    bool EditorAssetCommands::deleteAsset( string_view absolutePath )
    {
        return AssetDatabase::deleteAssetFile( absolutePath );
    }

    bool EditorAssetCommands::showInFileExplorer( string_view absolutePath )
    {
        if ( absolutePath.empty() )
            return false;

        const string path = FileUtil::normalizeSeparators( absolutePath );
        string       command;
#if defined( SW_PLATFORM_WINDOWS )
        // explorer 는 백슬래시만 받는다(슬래시를 주면 선택이 안 되고 내 문서를 연다).
        string windowsPath = path;
        for ( utf8& ch : windowsPath )
        {
            if ( ch == '/' )
                ch = '\\';
        }
        command = "explorer.exe /select,\"" + windowsPath + "\"";
#else
        // 리눅스 파일 관리자에는 "선택한 채로 열기" 가 표준이 아니다. 그래서 폴더까지만 연다.
        command = "xdg-open \"" + FileUtil::getDirectoryPart( path ) + "\"";
#endif

        // **띄우고 기다리지 않는다.** `Process::execute` 로 기다리면 UI 스레드가 탐색기(또는 그것이 띄운 창)가 출력 파이프를 놓을
        // 때까지 멈추고, `explorer.exe /select,` 는 성공해도 종료 코드 1 을 돌려준다. 알 수 있는 실패는 "띄우지 못했다" 뿐이다.
        if ( Process::launchDetached( command ) == false )
        {
            SW_LOG_WARNING( "Failed to open file explorer for %#", path.c_str() );
            return false;
        }
        return true;
    }

    void EditorAssetCommands::collectFolderListing( string_view folderAbs, vector<EditorFolderListingEntry>& outList )
    {
        outList.clear();
        if ( folderAbs.empty() || FileUtil::isDirectory( folderAbs ) == false )
            return;

        vector<string> listFolder;
        vector<string> listFile;
        FileUtil::collectFolders( folderAbs, listFolder, false );
        FileUtil::collectFiles( folderAbs, {}, listFile, false );

        const string& resourceRoot = ResourceUtil::getRootFolderPath();
        const string  rootNorm     = resourceRoot.empty() ? string{} : FileUtil::normalizePath( resourceRoot );

        for ( const string& folder : listFolder )
            EditorAssetCommandsInternal::appendFolderListingEntry( outList, folder, true, rootNorm );
        for ( const string& file : listFile )
            EditorAssetCommandsInternal::appendFolderListingEntry( outList, file, false, rootNorm );
    }

    void EditorAssetCommands::collectChildFolders( string_view folderAbs, vector<string>& outList )
    {
        outList.clear();
        if ( folderAbs.empty() || FileUtil::isDirectory( folderAbs ) == false )
            return;

        FileUtil::collectFolders( folderAbs, outList, false );
        for ( string& child : outList )
            child = FileUtil::normalizeSeparators( child );
    }

    bool EditorAssetCommands::enterPrefabIsolation( string_view prefabPath )
    {
        if ( prefabPath.empty() )
            return false;
        if ( EditorPlaySession::isStopped() == false )
            return false;

        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return false;
        EditorWorkspace& ws = pContext->getWorkspace();
        if ( ws.isPrefabIsolationActive() == false && ws.isSceneDirty() &&
             EditorSessionPolicy::requiresCleanSceneForPrefabIsolation() )
        {
            pContext->getNotificationManager().push( "Prefab", "Save the scene before opening prefab isolation",
                                                     NotificationType::Warning );
            return false;
        }

        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return false;
        GameObjectManager* pManager = pScene->getObjectManager();

        GameObject* pUnderRoot = nullptr;
        if ( ws.isPrefabIsolationActive() )
            pUnderRoot = pManager->findGameObjectById( ws.getPrefabIsolationRootId() );

        const string pathStr{ prefabPath };
        GameObject*  pRoot = EditorAssetCommandsInternal::findPrefabInstance( pManager, ws, prefabPath, pUnderRoot );
        uint8        bSpawnedRoot{ SW_FALSE };
        if ( pRoot == nullptr )
        {
            pRoot = EditorUtil::spawnPrefabFromAssetPath( pManager, pathStr.c_str(), pUnderRoot );
            if ( pRoot == nullptr )
                return false;
            bSpawnedRoot = SW_TRUE;
        }

        PrefabIsolationFrame frame{};
        frame._prefabPath   = pathStr;
        frame._rootObjectId = pRoot->getObjectId();
        frame._bSpawnedRoot = bSpawnedRoot;
        EditorAssetCommandsInternal::hideObjectsOutsideIsolation( pManager, pRoot, frame._listHidden );
        ws.pushPrefabIsolation( std::move( frame ) );
        ws.selectGameObject( pRoot );
        ws.setFocusedAssetPath( pathStr.c_str() );
        ws.requestOpenPanel( "Prefab Editor" );
        pContext->getNotificationManager().push( "Prefab", "Isolated prefab in the current scene", NotificationType::Info );
        return true;
    }

    bool EditorAssetCommands::exitPrefabIsolation( bool bSaveToPrefab )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return false;
        EditorWorkspace& ws = pContext->getWorkspace();
        if ( ws.isPrefabIsolationActive() == false )
            return false;
        const PrefabIsolationFrame* pFrame = ws.getPrefabIsolationFrame();
        if ( pFrame == nullptr )
            return false;

        const PrefabIsolationFrame frame         = *pFrame;
        SceneManager*              pSceneManager = editor::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return false;
        Scene*             pScene   = pSceneManager->getActiveScene();
        GameObjectManager* pManager = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
        GameObject*        pRoot    = nullptr;
        if ( pManager != nullptr )
            pRoot = pManager->findGameObjectById( frame._rootObjectId );

        // 저장하지 못하면 격리를 끝내지 않는다 — 끝내면 편집한 프리팹 내용이 저장 없이 사라진다.
        if ( bSaveToPrefab && pRoot != nullptr && EditorInspectorCommands::applyToPrefab( pRoot, frame._prefabPath ) == false )
        {
            SW_LOG_ERROR( "Prefab isolation could not save '%#' - still editing it", frame._prefabPath.c_str() );
            return false;
        }

        EditorAssetCommandsInternal::restoreIsolationHidden( pManager, frame._listHidden );

        const bool bFullyExit = ws.popPrefabIsolation();
        if ( frame._bSpawnedRoot == SW_TRUE && bSaveToPrefab == false && pManager != nullptr && pRoot != nullptr )
        {
            if ( pContext->getEditorSelection().hasObject( pRoot ) )
                pContext->getEditorSelection().selectObject( pRoot, SelectionMode::Remove );
            pManager->destroyObject( pRoot );
            pRoot = nullptr;
        }
        else if ( frame._bSpawnedRoot == SW_TRUE && bSaveToPrefab )
            ws.markSceneDirty();

        if ( bFullyExit )
        {
            pContext->getNotificationManager().push( "Prefab", "Exited prefab isolation", NotificationType::Info );
            return true;
        }

        GameObject* pParentRoot = nullptr;
        if ( pManager != nullptr )
            pParentRoot = pManager->findGameObjectById( ws.getPrefabIsolationRootId() );
        if ( pParentRoot != nullptr )
            ws.selectGameObject( pParentRoot );
        ws.setFocusedAssetPath( ws.getPrefabIsolationPrefabPath().c_str() );
        return true;
    }
} // namespace sw::editor
