#include "pch.h"

#include "Core/Task/TaskNodePool.h"

namespace sw
{
    TaskNodePool::TaskNodePool( TaskManager* pOwner )
        : _pOwner{ pOwner }
        , _freeQueue{}
        , _listOverflowFree{}
        , _listSlab{}
        , _slabMutex{}
        , _listStageFree{}
        , _listStageAll{}
        , _stageMutex{}
        , _groupPool{}
        , _detachedReferenceCount{ 0 }
        , _bDetached{ false }
        , _dependencyMutex{}
    {
    }

    TaskNodePool::~TaskNodePool()
    {
        ScopedLock<mutex> lock{ _slabMutex };
        for ( const MemoryBlock& slab : _listSlab )
        {
            TaskNode* pSlab = slab.getAs<TaskNode>();
            if ( pSlab == nullptr )
                continue;
            for ( uint32 index = 0; index < kSlabSize; ++index )
            {
                pSlab[index].~TaskNode();
            }
        }
        _listSlab.clear(); // 노드를 소멸시킨 뒤 블록을 푼다
    }

    TaskNode* TaskNodePool::allocate()
    {
        TaskNode* pMem{ nullptr };
        if ( _freeQueue.dequeue( pMem ) == false || pMem == nullptr )
        {
            {
                ScopedLock<mutex> lock{ _slabMutex };
                if ( _listOverflowFree.empty() == false )
                {
                    pMem = _listOverflowFree.back();
                    _listOverflowFree.pop_back();
                }
            }
            if ( pMem == nullptr )
            {
                MemoryBlock slab  = MemoryBlock::allocate( sizeof( TaskNode ) * kSlabSize );
                TaskNode*   pSlab = slab.getAs<TaskNode>();
                for ( uint32 index = 0; index < kSlabSize; ++index )
                {
                    sw_placement_new( &pSlab[index] ) TaskNode();
                    pSlab[index]._pPool = this;
                    pSlab[index]._refCount.store( 0, std::memory_order_relaxed ); // 자유 노드는 0 이다(`detachOwner` 가 참조 남은 노드를 이것으로 가린다)
                }
                {
                    ScopedLock<mutex> lock{ _slabMutex };
                    _listSlab.push_back( std::move( slab ) ); // pSlab 은 블록 주소라 이동해도 그대로다
                }
                for ( uint32 index = 1; index < kSlabSize; ++index )
                {
                    if ( _freeQueue.enqueue( &pSlab[index] ) == false )
                    {
                        ScopedLock<mutex> lock{ _slabMutex };
                        _listOverflowFree.push_back( &pSlab[index] );
                    }
                }
                pMem = &pSlab[0];
            }
        }

        pMem->_unresolvedDependencies.store( 1, std::memory_order_relaxed );
        pMem->_pendingChildren.store( 1, std::memory_order_relaxed );
        pMem->_bScheduled.store( false, std::memory_order_relaxed );
        pMem->_bCancelled.store( false, std::memory_order_relaxed );
        pMem->_bParallelGroupParent.store( false, std::memory_order_relaxed );
        pMem->_refCount.store( 1, std::memory_order_relaxed );
        pMem->_pParent = nullptr;
#if !defined( SW_SHIPPING )
        pMem->_arrName[0] = 0;
#endif
        pMem->_affinity = TaskThreadAffinity::Any;
        pMem->_priority = TaskPriority::Normal;
        return pMem;
    }

    void TaskNodePool::deallocate( TaskNode* pNode )
    {
        if ( pNode == nullptr )
            return;
        pNode->_successors.clearAndRelease();
        // `whenAny` 트리거의 숨은 후속(사이클 검사가 따라간다)이 쥔 참조를 놓는다 — 노드가 더 이상 어느 목록에도 없어 검사가 닿지 않는다.
        TaskNode* const pAnyOfSuccessor = pNode->_pAnyOfSuccessor;
        pNode->_pAnyOfSuccessor         = nullptr;
        if ( pAnyOfSuccessor != nullptr )
            pAnyOfSuccessor->release();
        pNode->_callable    = std::monostate{};
        pNode->_parentStage = nullptr;
#if !defined( SW_SHIPPING )
        pNode->_arrName[0] = 0;
#endif
        if ( _bDetached.load( std::memory_order_acquire ) )
        {
            returnDetachedReference(); // 매니저가 없으니 다시 꺼낼 쪽도 없다 — 자유 목록에 넣지 않는다
            return;
        }

        if ( _freeQueue.enqueue( pNode ) == false )
        {
            ScopedLock<mutex> lock{ _slabMutex };
            _listOverflowFree.push_back( pNode );
        }
    }

