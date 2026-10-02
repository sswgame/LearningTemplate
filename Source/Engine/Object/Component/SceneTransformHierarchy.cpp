/**
 * @file SceneTransformHierarchy.cpp
 * @brief 트랜스폼 계층 구현입니다. 틱 중 쓰기의 적용 · 배치 쓰기 · 로컬 값이 바뀐 칸의 뒤처리 · 플러시(더티 루트만, 루트 단위 병렬, DFS 스택은 슬롯별).
 */
#include "pch.h"

#include "Engine/Object/Component/SceneTransformHierarchy.h"

#include "Core/Delegate/Delegate.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"

namespace sw
{
    namespace
    {
        /** @brief 이 스레드가 쓰기 큐에 올린 건의 순번입니다. 같은 오브젝트의 틱이 잇따라 쓴 건의 순서를 정합니다(한 오브젝트의 틱은 한 스레드가 돈다). */
        thread_local uint32 t_writeSequence = 0;

        struct SceneTransformHierarchyInternal
        {
            /** @brief 배치를 나눌 대상 버킷의 최대 수입니다. 잡 하나가 버킷 몇 개를 돕니다. */
            static constexpr uint32 kMaxBatchBucketCount = 64;

            /** @brief 배치 버킷 하나가 맡는 componentId 연속 구간의 크기(2 의 거듭제곱 지수)입니다. 256 개씩 묶어 돌려 가며 버킷에 줍니다. */
            static constexpr uint32 kBatchBucketRunShift = 8;

            /**
             * @brief 배치 건이 갈 버킷입니다. 같은 대상(componentId)은 늘 같은 버킷이라 한 워커가 배열 순서대로 씁니다.
             * @details componentId 를 256 개씩 묶은 **연속 구간**을 돌려 가며 버킷에 줍니다. 이웃한 컴포넌트(이웃한 칸)가 같은 버킷이라 워커끼리
             *          칸의 캐시 라인을 나눠 쓰지 않습니다. 처음에는 나머지(`id % 버킷 수`)로 나눠 이웃한 칸을 서로 다른 워커가 썼고(캐시 라인
             *          핑퐁) 잎 루트 8000 건이 150 → 450 us 였습니다(Release p50). 최솟값 · 최댓값으로 구간을 나누면 한 번 더 훑어야 합니다.
             */
            static uint32 batchBucketOf( const SceneTransformWrite& write, uint32 bucketCount )
            {
                return static_cast<uint32>( ( write._handle.componentId() >> kBatchBucketRunShift ) % bucketCount );
            }

            /** @brief 배치의 대상(componentId)이 엄격히 늘어나면 true 입니다 — 같은 핸들이 둘일 수 없어 연속 구간으로 나눠도 안전합니다. */
            static bool hasStrictlyIncreasingTargets( const SceneTransformWrite* pWrite, uint32 count )
            {
                for ( uint32 index = 1; index < count; ++index )
                {
                    if ( pWrite[index]._handle.componentId() <= pWrite[index - 1]._handle.componentId() )
                        return false;
                }
                return true;
            }

            /** @brief 틱 큐 건의 적용 순서입니다 — 대상(componentId), 쓴 오브젝트, 그 스레드의 순번. 같은 대상의 건이 연속하고 마지막이 이깁니다. */
            template <typename TOrderedWrite>
            static bool isWriteOrderedBefore( const TOrderedWrite& left, const TOrderedWrite& right )
            {
                if ( left._targetId != right._targetId )
                    return left._targetId < right._targetId;
                if ( left._key._writerId != right._key._writerId )
                    return left._key._writerId < right._key._writerId;
                return left._key._sequence < right._key._sequence;
            }

            /** @brief 핸들을 씬 컴포넌트로 풉니다(리플렉션 캐스트 없이 플래그 비트로). 씬 컴포넌트가 아니거나 죽었으면 nullptr 입니다. */
            static SceneComponent* resolveSceneComponent( GameObjectManager& manager, ComponentHandle handle )
            {
                Component* pComp = manager.resolveComponent( handle );
                if ( pComp == nullptr || pComp->isSceneComponent() == false )
                    return nullptr;
                return static_cast<SceneComponent*>( pComp );
            }

