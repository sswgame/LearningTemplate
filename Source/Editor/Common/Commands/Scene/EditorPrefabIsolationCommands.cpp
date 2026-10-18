#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorPrefabIsolationCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    namespace
    {
        struct EditorPrefabIsolationCommandsInternal
        {
            static bool matchesPrefabPath( const EditorWorkspace& ws, const GameObject* pObject, string_view prefabPath )
            {
                if ( pObject == nullptr || prefabPath.empty() )
                    return false;
                const string_view mapped = ws.findPrefabPath( pObject );
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
                    entry._objectID   = pObject->getObjectID();
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
                    GameObject* pObject = pManager->findGameObjectByID( entry._objectID );
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
    SW_LOG_CALLER( "EditorPrefabIsolationCommands" );

    bool EditorPrefabIsolationCommands::enterPrefabIsolation( string_view prefabPath )
    {
        if ( prefabPath.empty() )
            return false;
        if ( EditorPlaySession::isStopped() == false )
            return false;

        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return false;
        EditorWorkspace& ws                   = pContext->getSubsystem<EditorWorkspace>();
        const bool       bSceneNeedsSaveFirst = ws.isPrefabIsolationActive() == false && ws.isSceneDirty() &&
                                          EditorSessionPolicy::requiresCleanSceneForPrefabIsolation();
        if ( bSceneNeedsSaveFirst )
        {
            pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPrefabTitle" ), SW_LOCKEY( "EdCommon", "NotifyPrefabSaveFirst" ),
                                                                      NotificationType::Warning );
            return false;
        }

        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return false;
        GameObjectManager* pManager = pScene->getObjectManager();

        GameObject* pUnderRoot = nullptr;
        if ( ws.isPrefabIsolationActive() )
            pUnderRoot = pManager->findGameObjectByID( ws.getPrefabIsolationRootID() );

        const string pathStr{ prefabPath };
        GameObject*  pRoot = EditorPrefabIsolationCommandsInternal::findPrefabInstance( pManager, ws, prefabPath, pUnderRoot );
        uint8        bSpawnedRoot{ SW_FALSE };
        if ( pRoot == nullptr )
        {
            pRoot = EditorAssetCommands::spawnPrefabWithoutUndo( pManager, pathStr.c_str(), pUnderRoot );
            if ( pRoot == nullptr )
                return false;
            bSpawnedRoot = SW_TRUE;
        }

        PrefabIsolationFrame frame{};
        frame._prefabPath   = pathStr;
        frame._rootObjectID = pRoot->getObjectID();
        frame._bSpawnedRoot = bSpawnedRoot;
        EditorPrefabIsolationCommandsInternal::hideObjectsOutsideIsolation( pManager, pRoot, frame._listHidden );
        ws.pushPrefabIsolation( std::move( frame ) );
        ws.selectGameObject( pRoot );
        ws.setFocusedAssetPath( pathStr.c_str() );
        ws.requestOpenPanel( "Prefab Editor" );
        pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPrefabTitle" ), SW_LOCKEY( "EdCommon", "NotifyPrefabIsolated" ), NotificationType::Info );
        return true;
    }

    bool EditorPrefabIsolationCommands::exitPrefabIsolation( bool bSaveToPrefab )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return false;
        EditorWorkspace& ws = pContext->getSubsystem<EditorWorkspace>();
        if ( ws.isPrefabIsolationActive() == false )
            return false;
        const PrefabIsolationFrame* pFrame = ws.getPrefabIsolationFrame();
        if ( pFrame == nullptr )
            return false;

        const PrefabIsolationFrame frame         = *pFrame;
        SceneManager*              pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
            return false;
        Scene*             pScene   = pSceneManager->getActiveScene();
        GameObjectManager* pManager = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
        GameObject*        pRoot    = nullptr;
        if ( pManager != nullptr )
            pRoot = pManager->findGameObjectByID( frame._rootObjectID );

        // 저장하지 못하면 격리를 끝내지 않는다. 끝내면 편집한 프리팹 내용이 저장 없이 사라진다.
        const bool bPrefabSaveFailed = bSaveToPrefab && pRoot != nullptr && EditorInspectorCommands::applyToPrefab( pRoot, frame._prefabPath ) == false;
        if ( bPrefabSaveFailed )
        {
            SW_LOG_ERROR( "Prefab isolation could not save '%#' - still editing it", frame._prefabPath.c_str() );
            return false;
        }

        EditorPrefabIsolationCommandsInternal::restoreIsolationHidden( pManager, frame._listHidden );

        const bool bFullyExit          = ws.popPrefabIsolation();
        const bool bDiscardSpawnedRoot = frame._bSpawnedRoot == SW_TRUE && bSaveToPrefab == false && pManager != nullptr && pRoot != nullptr;
        if ( bDiscardSpawnedRoot )
        {
            if ( editor::getMainSceneSelection().hasObject( pRoot ) )
                editor::getMainSceneSelection().selectObject( pRoot, SelectionMode::Remove );
            pManager->destroyObject( pRoot );
            pRoot = nullptr;
        }
        else if ( frame._bSpawnedRoot == SW_TRUE && bSaveToPrefab )
            ws.markSceneDirty();

        if ( bFullyExit )
        {
            pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPrefabTitle" ), SW_LOCKEY( "EdCommon", "NotifyPrefabIsolationExited" ), NotificationType::Info );
            return true;
        }

        GameObject* pParentRoot = nullptr;
        if ( pManager != nullptr )
            pParentRoot = pManager->findGameObjectByID( ws.getPrefabIsolationRootID() );
        if ( pParentRoot != nullptr )
            ws.selectGameObject( pParentRoot );
        ws.setFocusedAssetPath( ws.getPrefabIsolationPrefabPath().c_str() );
        return true;
    }
} // namespace sw::editor
