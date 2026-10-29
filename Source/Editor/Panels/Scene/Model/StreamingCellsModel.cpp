#include "pch.h"

#include "Editor/Panels/Scene/Model/StreamingCellsModel.h"

#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneSettings.h"
#include "Engine/Scene/StreamingCellGrid.h"

namespace sw::editor
{
    StreamingCellsModel::StreamingCellsModel()
        : _preview{}
        , _previewSceneGeneration{ 0 }
        , _bHasPreview{ SW_FALSE }
        , _bLastFailed{ SW_FALSE }
        , _reservedFlag{ 0 }
    {
    }

    bool StreamingCellsModel::refreshPreview()
    {
        _preview                   = StreamingCellPreview{};
        _bHasPreview               = SW_FALSE;
        Scene*        pScene       = editor::getActiveScene();
        SceneManager& sceneManager = editor::getSubsystem<SceneManager>();
        if ( pScene == nullptr )
            return false; // 활성 씬이 없을 수 있다 — 패널이 빈 안내를 그린다
        const bool bSplit = SceneCooker::previewStreamingCells( *pScene, _preview );
        _bLastFailed      = bSplit ? SW_FALSE : SW_TRUE;
        if ( bSplit == false )
            return false;
        _previewSceneGeneration = sceneManager.getSceneGeneration();
        _bHasPreview            = SW_TRUE;
        return true;
    }

    void StreamingCellsModel::discardIfSceneChanged( uint64 sceneGeneration )
    {
        if ( _bHasPreview == SW_FALSE || _previewSceneGeneration == sceneGeneration )
            return;
        _preview     = StreamingCellPreview{};
        _bHasPreview = SW_FALSE;
    }

    StreamingCellPreviewState StreamingCellsModel::computeState( const StreamingCellGrid& grid, const SceneSettings& settings, const StreamingCellCoord& cell, const CameraComponent* pCamera )
    {
        if ( pCamera == nullptr )
            return StreamingCellPreviewState::NoCamera;
        const float32 distanceSquared = grid.computeCellDistanceSquared( cell, pCamera->getWorldPosition() );
        const float32 loadRadius      = grid.getLoadingRadius();
        const float32 keepRadius      = loadRadius + settings._unloadMargin;
        if ( distanceSquared <= loadRadius * loadRadius )
            return StreamingCellPreviewState::Load;
        return distanceSquared <= keepRadius * keepRadius ? StreamingCellPreviewState::Keep : StreamingCellPreviewState::Unload;
    }

    const utf8* StreamingCellsModel::getStateLabel( StreamingCellPreviewState state )
    {
        switch ( state )
        {
            case StreamingCellPreviewState::Load:
                return "Load";
            case StreamingCellPreviewState::Keep:
                return "Keep";
            case StreamingCellPreviewState::Unload:
                return "Unload";
            case StreamingCellPreviewState::NoCamera:
                return "-";
        }
        return "-";
    }

    void StreamingCellsModel::selectObjects( const vector<uint64>& listObjectID )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr )
            return;
        vector<GameObject*> listObject;
        listObject.reserve( listObjectID.size() );
        for ( const uint64 objectID : listObjectID )
        {
            GameObject* pObject = objectID != 0 ? pManager->findGameObjectByID( objectID ) : nullptr;
            if ( pObject != nullptr )
                listObject.push_back( pObject );
        }
        editor::getMainSceneSelection().selectObjects( listObject );
    }

    string StreamingCellsModel::findObjectName( uint64 objectID )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        GameObject*        pObject  = pManager != nullptr && objectID != 0 ? pManager->findGameObjectByID( objectID ) : nullptr;
        return pObject != nullptr ? string( pObject->getName().c_str() ) : string( "?" );
    }
} // namespace sw::editor