            /** @brief 비트 하나의 로컬 값을 세터로 씁니다(틱 중 배치 — 세터가 틱 중 쓰기 길을 탄다). */
            static void setLocalValue( SceneComponent& target, uint8 bit, const float3& value )
            {
                if ( bit == SceneTransformPage::kLocalPosition )
                    target.setLocalPosition( value );
                else if ( bit == SceneTransformPage::kLocalRotation )
                    target.setLocalRotation( value );
                else
                    target.setLocalScale( value );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SceneTransformHierarchy::SceneTransformHierarchy()
        : _listDirtyRoot{}
        , _listDirtyRootScratch{}
        , _pDirtyRootScratch{ nullptr }
        , _dirtyRootScratchCount{ 0 }
        , _listWriteScratch{}
        , _pWriteScratch{ nullptr }
        , _listWriteKeyScratch{}
        , _pWriteKeyScratch{ nullptr }
        , _listPendingSlotScratch{}
        , _pPendingSlotScratch{ nullptr }
        , _writeScratchCount{ 0 }
        , _listActiveScratchSlot{}
        , _listOrderedWrite{}
        , _listWriteGroupStart{}
        , _listBatchBucket{}
        , _listScratchStack{}
        , _dirtyGeneration{ 1 }
    {
    }

    void SceneTransformHierarchy::registerRoot( SceneComponent* pComp )
    {
        // 컴포넌트는 더티로 태어난다. 루트가 되는 순간 플러시 목록에 올라야 첫 플러시가 월드 캐시를 만든다.
        if ( pComp != nullptr && ( pComp->isTransformDirty() || pComp->hasDirtyDescendant() ) )
            queueDirtyRoot( pComp );
    }

    void SceneTransformHierarchy::unregisterRoot( SceneComponent* pComp )
    {
        if ( pComp == nullptr )
            return;

        // 더 이상 루트가 아니다. 플러시 목록에서도 뺀다(부모 아래로 들어갔으면 그 루트가 대신 오른다). 자리를 알면 O(1),
        // 병렬 스크래치에 있어 모르면(배치 도중의 재부모는 금지라 실제로는 없다) 훑는다.
        if ( pComp->_bQueuedDirtyRoot.exchange( SW_FALSE, std::memory_order_acq_rel ) != SW_FALSE )
        {
            const uint32 dirtyIndex = pComp->_dirtyRootIndex;
            if ( dirtyIndex < _listDirtyRoot.size() && _listDirtyRoot[dirtyIndex] == pComp )
            {
                SceneComponent* pMoved     = _listDirtyRoot.back();
                _listDirtyRoot[dirtyIndex] = pMoved;
                if ( pMoved != nullptr )
                    pMoved->_dirtyRootIndex = dirtyIndex;
                _listDirtyRoot.pop_back();
            }
            else
            {
                for ( size_t index = 0; index < _listDirtyRoot.size(); ++index )
                {
                    if ( _listDirtyRoot[index] != pComp )
                        continue;
                    SceneComponent* pMoved = _listDirtyRoot.back();
                    _listDirtyRoot[index]  = pMoved;
                    if ( pMoved != nullptr )
                        pMoved->_dirtyRootIndex = static_cast<uint32>( index );
                    _listDirtyRoot.pop_back();
                    break;
                }
                for ( vector<SceneComponent*>& listScratch : _listDirtyRootScratch )
                {
                    for ( SceneComponent*& pQueued : listScratch )
                    {
                        if ( pQueued == pComp )
                            pQueued = nullptr; // 병합·플러시가 빈 자리를 건너뛴다
                    }
                }
            }
            pComp->_dirtyRootIndex = SceneComponent::kNotInList;
        }
    }

    bool SceneTransformHierarchy::tryMarkQueued( SceneComponent* pRoot )
    {
        return pRoot->_bQueuedDirtyRoot.exchange( SW_TRUE, std::memory_order_acq_rel ) == SW_FALSE;
    }

    void SceneTransformHierarchy::queueDirtyRoot( SceneComponent* pRoot )
    {
        if ( pRoot == nullptr || tryMarkQueued( pRoot ) == false )
            return;
        pRoot->_dirtyRootIndex = static_cast<uint32>( _listDirtyRoot.size() );
        _listDirtyRoot.push_back( pRoot );
    }

    void SceneTransformHierarchy::queueDirtyRootParallel( SceneComponent* pRoot )
    {
        if ( pRoot == nullptr || tryMarkQueued( pRoot ) == false )
            return;
        // 스크래치는 배치가 시작하기 전(mergeQueuedDirtyRoots 의 짝)에 슬롯 수만큼 잡혀 있다. 여기서는 자기 칸만 만진다.
        const uint32 slot = engine::getParallelScratchSlot();
        if ( slot < _dirtyRootScratchCount )
            _pDirtyRootScratch[slot].push_back( pRoot );
        else
        {
            // 서비스가 묶이지 않은 곳(테스트 · 도구)은 직렬이라 본 목록에 바로 올린다.
            pRoot->_dirtyRootIndex = static_cast<uint32>( _listDirtyRoot.size() );
            _listDirtyRoot.push_back( pRoot );
        }
    }

    void SceneTransformHierarchy::mergeQueuedDirtyRoots()
    {
        const uint32 slotCount = engine::getParallelScratchSlotCount();
        if ( _listDirtyRootScratch.size() < slotCount )
            _listDirtyRootScratch.resize( slotCount );
        _pDirtyRootScratch     = _listDirtyRootScratch.data();
        _dirtyRootScratchCount = static_cast<uint32>( _listDirtyRootScratch.size() );
        for ( vector<SceneComponent*>& listScratch : _listDirtyRootScratch )
        {
            if ( listScratch.empty() )
                continue;
            const size_t firstIndex = _listDirtyRoot.size();
            _listDirtyRoot.insert( _listDirtyRoot.end(), listScratch.begin(), listScratch.end() );
            listScratch.clear();
            // 스크래치에 있던 동안은 자리를 몰랐다. 본 목록에 들어온 지금 적는다.
            for ( size_t index = firstIndex; index < _listDirtyRoot.size(); ++index )
            {
                if ( _listDirtyRoot[index] != nullptr )
                    _listDirtyRoot[index]->_dirtyRootIndex = static_cast<uint32>( index );
            }
        }
    }

    void SceneTransformHierarchy::beginTickWrites()
    {
        const uint32 slotCount = engine::getParallelScratchSlotCount();
        if ( _listWriteScratch.size() < slotCount )
            _listWriteScratch.resize( slotCount );
        if ( _listPendingSlotScratch.size() < _listWriteScratch.size() )
            _listPendingSlotScratch.resize( _listWriteScratch.size() );
        if ( _listWriteKeyScratch.size() < _listWriteScratch.size() )
            _listWriteKeyScratch.resize( _listWriteScratch.size() );
        _pWriteKeyScratch    = _listWriteKeyScratch.data();
        _pWriteScratch       = _listWriteScratch.data();
        _pPendingSlotScratch = _listPendingSlotScratch.data();
        _writeScratchCount   = static_cast<uint32>( _listWriteScratch.size() );
    }

    bool SceneTransformHierarchy::queuePendingSlot( uint32 transformSlot )
    {
        const uint32 slot = engine::getParallelScratchSlot();
        if ( slot >= _writeScratchCount )
            return false;
        _pPendingSlotScratch[slot].push_back( transformSlot );
        return true;
    }

    bool SceneTransformHierarchy::queueWriteParallel( const SceneTransformWrite& write, uint64 writerId )
    {
        const uint32 slot = engine::getParallelScratchSlot();
        if ( slot >= _writeScratchCount )
            return false;

        vector<SceneTransformWrite>& listSlot = _pWriteScratch[slot];
        vector<TickWriteKey>&        listKey  = _pWriteKeyScratch[slot];
        // 같은 오브젝트의 틱이 같은 컴포넌트에 잇따라 쓰면 한 건으로 합친다. 마지막 값이 이긴다(세터를 차례로 부른 것과 같다). 쓴 오브젝트가
        // 다르면 합치지 않는다 — 합친 건은 앞 건의 순서 키를 들고 있어, 사이에 다른 스레드가 쓴 건과의 순서가 틀어진다.
        if ( listSlot.empty() == false && listSlot.back()._handle == write._handle && listKey.back()._writerId == writerId )
        {
            SceneTransformWrite& last      = listSlot.back();
            const uint8          valueMask = write.getValueMask();
            for ( uint8 bit = SceneTransformPage::kLocalPosition; bit <= SceneTransformPage::kLocalScale; bit = static_cast<uint8>( bit << 1u ) )
            {
                if ( ( valueMask & bit ) != 0 )
                    last.setValue( bit, write.getValue( bit ) );
            }
            return true;
        }
        listSlot.push_back( write );
        listKey.push_back( TickWriteKey{ writerId, t_writeSequence++ } );
        return true;
    }

    bool SceneTransformHierarchy::hasQueuedWrites() const
    {
        for ( const vector<SceneTransformWrite>& listSlot : _listWriteScratch )
        {
            if ( listSlot.empty() == false )
                return true;
        }
        for ( const vector<uint32>& listSlot : _listPendingSlotScratch )
        {
            if ( listSlot.empty() == false )
                return true;
        }
        return false;
    }

    void SceneTransformHierarchy::clearQueuedWrites()
    {
        for ( vector<SceneTransformWrite>& listSlot : _listWriteScratch )
            listSlot.clear();
        for ( vector<TickWriteKey>& listKey : _listWriteKeyScratch )
            listKey.clear();
        for ( vector<uint32>& listSlot : _listPendingSlotScratch )
            listSlot.clear();
    }

    void SceneTransformHierarchy::notifyWorldUpdated( SceneTransformPage& page, uint32 pageIndex, PrimitiveRegistry* pRegistry )
    {
        // 렌더 프리미티브는 칸에 적힌 번호로 등록부에 바로 찍는다(메시 컴포넌트는 훅을 끈다). 그 밖의 파생은 알림 비트로 훅을 받는다.
        const uint32 primitiveIndex = page._arrPrimitiveIndex[pageIndex];
        if ( primitiveIndex != SceneTransformStorage::kNoPrimitive && pRegistry != nullptr )
            pRegistry->markTransformDirty( primitiveIndex );
        SceneComponent* pOwner = page._arrOwner[pageIndex];
        if ( ( page._arrFlag[pageIndex] & SceneTransformPage::kNotifyOwner ) != 0 && pOwner != nullptr )
            pOwner->onWorldTransformUpdated();
    }

    bool SceneTransformHierarchy::applyLocalChange( SceneTransformPage& page, uint32 pageIndex, PrimitiveRegistry* pRegistry )
    {
        if ( ( page._arrFlag[pageIndex] & ( SceneTransformPage::kHasParent | SceneTransformPage::kHasChildren ) ) == 0 )
        {
            // 잎 루트다. 순서를 기다릴 부모도 내려갈 자식도 없으니 여기서 곧장 월드를 만든다 — 컴포넌트를 만지지 않는다.
            SceneTransformStorage::composeWorld( page, pageIndex, nullptr, nullptr );
            notifyWorldUpdated( page, pageIndex, pRegistry );
            return true;
        }

        // 계층이 있는 것은 소유 컴포넌트를 거쳐 더티를 세우고 루트를 올린다. 플러시가 위에서부터 내려간다.
        SceneComponent* pOwner = page._arrOwner[pageIndex];
        if ( pOwner != nullptr )
            pOwner->markHierarchyDirtyParallel();
        return false;
    }

    bool SceneTransformHierarchy::applyPendingSlot( SceneTransformStorage& storage, uint32 transformSlot, PrimitiveRegistry& registry )
    {
        SceneTransformPage* pPage = storage.findPage( transformSlot );
        if ( pPage == nullptr )
            return false;
        SceneTransformPage& page        = *pPage;
        const uint32        pageIndex   = transformSlot & SceneTransformPage::kSlotMask;
        const uint8         pendingMask = page._arrPendingMask[pageIndex];
        page._arrPendingMask[pageIndex] = 0;

        bool bChanged = false;
        for ( uint8 bit = SceneTransformPage::kLocalPosition; bit <= SceneTransformPage::kLocalScale; bit = static_cast<uint8>( bit << 1u ) )
        {
            if ( ( pendingMask & bit ) != 0 && page.writeLocalValue( pageIndex, bit, page.getPendingValueRef( pageIndex, bit ) ) )
                bChanged = true;
        }
        if ( bChanged == false )
            return false;

        // 잎 루트의 컴포넌트가 이미 더티였다면(틱 전에 쓰였고 플러시 전) 더티 목록에 올라 있어 플러시가 한 번 더 합성한다. 값은 같다.
        // 그 더티를 내리려면 컴포넌트를 만져야 하는데, 이 길은 그것을 피하려고 칸만 본다.
        (void)applyLocalChange( page, pageIndex, &registry ); // 값은 이미 바뀌었다(위) — 뒤처리의 결과는 여기서 쓰지 않는다
        return true;
    }

    bool SceneTransformHierarchy::applyWrite( SceneComponent& target, const SceneTransformWrite& write )
    {
        SceneTransformPage& page      = *target._pTransformPage;
        const uint32        pageIndex = target.getPageIndex();
        const uint8         valueMask = write.getValueMask();
        bool                bChanged  = false;
        for ( uint8 bit = SceneTransformPage::kLocalPosition; bit <= SceneTransformPage::kLocalScale; bit = static_cast<uint8>( bit << 1u ) )
        {
            if ( ( valueMask & bit ) != 0 && page.writeLocalValue( pageIndex, bit, write.getValue( bit ) ) )
                bChanged = true;
        }
        if ( bChanged == false )
            return false;

        PrimitiveRegistry* pRegistry = ( target._pManager != nullptr ) ? &target._pManager->getPrimitiveRegistry() : nullptr;
        if ( applyLocalChange( page, pageIndex, pRegistry ) )
            target._bIsTransformDirty.store( SW_FALSE, std::memory_order_relaxed );
        return true;
    }

    uint32 SceneTransformHierarchy::applyWriteRange( GameObjectManager& manager, const SceneTransformWrite* pWrite, uint32 start, uint32 end, bool bUseCachedTarget )
    {
        uint32 changedCount = 0;
        for ( uint32 index = start; index < end; ++index )
        {
            SceneComponent* pScene = ( bUseCachedTarget && pWrite[index]._pTarget != nullptr )
                                       ? pWrite[index]._pTarget
                                       : SceneTransformHierarchyInternal::resolveSceneComponent( manager, pWrite[index]._handle );
            if ( pScene == nullptr || pScene->isPendingDestroy() )
                continue;
            if ( applyWrite( *pScene, pWrite[index] ) )
                ++changedCount;
        }
        return changedCount;
    }

    uint32 SceneTransformHierarchy::applyPendingSlots( PrimitiveRegistry& registry )
    {
        uint32 totalCount = 0;
        _listActiveScratchSlot.clear();
        for ( uint32 slot = 0; slot < _writeScratchCount; ++slot )
        {
            if ( _pPendingSlotScratch[slot].empty() )
                continue;
            totalCount += static_cast<uint32>( _pPendingSlotScratch[slot].size() );
            _listActiveScratchSlot.push_back( slot );
        }
        if ( totalCount == 0 )
            return 0;

        // 스레드 슬롯 하나가 잡 하나다. 칸 하나는 처음 대기에 든 스레드의 목록에만 있으므로 워커끼리 같은 칸을 만지지 않는다.
        struct PendingSlotJob
        {
            SceneTransformStorage* _pStorage{ nullptr };
            PrimitiveRegistry*     _pRegistry{ nullptr };
            vector<uint32>*        _pSlotList{ nullptr };
            const uint32*          _pActiveSlot{ nullptr };
            atomic<uint32>         _changedCount{ 0 };

            void applyRange( uint32 start, uint32 end )
            {
                uint32 changedCount = 0;
                for ( uint32 index = start; index < end; ++index )
                {
                    for ( const uint32 transformSlot : std::as_const( _pSlotList[_pActiveSlot[index]] ) )
                    {
                        if ( applyPendingSlot( *_pStorage, transformSlot, *_pRegistry ) )
                            ++changedCount;
                    }
                }
                if ( changedCount > 0 )
                    _changedCount.fetch_add( changedCount, std::memory_order_relaxed );
            }
        };
        PendingSlotJob job{};
        job._pStorage            = &SceneTransformStorage::get();
        job._pRegistry           = &registry;
        job._pSlotList           = _pPendingSlotScratch;
        job._pActiveSlot         = _listActiveScratchSlot.data();
        const uint32 activeCount = static_cast<uint32>( _listActiveScratchSlot.size() );
        if ( totalCount < kParallelWriteCount )
            job.applyRange( 0, activeCount );
        else
            engine::runParallel( activeCount, 1, SW_DELEGATE_METHOD( ParallelBlockDelegate, &PendingSlotJob::applyRange, &job ) );
        return job._changedCount.load( std::memory_order_relaxed );
    }

    uint32 SceneTransformHierarchy::applyOrderedWrites( const OrderedTickWrite* pOrdered, uint32 start, uint32 end )
    {
        uint32 changedCount = 0;
        for ( uint32 index = start; index < end; ++index )
        {
            const SceneTransformWrite& write   = *pOrdered[index]._pWrite;
            SceneComponent*            pTarget = write._pTarget;
            if ( pTarget == nullptr || pTarget->isPendingDestroy() )
                continue;
            if ( applyWrite( *pTarget, write ) )
                ++changedCount;
        }
        return changedCount;
    }

    uint32 SceneTransformHierarchy::applyQueuedWriteSlots( GameObjectManager& manager )
    {
        // 틱 큐의 대상은 같은 `tick()` 안에서 쌓이고 적용되므로 건이 든 `_pTarget` 을 믿는다(핸들을 다시 풀지 않는다).
        (void)manager;
        _listOrderedWrite.clear();
        for ( uint32 slot = 0; slot < _writeScratchCount; ++slot )
        {
            const vector<SceneTransformWrite>& listWrite = std::as_const( _pWriteScratch[slot] );
            const vector<TickWriteKey>&        listKey   = std::as_const( _pWriteKeyScratch[slot] );
            for ( size_t index = 0; index < listWrite.size(); ++index )
                _listOrderedWrite.push_back( OrderedTickWrite{ &listWrite[index], listWrite[index]._handle.componentId(), listKey[index] } );
        }
        const uint32 totalCount = static_cast<uint32>( _listOrderedWrite.size() );
        if ( totalCount == 0 )
            return 0;

        // **대상 단위로 정렬한다.** 같은 대상의 건이 연속하고, 그 안은 (쓴 오브젝트, 순번) 순이다 — 이기는 값이 스레드 배정과 무관하다.
        // 다른 오브젝트에 쓰는 건만 여기 오므로(자기 오브젝트는 대기 칸) 수가 작다.
        std::sort( _listOrderedWrite.begin(), _listOrderedWrite.end(), &SceneTransformHierarchyInternal::isWriteOrderedBefore<OrderedTickWrite> );
        if ( totalCount < kParallelWriteCount )
            return applyOrderedWrites( _listOrderedWrite.data(), 0, totalCount );

        // 대상 경계에서만 자른다 — 한 컴포넌트는 한 워커만 쓴다.
        const uint32 slotCount = MathUtil::max( 1u, engine::getParallelScratchSlotCount() );
        const uint32 groupSize = MathUtil::max( kParallelWriteCount / 4, totalCount / ( slotCount * 2 ) );
        _listWriteGroupStart.clear();
        _listWriteGroupStart.push_back( 0 );
        for ( uint32 index = 1; index < totalCount; ++index )
        {
            const bool bSameTarget = _listOrderedWrite[index]._targetId == _listOrderedWrite[index - 1]._targetId;
            if ( index - _listWriteGroupStart.back() >= groupSize && bSameTarget == false )
                _listWriteGroupStart.push_back( index );
        }
        _listWriteGroupStart.push_back( totalCount );

        struct GroupWriteJob
        {
            const OrderedTickWrite* _pOrdered{ nullptr };
            const uint32*           _pGroupStart{ nullptr };
            atomic<uint32>          _changedCount{ 0 };

            void applyRange( uint32 start, uint32 end )
            {
                uint32 changedCount = 0;
                for ( uint32 group = start; group < end; ++group )
                    changedCount += applyOrderedWrites( _pOrdered, _pGroupStart[group], _pGroupStart[group + 1] );
                if ( changedCount > 0 )
                    _changedCount.fetch_add( changedCount, std::memory_order_relaxed );
            }
        };
        GroupWriteJob job{};
        job._pOrdered           = _listOrderedWrite.data();
        job._pGroupStart        = _listWriteGroupStart.data();
        const uint32 groupCount = static_cast<uint32>( _listWriteGroupStart.size() - 1 );
        engine::runParallel( groupCount, 1, SW_DELEGATE_METHOD( ParallelBlockDelegate, &GroupWriteJob::applyRange, &job ) );
        return job._changedCount.load( std::memory_order_relaxed );
    }

    uint32 SceneTransformHierarchy::applyTickWrites( GameObjectManager& manager, PrimitiveRegistry& registry )
    {
        // 워커가 올리는 더티 루트(계층이 있는 칸)는 슬롯별 스크래치로 간다. 앞에서 슬롯 수만큼 잡아 두고, 끝나면 본 목록으로 합친다.
        mergeQueuedDirtyRoots();
        const uint32 changedCount = applyPendingSlots( registry ) + applyQueuedWriteSlots( manager );
        mergeQueuedDirtyRoots();
        clearQueuedWrites();
        if ( changedCount > 0 )
            notifyDirtied();
        return changedCount;
    }

    uint32 SceneTransformHierarchy::applyBatch( GameObjectManager& manager, const SceneTransformWrite* pWrite, uint32 count )
    {
        if ( pWrite == nullptr || count == 0 )
            return 0;

        // 틱 중이면 세터로 돌린다. 세터가 틱 중 쓰기 길을 탄다. 배치의 병렬 적용은 틱 밖에서만 안전하다.
        if ( manager.isStructuralMutationFrozen() )
        {
            uint32 deferredCount = 0;
            for ( uint32 index = 0; index < count; ++index )
            {
                SceneComponent* pScene = SceneTransformHierarchyInternal::resolveSceneComponent( manager, pWrite[index]._handle );
                if ( pScene == nullptr || pScene->isPendingDestroy() )
                    continue;
                const uint8 valueMask = pWrite[index].getValueMask();
                for ( uint8 bit = SceneTransformPage::kLocalPosition; bit <= SceneTransformPage::kLocalScale; bit = static_cast<uint8>( bit << 1u ) )
                {
                    if ( ( valueMask & bit ) != 0 )
                        SceneTransformHierarchyInternal::setLocalValue( *pScene, bit, pWrite[index].getValue( bit ) );
                }
                ++deferredCount;
            }
            return deferredCount;
        }

        // 워커가 올리는 더티 루트는 슬롯별 스크래치로 간다. 앞에서 슬롯 수만큼 잡아 두고, 끝나면 본 목록으로 합친다.
        mergeQueuedDirtyRoots();
        uint32 changedCount = 0;
        if ( count < kParallelWriteCount )
            changedCount = applyWriteRange( manager, pWrite, 0, count, false );
        else if ( SceneTransformHierarchyInternal::hasStrictlyIncreasingTargets( pWrite, count ) )
        {
            // 빠른 길: 대상이 엄격히 늘어나면(만든 순서대로 모은 배열 — 벤치 씬 · 인스턴스 갱신의 모양) 같은 핸들이 둘일 수 없다. 연속 구간으로
            // 나눠도 한 대상은 한 구간에만 있으므로 버킷 목록을 채우지 않는다(잎 루트 8000 건에서 그 패스가 ~35 us 였다).
            struct RangeWriteJob
            {
                GameObjectManager*         _pManager{ nullptr };
                const SceneTransformWrite* _pWrite{ nullptr };
                atomic<uint32>             _changedCount{ 0 };

                void applyRange( uint32 start, uint32 end )
                {
                    const uint32 rangeChanged = applyWriteRange( *_pManager, _pWrite, start, end, false );
                    if ( rangeChanged > 0 )
                        _changedCount.fetch_add( rangeChanged, std::memory_order_relaxed );
                }
            };
            RangeWriteJob job{};
            job._pManager = &manager;
            job._pWrite   = pWrite;
            engine::runParallel( count, kParallelWriteCount, SW_DELEGATE_METHOD( ParallelBlockDelegate, &RangeWriteJob::applyRange, &job ) );
            changedCount = job._changedCount.load( std::memory_order_relaxed );
        }
        else
        {
            // **대상 버킷으로 나눈다**(`batchBucketOf`). 같은 핸들은 늘 같은 버킷이라 한 워커가 배열 순서대로 쓴다 — 뒤의 건이 이긴다.
            // 예전에는 배열을 연속 구간으로 잘라 같은 핸들이 두 구간에 있으면 두 워커가 한 칸을 동시에 썼다. 나누기는 한 번 훑으며 건 번호를
            // 버킷 목록에 넣는 것뿐이다(버킷 목록은 프레임마다 재사용한다).
            const uint32 bucketCount = MathUtil::min( SceneTransformHierarchyInternal::kMaxBatchBucketCount, count / ( kParallelWriteCount / 4 ) );
            if ( _listBatchBucket.size() < bucketCount )
                _listBatchBucket.resize( bucketCount );
            for ( uint32 bucket = 0; bucket < bucketCount; ++bucket )
                _listBatchBucket[bucket].clear();
            for ( uint32 index = 0; index < count; ++index )
                _listBatchBucket[SceneTransformHierarchyInternal::batchBucketOf( pWrite[index], bucketCount )].push_back( index );

            // 워커는 컨테이너를 만지지 않는다. 포인터만 받는다. 핸들 해석은 슬롯 표라 락이 없고, 쓰기는 자기 버킷의 칸(과 부모 · 자식의
            // 더티 바이트)뿐이다.
            struct BucketWriteJob
            {
                GameObjectManager*         _pManager{ nullptr };
                const SceneTransformWrite* _pWrite{ nullptr };
                const vector<uint32>*      _pBucket{ nullptr };
                atomic<uint32>             _changedCount{ 0 };

                void applyRange( uint32 start, uint32 end )
                {
                    uint32 bucketChanged = 0;
                    for ( uint32 bucket = start; bucket < end; ++bucket )
                    {
                        for ( const uint32 writeIndex : std::as_const( _pBucket[bucket] ) )
                        {
                            const SceneTransformWrite& write  = _pWrite[writeIndex];
                            SceneComponent*            pScene = SceneTransformHierarchyInternal::resolveSceneComponent( *_pManager, write._handle );
                            if ( pScene != nullptr && pScene->isPendingDestroy() == false && applyWrite( *pScene, write ) )
                                ++bucketChanged;
                        }
                    }
                    if ( bucketChanged > 0 )
                        _changedCount.fetch_add( bucketChanged, std::memory_order_relaxed );
                }
            };
            BucketWriteJob job{};
            job._pManager = &manager;
            job._pWrite   = pWrite;
            job._pBucket  = _listBatchBucket.data();
            engine::runParallel( bucketCount, 1, SW_DELEGATE_METHOD( ParallelBlockDelegate, &BucketWriteJob::applyRange, &job ) );
            changedCount = job._changedCount.load( std::memory_order_relaxed );
        }
        mergeQueuedDirtyRoots();

        if ( changedCount > 0 )
            notifyDirtied();
        return changedCount;
    }

    void SceneTransformHierarchy::flush()
    {
        if ( _listDirtyRoot.empty() )
            return;

        // 스크래치는 스레드 슬롯마다 하나다. 워커 수는 서비스가 묶인 뒤에야 알 수 있으므로 여기서 맞춘다(한 번만 자란다).
        const uint32 slotCount = engine::getParallelScratchSlotCount();
        if ( _listScratchStack.size() < slotCount )
            _listScratchStack.resize( slotCount );

        // **루트 서브트리 단위로 병렬이다.** 서브트리끼리는 트리라 겹치지 않고, 부모의 월드 행렬을 읽는 것은 같은
        // 잡 안에서 순서대로 일어난다. 워커는 컨테이너를 만지지 않는다. 포인터만 넘긴다(컨테이너 레이스 탐지기가
        // 워커의 인덱싱을 잡는다). 스택은 자기 슬롯의 것을 쓴다. 도는 것은 **더티 루트 목록**뿐이다.
        struct RootFlushJob
        {
            SceneComponent* const* _ppRoot{ nullptr };
            FlushStack*            _pScratch{ nullptr };

            void flushRange( uint32 start, uint32 end )
            {
                FlushStack& stack = _pScratch[engine::getParallelScratchSlot()];
                for ( uint32 rootIndex = start; rootIndex < end; ++rootIndex )
                {
                    if ( _ppRoot[rootIndex] != nullptr )
                        flushSubtree( _ppRoot[rootIndex], false, stack );
                }
            }
        };
        RootFlushJob job{};
        job._ppRoot   = _listDirtyRoot.data();
        job._pScratch = _listScratchStack.data();

        engine::runParallel( static_cast<uint32>( _listDirtyRoot.size() ), kParallelFlushRootCount,
                             SW_DELEGATE_METHOD( ParallelBlockDelegate, &RootFlushJob::flushRange, &job ) );

        releaseDirtyRoots();
    }

    void SceneTransformHierarchy::releaseDirtyRoots()
    {
        // 목록을 비우며 대기 플래그를 내린다. 다음 더티가 다시 올릴 수 있게 한다.
        for ( SceneComponent* pRoot : _listDirtyRoot )
        {
            if ( pRoot == nullptr )
                continue;
            pRoot->_dirtyRootIndex = SceneComponent::kNotInList;
            pRoot->_bQueuedDirtyRoot.store( SW_FALSE, std::memory_order_release );
        }
        _listDirtyRoot.clear();
    }

    void SceneTransformHierarchy::clear()
    {
        releaseDirtyRoots();
        for ( vector<SceneComponent*>& listScratch : _listDirtyRootScratch )
            listScratch.clear();
    }

    void SceneTransformHierarchy::flushSubtree( SceneComponent* pRoot, bool bParentChanged, FlushStack& stack )
    {
        if ( pRoot == nullptr )
            return;

        // 깊은 계층에서 스택이 넘치지 않도록 명시적 스택으로 도는 DFS. 원소는 (노드, 부모가 바뀌었나).
        stack.clear();
        stack.emplace_back( pRoot, bParentChanged );

        while ( stack.empty() == false )
        {
            auto [node, parentDirty] = stack.back();
            stack.pop_back();

            if ( node == nullptr )
                continue;

            const bool bNeedsUpdate = parentDirty || node->isTransformDirty();
            if ( bNeedsUpdate )
                node->updateWorldTransformFromParent();

            if ( bNeedsUpdate || node->hasDirtyDescendant() )
            {
                const auto& children = node->getChildren();
                for ( auto it = children.rbegin(); it != children.rend(); ++it )
                {
                    stack.emplace_back( *it, bNeedsUpdate );
                }
            }
            node->clearDirtyDescendant();
        }
    }
} // namespace sw
