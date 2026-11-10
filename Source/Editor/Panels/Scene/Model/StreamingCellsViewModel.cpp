#include "pch.h"

#include "Editor/Panels/Scene/Model/StreamingCellsViewModel.h"

#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneSettings.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"
#include "Engine/Scene/StreamingCellGrid.h"

namespace sw::editor
{
    StreamingCellsViewModel::StreamingCellsViewModel()
        : _pPreview{ sw::make_unique<StreamingCellPreview>() }
        , _previewSceneGeneration{ 0 }
        , _bHasPreview{ SW_FALSE }
        , _bLastFailed{ SW_FALSE }
        , _reservedFlag{ 0 }
    {
    }

    StreamingCellsViewModel::~StreamingCellsViewModel() = default;

    void StreamingCellsViewModel::gather( StreamingCellsViewState& outState )
    {
        outState._listViolation.clear();
        outState._listCell.clear();
        outState._content          = StreamingCellsContent::NoActiveScene;
        outState._bLastSplitFailed = _bLastFailed;
        outState._bHasGameCamera   = SW_FALSE;

        Scene*        pScene        = editor::getActiveScene();
        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pScene == nullptr || pSceneManager == nullptr || pScene->getObjectManager() == nullptr )
            return;
        const SceneSettings& settings  = pScene->getSceneSettings();
        outState._cellSize             = settings._cellGrid._cellSize;
        outState._loadingRadius        = settings._cellGrid._loadingRadius;
        outState._unloadMargin         = settings._unloadMargin;
        outState._maxCellLoadsPerFrame = static_cast<uint32>( settings._maxCellLoadsPerFrame );
        if ( settings._bStreaming == false )
        {
            outState._content = StreamingCellsContent::NotStreaming;
            return;
        }
        // 쿠킹본으로 지은 씬은 이미 셀로 나뉘어 있고 문서로 쓰지 않는다 — 나눠 보지 않고 올라 있는 셀 수만 보인다.
        const SceneStreamingSubsystem& streaming = pScene->getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
        if ( streaming.isStreaming() )
        {
            outState._content         = StreamingCellsContent::CookedScene;
            outState._loadedCellCount = static_cast<uint32>( streaming.getLoadedCellCount() );
            outState._cellCount       = static_cast<uint32>( streaming.getCellCount() );
            return;
        }
        // 다른 씬의 결과는 그 씬의 오브젝트 id 라 버린다.
        if ( _bHasPreview != SW_FALSE && _previewSceneGeneration != pSceneManager->getSceneGeneration() )
        {
            *_pPreview   = StreamingCellPreview{};
            _bHasPreview = SW_FALSE;
        }
        if ( _bHasPreview == SW_FALSE )
        {
            outState._content = StreamingCellsContent::NoPreview;
            return;
        }

        const StreamingCellPreview& preview = *_pPreview;
        if ( preview._listViolation.empty() == false )
        {
            outState._content = StreamingCellsContent::Violations;
            outState._listViolation.reserve( preview._listViolation.size() );
            for ( const StreamingCellReference& violation : preview._listViolation )
                outState._listViolation.push_back( StreamingCellViolationRow{ findObjectName( violation._fromObjectID ), findObjectName( violation._toObjectID ) } );
            return;
        }

        StreamingCellGrid grid;
        if ( StreamingCellGrid::tryCreate( settings._cellGrid, grid ) == false )
        {
            outState._content = StreamingCellsContent::GridInvalid; // 미리보기가 섰다면 격자는 쓸 수 있다 — 그 사이 설정을 고쳤다(오류는 로그에)
            return;
        }
        const CameraComponent* pCamera = pScene->getActiveGameCamera();
        outState._content              = StreamingCellsContent::Cells;
        outState._bHasGameCamera       = pCamera != nullptr ? SW_TRUE : SW_FALSE;
        outState._alwaysLoadedCount    = static_cast<uint32>( preview._listAlwaysLoadedID.size() );
        outState._listCell.reserve( preview._listCell.size() );
        for ( const StreamingCellPreviewCell& cell : preview._listCell )
            outState._listCell.push_back( StreamingCellRow{ cell._cell._x, cell._cell._z, static_cast<uint32>( cell._listObjectID.size() ),
                                                            computeState( grid, settings, cell._cell, pCamera ) } );
    }

    bool StreamingCellsViewModel::refreshPreview()
    {
        *_pPreview                 = StreamingCellPreview{};
        _bHasPreview               = SW_FALSE;
        Scene*        pScene       = editor::getActiveScene();
        SceneManager& sceneManager = editor::getSubsystem<SceneManager>();
        if ( pScene == nullptr )
            return false; // 활성 씬이 없을 수 있다 — 패널이 빈 안내를 그린다
        const bool bSplit = SceneCooker::previewStreamingCells( *pScene, *_pPreview );
        _bLastFailed      = bSplit ? SW_FALSE : SW_TRUE;
        if ( bSplit == false )
            return false;
        _previewSceneGeneration = sceneManager.getSceneGeneration();
        _bHasPreview            = SW_TRUE;
        return true;
    }

    void StreamingCellsViewModel::selectViolationObjects( uint32 rowIndex )
    {
        if ( _bHasPreview == SW_FALSE || rowIndex >= _pPreview->_listViolation.size() )
            return;
        const StreamingCellReference& violation = _pPreview->_listViolation[rowIndex];
        selectObjects( vector<uint64>{ violation._fromObjectID, violation._toObjectID } );
    }

    void StreamingCellsViewModel::selectCellObjects( uint32 rowIndex )
    {
        if ( _bHasPreview == SW_FALSE || rowIndex >= _pPreview->_listCell.size() )
            return;
        selectObjects( _pPreview->_listCell[rowIndex]._listObjectID );
    }

    void StreamingCellsViewModel::selectAlwaysLoadedObjects()
    {
        if ( _bHasPreview == SW_FALSE )
            return;
        selectObjects( _pPreview->_listAlwaysLoadedID );
    }

    StreamingCellPreviewState StreamingCellsViewModel::computeState( const StreamingCellGrid& grid, const SceneSettings& settings, const StreamingCellCoord& cell, const CameraComponent* pCamera )
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

    const utf8* StreamingCellsViewModel::getStateLabel( StreamingCellPreviewState state )
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

    void StreamingCellsViewModel::selectObjects( const vector<uint64>& listObjectID )
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

    string StreamingCellsViewModel::findObjectName( uint64 objectID )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        GameObject*        pObject  = pManager != nullptr && objectID != 0 ? pManager->findGameObjectByID( objectID ) : nullptr;
        return pObject != nullptr ? string( pObject->getName().c_str() ) : string( "?" );
    }
} // namespace sw::editor
