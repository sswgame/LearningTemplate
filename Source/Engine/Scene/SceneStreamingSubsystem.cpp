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
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDataLayerSubsystem.h"
#include "Engine/Serialization/Format/Archive.h"

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

    namespace
    {
        struct SceneStreamingSaveInternal
        {
            /** @brief 세이브의 조각 하나가 적어도 쓰는 바이트입니다(좌표 둘 · 레이어 수 · 든 표시 · 섹션 머리 넷). 깨진 수가 큰 할당이 되지 않게 `readCount` 가 쓴다. */
            static constexpr uint32      kMinBytesPerCell = sizeof( int32 ) * 2 + sizeof( uint32 ) + sizeof( uint8 ) + sizeof( uint32 ) * 4;
            static constexpr const utf8* kSaveSource      = "save game (streaming cells)";

            /** @brief 세이브 엔티티 목록(`SceneDocument::writeSavedNodes`)을 섹션 하나로 씁니다. */
            static void writeNodes( Archive& arch, const vector<SceneDocument::SceneObjectNode>& listNode )
            {
                vector<uint8> bytes;
                SceneDocument::writeSavedNodes( listNode, bytes );
                arch.writeSection( bytes.data(), static_cast<uint32>( bytes.size() ) );
            }

            /** @brief `writeNodes` 의 짝입니다. */
            [[nodiscard]] static bool readNodes( Archive& arch, vector<SceneDocument::SceneObjectNode>& outListNode )
            {
                vector<uint8> bytes;
                return arch.readSection( bytes ) && SceneDocument::readSavedNodes( bytes.data(), bytes.size(), outListNode, kSaveSource );
            }
        };
    } // namespace

    SceneStreamingSubsystem::SceneStreamingSubsystem( GameObjectManager& /*manager*/ )
        : _pScene{ nullptr }
        , _settings{}
        , _contentPath{}
        , _grid{}
        , _listSourceCell{}
        , _listCell{}
        , _mapCellIndex{}
        , _mapObjectSlot{}
        , _maxFileCellFileID{ 0 }
        , _listExternalSource{}
        , _cameraSource{}
        , _bCameraSourceSet{ SW_FALSE }
        , _bExternalSources{ SW_FALSE }
        , _changeSerial{ 0 }
        , _listScratchCell{}
        , _listScratchLoad{}
        , _listScratchUnload{}
    {
    }

    bool SceneStreamingSubsystem::beginWorld( Scene& scene, const SceneSettings& settings, const vector<SceneDocument::StreamingCell>& listCell, string_view contentPath,
                                              const uint64 maxFileCellFileID )
    {
        endWorld();
        if ( settings._bStreaming == false || settings.validate( scene.getName() ) == false )
        {
            SW_LOG_ERROR_ID( "SceneStreaming.SceneStreamingCellsBut", "Scene '%#' has streaming cells but no usable streaming settings - its cells are not loaded", scene.getName() );
            return false;
        }
        if ( StreamingCellGrid::tryCreate( settings._cellGrid, _grid ) == false )
            return false;

        // 같은 좌표 · 레이어 집합이 두 번 있거나 파일 내용을 읽을 쿠킹본이 없으면 받지 않는다.
        unordered_map<uint64, vector<uint32>> mapSeen;
        mapSeen.reserve( listCell.size() );
        for ( uint32 cellIndex = 0; cellIndex < static_cast<uint32>( listCell.size() ); ++cellIndex )
        {
            const SceneDocument::StreamingCell& cell          = listCell[cellIndex];
            vector<uint32>&                     listSeenIndex = mapSeen[makeCellKey( cell._cell )];
            bool                                bRepeated     = false;
            for ( const uint32 seenIndex : listSeenIndex )
            {
                bRepeated = bRepeated || listCell[seenIndex]._listDataLayer == cell._listDataLayer;
            }
            const bool bNoContentFile = cell.hasFileContent() && contentPath.empty();
            if ( bRepeated || bNoContentFile )
            {
                SW_LOG_ERROR_ID( "SceneStreaming.SceneListsCellTwice", "Scene '%#' lists cell (%#, %#) twice or without a cooked file for its content - its cells are not loaded", scene.getName(),
                                 cell._cell._x, cell._cell._z );
                return false;
            }
            listSeenIndex.push_back( cellIndex );
        }
        _listSourceCell    = listCell;
        _maxFileCellFileID = maxFileCellFileID;
        _settings          = settings;
        _contentPath       = contentPath;
        _pScene            = &scene;
        resetCells();
        SW_LOG_INFO_ID( "SceneStreaming.SceneStreamsCellsCell", "Scene '%#' streams %# cells (cell %# m, radius %# m)", scene.getName(), static_cast<uint32>( _listCell.size() ),
                        _settings._cellGrid._cellSize, _settings._cellGrid._loadingRadius );
        return true;
    }

    void SceneStreamingSubsystem::endWorld()
    {
        if ( _pScene != nullptr )
        {
            for ( unique_ptr<CellSlot>& pSlot : _listCell )
            {
                unloadCell( *pSlot, false ); // 월드를 버린다 — 상태를 뜰 까닭이 없다
            }
        }
        _listCell.clear();
        _mapCellIndex.clear();
        _mapObjectSlot.clear();
        _listSourceCell.clear();
        _maxFileCellFileID = 0;
        _contentPath.clear();
        _pScene = nullptr;
    }

    void SceneStreamingSubsystem::onSceneCleared()
    {
        if ( _pScene == nullptr )
            return;
        // 매니저가 오브젝트를 이미 비웠다(세이브 복원) — 걸린 읽기를 거두고 조각을 문서 내용으로 되돌린다. 월드는 놓지 않는다 — 복원이 세이브의 상태를 얹고(`applySaveState`) 다음 갱신이 다시 짓는다.
        for ( unique_ptr<CellSlot>& pSlot : _listCell )
        {
            if ( pSlot->_state == CellState::Loading )
                (void)pSlot->_readHandle.cancel();
            pSlot->_pBuild.reset(); // 짓던 오브젝트는 매니저가 함께 비웠다 — 배치만 놓는다
        }
        resetCells();
        // 올라 있던 셀이 모두 내려갔다 — 셀 구역을 따라 다시 재는 쪽(내비메시 타일)이 모든 셀을 다시 본다.
        _changeSerial += 1;
        for ( unique_ptr<CellSlot>& pSlot : _listCell )
        {
            pSlot->_changeSerial = _changeSerial;
        }
        _bCameraSourceSet = SW_FALSE;
    }

    void SceneStreamingSubsystem::resetCells()
    {
        _listCell.clear();
        _mapCellIndex.clear();
        _mapObjectSlot.clear();
        _listCell.reserve( _listSourceCell.size() );
        _mapCellIndex.reserve( _listSourceCell.size() );
        for ( const SceneDocument::StreamingCell& cell : _listSourceCell )
        {
            unique_ptr<CellSlot> pSlot = sw::make_unique<CellSlot>();
            pSlot->_cell               = cell._cell;
            pSlot->_listDataLayer      = cell._listDataLayer;
            pSlot->_contentOffset      = cell._contentOffset;
            pSlot->_contentSize        = cell._contentSize;
            pSlot->_bFromDocument      = SW_TRUE;
            // 메모리 문서의 셀(쿠커 · 테스트)은 내용이 이미 있다 — 읽지 않고 짓는다.
            if ( cell.hasFileContent() == false )
            {
                pSlot->_listNode     = cell._listSceneObjectNode;
                pSlot->_bContentHeld = SW_TRUE;
            }
            _mapCellIndex[makeCellKey( cell._cell )].push_back( static_cast<uint32>( _listCell.size() ) );
            _listCell.push_back( std::move( pSlot ) );
        }
    }

    uint32 SceneStreamingSubsystem::findOrAddSlot( const StreamingCellCoord& cell, const vector<string>& listDataLayer )
    {
        vector<uint32>& listSlotIndex = _mapCellIndex[makeCellKey( cell )];
        for ( const uint32 slotIndex : listSlotIndex )
        {
            if ( getSlot( slotIndex )._listDataLayer == listDataLayer )
                return slotIndex;
        }
        // 좌표의 끝 조각이다 — 같은 좌표의 문서 조각이 먼저 지어진다(`areEarlierSlotsBuilt`). 떠 둔 상태가 빈 목록이라 오를 때 읽지 않는다.
        unique_ptr<CellSlot> pSlot = sw::make_unique<CellSlot>();
        pSlot->_cell               = cell;
        pSlot->_listDataLayer      = listDataLayer;
        pSlot->_bContentHeld       = SW_TRUE;
        const uint32 slotIndex     = static_cast<uint32>( _listCell.size() );
        listSlotIndex.push_back( slotIndex );
        _listCell.push_back( std::move( pSlot ) );
        return slotIndex;
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
        for ( const unique_ptr<CellSlot>& pSlot : _listCell )
        {
            count += pSlot->_state == CellState::Loaded ? 1u : 0u;
        }
        return count;
    }

    uint32 SceneStreamingSubsystem::getLoadingCellCount() const
    {
        uint32 count = 0;
        for ( const unique_ptr<CellSlot>& pSlot : _listCell )
        {
            count += pSlot->_state == CellState::Loading ? 1u : 0u;
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
            if ( getSlot( slotIndex )._state == CellState::Loaded )
                return true;
        }
        return false;
    }

    uint64 SceneStreamingSubsystem::getMaxCellFileID() const
    {
        if ( _pScene == nullptr )
            return 0;
        // 올라 있는 셀의 오브젝트는 씬의 파일 id 표가 이미 센다 — 여기는 오브젝트가 없는 엔티티(읽지 않은 내용 · 떠 둔 상태 · 들어온 엔티티)만 센다.
        uint64 maxFileID = _maxFileCellFileID;
        for ( const unique_ptr<CellSlot>& pSlot : _listCell )
        {
            maxFileID = MathUtil::max( maxFileID, SceneDocument::computeMaxFileID( pSlot->_listNode ) );
            maxFileID = MathUtil::max( maxFileID, SceneDocument::computeMaxFileID( pSlot->_listArrivedNode ) );
        }
        return maxFileID;
    }

    bool SceneStreamingSubsystem::isCellObject( const uint64 objectID ) const
    {
        if ( _pScene == nullptr || _mapObjectSlot.empty() )
            return false;
        // 셀 엔티티에 붙은 자식(중첩 프리팹 부품)은 셀이 다시 지을 때 함께 선다 — 조상이 셀에 들었으면 셀 몫이다.
        for ( const GameObject* pObject = _pScene->getObjectManager()->findGameObjectByID( objectID ); pObject != nullptr; pObject = pObject->getParent() )
        {
            if ( _mapObjectSlot.find( pObject->getObjectID() ) != _mapObjectSlot.end() )
                return true;
        }
        return false;
    }

    void SceneStreamingSubsystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        if ( _pScene == nullptr )
            return;
        const bool bFinishEachFrame = engine::getSubsystem<DeterministicRun>().getPolicy()._bFinishAssetJobsEachFrame == SW_TRUE;

        // 1. 모든 원점에서 로딩 반경 + 여유보다 멀거나 레이어가 꺼진 셀을 되돌린다(읽기 취소 · 상태를 떠 두고 내리기). 내리기 전에 소속을 지금 자리로 다시 정한다(파일 머리말의 셀 경계).
        const float32 unloadRadius        = getUnloadRadius();
        const float32 unloadRadiusSquared = unloadRadius * unloadRadius;
        _listScratchUnload.clear();
        for ( uint32 slotIndex = 0; slotIndex < static_cast<uint32>( _listCell.size() ); ++slotIndex )
        {
            const CellSlot& slot    = getSlot( slotIndex );
            const bool      bActive = slot._state != CellState::Unloaded && slot._state != CellState::Failed;
            if ( bActive && ( computeNearestSourceDistanceSquared( slot._cell ) > unloadRadiusSquared || areSlotLayersActive( slot ) == false ) )
                _listScratchUnload.push_back( slotIndex );
        }
        if ( _listScratchUnload.empty() == false )
        {
            regroupObjects( _listScratchUnload );
            for ( const uint32 slotIndex : _listScratchUnload )
            {
                unloadCell( getSlot( slotIndex ), true );
            }
        }
        // 2. 원점마다 로딩 반경 안의 셀(합집합) 중 아직 지어지지 않은 것을 모은다.
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
                    const CellSlot& slot    = getSlot( slotIndex );
                    const bool      bStable = slot._state == CellState::Loaded || slot._state == CellState::Failed || areSlotLayersActive( slot ) == false; // 이미 올라 있거나 짓지 못한 셀이거나 레이어가 꺼졌다
                    if ( bStable )
                        continue;
                    _listScratchLoad.push_back( slotIndex ); // 원점 반경이 겹치면 같은 셀이 여럿 들어온다 — 아래에서 한 번에 거른다
                }
            }
        }
        VectorUtil::sortUnique( _listScratchLoad ); // 모양이 `contains` + `push_back` 이면 셀 수의 제곱이 든다
        // 가까운 셀부터. 같은 거리는 셀 순서(z, x, 레이어 조각)로 — 결정적이다.
        VectorUtil::sort( _listScratchLoad, [this]( uint32 lhs, uint32 rhs )
        {
            const float32 lhsDistance = computeNearestSourceDistanceSquared( getSlot( lhs )._cell );
            const float32 rhsDistance = computeNearestSourceDistanceSquared( getSlot( rhs )._cell );
            return lhsDistance != rhsDistance ? lhsDistance < rhsDistance : lhs < rhs;
        } );

        // 3. 내용이 없는 셀의 읽기를 프레임당 `_maxCellLoadsPerFrame` 개까지 건다. 내용을 든 셀(메모리 문서 · 떠 둔 상태)은 읽지 않고 짓기로 간다.
        uint32 requestCount = 0;
        for ( const uint32 slotIndex : _listScratchLoad )
        {
            CellSlot& slot = getSlot( slotIndex );
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
        for ( unique_ptr<CellSlot>& pSlot : _listCell )
        {
            if ( pSlot->_state != CellState::Loading )
                continue;
            if ( bFinishEachFrame )
                pSlot->_readHandle.wait();
            collectCellContent( *pSlot );
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
                const CellSlot& slot = getSlot( slotIndex );
                if ( slot._state != pass || ( pass == CellState::Ready && areEarlierSlotsBuilt( slotIndex ) == false ) )
                    continue;
                if ( bProgressed && budgetNanoseconds > 0 && stopwatch.getElapsedNanoseconds() >= budgetNanoseconds )
                    return; // 남은 셀은 다음 프레임에 잇는다
                const bool bFinished = buildCell( slotIndex, stopwatch, budgetNanoseconds, bProgressed == false );
                bProgressed          = true;
                if ( bFinished == false )
                    return; // 예산에 닿았다
            }
        }
    }

    void SceneStreamingSubsystem::collectCellsChangedSince( const uint64 serial, vector<StreamingCellCoord>& outListCell ) const
    {
        outListCell.clear();
        for ( const unique_ptr<CellSlot>& pSlot : _listCell )
        {
            if ( pSlot->_changeSerial > serial )
                outListCell.push_back( pSlot->_cell );
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

    bool SceneStreamingSubsystem::isWithinUnloadRadius( const StreamingCellCoord& cell ) const
    {
        const float32 unloadRadius = getUnloadRadius();
        return computeNearestSourceDistanceSquared( cell ) <= unloadRadius * unloadRadius;
    }

    bool SceneStreamingSubsystem::areSlotLayersActive( const CellSlot& slot ) const
    {
        return slot._listDataLayer.empty() || _pScene->getObjectManager()->getSubsystem<SceneDataLayerSubsystem>().areLayersActive( slot._listDataLayer );
    }

    bool SceneStreamingSubsystem::areEarlierSlotsBuilt( const uint32 slotIndex ) const
    {
        const auto indexIt = _mapCellIndex.find( makeCellKey( getSlot( slotIndex )._cell ) );
        if ( indexIt == _mapCellIndex.end() )
            return true;
        for ( const uint32 earlierIndex : indexIt->second )
        {
            if ( earlierIndex == slotIndex )
                return true; // 조각은 문서 순서다 — 여기까지가 앞 조각이다
            const CellSlot& earlier   = getSlot( earlierIndex );
            const bool      bComplete = earlier._state == CellState::Loaded || earlier._state == CellState::Failed || areSlotLayersActive( earlier ) == false;
            if ( bComplete == false )
                return false;
        }
        return true;
    }

    bool SceneStreamingSubsystem::computeObjectCell( const GameObject& object, StreamingCellCoord& outCell ) const
    {
        const SceneComponent* pRoot = object.getPrimarySceneComponent();
        if ( pRoot == nullptr )
            return false;
        outCell = _grid.computeCell( pRoot->getWorldPosition() );
        return true;
    }

    void SceneStreamingSubsystem::regroupObjects( const vector<uint32>& listUnloading )
    {
        /** @brief 소속을 바꿀 루트 하나입니다. */
        struct MoveCandidate
        {
            uint64             _objectID{ 0 };
            uint32             _ownerIndex{ kNoSlot };
            StreamingCellCoord _cell{};
        };
        // 순회 안에서는 오브젝트를 지울 수 없다 — 바꿀 것을 모은 뒤 옮긴다.
        vector<MoveCandidate> listMove;
        _pScene->getObjectManager()->forEachGameObject( SceneIterationKey{}, [this, &listMove]( const GameObject* pObject )
        {
            if ( pObject->getParent() != nullptr || pObject->isTransient() || pObject->isPendingDestroy() )
                return; // 자식은 루트와 함께 간다 · 파생 오브젝트는 세운 쪽이 다시 세운다
            StreamingCellCoord cell{};
            if ( computeObjectCell( *pObject, cell ) == false )
                return; // 자리가 없다 — 쿠커도 기본 묶음에 둔다
            const uint64 objectID = pObject->getObjectID();
            const auto   ownerIt  = _mapObjectSlot.find( objectID );
            if ( ownerIt != _mapObjectSlot.end() )
            {
                if ( getSlot( ownerIt->second )._cell != cell )
                    listMove.push_back( MoveCandidate{ objectID, ownerIt->second, cell } );
                return;
            }
            // 셀에 들지 않은 루트 — 늘 올라 있는 것 · 레이어 묶음은 그대로, 런타임 스폰은 반경 밖이면 그 자리 셀에 든다.
            const bool bResident = pObject->isAlwaysLoaded() || _pScene->findEntityDataLayers( objectID ) != nullptr;
            if ( bResident == false && isWithinUnloadRadius( cell ) == false )
                listMove.push_back( MoveCandidate{ objectID, kNoSlot, cell } );
        } );
        for ( const MoveCandidate& candidate : listMove )
        {
            const bool           bOwned         = candidate._ownerIndex != kNoSlot;
            const bool           bFromUnloading = bOwned == false || VectorUtil::contains( listUnloading, candidate._ownerIndex );
            const vector<string> listDataLayer  = bOwned ? getSlot( candidate._ownerIndex )._listDataLayer : vector<string>{};
            moveObjectGroup( candidate._objectID, candidate._ownerIndex, candidate._cell, listDataLayer, bFromUnloading );
        }
    }

    void SceneStreamingSubsystem::moveObjectGroup( const uint64 rootObjectID, const uint32 fromIndex, const StreamingCellCoord& cell, const vector<string>& listDataLayer,
                                                   const bool bFromUnloading )
    {
        const GameObjectManager& objects = *_pScene->getObjectManager();
        // 루트와, 같은 조각에 든 자손 엔티티(엔티티에 붙은 엔티티) — 함께 떠야 부착의 파일 id 가 이어진다.
        vector<uint64> listGroup;
        listGroup.push_back( rootObjectID );
        if ( fromIndex != kNoSlot )
        {
            for ( const uint64 objectID : getSlot( fromIndex )._listObjectID )
            {
                const GameObject* pObject = objectID != rootObjectID ? objects.findGameObjectByID( objectID ) : nullptr;
                for ( const GameObject* pParent = pObject != nullptr ? pObject->getParent() : nullptr; pParent != nullptr; pParent = pParent->getParent() )
                {
                    if ( pParent->getObjectID() == rootObjectID )
                    {
                        listGroup.push_back( objectID );
                        break;
                    }
                }
            }
        }

        const uint32 toIndex  = findOrAddSlot( cell, listDataLayer );
        CellSlot&    to       = getSlot( toIndex );
        const bool   bInRange = isWithinUnloadRadius( cell ) && areSlotLayersActive( to );
        // 반경 안의 빈 조각(옮겨 온 것을 받으려 만든 것) — 올라 있는 조각으로 두고 오브젝트를 살려 받는다(사라졌다 나타나지 않게).
        const bool bEmptyAddedSlot = to._bFromDocument == false && to._state == CellState::Unloaded && to._listNode.empty() && to._listArrivedNode.empty() && to._listObjectID.empty();
        if ( bEmptyAddedSlot && bInRange )
        {
            to._state        = CellState::Loaded;
            to._changeSerial = ++_changeSerial;
        }
        const bool bKeepAlive = to._state == CellState::Loaded;
        // 실패한 조각에는 넘기지 않는다(주인과 함께 떠 둔다). 주인이 남고 옮길 조각이 반경 안인데 아직 오르지 않았으면 오른 뒤에 넘긴다.
        if ( bKeepAlive == false && ( to._state == CellState::Failed || ( bInRange && bFromUnloading == false ) ) )
            return;

        if ( fromIndex != kNoSlot )
        {
            vector<uint64>& listFrom = getSlot( fromIndex )._listObjectID;
            vector<uint64>  listKept;
            listKept.reserve( listFrom.size() );
            for ( const uint64 objectID : listFrom )
            {
                if ( VectorUtil::contains( listGroup, objectID ) == false )
                    listKept.push_back( objectID );
            }
            listFrom = std::move( listKept );
        }
        if ( bKeepAlive )
        {
            if ( fromIndex == kNoSlot )
                (void)_pScene->getOrAssignFileID( rootObjectID ); // 셀에 드는 스폰도 셀 엔티티처럼 파일 id 를 든다
            for ( const uint64 objectID : listGroup )
            {
                to._listObjectID.push_back( objectID );
                _mapObjectSlot[objectID] = toIndex;
            }
            return;
        }

        // 조각이 내려가 있다 — 떠서 그 조각의 상태에 합치고 지운다. 다음에 오를 때 그 조각과 같은 배치로 다시 선다.
        // 런타임 스폰은 여기서 파일 id 를 받는다 — 다른 엔티티가 가리키던 핸들이 떠 둔 상태에서 이어지고, 세이브 몫이 같은 id 로 싣는다(새 id 는 읽지 않은 셀 내용 위에서 고른다).
        if ( fromIndex == kNoSlot )
            (void)_pScene->getOrAssignFileID( rootObjectID );
        vector<SceneDocument::SceneObjectNode> listNode;
        _pScene->captureCellNodes( listGroup, listNode );
        for ( const uint64 objectID : listGroup )
        {
            _mapObjectSlot.erase( objectID );
        }
        _pScene->unloadCell( listGroup );
        // 떠 둔 상태를 든 조각이 아직 짓기 전이면 그 상태에, 아니면(쿠킹 내용을 읽는 중 · 짓는 중 · 쿠킹 내용만 듦) 들어온 엔티티로 둔다.
        const bool                              bIntoContent = to._bContentHeld && ( to._state == CellState::Unloaded || to._state == CellState::Ready );
        vector<SceneDocument::SceneObjectNode>& listInto     = bIntoContent ? to._listNode : to._listArrivedNode;
        for ( SceneDocument::SceneObjectNode& node : listNode )
        {
            listInto.push_back( std::move( node ) );
        }
    }

    bool SceneStreamingSubsystem::isFloorMissing( const float3& position ) const
    {
        return _pScene != nullptr && isFloorMissing( _grid.computeCell( position ) );
    }

    bool SceneStreamingSubsystem::isFloorMissing( const StreamingCellCoord& cell ) const
    {
        const auto indexIt = _mapCellIndex.find( makeCellKey( cell ) );
        if ( indexIt == _mapCellIndex.end() )
            return false;
        for ( const uint32 slotIndex : indexIt->second )
        {
            const CellSlot& slot     = getSlot( slotIndex );
            const bool      bPending = slot._state != CellState::Loaded && slot._state != CellState::Failed;
            if ( slot._bFromDocument && bPending && areSlotLayersActive( slot ) )
                return true;
        }
        return false;
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

    bool SceneStreamingSubsystem::buildCell( const uint32 slotIndex, const Stopwatch& stopwatch, const int64 budgetNanoseconds, const bool bForceProgress )
    {
        CellSlot& slot = getSlot( slotIndex );
        if ( slot._state == CellState::Ready )
        {
            // 들어온 엔티티를 내용 뒤에 붙여 같은 배치로 짓는다 — 따로 지으면 먼저 선 쪽이 아직 없는 쪽을 찾는다. 붙인 뒤로는 내용을 든다(짓다 내려도 들어온 엔티티를 잃지 않게).
            if ( slot._listArrivedNode.empty() == false )
            {
                for ( SceneDocument::SceneObjectNode& node : slot._listArrivedNode )
                {
                    slot._listNode.push_back( std::move( node ) );
                }
                slot._listArrivedNode.clear();
                slot._bContentHeld = SW_TRUE;
            }
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
        for ( const uint64 objectID : slot._listObjectID )
        {
            _mapObjectSlot[objectID] = slotIndex;
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
                for ( const uint64 objectID : slot._listObjectID )
                {
                    _mapObjectSlot.erase( objectID );
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

    void SceneStreamingSubsystem::writeSaveState( vector<uint8>& outBytes ) const
    {
        // 문서 내용 그대로인 조각(쿠킹 내용만 들고 들어온 것도 없다)은 싣지 않는다 — 올라 있는 조각은 지금 떠서 싣는다(그동안 바뀌었을 수 있다).
        vector<const CellSlot*> listSaved;
        for ( const unique_ptr<CellSlot>& pSlot : _listCell )
        {
            if ( pSlot->_state == CellState::Loaded || pSlot->_bContentHeld || pSlot->_listArrivedNode.empty() == false )
                listSaved.push_back( pSlot.get() );
        }
        Archive arch;
        arch << static_cast<uint32>( listSaved.size() );
        const vector<SceneDocument::SceneObjectNode> listNone;
        for ( const CellSlot* pSlot : listSaved )
        {
            arch << pSlot->_cell._x << pSlot->_cell._z;
            arch << static_cast<uint32>( pSlot->_listDataLayer.size() );
            for ( const string& layer : pSlot->_listDataLayer )
            {
                arch << string_view( layer );
            }
            vector<SceneDocument::SceneObjectNode> listCaptured;
            if ( pSlot->_state == CellState::Loaded )
                _pScene->captureCellNodes( pSlot->_listObjectID, listCaptured );
            const bool                                    bHeld     = pSlot->_state == CellState::Loaded || pSlot->_bContentHeld;
            const vector<SceneDocument::SceneObjectNode>& listState = pSlot->_state == CellState::Loaded ? listCaptured : ( bHeld ? pSlot->_listNode : listNone );
            arch << static_cast<uint8>( bHeld ? 1u : 0u );
            SceneStreamingSaveInternal::writeNodes( arch, listState );
            SceneStreamingSaveInternal::writeNodes( arch, pSlot->_listArrivedNode );
        }
        outBytes.assign( arch.getData(), arch.getData() + arch.getSize() );
    }

    bool SceneStreamingSubsystem::readSaveState( const uint8* pData, const size_t size, SaveState& outState )
    {
        outState._listCell.clear();
        if ( pData == nullptr )
            return false;
        Archive arch( pData, size );
        uint32  cellCount{ 0 };
        if ( arch.readCount( SceneStreamingSaveInternal::kMinBytesPerCell, cellCount ) == false )
            return false;
        outState._listCell.reserve( cellCount );
        for ( uint32 cellIndex = 0; cellIndex < cellCount; ++cellIndex )
        {
            SavedCell saved{};
            arch >> saved._cell._x >> saved._cell._z;
            uint32 layerCount{ 0 };
            if ( arch.readCount( sizeof( uint32 ), layerCount ) == false )
                return false;
            saved._listDataLayer.resize( layerCount );
            for ( string& layer : saved._listDataLayer )
            {
                arch >> layer;
            }
            uint8 held{ 0 };
            arch >> held;
            saved._bContentHeld = held != 0;
            if ( SceneStreamingSaveInternal::readNodes( arch, saved._listNode ) == false || SceneStreamingSaveInternal::readNodes( arch, saved._listArrivedNode ) == false || arch.isError() )
            {
                SW_LOG_ERROR_ID( "SceneStreaming.SaveCellsCorrupted", "Save game streaming cell %# of %# could not be read - the save is not used", cellIndex, cellCount );
                outState._listCell.clear();
                return false;
            }
            outState._listCell.push_back( std::move( saved ) );
        }
        return arch.isError() == false;
    }

    void SceneStreamingSubsystem::applySaveState( SaveState state )
    {
        if ( _pScene == nullptr )
        {
            if ( state._listCell.empty() == false )
                SW_LOG_ERROR_ID( "SceneStreaming.SaveCellsWithoutStreaming", "The save carries %# streaming cells but the active scene does not stream cells - those objects are not restored",
                                 static_cast<uint32>( state._listCell.size() ) );
            return;
        }
        for ( SavedCell& saved : state._listCell )
        {
            const uint32 slotIndex = findOrAddSlot( saved._cell, saved._listDataLayer );
            CellSlot&    slot      = getSlot( slotIndex );
            unloadCell( slot, false ); // 복원은 비운 매니저 위다 — 남은 오브젝트가 있으면 세이브의 상태로 갈아 끼운다
            slot._state = CellState::Unloaded;
            // 메모리 문서의 조각은 늘 내용을 든다 — 든 표시가 없는 세이브라도 문서 내용을 지우지 않는다(읽을 파일 구간이 없다).
            if ( saved._bContentHeld || slot._contentSize == 0 )
            {
                if ( saved._bContentHeld )
                    slot._listNode = std::move( saved._listNode );
                slot._bContentHeld = SW_TRUE;
            }
            else
            {
                slot._listNode.clear();
                slot._bContentHeld = SW_FALSE;
            }
            slot._listArrivedNode = std::move( saved._listArrivedNode );
        }
    }
} // namespace sw
