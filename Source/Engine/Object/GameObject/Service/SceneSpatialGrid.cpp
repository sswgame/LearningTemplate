#include "pch.h"

#include "Engine/Object/GameObject/Service/SceneSpatialGrid.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneServiceRegistry.h"

namespace sw
{
    // 일은 `SpatialIndex` 단계 끝에 넘겨 `ApplyPostPhysics` 본문 앞에서 마친다 — 그 사이 `TickPostPhysics` 틱이 앞 버퍼를 읽는다. 일은 자기 뒤 버퍼만 쓴다.
    SW_SCENE_SERVICE_WITH_FRAME_JOB( SceneSpatialGrid, SpatialIndex, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::SpatialIndex }, SceneDataKindSet{ SceneDataKind::Transform }, SceneDataKindSet{} ), ApplyPostPhysics, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::SpatialIndex }, SceneDataKindSet{}, SceneDataKindSet{} ) )

    SceneSpatialGrid::SceneSpatialGrid( GameObjectManager& manager )
        : _manager{ manager }
        , _moves{ manager }
        , _mapSourceEntry{}
        , _listMovedSource{}
        , _arrPendingEntry{}
        , _listEntry{}
        , _listFreeEntry{}
        , _listRemovedEntry{}
        , _listLayer{}
        , _listChange{}
        , _pJobLayer{ nullptr }
        , _jobLayerCount{ 0 }
        , _jobStage{}
        , _nextSequence{ 0 }
        , _appliedChangeCount{}
        , _frontIndex{ 0 }
        , _bJobLaunched{ SW_FALSE }
        , _bJobOnWorkers{ SW_FALSE }
    {
    }

    SceneSpatialGrid::~SceneSpatialGrid()
    {
        waitBackBufferRebuild();
        for ( const auto& pair : _mapSourceEntry )
        {
            _moves.untrack( _listEntry[pair.second]._sourceSlot, pair.first );
        }
    }

    void SceneSpatialGrid::projectToPlane( const SpatialGridLayerDesc& layer, const float3& position, float32& outPlaneX, float32& outPlaneY )
    {
        outPlaneX = position._x;
        outPlaneY = layer._plane == SpatialGridPlane::XY ? position._y : position._z;
    }

    uint32 SceneSpatialGrid::getOrCreateLayer( const SpatialGridLayerDesc& layer )
    {
        for ( uint32 layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            if ( _listLayer[layerIndex]._pDesc == &layer )
                return layerIndex;
        }
        // 새 층은 벡터를 키운다(재할당) — 넘긴 일이 층 배열을 쓰는 동안 키우지 않도록 먼저 기다린다. 층은 처음 쓰일 때 한 번 생겨 드물다.
        waitBackBufferRebuild();
        Layer added; // 기본 초기화 — `{}` 집합 초기화는 explicit 기본 생성자의 격자 칸을 복사 목록 초기화한다
        added._pDesc = &layer;
        _listLayer.push_back( std::move( added ) );
        return static_cast<uint32>( _listLayer.size() - 1 );
    }

    const SceneSpatialGrid::Layer* SceneSpatialGrid::findLayer( const SpatialGridLayerDesc& layer ) const
    {
        for ( const Layer& candidate : _listLayer )
        {
            if ( candidate._pDesc == &layer )
                return &candidate;
        }
        return nullptr;
    }

    SceneSpatialGrid::Entry* SceneSpatialGrid::resolve( SlotHandle entry )
    {
        if ( entry.isValid() == false || entry.index() >= _listEntry.size() )
            return nullptr;
        Entry& found = _listEntry[entry.index()];
        return found._generation == entry.generation() && found._pKey != nullptr ? &found : nullptr;
    }

    SceneSpatialGrid::Change SceneSpatialGrid::makeInsertChange( uint32 entryIndex, uint32& outVersion ) const
    {
        const Entry& entry = _listEntry[entryIndex];
        // 게임 스레드에서 읽는다 — 더티면 여기서 합성한다(`getWorldTransformVersion` · `getWorldPosition`).
        outVersion = entry._pSource->getWorldTransformVersion();
        float32 planeX{ 0.0f };
        float32 planeY{ 0.0f };
        projectToPlane( *_listLayer[entry._layerIndex]._pDesc, entry._pSource->getWorldPosition(), planeX, planeY );
        Change change{};
        change._handle     = SlotHandle::make( entryIndex, entry._generation );
        change._layerIndex = entry._layerIndex;
        change._minX       = planeX - entry._extent;
        change._minY       = planeY - entry._extent;
        change._maxX       = planeX + entry._extent;
        change._maxY       = planeY + entry._extent;
        return change;
    }

    SlotHandle SceneSpatialGrid::add( const SpatialGridLayerDesc& layer, Component& key, const SceneComponent& source, float32 extent )
    {
        waitBackBufferRebuild();
        SW_ASSERT( extent >= 0.0f );
        const uint32 layerIndex = getOrCreateLayer( layer );
        uint32       entryIndex{ 0 };
        if ( _listFreeEntry.empty() == false )
        {
            entryIndex = _listFreeEntry.back();
            _listFreeEntry.pop_back();
        }
        else
        {
            entryIndex = static_cast<uint32>( _listEntry.size() );
            _listEntry.emplace_back();
        }
        Layer& addedLayer        = _listLayer[layerIndex];
        Entry& entry             = _listEntry[entryIndex];
        entry._pKey              = &key;
        entry._pSource           = &source;
        entry._sourceSlot        = source.getTransformSlot();
        entry._sequence          = _nextSequence++;
        entry._extent            = extent;
        entry._layerIndex        = layerIndex;
        entry._positionInLayer   = static_cast<uint32>( addedLayer._listEntryIndex.size() );
        entry._arrSeenVersion[0] = 0;
        entry._arrSeenVersion[1] = 0;
        addedLayer._listEntryIndex.push_back( entryIndex );
        linkSource( entryIndex );
        _arrPendingEntry[0].push_back( entryIndex ); // 같은 소스의 둘째 항목은 움직임 줄에 새로 서지 않는다
        _arrPendingEntry[1].push_back( entryIndex );

        const float32 neededCell = extent * 2.0f;
        if ( neededCell > addedLayer._cellSize )
        {
            addedLayer._cellSize = neededCell;
            refreshLayer( layerIndex ); // 새 항목도 다시 짓는 앞 버퍼에 든다
        }
        else if ( addedLayer._arrBuiltCell[_frontIndex] > 0.0f )
        {
            uint32       version{ 0 };
            const Change change = makeInsertChange( entryIndex, version );
            addedLayer._arrGrid[_frontIndex].update( change._handle, change._minX, change._minY, change._maxX, change._maxY );
            entry._arrSeenVersion[_frontIndex] = version;
        }
        return SlotHandle::make( entryIndex, entry._generation );
    }

    void SceneSpatialGrid::remove( SlotHandle& inoutEntry )
    {
        waitBackBufferRebuild();
        Entry* pEntry = resolve( inoutEntry );
        inoutEntry    = SlotHandle{};
        if ( pEntry == nullptr )
            return;
        const uint32 entryIndex = static_cast<uint32>( pEntry - _listEntry.data() );
        Layer&       layer      = _listLayer[pEntry->_layerIndex];
        // 층 목록에서 끝 칸과 바꿔 뺀다(순서는 `_sequence` 가 든다).
        const uint32 lastEntryIndex                     = layer._listEntryIndex.back();
        layer._listEntryIndex[pEntry->_positionInLayer] = lastEntryIndex;
        _listEntry[lastEntryIndex]._positionInLayer     = pEntry->_positionInLayer;
        layer._listEntryIndex.pop_back();
        // 앞 버퍼는 지금 뺀다 — 같은 프레임의 질의가 지울 컴포넌트를 받지 않는다. 뒤 버퍼는 다음 일이 빼고, 칸은 그 뒤에 다시 쓴다.
        layer._arrGrid[_frontIndex].remove( SlotHandle::make( entryIndex, pEntry->_generation ) );
        unlinkSource( entryIndex );
        pEntry->_pKey    = nullptr;
        pEntry->_pSource = nullptr;
        _listRemovedEntry.push_back( entryIndex );
    }

    void SceneSpatialGrid::setExtent( SlotHandle entry, float32 extent )
    {
        waitBackBufferRebuild();
        SW_ASSERT( extent >= 0.0f );
        Entry* pEntry = resolve( entry );
        SW_ASSERT( pEntry != nullptr );
        if ( pEntry == nullptr || pEntry->_extent == extent )
            return;
        pEntry->_extent = extent;
        // 두 버퍼 모두 다시 넣는다 — 앞은 지금, 뒤는 다음 일이(본 번호를 지운다).
        pEntry->_arrSeenVersion[0] = 0;
        pEntry->_arrSeenVersion[1] = 0;
        _arrPendingEntry[0].push_back( entry.index() );
        _arrPendingEntry[1].push_back( entry.index() );
        Layer&        layer      = _listLayer[pEntry->_layerIndex];
        const float32 neededCell = extent * 2.0f;
        if ( neededCell > layer._cellSize )
        {
            layer._cellSize = neededCell;
            refreshLayer( pEntry->_layerIndex );
            return;
        }
        if ( layer._arrBuiltCell[_frontIndex] > 0.0f && isSourceAlive( *pEntry ) )
        {
            uint32       version{ 0 };
            const Change change = makeInsertChange( entry.index(), version );
            layer._arrGrid[_frontIndex].update( change._handle, change._minX, change._minY, change._maxX, change._maxY );
            pEntry->_arrSeenVersion[_frontIndex] = version;
        }
    }

    void SceneSpatialGrid::addLayerReader( const SpatialGridLayerDesc& layer )
    {
        waitBackBufferRebuild();
        const uint32 layerIndex = getOrCreateLayer( layer );
        ++_listLayer[layerIndex]._userCount;
        refreshLayer( layerIndex );
    }

    void SceneSpatialGrid::removeLayerReader( const SpatialGridLayerDesc& layer )
    {
        waitBackBufferRebuild();
        const uint32 layerIndex = getOrCreateLayer( layer );
        SW_ASSERT( _listLayer[layerIndex]._userCount > 0 );
        if ( _listLayer[layerIndex]._userCount == 0 )
            return;
        --_listLayer[layerIndex]._userCount;
        refreshLayer( layerIndex );
    }

    void SceneSpatialGrid::requireCellSize( const SpatialGridLayerDesc& layer, float32 cellSize )
    {
        waitBackBufferRebuild();
        const uint32 layerIndex = getOrCreateLayer( layer );
        if ( cellSize <= _listLayer[layerIndex]._cellSize )
            return;
        _listLayer[layerIndex]._cellSize = cellSize;
        refreshLayer( layerIndex );
    }

    void SceneSpatialGrid::refreshLayer( uint32 layerIndex )
    {
        Layer&      layer = _listLayer[layerIndex];
        const uint8 front = _frontIndex;
        const uint8 back  = static_cast<uint8>( 1u - front );
        if ( layer.needsGrid() == false )
        {
            // 맞추지 않는 층은 두 버퍼를 놓는다 — 다시 맞출 때 처음부터 짓는다.
            for ( uint8 bufferIndex = 0; bufferIndex < 2; ++bufferIndex )
            {
                layer._arrGrid[bufferIndex]      = SpatialHashGrid2D{};
                layer._arrBuiltCell[bufferIndex] = 0.0f;
            }
            return;
        }
        if ( layer._arrBuiltCell[front] == layer._cellSize )
            return;
        // 앞 버퍼를 지금 셀로 다시 짓는다(게임 스레드 — 셀은 커지기만 해서 드물다). 뒤 버퍼는 다음 일이 새 셀로 다시 짓는다.
        layer._arrGrid[front]      = layer.makeGrid();
        layer._arrBuiltCell[front] = layer._cellSize;
        layer._arrBuiltCell[back]  = 0.0f;
        for ( const uint32 entryIndex : layer._listEntryIndex )
        {
            if ( isSourceAlive( _listEntry[entryIndex] ) == false )
                continue;
            uint32       version{ 0 };
            const Change change = makeInsertChange( entryIndex, version );
            layer._arrGrid[front].insert( change._handle, change._minX, change._minY, change._maxX, change._maxY );
            _listEntry[entryIndex]._arrSeenVersion[front] = version;
        }
    }

    bool SceneSpatialGrid::isActive( const SpatialGridLayerDesc& layer ) const
    {
        const Layer* pLayer = findLayer( layer );
        return pLayer != nullptr && pLayer->_arrBuiltCell[_frontIndex] > 0.0f;
    }

    void SceneSpatialGrid::query( const SpatialGridLayerDesc& layer, float32 minX, float32 minY, float32 maxX, float32 maxY, vector<Component*>& outListKey ) const
    {
        outListKey.clear();
        const Layer* pLayer = findLayer( layer );
        SW_ASSERT( pLayer != nullptr && pLayer->_arrBuiltCell[_frontIndex] > 0.0f );
        if ( pLayer == nullptr )
            return;
        // 질의마다 제 버퍼다 — 병렬 틱이 같은 격자를 동시에 읽는다. 보통의 결과는 인라인 버퍼에 들어 힙을 건드리지 않는다(AI 시각 · 상호작용 · 존이 틱마다 묻는다 —
        // 언리얼 겹침 질의가 결과를 `TInlineAllocator` 배열로 받는 자리). 여러 셀에 걸친 핸들은 겹쳐 오므로 올린 순서로 정렬하며 하나만 남긴다(순서 번호는 항목마다 다르다).
        static constexpr size_t                      kInlineHandleCount = 64;
        small_vector<SlotHandle, kInlineHandleCount> listHandle;
        const AABB2D                                 bounds{
            float2{MathUtil::min( minX, maxX ), MathUtil::min( minY, maxY )},
            float2{MathUtil::max( minX, maxX ), MathUtil::max( minY, maxY )}
        };
        pLayer->_arrGrid[_frontIndex].visitAABB( bounds, [&listHandle]( SlotHandle handle )
        { listHandle.push_back( handle ); } );
        VectorUtil::sortUnique( listHandle, [this]( SlotHandle left, SlotHandle right )
        {
            return _listEntry[left.index()]._sequence < _listEntry[right.index()]._sequence;
        } );
        outListKey.reserve( listHandle.size() );
        for ( const SlotHandle handle : listHandle )
        {
            // 꺼진 열쇠(컴포넌트나 그 주인이 계층에서 꺼짐 — 풀에 든 적)는 질의에 들지 않는다. 언리얼 · 유니티의 꺼진 액터처럼 판정 · 겹침에서 빠진다 — 항목은 두어 다시 켜면 바로 돌아온다.
            Component* const pKey = _listEntry[handle.index()]._pKey;
            if ( pKey->isActive() )
                outListKey.push_back( pKey );
        }
    }

    float32 SceneSpatialGrid::getMaxExtent( const SpatialGridLayerDesc& layer ) const
    {
        const Layer* pLayer = findLayer( layer );
        return pLayer != nullptr ? pLayer->_cellSize * 0.5f : 0.0f; // 셀은 항목 반 크기 두 배보다 작아지지 않는다
    }

    void SceneSpatialGrid::update( float32 deltaTime )
    {
        (void)deltaTime;
        waitBackBufferRebuild();
        collectMovedEntries();
        const uint8 back = static_cast<uint8>( 1u - _frontIndex );
        _listChange.clear();
        for ( const uint32 entryIndex : _listRemovedEntry )
        {
            Change change{};
            change._handle     = SlotHandle::make( entryIndex, _listEntry[entryIndex]._generation );
            change._layerIndex = _listEntry[entryIndex]._layerIndex;
            change._bRemove    = SW_TRUE;
            _listChange.push_back( change );
        }
        for ( uint32 layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            Layer& layer        = _listLayer[layerIndex];
            layer._bRebuildBack = SW_FALSE;
            if ( layer.needsGrid() == false )
                continue;
            // 셀이 바뀐 뒤 버퍼는 처음부터 다시 짓는다 — 모든 항목을 넣는다.
            if ( layer._arrBuiltCell[back] == layer._cellSize )
                continue;
            layer._bRebuildBack = SW_TRUE;
            for ( const uint32 entryIndex : layer._listEntryIndex )
            {
                if ( isSourceAlive( _listEntry[entryIndex] ) == false )
                    continue;
                uint32 seenVersion{ 0 };
                _listChange.push_back( makeInsertChange( entryIndex, seenVersion ) );
                _listEntry[entryIndex]._arrSeenVersion[back] = seenVersion;
            }
            layer._arrBuiltCell[back] = layer._cellSize;
        }
        // 움직였거나(`collectMovedEntries`) 반 크기가 바뀐 항목만 고쳐 넣는다 — 다시 짓는 층의 것은 위에서 이미 넣어 본 번호가 같다.
        for ( const uint32 entryIndex : _arrPendingEntry[back] )
        {
            Entry&     entry = _listEntry[entryIndex];
            const bool bSkip = entry._pKey == nullptr || _listLayer[entry._layerIndex].needsGrid() == false || isSourceAlive( entry ) == false;
            if ( bSkip )
                continue; // 뺀 칸이거나 소스가 사라졌거나 맞추지 않는 층이다 — 다시 맞출 때 처음부터 짓는다
            if ( entry._arrSeenVersion[back] == entry._pSource->getWorldTransformVersion() )
                continue;
            uint32 seenVersion{ 0 };
            _listChange.push_back( makeInsertChange( entryIndex, seenVersion ) );
            entry._arrSeenVersion[back] = seenVersion;
        }
        _arrPendingEntry[back].clear();
        _bJobLaunched = SW_TRUE;
        // 넘긴 일은 이 배열만 본다 — 일이 도는 동안 층 벡터의 구조는 바뀌지 않고(새 층은 `getOrCreateLayer` 가 일을 기다린 뒤 더한다), 일은 층마다 뒤 버퍼 칸만 쓴다.
        _pJobLayer     = _listLayer.data();
        _jobLayerCount = static_cast<uint32>( _listLayer.size() );
        if ( engine::areEngineServicesBound() == false )
        {
            rebuildBackBuffer(); // 워커가 없으면(테스트 · 도구) 지금 맞춘다 — 앞뒤를 바꾸는 것은 그래도 `completeFrameJob` 이다
            return;
        }
        TaskManager& taskManager = engine::getSubsystem<TaskManager>();
        TaskHandle   task        = taskManager.emplaceTask( "SceneSpatialGrid", SW_DELEGATE_METHOD( TaskDelegate, &SceneSpatialGrid::rebuildBackBuffer, this ) );
        taskManager.addToStage( _jobStage, task, "SceneSpatialGrid" );
        task.submit();
        _bJobOnWorkers = SW_TRUE;
    }

    void SceneSpatialGrid::collectMovedEntries()
    {
        // 프레임 단계에서는 이미 펼쳐져 있다. 그 밖에서 부르면(도구 · 테스트) 쓴 자리를 먼저 펼쳐야 움직임이 줄에 선다.
        if ( _manager.hasDirtySceneTransforms() )
            _manager.flushSceneTransforms();
        _moves.takeMoved( _listMovedSource );
        for ( const SceneComponent* pSource : _listMovedSource )
        {
            const auto it = _mapSourceEntry.find( pSource );
            if ( it == _mapSourceEntry.end() )
                continue;
            for ( uint32 entryIndex = it->second; entryIndex != constant::invalid_index::kUint32; entryIndex = _listEntry[entryIndex]._nextOfSource )
            {
                _arrPendingEntry[0].push_back( entryIndex );
                _arrPendingEntry[1].push_back( entryIndex );
            }
        }
    }

    void SceneSpatialGrid::linkSource( uint32 entryIndex )
    {
        Entry&     entry = _listEntry[entryIndex];
        const auto it    = _mapSourceEntry.find( entry._pSource );
        _moves.track( *entry._pSource ); // 이미 듣고 있으면 그대로다 — 옛 소스가 사라진 주소를 새 소스가 받았어도 듣는다
        entry._nextOfSource = it != _mapSourceEntry.end() ? it->second : constant::invalid_index::kUint32;
        if ( it != _mapSourceEntry.end() )
            it->second = entryIndex;
        else
            _mapSourceEntry[entry._pSource] = entryIndex;
    }

    void SceneSpatialGrid::unlinkSource( uint32 entryIndex )
    {
        Entry&     entry = _listEntry[entryIndex];
        const auto it    = _mapSourceEntry.find( entry._pSource );
        SW_ASSERT( it != _mapSourceEntry.end() );
        uint32* pLink = &it->second;
        while ( *pLink != entryIndex )
        {
            pLink = &_listEntry[*pLink]._nextOfSource;
        }
        *pLink              = entry._nextOfSource;
        entry._nextOfSource = constant::invalid_index::kUint32;
        if ( it->second != constant::invalid_index::kUint32 )
            return;
        _moves.untrack( entry._sourceSlot, entry._pSource );
        _mapSourceEntry.erase( it );
    }

    void SceneSpatialGrid::rebuildBackBuffer()
    {
        const uint8 back = static_cast<uint8>( 1u - _frontIndex );
        for ( uint32 layerIndex = 0; layerIndex < _jobLayerCount; ++layerIndex )
        {
            Layer& layer = _pJobLayer[layerIndex];
            if ( layer._bRebuildBack == SW_TRUE )
                layer._arrGrid[back] = layer.makeGrid();
        }
        for ( const Change& change : _listChange )
        {
            SW_ASSERT( change._layerIndex < _jobLayerCount );
            SpatialHashGrid2D& grid = _pJobLayer[change._layerIndex]._arrGrid[back];
            if ( change._bRemove == SW_TRUE )
                grid.remove( change._handle );
            else
                grid.update( change._handle, change._minX, change._minY, change._maxX, change._maxY );
        }
    }

    void SceneSpatialGrid::waitBackBufferRebuild()
    {
        if ( _bJobLaunched == SW_FALSE )
            return;
        if ( _bJobOnWorkers == SW_TRUE )
            engine::getSubsystem<TaskManager>().waitStage( _jobStage, "SceneSpatialGrid" );
        _bJobOnWorkers = SW_FALSE;
        _bJobLaunched  = SW_FALSE;
        _appliedChangeCount.add( _listChange.size() );
        _listChange.clear();
        // 뒤 버퍼에서도 뺐다 — 칸을 다시 쓸 수 있다(세대를 올려 옛 핸들을 무효로).
        for ( const uint32 entryIndex : _listRemovedEntry )
        {
            ++_listEntry[entryIndex]._generation;
            _listFreeEntry.push_back( entryIndex );
        }
        _listRemovedEntry.clear();
        _frontIndex = static_cast<uint8>( 1u - _frontIndex );
    }

    void SceneSpatialGrid::completeFrameJob()
    {
        waitBackBufferRebuild();
    }

    void SceneSpatialGrid::onSceneCleared()
    {
        waitBackBufferRebuild();
    }

    void SceneSpatialGrid::onWorldOriginShifted( const float3& /*delta*/ ) // 자리는 소스에서 다시 읽는다 — 씬이 루트를 옮기고 트랜스폼을 펼친 뒤 부른다
    {
        waitBackBufferRebuild();
        for ( Layer& layer : _listLayer )
        {
            if ( layer._arrBuiltCell[_frontIndex] <= 0.0f )
                continue;
            for ( const uint32 entryIndex : layer._listEntryIndex )
            {
                if ( isSourceAlive( _listEntry[entryIndex] ) == false )
                    continue;
                uint32       version{ 0 };
                const Change change = makeInsertChange( entryIndex, version );
                layer._arrGrid[_frontIndex].update( change._handle, change._minX, change._minY, change._maxX, change._maxY );
                _listEntry[entryIndex]._arrSeenVersion[_frontIndex] = version;
            }
        }
    }
} // namespace sw
