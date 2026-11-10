#include "pch.h"

#include "Engine/Scene/SceneStreamingSubsystem.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDataLayerSubsystem.h"

namespace sw
{
    // 셀을 짓는 일은 파괴를 처리한 뒤(`DestroyPost`)다 — 이번 프레임에 내린 셀의 오브젝트와 겹치지 않고, 만든 오브젝트는 다음 프레임 병합에서 틱에 든다.
    SW_SCENE_SERVICE( SceneStreamingSubsystem, DestroyPost, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::Structure }, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Transform } ) )

    SceneStreamingSubsystem::SceneStreamingSubsystem( GameObjectManager& /*manager*/ )
        : _pScene{ nullptr }
        , _settings{}
        , _grid{}
        , _listCell{}
        , _mapCellIndex{}
        , _listExternalSource{}
        , _cameraSource{}
        , _bCameraSourceSet{ SW_FALSE }
        , _bExternalSources{ SW_FALSE }
        , _listScratchCell{}
        , _listScratchLoad{}
    {
    }

    bool SceneStreamingSubsystem::beginWorld( Scene& scene, const SceneSettings& settings, const vector<SceneDocument::StreamingCell>& listCell )
    {
        endWorld();
        if ( settings._bStreaming == false || settings.validate( scene.getName() ) == false )
        {
            SW_LOG_ERROR_ID( "SceneStreaming.SceneStreamingCellsBut", "Scene '%#' has streaming cells but no usable streaming settings - its cells are not loaded", scene.getName() );
            return false;
        }
        if ( StreamingCellGrid::tryCreate( settings._cellGrid, _grid ) == false )
            return false;

        _listCell.reserve( listCell.size() );
        _mapCellIndex.reserve( listCell.size() );
        for ( const SceneDocument::StreamingCell& cell : listCell )
        {
            vector<uint32>& listSlotIndex = _mapCellIndex[makeCellKey( cell._cell )];
            bool            bRepeated     = false;
            for ( const uint32 slotIndex : listSlotIndex )
            {
                bRepeated = bRepeated || _listCell[slotIndex]._listDataLayer == cell._listDataLayer;
            }
            if ( bRepeated )
            {
                SW_LOG_ERROR_ID( "SceneStreaming.SceneListsCellTwice", "Scene '%#' lists cell (%#, %#) twice - its cells are not loaded", scene.getName(), cell._cell._x, cell._cell._z );
                _listCell.clear();
                _mapCellIndex.clear();
                return false;
            }
            CellSlot slot{};
            slot._cell          = cell._cell;
            slot._listNode      = cell._listSceneObjectNode;
            slot._listDataLayer = cell._listDataLayer;
            listSlotIndex.push_back( static_cast<uint32>( _listCell.size() ) );
            _listCell.push_back( std::move( slot ) );
        }
        _settings = settings;
        _pScene   = &scene;
        SW_LOG_INFO_ID( "SceneStreaming.SceneStreamsCellsCell", "Scene '%#' streams %# cells (cell %# m, radius %# m)", scene.getName(), static_cast<uint32>( _listCell.size() ),
                        _settings._cellGrid._cellSize, _settings._cellGrid._loadingRadius );
        return true;
    }

    void SceneStreamingSubsystem::endWorld()
    {
        if ( _pScene != nullptr )
        {
            for ( CellSlot& slot : _listCell )
            {
                unloadCell( slot );
            }
        }
        _listCell.clear();
        _mapCellIndex.clear();
        _pScene = nullptr;
    }

    void SceneStreamingSubsystem::onSceneCleared()
    {
        // 매니저가 오브젝트를 이미 비웠다 — 내릴 것 없이 셀만 버린다.
        _listCell.clear();
        _mapCellIndex.clear();
        _pScene           = nullptr;
        _bCameraSourceSet = SW_FALSE;
    }

    void SceneStreamingSubsystem::setCameraSource( const float3& position )
    {
        _cameraSource     = position;
        _bCameraSourceSet = SW_TRUE;
    }

    void SceneStreamingSubsystem::setExternalSources( span<const float3> listSource )
    {
        _listExternalSource.assign( listSource.begin(), listSource.end() );
        _bExternalSources = SW_TRUE;
    }

    void SceneStreamingSubsystem::clearExternalSources()
    {
        _listExternalSource.clear();
        _bExternalSources = SW_FALSE;
    }

    uint32 SceneStreamingSubsystem::getLoadedCellCount() const
    {
        uint32 count = 0;
        for ( const CellSlot& slot : _listCell )
        {
            count += slot._bLoaded ? 1u : 0u;
        }
        return count;
    }

    bool SceneStreamingSubsystem::isCellLoaded( const StreamingCellCoord& cell ) const
    {
        // 좌표의 조각 가운데 하나라도 올라 있으면 올라 있다.
        const auto indexIt = _mapCellIndex.find( makeCellKey( cell ) );
        if ( indexIt == _mapCellIndex.end() )
            return false;
        for ( const uint32 slotIndex : indexIt->second )
        {
            if ( _listCell[slotIndex]._bLoaded )
                return true;
        }
        return false;
    }

    void SceneStreamingSubsystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        if ( _pScene == nullptr )
            return;

        // 2. 모든 원점에서 로딩 반경 + 여유보다 먼 셀을 내린다.
        const float32 unloadRadius        = _settings._cellGrid._loadingRadius + _settings._unloadMargin;
        const float32 unloadRadiusSquared = unloadRadius * unloadRadius;
        for ( CellSlot& slot : _listCell )
        {
            if ( slot._bLoaded && ( computeNearestSourceDistanceSquared( slot._cell ) > unloadRadiusSquared || areSlotLayersActive( slot ) == false ) )
                unloadCell( slot );
        }

        // 1. 원점마다 로딩 반경 안의 셀(합집합) 중 내려가 있는 것을 모은다.
        _listScratchLoad.clear();
        for ( const float3& source : getActiveSources() )
        {
            _grid.computeCellsInRange( source, _listScratchCell );
            for ( const StreamingCellCoord& cell : _listScratchCell )
            {
                const auto indexIt = _mapCellIndex.find( makeCellKey( cell ) );
                if ( indexIt == _mapCellIndex.end() )
                    continue; // 엔티티가 없는 셀이다
                for ( const uint32 slotIndex : indexIt->second )
                {
                    const CellSlot& slot     = _listCell[slotIndex];
                    const bool      bSettled = slot._bLoaded || slot._bFailed || areSlotLayersActive( slot ) == false; // 이미 올라 있거나 짓지 못한 셀이거나 레이어가 꺼졌다
                    if ( bSettled )
                        continue;
                    _listScratchLoad.push_back( slotIndex ); // 원점 반경이 겹치면 같은 셀이 여럿 들어온다 — 아래에서 한 번에 거른다
                }
            }
        }
        VectorUtil::sortUnique( _listScratchLoad ); // 모양이 `contains` + `push_back` 이면 셀 수의 제곱이 든다
        if ( _listScratchLoad.empty() )
            return;

        // 3. 가까운 셀부터 프레임당 예산만큼 짓는다. 같은 거리는 셀 순서(z, x)로 — 결정적이다.
        VectorUtil::sort( _listScratchLoad, [this]( uint32 lhs, uint32 rhs )
        {
            const float32 lhsDistance = computeNearestSourceDistanceSquared( _listCell[lhs]._cell );
            const float32 rhsDistance = computeNearestSourceDistanceSquared( _listCell[rhs]._cell );
            return lhsDistance != rhsDistance ? lhsDistance < rhsDistance : lhs < rhs;
        } );
        const size_t loadCount = MathUtil::min( _listScratchLoad.size(), static_cast<size_t>( _settings._maxCellLoadsPerFrame ) );
        for ( size_t loadIndex = 0; loadIndex < loadCount; ++loadIndex )
        {
            loadCell( _listCell[_listScratchLoad[loadIndex]] );
        }
    }

    uint64 SceneStreamingSubsystem::makeCellKey( const StreamingCellCoord& cell )
    {
        return ( static_cast<uint64>( static_cast<uint32>( cell._x ) ) << 32 ) | static_cast<uint64>( static_cast<uint32>( cell._z ) );
    }

    span<const float3> SceneStreamingSubsystem::getActiveSources() const
    {
        if ( _bExternalSources )
            return span<const float3>( _listExternalSource.data(), _listExternalSource.size() );
        if ( _bCameraSourceSet )
            return span<const float3>( &_cameraSource, 1 );
        return {};
    }

    float32 SceneStreamingSubsystem::computeNearestSourceDistanceSquared( const StreamingCellCoord& cell ) const
    {
        float32 nearest = MathUtil::kMaxFloat;
        for ( const float3& source : getActiveSources() )
        {
            nearest = MathUtil::min( nearest, _grid.computeCellDistanceSquared( cell, source ) );
        }
        return nearest;
    }

    bool SceneStreamingSubsystem::areSlotLayersActive( const CellSlot& slot ) const
    {
        return slot._listDataLayer.empty() || _pScene->getObjectManager()->getSubsystem<SceneDataLayerSubsystem>().areLayersActive( slot._listDataLayer );
    }

    void SceneStreamingSubsystem::loadCell( CellSlot& slot )
    {
        slot._listObjectID.clear();
        if ( _pScene->instantiateCell( slot._listNode, slot._listObjectID ) == false )
        {
            SW_LOG_ERROR_ID( "SceneStreaming.SceneCouldNotLoad", "Scene '%#' could not load cell (%#, %#) - it stays unloaded", _pScene->getName(), slot._cell._x, slot._cell._z );
            // 반쯤 지은 것을 남기지 않는다.
            _pScene->unloadCell( slot._listObjectID );
            slot._listObjectID.clear();
            slot._bFailed = SW_TRUE;
            return;
        }
        slot._bLoaded = SW_TRUE;
    }

    void SceneStreamingSubsystem::unloadCell( CellSlot& slot )
    {
        if ( slot._bLoaded == false )
            return;
        _pScene->unloadCell( slot._listObjectID );
        slot._listObjectID.clear();
        slot._bLoaded = SW_FALSE;
    }
} // namespace sw
