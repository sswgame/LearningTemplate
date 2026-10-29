#include "pch.h"

#include "Editor/Panels/Prefab/Model/PrefabEditorModel.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    SW_LOG_CALLER( "PrefabEditorPanel" );

    PrefabEditorModel::PrefabEditorModel()
        : _isolation{}
        , _pendingNestedPath{}
        , _spawnedDocumentPath{}
        , _status{}
    {
    }

    void PrefabEditorModel::resetForNewScene()
    {
        _spawnedDocumentPath.clear(); // 새 씬에는 문서 루트가 아직 없다
        _isolation.clear();
    }

    bool PrefabEditorModel::rebuildRoot( GameObjectManager& manager, EditorSelection& selection, string_view documentPath )
    {
        // 옛 문서 — 루트를 지우면 중첩 단계의 루트 · 숨긴 오브젝트도 함께 사라진다(모두 문서 루트의 자손이다).
        GameObject* pOldRoot = _isolation.isActive() ? manager.findGameObjectByID( _isolation.getDocumentRootObjectID() ) : nullptr;
        selection.clearObjectSelection();
        _isolation.clear();
        if ( pOldRoot != nullptr )
            manager.destroyObject( pOldRoot );
        _spawnedDocumentPath = documentPath;

        GameObject* pRoot = EditorAssetCommands::spawnPrefabWithoutUndo( &manager, _spawnedDocumentPath.c_str(), nullptr );
        if ( pRoot == nullptr )
        {
            _status = "Could not spawn the prefab";
            return false;
        }
        _status.clear();
        _isolation.push( manager, documentPath, *pRoot, true );
        selection.selectObject( pRoot );
        return true;
    }

    void PrefabEditorModel::enterPendingNested( GameObjectManager* pManager, EditorSelection& selection )
    {
        if ( _pendingNestedPath.empty() )
            return;
        const string path = std::move( _pendingNestedPath );
        _pendingNestedPath.clear();
        if ( enterNested( pManager, selection, path ) == false )
            _status = "Could not open the nested prefab";
    }

    bool PrefabEditorModel::enterNested( GameObjectManager* pManager, EditorSelection& selection, string_view prefabPath )
    {
        const bool bCannotEnter = pManager == nullptr || prefabPath.empty() || _isolation.isActive() == false;
        if ( bCannotEnter )
            return false;

        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        GameObject*    pUnderRoot    = pManager->findGameObjectByID( _isolation.getRootObjectID() );
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
        _isolation.push( *pManager, prefabPath, *pRoot, bSpawned );
        selection.selectObject( pRoot );
        return true;
    }

    PrefabFrameExit PrefabEditorModel::exitFrame( GameObjectManager* pManager, EditorSelection& selection, bool bSaveToPrefab )
    {
        const PrefabIsolationFrame* pFrame = _isolation.getFrame();
        if ( pManager == nullptr || pFrame == nullptr )
            return PrefabFrameExit::Failed;

        const PrefabIsolationFrame frame = *pFrame;
        GameObject*                pRoot = pManager->findGameObjectByID( frame._rootObjectID );

        // 저장하지 못하면 나가지 않는다. 나가면 편집한 프리팹 내용이 저장 없이 사라진다.
        const bool bSaveFailed = bSaveToPrefab && pRoot != nullptr && EditorInspectorCommands::applyToPrefab( pRoot, AssetID{ frame._prefabPath } ) == false;
        if ( bSaveFailed )
        {
            SW_LOG_ERROR( "Prefab window could not save '%#' - still editing it", frame._prefabPath.c_str() );
            _status = "Could not save the prefab";
            return PrefabFrameExit::Failed;
        }
        if ( _isolation.getDepth() <= 1 )
            return PrefabFrameExit::DocumentStep;

        PrefabIsolationFrame popped;
        (void)_isolation.pop( pManager, popped );
        const bool bDiscardSpawnedRoot = popped._bSpawnedRoot == SW_TRUE && bSaveToPrefab == false && pRoot != nullptr;
        if ( bDiscardSpawnedRoot )
        {
            selection.selectObject( pRoot, SelectionMode::Remove );
            pManager->destroyObject( pRoot );
        }
        selection.selectObject( pManager->findGameObjectByID( _isolation.getRootObjectID() ) );
        return PrefabFrameExit::PoppedNested;
    }

    bool PrefabEditorModel::saveFrame( GameObjectManager* pManager )
    {
        const PrefabIsolationFrame* pFrame = _isolation.getFrame();
        GameObject*                 pRoot  = ( pManager != nullptr && pFrame != nullptr ) ? pManager->findGameObjectByID( pFrame->_rootObjectID ) : nullptr;
        if ( pRoot == nullptr || EditorInspectorCommands::applyToPrefab( pRoot, AssetID{ pFrame->_prefabPath } ) == false )
        {
            _status = "Could not save the prefab";
            return false;
        }
        _status = "Saved";
        return true;
    }
} // namespace sw::editor
