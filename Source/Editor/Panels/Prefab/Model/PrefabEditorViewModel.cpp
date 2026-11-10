#include "pch.h"

#include "Editor/Panels/Prefab/Model/PrefabEditorViewModel.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/Commands/Scene/EditorPrefabIsolation.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        struct PrefabEditorViewModelInternal
        {
            /** @brief @p pScene 의 매니저입니다. 씬이 없으면 nullptr 입니다. */
            static GameObjectManager* findManager( Scene* pScene ) { return pScene != nullptr ? pScene->getObjectManager() : nullptr; }

            /** @brief @p object 와 그 자손을 깊이 우선으로 줄에 담습니다(지우는 중인 오브젝트는 뺀다). */
            static void collectRows( const GameObject& object, int32 depth, const EditorSelection& selection, vector<PrefabEditorChildRow>& outListRow )
            {
                PrefabEditorChildRow row{};
                row._name      = object.getName().c_str();
                row._objectID  = object.getObjectID();
                row._depth     = depth;
                row._bSelected = selection.hasObject( &object ) ? SW_TRUE : SW_FALSE;
                outListRow.push_back( std::move( row ) );
                vector<GameObject*> listChild;
                object.getChildren( listChild );
                for ( const GameObject* pChild : listChild )
                {
                    if ( pChild != nullptr && pChild->isPendingDestroy() == false )
                        collectRows( *pChild, depth + 1, selection, outListRow );
                }
            }
        };
    } // namespace

    PrefabEditorViewModel::PrefabEditorViewModel()
        : _pIsolation{ make_unique<EditorPrefabIsolation>() }
        , _pendingNestedPath{}
        , _spawnedDocumentPath{}
        , _status{}
    {
    }

    PrefabEditorViewModel::~PrefabEditorViewModel() = default;

    EditorAssetType PrefabEditorViewModel::getDocumentType()
    {
        return EditorAssetType{ PrefabAsset::kAssetType };
    }

    bool PrefabEditorViewModel::requestOpenWindow( string_view prefabPath, string_view panelID )
    {
        if ( prefabPath.empty() || EditorPlaySession::isStopped() == false )
            return false;
        EditorContext&   editorContext = editor::getSubsystem<EditorContext>();
        EditorWorkspace& workspace     = editorContext.getSubsystem<EditorWorkspace>();
        workspace.setFocusedAssetPath( string{ prefabPath }.c_str() ); // 문서 패널이 다음 그리기에서 문서를 바꾼다
        workspace.requestOpenPanelByID( panelID );
        editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPrefabTitle" ), SW_LOCKEY( "EdCommon", "NotifyPrefabIsolated" ),
                                                                      NotificationKind::Info );
        return true;
    }

    void PrefabEditorViewModel::notifyExited()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPrefabTitle" ), SW_LOCKEY( "EdCommon", "NotifyPrefabIsolationExited" ),
                                                                      NotificationKind::Info );
    }

    void PrefabEditorViewModel::gather( Scene* pScene, const EditorSelection& selection, PrefabEditorViewState& outState ) const
    {
        outState._listChildRow.clear();
        outState._prefabPath        = getPrefabPath();
        outState._status            = _status;
        outState._bIsolating        = _pIsolation->isActive() ? SW_TRUE : SW_FALSE;
        outState._bNested           = _pIsolation->getDepth() > 1 ? SW_TRUE : SW_FALSE;
        GameObjectManager* pManager = PrefabEditorViewModelInternal::findManager( pScene );
        const GameObject*  pTop     = pManager != nullptr ? pManager->findGameObjectByID( _pIsolation->getRootObjectID() ) : nullptr;
        if ( pTop != nullptr )
            PrefabEditorViewModelInternal::collectRows( *pTop, 0, selection, outState._listChildRow );
    }

    bool PrefabEditorViewModel::isIsolating() const
    {
        return _pIsolation->isActive();
    }

    string_view PrefabEditorViewModel::getPrefabPath() const
    {
        return _pIsolation->getPrefabPath();
    }

    void PrefabEditorViewModel::resetForNewScene()
    {
        _spawnedDocumentPath.clear(); // 새 씬에는 문서 루트가 아직 없다
        _pIsolation->clear();
    }

    bool PrefabEditorViewModel::rebuildRoot( Scene* pScene, EditorSelection& selection, string_view documentPath )
    {
        GameObjectManager* pManager = PrefabEditorViewModelInternal::findManager( pScene );
        if ( pManager == nullptr )
            return false;
        // 옛 문서 — 루트를 지우면 중첩 단계의 루트 · 숨긴 오브젝트도 함께 사라진다(모두 문서 루트의 자손이다).
        GameObject* pOldRoot = _pIsolation->isActive() ? pManager->findGameObjectByID( _pIsolation->getDocumentRootObjectID() ) : nullptr;
        selection.clearObjectSelection();
        _pIsolation->clear();
        if ( pOldRoot != nullptr )
            pManager->destroyGameObject( pOldRoot );
        _spawnedDocumentPath = documentPath;

        GameObject* pRoot = EditorAssetCommands::spawnPrefabWithoutUndo( pManager, _spawnedDocumentPath.c_str(), nullptr );
        if ( pRoot == nullptr )
        {
            _status = "Could not spawn the prefab";
            return false;
        }
        _status.clear();
        _pIsolation->push( *pManager, documentPath, *pRoot, true );
        selection.selectObject( pRoot );
        return true;
    }

    void PrefabEditorViewModel::enterPendingNested( Scene* pScene, EditorSelection& selection )
    {
        if ( _pendingNestedPath.empty() )
            return;
        const string path = std::move( _pendingNestedPath );
        _pendingNestedPath.clear();
        if ( enterNested( pScene, selection, path ) == false )
            _status = "Could not open the nested prefab";
    }

    bool PrefabEditorViewModel::enterNested( Scene* pScene, EditorSelection& selection, string_view prefabPath )
    {
        GameObjectManager* pManager     = PrefabEditorViewModelInternal::findManager( pScene );
        const bool         bCannotEnter = pManager == nullptr || prefabPath.empty() || _pIsolation->isActive() == false;
        if ( bCannotEnter )
            return false;

        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        GameObject*    pUnderRoot    = pManager->findGameObjectByID( _pIsolation->getRootObjectID() );
        GameObject*    pRoot =
            EditorPrefabIsolation::findInstance( *pManager, editorContext.getSubsystem<EditorWorkspace>(), prefabPath, selection.getPrimaryObject(), pUnderRoot );
        bool bSpawned{ false };
        if ( pRoot == nullptr )
        {
            const string path{ prefabPath };
            pRoot = EditorAssetCommands::spawnPrefabWithoutUndo( pManager, path.c_str(), pUnderRoot );
            if ( pRoot == nullptr )
                return false;
            bSpawned = true;
        }
        _pIsolation->push( *pManager, prefabPath, *pRoot, bSpawned );
        selection.selectObject( pRoot );
        return true;
    }

    PrefabFrameExit PrefabEditorViewModel::exitFrame( Scene* pScene, EditorSelection& selection, bool bSaveToPrefab )
    {
        GameObjectManager*          pManager = PrefabEditorViewModelInternal::findManager( pScene );
        const PrefabIsolationFrame* pFrame   = _pIsolation->getFrame();
        if ( pManager == nullptr || pFrame == nullptr )
            return PrefabFrameExit::Failed;

        const PrefabIsolationFrame frame = *pFrame;
        GameObject*                pRoot = pManager->findGameObjectByID( frame._rootObjectID );

        // 저장하지 못하면 나가지 않는다. 나가면 편집한 프리팹 내용이 저장 없이 사라진다.
        const bool bSaveFailed = bSaveToPrefab && pRoot != nullptr && EditorInspectorCommands::applyToPrefab( pRoot, AssetID{ frame._prefabPath } ) == false;
        if ( bSaveFailed )
        {
            SW_LOG_ERROR_ID( "PrefabEditorPanel.PrefabWindowCouldNot", "Prefab window could not save '%#' - still editing it", frame._prefabPath.c_str() );
            _status = "Could not save the prefab";
            return PrefabFrameExit::Failed;
        }
        if ( _pIsolation->getDepth() <= 1 )
            return PrefabFrameExit::DocumentStep;

        PrefabIsolationFrame popped;
        (void)_pIsolation->pop( pManager, popped );
        const bool bDiscardSpawnedRoot = popped._bSpawnedRoot == SW_TRUE && bSaveToPrefab == false && pRoot != nullptr;
        if ( bDiscardSpawnedRoot )
        {
            selection.selectObject( pRoot, SelectionMode::Remove );
            pManager->destroyGameObject( pRoot );
        }
        selection.selectObject( pManager->findGameObjectByID( _pIsolation->getRootObjectID() ) );
        return PrefabFrameExit::PoppedNested;
    }

    bool PrefabEditorViewModel::saveFrame( Scene* pScene )
    {
        GameObjectManager*          pManager = PrefabEditorViewModelInternal::findManager( pScene );
        const PrefabIsolationFrame* pFrame   = _pIsolation->getFrame();
        GameObject*                 pRoot    = ( pManager != nullptr && pFrame != nullptr ) ? pManager->findGameObjectByID( pFrame->_rootObjectID ) : nullptr;
        if ( pRoot == nullptr || EditorInspectorCommands::applyToPrefab( pRoot, AssetID{ pFrame->_prefabPath } ) == false )
        {
            _status = "Could not save the prefab";
            return false;
        }
        _status = "Saved";
        return true;
    }

    void PrefabEditorViewModel::selectObject( Scene* pScene, EditorSelection& selection, const uint64 objectID ) const
    {
        GameObjectManager* pManager = PrefabEditorViewModelInternal::findManager( pScene );
        GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByID( objectID ) : nullptr;
        if ( pObject != nullptr )
            selection.selectObject( pObject );
    }
} // namespace sw::editor
