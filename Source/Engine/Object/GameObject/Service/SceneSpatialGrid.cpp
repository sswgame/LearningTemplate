#include "pch.h"

#include "Engine/Object/GameObject/Service/SceneSpatialGrid.h"

#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    // 일은 `SpatialIndex` 단계 끝에 넘겨 `ApplyPostPhysics` 본문 앞에서 마친다 — 그 사이 `TickPostPhysics` 틱이 앞 버퍼를 읽는다. 일은 자기 뒤 버퍼만 쓴다.
    SW_SCENE_SERVICE_WITH_FRAME_JOB( SceneSpatialGrid, SpatialIndex, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::SpatialIndex }, SceneDataKindSet{ SceneDataKind::Transform }, SceneDataKindSet{} ), ApplyPostPhysics, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::SpatialIndex }, SceneDataKindSet{}, SceneDataKindSet{} ) )

    SceneSpatialGrid::SceneSpatialGrid( GameObjectManager& manager )
        : _manager{ manager }
        , _listEntry{}
        , _listFreeEntry{}
        , _listRemovedEntry{}
        , _listLayer{}
        , _listChange{}
        , _jobStage{}
        , _nextSequence{ 0 }
        , _appliedChangeCount{ 0 }
        , _frontIndex{ 0 }
        , _bJobLaunched{ SW_FALSE }
        , _bJobOnWorkers{ SW_FALSE }
    {
    }

    SceneSpatialGrid::~SceneSpatialGrid()
    {
        waitJob();
    }

    void SceneSpatialGrid::projectToPlane( const SpatialGridLayerDesc& layer, const float3& position, float32& outPlaneX, float32& outPlaneY )
    {
        outPlaneX = position._x;
        outPlaneY = layer._plane == SpatialGridPlane::XY ? position._y : position._z;
    }

    uint32 SceneSpatialGrid::findOrAddLayer( const SpatialGridLayerDesc& layer )
    {
        for ( uint32 layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            if ( _listLayer[layerIndex]._pDesc == &layer )
                return layerIndex;
        }
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

    SceneSpatialGrid::Change SceneSpatialGrid::makeInsert( uint32 entryIndex, uint32& outVersion ) const
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
        waitJob();
        SW_ASSERT( extent >= 0.0f );
        const uint32 layerIndex = findOrAddLayer( layer );
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
        entry._sequence          = _nextSequence++;
        entry._extent            = extent;
        entry._layerIndex        = layerIndex;
        entry._positionInLayer   = static_cast<uint32>( addedLayer._listEntryIndex.size() );
        entry._arrSeenVersion[0] = 0;
        entry._arrSeenVersion[1] = 0;
        addedLayer._listEntryIndex.push_back( entryIndex );

        const float32 neededCell = extent * 2.0f;
        if ( neededCell > addedLayer._cellSize )
        {
            addedLayer._cellSize = neededCell;
            refreshLayer( layerIndex ); // 새 항목도 다시 짓는 앞 버퍼에 든다
        }
        else if ( addedLayer._arrBuiltCell[_frontIndex] > 0.0f )
        {
            uint32       version{ 0 };
            const Change change = makeInsert( entryIndex, version );
            addedLayer._arrGrid[_frontIndex].update( change._handle, change._minX, change._minY, change._maxX, change._maxY );
            entry._arrSeenVersion[_frontIndex] = version;
        }
        return SlotHandle::make( entryIndex, entry._generation );
    }

    void SceneSpatialGrid::remove( SlotHandle& inoutEntry )
    {
        waitJob();
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
        pEntry->_pKey    = nullptr;
        pEntry->_pSource = nullptr;
        _listRemovedEntry.push_back( entryIndex );
    }

    void SceneSpatialGrid::setExtent( SlotHandle entry, float32 extent )
    {
        waitJob();
        SW_ASSERT( extent >= 0.0f );
        Entry* pEntry = resolve( entry );
        SW_ASSERT( pEntry != nullptr );
        if ( pEntry == nullptr || pEntry->_extent == extent )
            return;
        pEntry->_extent = extent;
        // 두 버퍼 모두 다시 넣는다 — 앞은 지금, 뒤는 다음 일이(본 번호를 지운다).
        pEntry->_arrSeenVersion[0] = 0;
        pEntry->_arrSeenVersion[1] = 0;
        Layer&        layer        = _listLayer[pEntry->_layerIndex];
        const float32 neededCell   = extent * 2.0f;
        if ( neededCell > layer._cellSize )
        {
            layer._cellSize = neededCell;
            refreshLayer( pEntry->_layerIndex );
            return;
        }
        if ( layer._arrBuiltCell[_frontIndex] > 0.0f )
        {
            uint32       version{ 0 };
            const Change change = makeInsert( entry.index(), version );
            layer._arrGrid[_frontIndex].update( change._handle, change._minX, change._minY, change._maxX, change._maxY );
            pEntry->_arrSeenVersion[_frontIndex] = version;
        }
    }

    void SceneSpatialGrid::addUser( const SpatialGridLayerDesc& layer )
    {
        waitJob();
        const uint32 layerIndex = findOrAddLayer( layer );
        ++_listLayer[layerIndex]._userCount;
        refreshLayer( layerIndex );
    }

    void SceneSpatialGrid::removeUser( const SpatialGridLayerDesc& layer )
    {
        waitJob();
        const uint32 layerIndex = findOrAddLayer( layer );
        SW_ASSERT( _listLayer[layerIndex]._userCount > 0 );
        if ( _listLayer[layerIndex]._userCount == 0 )
            return;
        --_listLayer[layerIndex]._userCount;
        refreshLayer( layerIndex );
    }

    void SceneSpatialGrid::requireCellSize( const SpatialGridLayerDesc& layer, float32 cellSize )
    {
        waitJob();
        const uint32 layerIndex = findOrAddLayer( layer );
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
        if ( layer.wantsGrid() == false )
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
        layer._arrGrid[front]      = SpatialHashGrid2D{ layer._cellSize };
        layer._arrBuiltCell[front] = layer._cellSize;
        layer._arrBuiltCell[back]  = 0.0f;
        for ( const uint32 entryIndex : layer._listEntryIndex )
        {
            uint32       version{ 0 };
            const Change change = makeInsert( entryIndex, version );
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
        // 질의마다 새 목록이다 — 병렬 틱이 같은 격자를 동시에 읽는다.
        vector<SlotHandle> listHandle;
        pLayer->_arrGrid[_frontIndex].queryAABB( minX, minY, maxX, maxY, listHandle );
        std::sort( listHandle.begin(), listHandle.end(), [this]( SlotHandle left, SlotHandle right )
        {
            return _listEntry[left.index()]._sequence < _listEntry[right.index()]._sequence;
        } );
        outListKey.reserve( listHandle.size() );
        for ( const SlotHandle handle : listHandle )
        {
            outListKey.push_back( _listEntry[handle.index()]._pKey );
        }
    }

    void SceneSpatialGrid::update( float32 deltaTime )
    {
        (void)deltaTime;
        waitJob();
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
            if ( layer.wantsGrid() == false )
                continue;
            // 셀이 바뀐 뒤 버퍼는 처음부터 다시 짓는다 — 모든 항목을 넣는다.
            const bool bRebuild = layer._arrBuiltCell[back] != layer._cellSize;
            layer._bRebuildBack = bRebuild ? SW_TRUE : SW_FALSE;
            for ( const uint32 entryIndex : layer._listEntryIndex )
            {
                Entry&       entry   = _listEntry[entryIndex];
                const uint32 version = entry._pSource->getWorldTransformVersion();
                if ( bRebuild == false && entry._arrSeenVersion[back] == version )
                    continue;
                uint32 seenVersion{ 0 };
                _listChange.push_back( makeInsert( entryIndex, seenVersion ) );
                entry._arrSeenVersion[back] = seenVersion;
            }
            layer._arrBuiltCell[back] = layer._cellSize;
        }
        _bJobLaunched = SW_TRUE;
        if ( engine::areEngineServicesBound() == false )
        {
            runBackJob(); // 워커가 없으면(테스트 · 도구) 지금 맞춘다 — 앞뒤를 바꾸는 것은 그래도 `completeFrameJob` 이다
            return;
        }
        TaskManager& taskManager = engine::getSubsystem<TaskManager>();
        TaskHandle   task        = taskManager.emplaceTask( "SceneSpatialGrid", SW_DELEGATE_METHOD( TaskDelegate, &SceneSpatialGrid::runBackJob, this ) );
        taskManager.addToStage( _jobStage, task, "SceneSpatialGrid" );
        task.submit();
        _bJobOnWorkers = SW_TRUE;
    }

    void SceneSpatialGrid::runBackJob()
    {
        const uint8 back = static_cast<uint8>( 1u - _frontIndex );
        for ( Layer& layer : _listLayer )
        {
            if ( layer._bRebuildBack == SW_TRUE )
                layer._arrGrid[back] = SpatialHashGrid2D{ layer._cellSize };
        }
        for ( const Change& change : _listChange )
        {
            SpatialHashGrid2D& grid = _listLayer[change._layerIndex]._arrGrid[back];
            if ( change._bRemove == SW_TRUE )
                grid.remove( change._handle );
            else
                grid.update( change._handle, change._minX, change._minY, change._maxX, change._maxY );
        }
    }

    void SceneSpatialGrid::waitJob()
    {
        if ( _bJobLaunched == SW_FALSE )
            return;
        if ( _bJobOnWorkers == SW_TRUE )
            engine::getSubsystem<TaskManager>().waitStage( _jobStage, "SceneSpatialGrid" );
        _bJobOnWorkers = SW_FALSE;
        _bJobLaunched  = SW_FALSE;
        _appliedChangeCount += _listChange.size();
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
        waitJob();
    }

    void SceneSpatialGrid::onSceneCleared()
    {
        waitJob();
    }
} // namespace sw
