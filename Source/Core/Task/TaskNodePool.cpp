#include "pch.h"

#include "Core/Task/TaskNodePool.h"

namespace sw
{
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
        pMem->_state.store( TaskState::Pending, std::memory_order_relaxed );
        pMem->_bCancelled.store( false, std::memory_order_relaxed );
        pMem->_refCount.store( 1, std::memory_order_relaxed );
        pMem->_pOwner  = nullptr;
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
        pNode->_pOwner = nullptr;

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
            pStage->_listTask.reserve( 16 );
            std::scoped_lock<mutex> lock{ _stageMutex };
            _listStageAll.push_back( std::move( uniqueStage ) );
        }
        pStage->_name.clear();
        pStage->_listTask.clear();
        pStage->_join.reset();
        pStage->_refCount.store( 1, std::memory_order_relaxed );
        pStage->_pPool = this;
        return pStage;
    }

    void TaskNodePool::deallocateStage( StageNode* pStage )
    {
        if ( pStage == nullptr )
            return;
        for ( TaskNode* pTask : pStage->_listTask )
        {
            if ( pTask != nullptr )
                pTask->release();
        }
        pStage->_listTask.clear();
        pStage->_name.clear();
        std::scoped_lock<mutex> lock{ _stageMutex };
        _listStageFree.push_back( pStage );
    }

    void TaskNodePool::resetAllStages()
    {
        vector<StageNode*> listLive;
        {
            std::scoped_lock<mutex> lock{ _stageMutex };
            for ( const unique_ptr<StageNode>& uniqueStage : _listStageAll )
            {
                if ( uniqueStage != nullptr && uniqueStage->_refCount.load( std::memory_order_relaxed ) > 0 )
                    listLive.push_back( uniqueStage.get() );
            }
        }
        for ( StageNode* pStage : listLive )
        {
            pStage->_refCount.store( 0, std::memory_order_relaxed );
            pStage->_join.reset();
            deallocateStage( pStage );
        }
    }

    ParallelGroup* TaskNodePool::allocateGroup()
    {
        ParallelGroup* pGroup = _groupPool.acquire();
        if ( pGroup != nullptr )
        {
            pGroup->_pPool = &_groupPool;
            return pGroup;
        }
        pGroup         = sw_new ParallelGroup();
        pGroup->_bHeap = true;
        return pGroup;
    }

    void TaskNodePool::deallocateGroup( ParallelGroup* pGroup )
    {
        if ( pGroup == nullptr )
            return;
        if ( pGroup->_bHeap )
        {
            sw_delete( pGroup );
            return;
        }
        ParallelGroupPool* pPool = pGroup->_pPool;
        if ( pPool != nullptr )
            pPool->release( pGroup );
    }
} // namespace sw
