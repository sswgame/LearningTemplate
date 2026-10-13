#include "pch.h"

#include "Core/Task/TaskNode.h"

#include "Core/Task/TaskManager.h"
#include "Core/Task/TaskNodePool.h"

namespace sw
{
    void InlineSuccessorList::clearAndRelease()
    {
        lock();
        const uint32 inlineCount = _count < kInlineCapacity ? _count : kInlineCapacity;
        for ( uint32 index = 0; index < inlineCount; ++index )
        {
            if ( _arrInlineNode[index] != nullptr )
            {
                _arrInlineNode[index]->release();
                _arrInlineNode[index] = nullptr;
            }
        }
        if ( _pOverflow != nullptr )
        {
            for ( TaskNode* pNode : *_pOverflow )
            {
                if ( pNode != nullptr )
                    pNode->release();
            }
            _pOverflow.reset();
        }
        _count   = 0;
        _bClosed = false;
        unlock();
    }

    void TaskNode::resolveOneDependency()
    {
        TaskManager* const pOwner = _pPool->getOwner();
        if ( pOwner != nullptr )
            pOwner->resolveDependency( this, true );
        else
            _unresolvedDependencies.fetch_sub( 1, std::memory_order_acq_rel );
    }

    void TaskNode::release()
    {
        if ( _refCount.fetch_sub( 1, std::memory_order_acq_rel ) != 1 )
            return;
        // 매니저가 살아 있으면 완료 처리(만들기만 한 태스크)를 거쳐 돌려준다. 이미 없으면 풀로 바로 돌려준다 — 풀은 마지막 참조까지 산다.
        TaskManager* const pOwner = _pPool->getOwner();
        if ( pOwner != nullptr )
            pOwner->deallocateNode( this );
        else
            _pPool->deallocate( this );
    }

    void StageNode::release()
    {
        if ( _refCount.fetch_sub( 1, std::memory_order_acq_rel ) != 1 )
            return;
        if ( _pPool != nullptr )
            _pPool->deallocateStage( this );
    }
} // namespace sw
