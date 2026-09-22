/**
 * @file SceneTransformHierarchy.cpp
 * @brief 트랜스폼 계층 플러시 구현 — 더티 루트만, 루트 단위 병렬, 슬롯별 DFS 스택.
 */
#include "pch.h"

#include "Engine/Object/Component/SceneTransformHierarchy.h"

#include "Core/Delegate/Delegate.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Object/Component/SceneComponent.h"

namespace sw
{
    SceneTransformHierarchy::SceneTransformHierarchy()
        : _listRoot{}
        , _listDirtyRoot{}
        , _listDirtyRootScratch{}
        , _pDirtyRootScratch{ nullptr }
        , _dirtyRootScratchCount{ 0 }
        , _rootMutex{}
        , _listScratchStack{}
        , _dirtyGeneration{ 1 }
        , _lastFlushedGeneration{ 0 }
    {
    }

    void SceneTransformHierarchy::registerRoot( SceneComponent* pComp )
    {
        if ( pComp == nullptr )
            return;
        {
            std::unique_lock<std::shared_mutex> lock{ _rootMutex };
            // 자기 자리를 들고 있으면 이미 루트다 — 목록을 훑지 않는다(8000 개면 등록마다 8000 번 비교였다).
            if ( pComp->_rootIndex == SceneComponent::kNotInList )
            {
                pComp->_rootIndex = static_cast<uint32>( _listRoot.size() );
                _listRoot.push_back( pComp );
            }
        }
        // 컴포넌트는 더티로 태어난다 — 루트가 되는 순간 플러시 목록에도 올라야 첫 플러시가 월드 캐시를 만든다.
        if ( pComp->isTransformDirty() || pComp->hasDirtyDescendant() )
            queueDirtyRoot( pComp );
    }

    void SceneTransformHierarchy::unregisterRoot( SceneComponent* pComp )
    {
        if ( pComp == nullptr )
            return;
        std::unique_lock<std::shared_mutex> lock{ _rootMutex };

        // 자기 자리로 O(1) swap-remove — 선형으로 찾던 때는 8000 개를 지우면 3200만 번 비교였다(개당 2 µs).
        const uint32 rootIndex = pComp->_rootIndex;
        if ( rootIndex != SceneComponent::kNotInList && rootIndex < _listRoot.size() && _listRoot[rootIndex] == pComp )
        {
            SceneComponent* pMoved = _listRoot.back();
            _listRoot[rootIndex]   = pMoved;
            pMoved->_rootIndex     = rootIndex;
            _listRoot.pop_back();
        }
        pComp->_rootIndex = SceneComponent::kNotInList;

        // 더 이상 루트가 아니다 — 플러시 목록에서도 뺀다(부모 아래로 들어갔으면 그 루트가 대신 오른다). 자리를 알면 O(1),
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
        // 스크래치는 배치가 시작하기 전(mergeQueuedDirtyRoots 의 짝)에 슬롯 수만큼 잡혀 있다 — 여기서는 자기 칸만 만진다.
        const uint32 slot = engine::getParallelScratchSlot();
        if ( slot < _dirtyRootScratchCount )
            _pDirtyRootScratch[slot].push_back( pRoot );
        else
        {
            // 서비스가 안 묶인 곳(테스트·도구)은 직렬이라 본 목록에 바로.
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
            // 스크래치에 있던 동안은 자리를 몰랐다 — 본 목록에 들어온 지금 적는다.
            for ( size_t index = firstIndex; index < _listDirtyRoot.size(); ++index )
            {
                if ( _listDirtyRoot[index] != nullptr )
                    _listDirtyRoot[index]->_dirtyRootIndex = static_cast<uint32>( index );
            }
        }
    }

    void SceneTransformHierarchy::flush()
    {
        const uint64 currentGeneration = _dirtyGeneration.load( std::memory_order_relaxed );
        if ( _listDirtyRoot.empty() )
        {
            _lastFlushedGeneration = currentGeneration;
            return;
        }

        std::shared_lock<std::shared_mutex> lock{ _rootMutex };

        // 스크래치는 스레드 슬롯마다 하나 — 워커 수는 서비스가 묶인 뒤에야 알 수 있으므로 여기서 맞춘다(한 번만 자란다).
        const uint32 slotCount = engine::getParallelScratchSlotCount();
        if ( _listScratchStack.size() < slotCount )
            _listScratchStack.resize( slotCount );

        // **루트 서브트리 단위로 병렬이다.** 서브트리끼리는 트리라 겹치지 않고, 부모의 월드 행렬을 읽는 것은 같은
        // 잡 안에서 순서대로 일어난다. 워커는 컨테이너를 만지지 않는다 — 포인터만 넘긴다(컨테이너 레이스 탐지기가
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

        // 목록을 비우며 대기 플래그를 내린다 — 다음 더티가 다시 올릴 수 있게.
        for ( SceneComponent* pRoot : _listDirtyRoot )
        {
            if ( pRoot == nullptr )
                continue;
            pRoot->_dirtyRootIndex = SceneComponent::kNotInList;
            pRoot->_bQueuedDirtyRoot.store( SW_FALSE, std::memory_order_release );
        }
        _listDirtyRoot.clear();
        _lastFlushedGeneration = currentGeneration;
    }

    void SceneTransformHierarchy::clear()
    {
        std::unique_lock<std::shared_mutex> lock{ _rootMutex };
        for ( SceneComponent* pRoot : _listDirtyRoot )
        {
            if ( pRoot == nullptr )
                continue;
            pRoot->_dirtyRootIndex = SceneComponent::kNotInList;
            pRoot->_bQueuedDirtyRoot.store( SW_FALSE, std::memory_order_release );
        }
        _listDirtyRoot.clear();
        for ( vector<SceneComponent*>& listScratch : _listDirtyRootScratch )
            listScratch.clear();
        for ( SceneComponent* pRoot : _listRoot )
        {
            if ( pRoot != nullptr )
                pRoot->_rootIndex = SceneComponent::kNotInList;
        }
        _listRoot.clear();
    }

    size_t SceneTransformHierarchy::getRootCount() const
    {
        std::shared_lock<std::shared_mutex> lock{ _rootMutex };
        return _listRoot.size();
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
