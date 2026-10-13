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
    {
    }

    TaskNodePool::~TaskNodePool()
    {
        std::scoped_lock<mutex> lock{ _slabMutex };
        for ( TaskNode* pSlab : _listSlab )
        {
            if ( pSlab == nullptr )
                continue;
            for ( uint32 index = 0; index < kSlabSize; ++index )
            {
                pSlab[index].~TaskNode();
            }
            Memory::free( pSlab );
        }
        _listSlab.clear();
    }

    TaskNode* TaskNodePool::allocate()
    {
        TaskNode* pMem{ nullptr };
        if ( _freeQueue.dequeue( pMem ) == false || pMem == nullptr )
        {
            {
                std::scoped_lock<mutex> lock{ _slabMutex };
                if ( _listOverflowFree.empty() == false )
                {
                    pMem = _listOverflowFree.back();
                    _listOverflowFree.pop_back();
                }
            }
            if ( pMem == nullptr )
            {
                TaskNode* pSlab = static_cast<TaskNode*>( Memory::allocate( sizeof( TaskNode ) * kSlabSize ) );
                for ( uint32 index = 0; index < kSlabSize; ++index )
                {
                    sw_placement_new( &pSlab[index] ) TaskNode();
                    pSlab[index]._pPool = this;
                    pSlab[index]._refCount.store( 0, std::memory_order_relaxed ); // 자유 노드는 0 이다(`detachOwner` 가 참조 남은 노드를 이것으로 가린다)
                }
                {
                    std::scoped_lock<mutex> lock{ _slabMutex };
                    _listSlab.push_back( pSlab );
                }
                for ( uint32 index = 1; index < kSlabSize; ++index )
                {
                    if ( _freeQueue.enqueue( &pSlab[index] ) == false )
                    {
                        std::scoped_lock<mutex> lock{ _slabMutex };
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
            std::scoped_lock<mutex> lock{ _slabMutex };
            _listOverflowFree.push_back( pNode );
        }
    }

    StageNode* TaskNodePool::allocateStage()
    {
        StageNode* pStage{ nullptr };
        {
            std::scoped_lock<mutex> lock{ _stageMutex };
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
            std::scoped_lock<mutex> lock{ _stageMutex };
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
        std::scoped_lock<mutex> lock{ _stageMutex };
        _listStageFree.push_back( pStage );
    }

    void TaskNodePool::resetAllStages()
    {
        // 남은 태스크가 있는 스테이지만 그 몫(스테이지가 스스로 잡은 참조 하나)을 놓는다. 핸들이 쥔 참조는 그대로 둔다 —
        // 0 으로 덮으면 그 핸들이 나중에 놓을 때 이미 재사용된 스테이지의 수를 내린다.
        vector<StageNode*> listPending;
        {
            std::scoped_lock<mutex> lock{ _stageMutex };
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
            std::scoped_lock<mutex> lock{ _slabMutex };
            for ( TaskNode* pSlab : _listSlab )
            {
                for ( uint32 index = 0; index < kSlabSize; ++index )
                {
                    if ( pSlab[index]._refCount.load( std::memory_order_acquire ) > 0 )
                        ++referenceCount;
                }
            }
        }
        {
            std::scoped_lock<mutex> lock{ _stageMutex };
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
        return sw_new ParallelGroup();
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
