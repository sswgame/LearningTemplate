/**
 * @file SceneTransformHierarchy.cpp
 * @brief 트랜스폼 계층 플러시 구현입니다. 더티 루트만 돌고, 루트 단위로 병렬이며, DFS 스택은 슬롯별로 씁니다.
 */
#include "pch.h"

#include "Engine/Object/Component/SceneTransformHierarchy.h"

#include "Core/Delegate/Delegate.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/SceneTransformStorage.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"

namespace sw
{
    SceneTransformHierarchy::SceneTransformHierarchy()
        : _listDirtyRoot{}
        , _listDirtyRootScratch{}
        , _pDirtyRootScratch{ nullptr }
        , _dirtyRootScratchCount{ 0 }
        , _listWriteScratch{}
        , _pWriteScratch{ nullptr }
        , _listPendingSlotScratch{}
        , _pPendingSlotScratch{ nullptr }
        , _writeScratchCount{ 0 }
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

    void SceneTransformHierarchy::beginQueuedWrites()
    {
        const uint32 slotCount = engine::getParallelScratchSlotCount();
        if ( _listWriteScratch.size() < slotCount )
            _listWriteScratch.resize( slotCount );
        if ( _listPendingSlotScratch.size() < _listWriteScratch.size() )
            _listPendingSlotScratch.resize( _listWriteScratch.size() );
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

    bool SceneTransformHierarchy::applyPendingSlot( SceneTransformStorage& storage, uint32 transformSlot, PrimitiveRegistry& registry )
    {
        SceneTransformPage* pPage = storage.findPage( transformSlot );
        if ( pPage == nullptr )
            return false;
        SceneTransformPage& page        = *pPage;
        const uint32        pageIndex   = transformSlot & SceneTransformPage::kSlotMask;
        const uint8         pendingMask = page._arrPendingMask[pageIndex];
        page._arrPendingMask[pageIndex] = 0;

        // 세터와 같은 규칙이다: 제곱 거리에 제곱한 허용치, 값이 같으면 쓰지 않는다.
        bool bChanged = false;
        if ( ( pendingMask & SceneTransformPage::kPendingPosition ) != 0 &&
             float3::getDistanceSquared( page._arrLocalPosition[pageIndex], page._arrPendingPosition[pageIndex] ) > MathUtil::EpsilonSquared )
        {
            page._arrLocalPosition[pageIndex] = page._arrPendingPosition[pageIndex];
            bChanged                          = true;
        }
        if ( ( pendingMask & SceneTransformPage::kPendingRotation ) != 0 &&
             float3::getDistanceSquared( page._arrLocalRotation[pageIndex], page._arrPendingRotation[pageIndex] ) > MathUtil::EpsilonSquared )
        {
            page._arrLocalRotation[pageIndex] = page._arrPendingRotation[pageIndex];
            bChanged                          = true;
        }
        if ( ( pendingMask & SceneTransformPage::kPendingScale ) != 0 &&
             float3::getDistanceSquared( page._arrLocalScale[pageIndex], page._arrPendingScale[pageIndex] ) > MathUtil::EpsilonSquared )
        {
            page._arrLocalScale[pageIndex] = page._arrPendingScale[pageIndex];
            bChanged                       = true;
        }
        if ( bChanged == false )
            return false;

        SceneComponent* pOwner = page._arrOwner[pageIndex];
        const uint8     flag   = page._arrFlag[pageIndex];
        if ( ( flag & ( SceneTransformPage::kHasParent | SceneTransformPage::kHasChildren ) ) == 0 )
        {
            // 잎 루트다. 순서를 기다릴 부모도 내려갈 자식도 없으니 여기서 곧장 월드를 만든다 — 컴포넌트를 만지지 않는다. 컴포넌트가
            // 이미 더티(틱 전에 쓰였고 플러시 전)라면 더티 목록에 올라 있으므로 플러시가 한 번 더 합성한다. 값은 같다.
            SceneTransformStorage::composeWorld( page, pageIndex, nullptr, nullptr );
            const uint32 primitiveIndex = page._arrPrimitiveIndex[pageIndex];
            if ( primitiveIndex != SceneTransformStorage::kNoPrimitive )
                registry.markTransformDirty( primitiveIndex );
            if ( ( flag & SceneTransformPage::kNotifyOwner ) != 0 && pOwner != nullptr )
                pOwner->onWorldTransformUpdated();
            return true;
        }

        // 계층이 있는 것은 컴포넌트를 거쳐 더티를 세우고 루트를 올린다. 플러시가 위에서부터 내려간다.
        if ( pOwner != nullptr )
            pOwner->markHierarchyDirtyParallel();
        return true;
    }

    bool SceneTransformHierarchy::queueWriteParallel( const SceneTransformWrite& write )
    {
        const uint32 slot = engine::getParallelScratchSlot();
        if ( slot >= _writeScratchCount )
            return false;

        vector<SceneTransformWrite>& listSlot = _pWriteScratch[slot];
        // 같은 컴포넌트에 잇따라 쓰면 한 건으로 합친다. 마지막 값이 이긴다(세터를 차례로 부른 것과 같다).
        if ( listSlot.empty() == false && listSlot.back()._handle == write._handle )
        {
            SceneTransformWrite& last = listSlot.back();
            if ( write._bSetPosition != SW_FALSE )
            {
                last._localPosition = write._localPosition;
                last._bSetPosition  = SW_TRUE;
            }
            if ( write._bSetRotation != SW_FALSE )
            {
                last._localRotation = write._localRotation;
                last._bSetRotation  = SW_TRUE;
            }
            if ( write._bSetScale != SW_FALSE )
            {
                last._localScale = write._localScale;
                last._bSetScale  = SW_TRUE;
            }
            return true;
        }
        listSlot.push_back( write );
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
        for ( vector<uint32>& listSlot : _listPendingSlotScratch )
            listSlot.clear();
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