    string TaskNodePool::describeFirstUnfinished( [[maybe_unused]] const StageNode* pStage )
    {
#if !defined( SW_SHIPPING )
        // 이름은 **참조를 잡은 뒤에** 읽는다. 참조가 있는 노드는 풀로 돌아가 이름을 지우거나 다시 쓰이지 않는다. 제출됐다(`_bScheduled`, 이름은 제출 전에 정해진다)는 것도
        // 참조를 잡은 **뒤에** 다시 확인한다 — 잡기 전에 노드가 돌아가 새 태스크로 다시 만들어졌다면 아직 제출 전이라 걸러지고, 제출된 것이면 이름은 이미 확정이다.
        TaskNode*         pHeld{ nullptr };
        string            name;
        vector<TaskNode*> listRejected; // 잡았다가 거른 노드. 잠금 밖에서 놓는다
        {
            ScopedLock<mutex> lock{ _slabMutex };
            for ( const MemoryBlock& slab : _listSlab )
            {
                TaskNode* pSlab = slab.getAs<TaskNode>();
                if ( pSlab == nullptr )
                    continue;
                for ( uint32 index = 0; index < kSlabSize && pHeld == nullptr; ++index )
                {
                    TaskNode& node = pSlab[index];
                    if ( node._bScheduled.load( std::memory_order_acquire ) == false || node._pendingChildren.load( std::memory_order_acquire ) <= 0 )
                        continue;
                    if ( node.tryRetain() == false )
                        continue;
                    // 스테이지를 주면 그 스테이지에 든 태스크만 본다(주인의 기한 대기 진단 — `TaskManager::waitStage`).
                    const bool bInStage         = pStage == nullptr || node._parentStage == pStage;
                    const bool bStillUnfinished = bInStage && node._bScheduled.load( std::memory_order_acquire ) && node._pendingChildren.load( std::memory_order_acquire ) > 0;
                    if ( bStillUnfinished && node._arrName[0] != '\0' )
                    {
                        name  = string( node._arrName );
                        pHeld = &node;
                    }
                    else
                    {
                        listRejected.push_back( &node );
                    }
                }
                if ( pHeld != nullptr )
                    break;
            }
        }
        // 잠금 밖에서 놓는다 — 마지막 참조면 풀로 돌려주며 같은 잠금을 잡을 수 있다(비재귀 뮤텍스).
        for ( TaskNode* pRejected : listRejected )
        {
            pRejected->release();
        }
        if ( pHeld != nullptr )
            pHeld->release();
        return name;
#else
        return string();
#endif
    }

    StageNode* TaskNodePool::allocateStage()
    {
        StageNode* pStage{ nullptr };
        {
            ScopedLock<mutex> lock{ _stageMutex };
            if ( _listStageFree.empty() == false )
            {
                pStage = _listStageFree.back();
                _listStageFree.pop_back();
            }
        }
        if ( pStage == nullptr )
        {
            unique_ptr<StageNode> uniqueStage = make_unique<StageNode>();
            pStage                            = uniqueStage.get();
            ScopedLock<mutex> lock{ _stageMutex };
            _listStageAll.push_back( std::move( uniqueStage ) );
        }
        pStage->_join.reset();
        pStage->_refCount.store( 1, std::memory_order_relaxed );
        pStage->_pPool = this;
        return pStage;
    }

    void TaskNodePool::deallocateStage( StageNode* pStage )
    {
        if ( pStage == nullptr )
            return;
        if ( _bDetached.load( std::memory_order_acquire ) )
        {
            returnDetachedReference();
            return;
        }
        ScopedLock<mutex> lock{ _stageMutex };
        _listStageFree.push_back( pStage );
    }

    void TaskNodePool::resetAllStages()
    {
        // 남은 태스크가 있는 스테이지만 그 참조(스테이지가 스스로 잡은 참조 하나)을 놓는다. 핸들이 쥔 참조는 그대로 둔다 —
        // 0 으로 덮으면 그 핸들이 나중에 놓을 때 이미 재사용된 스테이지의 수를 내린다.
        vector<StageNode*> listPending;
        {
            ScopedLock<mutex> lock{ _stageMutex };
            for ( const unique_ptr<StageNode>& uniqueStage : _listStageAll )
            {
                if ( uniqueStage != nullptr && uniqueStage->_join.getPending() > 0 )
                    listPending.push_back( uniqueStage.get() );
            }
        }
        for ( StageNode* pStage : listPending )
        {
            pStage->_join.reset();
            pStage->release();
        }
    }

    bool TaskNodePool::detachOwner()
    {
        _pOwner               = nullptr;
        uint32 referenceCount = 0;
        {
            ScopedLock<mutex> lock{ _slabMutex };
            for ( const MemoryBlock& slab : _listSlab )
            {
                const TaskNode* pSlab = slab.getAs<TaskNode>();
                for ( uint32 index = 0; index < kSlabSize; ++index )
                {
                    if ( pSlab[index]._refCount.load( std::memory_order_acquire ) > 0 )
                        ++referenceCount;
                }
            }
        }
        {
            ScopedLock<mutex> lock{ _stageMutex };
            for ( const unique_ptr<StageNode>& uniqueStage : _listStageAll )
            {
                if ( uniqueStage->_refCount.load( std::memory_order_acquire ) > 0 )
                    ++referenceCount;
            }
        }
        if ( referenceCount == 0 )
            return true;
        _detachedReferenceCount.store( referenceCount, std::memory_order_relaxed );
        _bDetached.store( true, std::memory_order_release );
        return false;
    }

    void TaskNodePool::returnDetachedReference()
    {
        if ( _detachedReferenceCount.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
            sw_delete( this );
    }

    ParallelGroup* TaskNodePool::allocateGroup()
    {
        ParallelGroup* pGroup = _groupPool.acquire();
        if ( pGroup != nullptr )
            return pGroup;
        return make_unique<ParallelGroup>().release(); // 풀이 비면 힙에서 만든다. `deallocateGroup` 이 주소로 갈라서 푼다
    }

    void TaskNodePool::deallocateGroup( ParallelGroup* pGroup )
    {
        if ( pGroup == nullptr )
            return;
        if ( _groupPool.owns( pGroup ) )
            _groupPool.release( pGroup );
        else
            sw_delete( pGroup );
    }
} // namespace sw
