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
        _count = 0;
        unlock();
    }

    void TaskNode::release()
    {
        if ( _refCount.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
        {
            if ( _pOwner != nullptr )
                _pOwner->deallocateNode( this );
        }
    }

    void StageNode::release()
    {
        if ( _refCount.fetch_sub( 1, std::memory_order_acq_rel ) != 1 )
            return;
        if ( _pPool != nullptr )
            _pPool->deallocateStage( this );
    }
} // namespace sw
