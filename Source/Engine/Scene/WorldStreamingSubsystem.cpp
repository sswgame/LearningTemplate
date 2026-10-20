#include "pch.h"

#include "Engine/Scene/WorldStreamingSubsystem.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    SW_LOG_CALLER( "WorldStreaming" );

    // 셀을 짓는 일은 파괴를 처리한 뒤(`DestroyPost`)다 — 이번 프레임에 내린 셀의 오브젝트와 겹치지 않고, 만든 오브젝트는 다음 프레임 병합에서 틱에 든다.
    SW_SCENE_SERVICE( WorldStreamingSubsystem, DestroyPost )

    WorldStreamingSubsystem::WorldStreamingSubsystem( GameObjectManager& /*manager*/ )
        : _pScene{ nullptr }
        , _settings{}
        , _grid{}
        , _listCell{}
        , _mapCellIndex{}
        , _listExternalSource{}
        , _cameraSource{}
        , _bCameraSourceSet{ false }
        , _bExternalSources{ false }
        , _listScratchCell{}
        , _listScratchLoad{}
    {
    }

    bool WorldStreamingSubsystem::beginWorld( Scene& scene, const WorldSettings& settings, const vector<SceneDocument::StreamingCell>& listCell )
    {
        endWorld();
        if ( settings._bStreaming == false || settings.validate( scene.getName() ) == false )
        {
            SW_LOG_ERROR( "Scene '%#' has streaming cells but no usable streaming settings - its cells are not loaded", scene.getName() );
            return false;
        }
        if ( WorldCellGrid::tryCreate( settings._cellGrid, _grid ) == false )
            return false;

        _listCell.reserve( listCell.size() );
        _mapCellIndex.reserve( listCell.size() );
        for ( const SceneDocument::StreamingCell& cell : listCell )
        {
            const bool bInserted = _mapCellIndex.emplace( makeCellKey( cell._cell ), static_cast<uint32>( _listCell.size() ) ).second;
            if ( bInserted == false )
            {
                SW_LOG_ERROR( "Scene '%#' lists cell (%#, %#) twice - its cells are not loaded", scene.getName(), cell._cell._x, cell._cell._z );
                _listCell.clear();
                _mapCellIndex.clear();
                return false;
            }
            CellSlot slot{};
            slot._cell     = cell._cell;
            slot._listNode = cell._listSceneObjectNode;
            _listCell.push_back( std::move( slot ) );
        }
        _settings = settings;
        _pScene   = &scene;
        SW_LOG_INFO( "Scene '%#' streams %# cells (cell %# m, radius %# m)", scene.getName(), static_cast<uint32>( _listCell.size() ),
                     _settings._cellGrid._cellSize, _settings._cellGrid._loadingRadius );
        return true;
    }

    void WorldStreamingSubsystem::endWorld()
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

    void WorldStreamingSubsystem::onSceneCleared()
    {
        // 매니저가 오브젝트를 이미 비웠다 — 내릴 것 없이 셀만 버린다.
        _listCell.clear();
        _mapCellIndex.clear();
        _pScene           = nullptr;
        _bCameraSourceSet = false;
    }

    void WorldStreamingSubsystem::setCameraSource( const float3& position )
    {
        _cameraSource     = position;
        _bCameraSourceSet = true;
    }

    void WorldStreamingSubsystem::setExternalSources( span<const float3> listSource )
    {
        _listExternalSource.assign( listSource.begin(), listSource.end() );
        _bExternalSources = true;
    }

    void WorldStreamingSubsystem::clearExternalSources()
    {
        _listExternalSource.clear();
        _bExternalSources = false;
    }

    uint32 WorldStreamingSubsystem::getLoadedCellCount() const
    {
        uint32 count = 0;
        for ( const CellSlot& slot : _listCell )
        {
            count += slot._bLoaded ? 1u : 0u;
        }
        return count;
    }

    bool WorldStreamingSubsystem::isCellLoaded( const WorldCellCoord& cell ) const
    {
        const auto indexIt = _mapCellIndex.find( makeCellKey( cell ) );
        return indexIt != _mapCellIndex.end() && _listCell[indexIt->second]._bLoaded;
    }

    void WorldStreamingSubsystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        if ( _pScene == nullptr )
            return;

        // 2. 모든 원점에서 로딩 반경 + 여유보다 먼 셀을 내린다.
        const float32 unloadRadius        = _settings._cellGrid._loadingRadius + _settings._unloadMargin;
        const float32 unloadRadiusSquared = unloadRadius * unloadRadius;
        for ( CellSlot& slot : _listCell )
        {
            if ( slot._bLoaded && computeNearestSourceDistanceSquared( slot._cell ) > unloadRadiusSquared )
                unloadCell( slot );
        }

        // 1. 원점마다 로딩 반경 안의 셀(합집합) 중 내려가 있는 것을 모은다.
        _listScratchLoad.clear();
        for ( const float3& source : getActiveSources() )
        {
            _grid.computeCellsInRange( source, _listScratchCell );
            for ( const WorldCellCoord& cell : _listScratchCell )
            {
                const auto indexIt = _mapCellIndex.find( makeCellKey( cell ) );
                if ( indexIt == _mapCellIndex.end() )
                    continue; // 엔티티가 없는 셀이다
                const CellSlot& slot     = _listCell[indexIt->second];
                const bool      bSettled = slot._bLoaded || slot._bFailed; // 이미 올라 있거나 짓지 못한 셀이다
                if ( bSettled )
                    continue;
                if ( std::find( _listScratchLoad.begin(), _listScratchLoad.end(), indexIt->second ) == _listScratchLoad.end() )
                    _listScratchLoad.push_back( indexIt->second );
            }
        }
        if ( _listScratchLoad.empty() )
            return;

        // 3. 가까운 셀부터 프레임당 예산만큼 짓는다. 같은 거리는 셀 순서(z, x)로 — 결정적이다.
        std::sort( _listScratchLoad.begin(), _listScratchLoad.end(), [this]( uint32 lhs, uint32 rhs )
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

    uint64 WorldStreamingSubsystem::makeCellKey( const WorldCellCoord& cell )
    {
        return ( static_cast<uint64>( static_cast<uint32>( cell._x ) ) << 32 ) | static_cast<uint64>( static_cast<uint32>( cell._z ) );
    }

    span<const float3> WorldStreamingSubsystem::getActiveSources() const
    {
        if ( _bExternalSources )
            return span<const float3>( _listExternalSource.data(), _listExternalSource.size() );
        if ( _bCameraSourceSet )
            return span<const float3>( &_cameraSource, 1 );
        return {};
    }

    float32 WorldStreamingSubsystem::computeNearestSourceDistanceSquared( const WorldCellCoord& cell ) const
    {
        float32 nearest = MathUtil::kMaxFloat;
        for ( const float3& source : getActiveSources() )
        {
            nearest = MathUtil::min( nearest, _grid.computeCellDistanceSquared( cell, source ) );
        }
        return nearest;
    }

    void WorldStreamingSubsystem::loadCell( CellSlot& slot )
    {
        slot._listObjectID.clear();
        if ( _pScene->instantiateCell( slot._listNode, slot._listObjectID ) == false )
        {
            SW_LOG_ERROR( "Scene '%#' could not load cell (%#, %#) - it stays unloaded", _pScene->getName(), slot._cell._x, slot._cell._z );
            // 반쯤 지은 것을 남기지 않는다.
            _pScene->unloadCell( slot._listObjectID );
            slot._listObjectID.clear();
            slot._bFailed = true;
            return;
        }
        slot._bLoaded = true;
    }

    void WorldStreamingSubsystem::unloadCell( CellSlot& slot )
    {
        if ( slot._bLoaded == false )
            return;
        _pScene->unloadCell( slot._listObjectID );
        slot._listObjectID.clear();
        slot._bLoaded = false;
    }
} // namespace sw
