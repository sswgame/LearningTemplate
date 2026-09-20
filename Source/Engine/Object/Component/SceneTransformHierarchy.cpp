/**
 * @file SceneTransformHierarchy.cpp
 * @brief 트랜스폼 계층 플러시 구현 — 루트 단위 병렬, 슬롯별 DFS 스택.
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
        std::unique_lock<std::shared_mutex> lock{ _rootMutex };
        for ( SceneComponent* pExisting : _listRoot )
        {
            if ( pExisting == pComp )
                return;
        }
        _listRoot.push_back( pComp );
    }

    void SceneTransformHierarchy::unregisterRoot( SceneComponent* pComp )
    {
        if ( pComp == nullptr )
            return;
        std::unique_lock<std::shared_mutex> lock{ _rootMutex };
        for ( size_t rootIndex = 0; rootIndex < _listRoot.size(); ++rootIndex )
        {
            if ( _listRoot[rootIndex] == pComp )
            {
                _listRoot[rootIndex] = _listRoot.back();
                _listRoot.pop_back();
                return;
            }
        }
    }

    bool SceneTransformHierarchy::hasDirty() const
    {
        if ( _dirtyGeneration.load( std::memory_order_relaxed ) == _lastFlushedGeneration )
            return false;

        std::shared_lock<std::shared_mutex> lock{ _rootMutex };
        for ( const SceneComponent* pRoot : _listRoot )
        {
            if ( pRoot != nullptr && ( pRoot->isTransformDirty() || pRoot->hasDirtyDescendant() ) )
                return true;
        }
        return false;
    }

    void SceneTransformHierarchy::flush()
    {
        const uint64 currentGeneration = _dirtyGeneration.load( std::memory_order_relaxed );
        if ( currentGeneration == _lastFlushedGeneration )
            return;

        std::shared_lock<std::shared_mutex> lock{ _rootMutex };

        // 스크래치는 스레드 슬롯마다 하나 — 워커 수는 서비스가 묶인 뒤에야 알 수 있으므로 여기서 맞춘다(한 번만 자란다).
        const uint32 slotCount = engine::getParallelScratchSlotCount();
        if ( _listScratchStack.size() < slotCount )
            _listScratchStack.resize( slotCount );

        // **루트 서브트리 단위로 병렬이다.** 서브트리끼리는 트리라 겹치지 않고, 부모의 월드 행렬을 읽는 것은 같은
        // 잡 안에서 순서대로 일어난다. 워커는 컨테이너를 만지지 않는다 — 포인터만 넘긴다(컨테이너 레이스 탐지기가
        // 워커의 인덱싱을 잡는다). 스택은 자기 슬롯의 것을 쓴다.
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
        job._ppRoot   = _listRoot.data();
        job._pScratch = _listScratchStack.data();

        engine::runParallel( static_cast<uint32>( _listRoot.size() ), kParallelFlushRootCount,
                             SW_DELEGATE_METHOD( ParallelBlockDelegate, &RootFlushJob::flushRange, &job ) );

        _lastFlushedGeneration = currentGeneration;
    }

    void SceneTransformHierarchy::clear()
    {
        std::unique_lock<std::shared_mutex> lock{ _rootMutex };
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
