#include "pch.h"

#include "Engine/Scene/SceneStreamingSubsystem.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDataLayerSubsystem.h"

namespace sw
{
    // 셀을 짓는 일은 파괴를 처리한 뒤(`DestroyPost`)다 — 이번 프레임에 내린 셀의 오브젝트와 겹치지 않고, 만든 오브젝트는 다음 프레임 병합에서 틱에 든다.
    SW_SCENE_SERVICE( SceneStreamingSubsystem, DestroyPost, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::Structure }, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Transform } ) )

    /** @brief 워커가 채우는 읽기 결과입니다. 워커는 `_listNode` 를 채운 뒤 `_state` 를 release 로 올리고, 게임 스레드는 acquire 로 본 뒤에만 `_listNode` 를 가져간다. */
    struct SceneStreamingSubsystem::CellContentRead
    {
        enum class State : uint8
        {
            Reading,
            Read,
            Failed,
        };
        atomic<uint8>                          _state{ static_cast<uint8>( State::Reading ) };
        vector<SceneDocument::SceneObjectNode> _listNode;
    };

    SceneStreamingSubsystem::SceneStreamingSubsystem( GameObjectManager& /*manager*/ )
        : _pScene{ nullptr }
        , _settings{}
        , _contentPath{}
        , _grid{}
        , _listCell{}
        , _mapCellIndex{}
        , _listExternalSource{}
        , _cameraSource{}
        , _bCameraSourceSet{ SW_FALSE }
        , _bExternalSources{ SW_FALSE }
        , _changeSerial{ 0 }
        , _listScratchCell{}
        , _listScratchLoad{}
    {
    }

    bool SceneStreamingSubsystem::beginWorld( Scene& scene, const SceneSettings& settings, const vector<SceneDocument::StreamingCell>& listCell, string_view contentPath )
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
            const bool bNoContentFile = cell.hasFileContent() && contentPath.empty();
            if ( bRepeated || bNoContentFile )
            {
                SW_LOG_ERROR_ID( "SceneStreaming.SceneListsCellTwice", "Scene '%#' lists cell (%#, %#) twice or without a cooked file for its content - its cells are not loaded", scene.getName(),
                                 cell._cell._x, cell._cell._z );
                _listCell.clear();
                _mapCellIndex.clear();
                return false;
            }
            CellSlot slot{};
            slot._cell          = cell._cell;
            slot._listDataLayer = cell._listDataLayer;
            slot._contentOffset = cell._contentOffset;
            slot._contentSize   = cell._contentSize;
            // 메모리 문서의 셀(쿠커 · 테스트)은 내용이 이미 있다 — 읽지 않고 짓는다.
            if ( cell.hasFileContent() == false )
            {
                slot._listNode     = cell._listSceneObjectNode;
                slot._bContentHeld = SW_TRUE;
            }
            listSlotIndex.push_back( static_cast<uint32>( _listCell.size() ) );
            _listCell.push_back( std::move( slot ) );
        }
        _settings                    = settings;
        _contentPath                 = contentPath;
        _pScene                      = &scene;
        _cameraSource._loadingRadius = settings._cellGrid._loadingRadius;
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
                unloadCell( slot, false ); // 월드를 버린다 — 상태를 뜰 까닭이 없다
            }
        }
        _listCell.clear();
        _mapCellIndex.clear();
        _contentPath.clear();
        _pScene = nullptr;
    }

    void SceneStreamingSubsystem::onSceneCleared()
    {
        // 매니저가 오브젝트를 이미 비웠다 — 걸린 읽기만 거두고 셀을 버린다.
        for ( CellSlot& slot : _listCell )
        {
            if ( slot._state == CellState::Loading )
                (void)slot._readHandle.cancel();
            slot._pBuild.reset(); // 짓던 오브젝트는 매니저가 함께 비웠다 — 배치만 놓는다
        }
        _listCell.clear();
        _mapCellIndex.clear();
        _contentPath.clear();
        _pScene           = nullptr;
        _bCameraSourceSet = SW_FALSE;
    }

    void SceneStreamingSubsystem::setCameraSource( const float3& position )
    {
        _cameraSource._position      = position;
        _cameraSource._loadingRadius = _settings._cellGrid._loadingRadius;
        _bCameraSourceSet            = SW_TRUE;
    }

    void SceneStreamingSubsystem::setExternalSources( span<const float3> listSource )
    {
        _listExternalSource.clear();
        for ( const float3& position : listSource )
        {
            _listExternalSource.push_back( StreamingSource{ position, _settings._cellGrid._loadingRadius, StreamingSourcePriority::Normal } );
        }
        _bExternalSources = SW_TRUE;
    }

    bool SceneStreamingSubsystem::setExternalSources( span<const StreamingSource> listSource )
    {
        for ( const StreamingSource& source : listSource )
        {
            // NaN · 무한 반경은 모든 셀을 덮거나 아무 셀도 덮지 않는다 — 맞춰 넣지 않고 거절한다.
            if ( MathUtil::isFinite( source._loadingRadius ) == false || source._loadingRadius < 0.0f )
            {
                SW_LOG_ERROR_ID( "SceneStreaming.SourceRadiusInvalid", "Scene streaming: refused sources - a source radius must be finite and not negative (got %#)", source._loadingRadius );
                return false;
            }
        }
        _listExternalSource.assign( listSource.begin(), listSource.end() );
        _bExternalSources = SW_TRUE;
        return true;
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
            count += slot._state == CellState::Loaded ? 1u : 0u;
        }
        return count;
    }

    uint32 SceneStreamingSubsystem::getLoadingCellCount() const
    {
        uint32 count = 0;
        for ( const CellSlot& slot : _listCell )
        {
            count += slot._state == CellState::Loading ? 1u : 0u;
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
            if ( _listCell[slotIndex]._state == CellState::Loaded )
                return true;
        }
        return false;
    }

    bool SceneStreamingSubsystem::isCellStreamingCompleted( const StreamingCellCoord& cell ) const
    {
        const auto indexIt = _pScene != nullptr ? _mapCellIndex.find( makeCellKey( cell ) ) : _mapCellIndex.end();
        if ( indexIt == _mapCellIndex.end() )
            return true; // 스트리밍하지 않거나 엔티티가 없는 셀이다 — 기다릴 것이 없다
        for ( const uint32 slotIndex : indexIt->second )
        {
            const CellSlot& slot = _listCell[slotIndex];
            if ( slot._state != CellState::Loaded && slot._state != CellState::Failed && areSlotLayersActive( slot ) )
                return false;
        }
        return true;
    }

    void SceneStreamingSubsystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        if ( _pScene == nullptr )
            return;
        const bool bFinishEachFrame = engine::getSubsystem<DeterministicRun>().getPolicy()._bFinishAssetJobsEachFrame == SW_TRUE;

        // 1. 모든 원점에서 (그 원점 반경 + 여유)보다 멀거나 레이어가 꺼진 셀을 되돌린다(읽기 취소 · 상태를 떠 두고 내리기).
        for ( CellSlot& slot : _listCell )
        {
            const bool bActive = slot._state != CellState::Unloaded && slot._state != CellState::Failed;
            if ( bActive && ( isCellWithinSources( slot._cell, _settings._unloadMargin ) == false || areSlotLayersActive( slot ) == false ) )
                unloadCell( slot, true );
        }

        // 2. 원점마다 그 반경 안의 셀(합집합) 중 아직 지어지지 않은 것을 모은다.
        _listScratchLoad.clear();
        for ( const StreamingSource& source : getActiveSources() )
        {
            _grid.computeCellsInRadius( source._position, source._loadingRadius, _listScratchCell );
            for ( const StreamingCellCoord& cell : _listScratchCell )
            {
                const auto indexIt = _mapCellIndex.find( makeCellKey( cell ) );
                if ( indexIt == _mapCellIndex.end() )
                    continue; // 엔티티가 없는 셀이다
                for ( const uint32 slotIndex : indexIt->second )
                {
                    const CellSlot& slot    = _listCell[slotIndex];
                    const bool      bStable = slot._state == CellState::Loaded || slot._state == CellState::Failed || areSlotLayersActive( slot ) == false; // 이미 올라 있거나 짓지 못한 셀이거나 레이어가 꺼졌다
                    if ( bStable )
                        continue;
                    _listScratchLoad.push_back( slotIndex ); // 원점 반경이 겹치면 같은 셀이 여럿 들어온다 — 아래에서 한 번에 거른다
                }
            }
        }
        VectorUtil::sortUnique( _listScratchLoad ); // 모양이 `contains` + `push_back` 이면 셀 수의 제곱이 든다
        // 덮는 원점의 우선순위가 급한 셀부터, 같으면 가까운 셀부터. 같은 거리는 셀 순서(z, x, 레이어 조각)로 — 결정적이다.
        VectorUtil::sort( _listScratchLoad, [this]( uint32 lhs, uint32 rhs )
        {
            const StreamingSourcePriority lhsPriority = computeCellPriority( _listCell[lhs]._cell );
            const StreamingSourcePriority rhsPriority = computeCellPriority( _listCell[rhs]._cell );
            if ( lhsPriority != rhsPriority )
                return lhsPriority < rhsPriority;
            const float32 lhsDistance = computeNearestSourceDistanceSquared( _listCell[lhs]._cell );
            const float32 rhsDistance = computeNearestSourceDistanceSquared( _listCell[rhs]._cell );
            return lhsDistance != rhsDistance ? lhsDistance < rhsDistance : lhs < rhs;
        } );

        // 3. 내용이 없는 셀의 읽기를 프레임당 `_maxCellLoadsPerFrame` 개까지 건다. 내용을 든 셀(메모리 문서 · 떠 둔 상태)은 읽지 않고 짓기로 간다.
        uint32 requestCount = 0;
        for ( const uint32 slotIndex : _listScratchLoad )
        {
            CellSlot& slot = _listCell[slotIndex];
            if ( slot._state != CellState::Unloaded )
                continue;
            if ( slot._bContentHeld )
            {
                slot._state = CellState::Ready;
                continue;
            }
            if ( requestCount >= _settings._maxCellLoadsPerFrame )
                continue;
            requestCount += 1;
            requestCellContent( slot );
        }

        // 4. 끝난 읽기를 거둔다(범위 밖으로 나가 여유 안에 머문 셀의 읽기도). 결정적 실행은 이 프레임에 기다린다.
        for ( CellSlot& slot : _listCell )
        {
            if ( slot._state != CellState::Loading )
                continue;
            if ( bFinishEachFrame )
                slot._readHandle.wait();
            collectCellContent( slot );
        }

        // 5. 셀을 가까운 순으로 시간 예산 안에서 엔티티 단위로 짓는다(언리얼 AddToWorld 시간 예산). 짓는 중인 셀을 먼저 잇고(반쯤 지은 셀이 쌓이지 않게) 그다음 내용이 온 셀을 시작한다.
        //    엔티티 하나는 예산과 상관없이 짓는다 — 엔티티 하나가 예산보다 커도 멈추지 않게. 결정적 실행은 예산 없이 끝까지 짓는다.
        const int64     budgetNanoseconds = bFinishEachFrame ? 0 : static_cast<int64>( _settings._addToWorldBudgetMicroseconds ) * constant::time::kNanosecondsPerMicrosecond;
        const Stopwatch stopwatch;
        bool            bProgressed = false;
        for ( const CellState pass : { CellState::Building, CellState::Ready } )
        {
            for ( const uint32 slotIndex : _listScratchLoad )
            {
                CellSlot& slot = _listCell[slotIndex];
                if ( slot._state != pass || ( pass == CellState::Ready && areEarlierSlotsBuilt( slotIndex ) == false ) )
                    continue;
                if ( bProgressed && budgetNanoseconds > 0 && stopwatch.getElapsedNanoseconds() >= budgetNanoseconds )
                    return; // 남은 셀은 다음 프레임에 잇는다
                const bool bFinished = buildCell( slot, stopwatch, budgetNanoseconds, bProgressed == false );
                bProgressed          = true;
                if ( bFinished == false )
                    return; // 예산에 닿았다
            }
        }
    }

    void SceneStreamingSubsystem::collectCellsChangedSince( const uint64 serial, vector<StreamingCellCoord>& outListCell ) const
    {
        outListCell.clear();
        for ( const CellSlot& slot : _listCell )
        {
            if ( slot._changeSerial > serial )
                outListCell.push_back( slot._cell );
        }
    }

    uint64 SceneStreamingSubsystem::makeCellKey( const StreamingCellCoord& cell )
    {
        return ( static_cast<uint64>( static_cast<uint32>( cell._x ) ) << 32 ) | static_cast<uint64>( static_cast<uint32>( cell._z ) );
    }

    span<const StreamingSource> SceneStreamingSubsystem::getActiveSources() const
    {
        if ( _bExternalSources )
            return span<const StreamingSource>( _listExternalSource.data(), _listExternalSource.size() );
        if ( _bCameraSourceSet )
            return span<const StreamingSource>( &_cameraSource, 1 );
        return {};
    }

    float32 SceneStreamingSubsystem::computeNearestSourceDistanceSquared( const StreamingCellCoord& cell ) const
    {
        float32 nearest = MathUtil::kMaxFloat;
        for ( const StreamingSource& source : getActiveSources() )
        {
            nearest = MathUtil::min( nearest, _grid.computeCellDistanceSquared( cell, source._position ) );
        }
        return nearest;
    }

    StreamingSourcePriority SceneStreamingSubsystem::computeCellPriority( const StreamingCellCoord& cell ) const
    {
        StreamingSourcePriority priority = StreamingSourcePriority::Lowest;
        for ( const StreamingSource& source : getActiveSources() )
        {
            const bool bCovers = _grid.computeCellDistanceSquared( cell, source._position ) <= source._loadingRadius * source._loadingRadius;
            if ( bCovers && source._priority < priority )
                priority = source._priority;
        }
        return priority;
    }

    bool SceneStreamingSubsystem::isCellWithinSources( const StreamingCellCoord& cell, const float32 margin ) const
    {
        for ( const StreamingSource& source : getActiveSources() )
        {
            const float32 radius = source._loadingRadius + margin;
            if ( _grid.computeCellDistanceSquared( cell, source._position ) <= radius * radius )
                return true;
        }
        return false;
    }

    bool SceneStreamingSubsystem::areSlotLayersActive( const CellSlot& slot ) const
    {
        return slot._listDataLayer.empty() || _pScene->getObjectManager()->getSubsystem<SceneDataLayerSubsystem>().areLayersActive( slot._listDataLayer );
    }

    bool SceneStreamingSubsystem::areEarlierSlotsBuilt( const uint32 slotIndex ) const
    {
        const auto indexIt = _mapCellIndex.find( makeCellKey( _listCell[slotIndex]._cell ) );
        if ( indexIt == _mapCellIndex.end() )
            return true;
        for ( const uint32 earlierIndex : indexIt->second )
        {
            if ( earlierIndex == slotIndex )
                return true; // 조각은 문서 순서다 — 여기까지가 앞 조각이다
            const CellSlot& earlier   = _listCell[earlierIndex];
            const bool      bComplete = earlier._state == CellState::Loaded || earlier._state == CellState::Failed || areSlotLayersActive( earlier ) == false;
            if ( bComplete == false )
                return false;
        }
        return true;
    }

    void SceneStreamingSubsystem::requestCellContent( CellSlot& slot )
    {
        // 읽기는 IO 스레드가, 풀기는 완료를 받은 태스크 워커가 한다 — 게임 스레드는 결과의 상태만 본다(`collectCellContent`).
        const shared_ptr<CellContentRead> pRead      = sw::make_shared<CellContentRead>();
        const string                      sourcePath = _contentPath;
        slot._readHandle                             = AssetIO::readBinaryAssetRangeAsync( AssetID{ _contentPath }, slot._contentOffset, slot._contentSize, AsyncIOPriority::Normal,
                                                                                           SW_DELEGATE_LAMBDA( AssetReadCompleteDelegate, [pRead, sourcePath]( bool bSuccess, vector<uint8>& bytes )
                                    {
            const bool                   bParsed = bSuccess && SceneDocument::readStreamingCellContent( bytes.data(), bytes.size(), pRead->_listNode, sourcePath );
            const CellContentRead::State state   = bParsed ? CellContentRead::State::Read : CellContentRead::State::Failed;
            pRead->_state.store( static_cast<uint8>( state ), std::memory_order_release );
        } ) );
        if ( slot._readHandle.isValid() == false )
        {
            SW_LOG_ERROR_ID( "SceneStreaming.CellReadNotStarted", "Scene '%#' could not start reading cell (%#, %#) from '%#' - it stays unloaded", _pScene->getName(), slot._cell._x, slot._cell._z,
                             _contentPath );
            slot._state = CellState::Failed;
            return;
        }
        slot._pRead = pRead;
        slot._state = CellState::Loading;
    }

    void SceneStreamingSubsystem::collectCellContent( CellSlot& slot )
    {
        const CellContentRead::State state = static_cast<CellContentRead::State>( slot._pRead->_state.load( std::memory_order_acquire ) );
        if ( state == CellContentRead::State::Reading )
            return;
        if ( state == CellContentRead::State::Read )
        {
            slot._listNode = std::move( slot._pRead->_listNode );
            slot._state    = CellState::Ready;
        }
        else
        {
            SW_LOG_ERROR_ID( "SceneStreaming.CellReadFailed", "Scene '%#' could not read cell (%#, %#) from '%#' - it stays unloaded", _pScene->getName(), slot._cell._x, slot._cell._z, _contentPath );
            slot._state = CellState::Failed;
        }
        slot._pRead.reset();
        slot._readHandle = AsyncReadHandle{};
    }

    bool SceneStreamingSubsystem::buildCell( CellSlot& slot, const Stopwatch& stopwatch, const int64 budgetNanoseconds, const bool bForceProgress )
    {
        if ( slot._state == CellState::Ready )
        {
            slot._listObjectID.clear();
            slot._pBuild = _pScene->beginCellBuild( slot._listNode );
            slot._state  = CellState::Building;
        }
        const Scene::CellBuildResult result = slot._pBuild != nullptr ? _pScene->continueCellBuild( *slot._pBuild, stopwatch, budgetNanoseconds, bForceProgress, slot._listObjectID )
                                                                      : Scene::CellBuildResult::Failed;
        if ( result == Scene::CellBuildResult::InProgress )
            return false;
        slot._pBuild.reset();
        // 파일에서 읽은 내용은 지은 뒤 버린다 — 다시 올리면 다시 읽는다(색인만 상주).
        if ( slot._bContentHeld == false )
        {
            slot._listNode.clear();
            slot._listNode.shrink_to_fit();
        }
        if ( result == Scene::CellBuildResult::Failed )
        {
            SW_LOG_ERROR_ID( "SceneStreaming.SceneCouldNotLoad", "Scene '%#' could not load cell (%#, %#) - it stays unloaded", _pScene->getName(), slot._cell._x, slot._cell._z );
            // 반쯤 지은 것을 남기지 않는다.
            _pScene->unloadCell( slot._listObjectID );
            slot._listObjectID.clear();
            slot._state = CellState::Failed;
            return true;
        }
        slot._state        = CellState::Loaded;
        slot._changeSerial = ++_changeSerial;
        return true;
    }

    void SceneStreamingSubsystem::unloadCell( CellSlot& slot, const bool bKeepState )
    {
        switch ( slot._state )
        {
            case CellState::Loading:
                // 큐에 있으면 OS 에 넘기지 않고, 진행 중이면 읽은 뒤 버린다. 결과는 완료 콜백이 쥔 사본과 함께 사라진다.
                (void)slot._readHandle.cancel();
                slot._pRead.reset();
                slot._readHandle = AsyncReadHandle{};
                break;
            case CellState::Ready:
                if ( slot._bContentHeld == false )
                {
                    slot._listNode.clear();
                    slot._listNode.shrink_to_fit();
                }
                break;
            case CellState::Building:
                // 드러난 적이 없다 — 상태를 뜨지 않는다. 진행을 먼저 버리고(배치를 잇지 않는다, `_listNode` 를 가리킨다) 지은 오브젝트를 지운다.
                slot._pBuild.reset();
                _pScene->unloadCell( slot._listObjectID );
                slot._listObjectID.clear();
                if ( slot._bContentHeld == false )
                {
                    slot._listNode.clear();
                    slot._listNode.shrink_to_fit();
                }
                break;
            case CellState::Loaded:
                // 다시 올릴 때 이 상태로 짓는다 — 내리기 전에 뜬다(언리얼 레벨 스트리밍의 상태 보존 · 세이브 델타).
                if ( bKeepState )
                {
                    _pScene->captureCellNodes( slot._listObjectID, slot._listNode );
                    slot._bContentHeld = SW_TRUE;
                }
                _pScene->unloadCell( slot._listObjectID );
                slot._listObjectID.clear();
                slot._changeSerial = ++_changeSerial;
                break;
            case CellState::Unloaded:
            case CellState::Failed:
                return;
        }
        slot._state = CellState::Unloaded;
    }
} // namespace sw
