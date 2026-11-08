#include "pch.h"

#include "Engine/Renderer/Frame/Resource/TransientLeasePool.h"

namespace sw
{
    TransientLeasePool::Lease TransientLeasePool::lease( uint64 descKey )
    {
        // 같은 서술로 쉬는 칸이 먼저, 그다음 놓아 비운 칸(새로 만든다), 없으면 끝에 더한다.
        uint32 emptySlot = static_cast<uint32>( _listSlot.size() );
        for ( uint32 slotIndex = 0; slotIndex < static_cast<uint32>( _listSlot.size() ); ++slotIndex )
        {
            Slot& slot = _listSlot[slotIndex];
            if ( slot._bLive != 0 && slot._bLeased == 0 && slot._descKey == descKey )
            {
                slot._bLeased        = 1;
                slot._bUsedThisFrame = 1;
                slot._idleFrames     = 0;
                return Lease{ slotIndex, 0 };
            }
            if ( slot._bLive == 0 && emptySlot == _listSlot.size() )
                emptySlot = slotIndex;
        }
        if ( emptySlot == _listSlot.size() )
            _listSlot.emplace_back();
        Slot& slot           = _listSlot[emptySlot];
        slot._descKey        = descKey;
        slot._idleFrames     = 0;
        slot._bLeased        = 1;
        slot._bLive          = 1;
        slot._bUsedThisFrame = 1;
        return Lease{ emptySlot, 1 };
    }

    void TransientLeasePool::returnLeases()
    {
        for ( Slot& slot : _listSlot )
        {
            slot._bLeased = 0;
        }
    }

    void TransientLeasePool::advanceFrame( vector<uint32>& outListReleased )
    {
        returnLeases();
        for ( uint32 slotIndex = 0; slotIndex < static_cast<uint32>( _listSlot.size() ); ++slotIndex )
        {
            Slot& slot = _listSlot[slotIndex];
            if ( slot._bLive == 0 )
                continue;
            slot._idleFrames     = ( slot._bUsedThisFrame != 0 ) ? 0u : slot._idleFrames + 1u;
            slot._bUsedThisFrame = 0;
            if ( slot._idleFrames < kIdleFramesBeforeRelease )
                continue;
            slot._bLive   = 0;
            slot._descKey = 0;
            outListReleased.push_back( slotIndex );
        }
    }

    uint32 TransientLeasePool::getLiveSlotCount() const
    {
        uint32 count = 0;
        for ( const Slot& slot : _listSlot )
        {
            count += ( slot._bLive != 0 ) ? 1u : 0u;
        }
        return count;
    }

    uint32 TransientLeasePool::getLeasedSlotCount() const
    {
        uint32 count = 0;
        for ( const Slot& slot : _listSlot )
        {
            count += ( slot._bLeased != 0 ) ? 1u : 0u;
        }
        return count;
    }
} // namespace sw
