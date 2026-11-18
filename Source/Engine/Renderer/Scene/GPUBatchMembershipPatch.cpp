#include "pch.h"

#include "Engine/Renderer/Scene/GPUBatchMembershipPatch.h"

#include "Core/Algorithm/VectorUtil.h"

namespace sw
{
    void GPUBatchMembershipPatch::reset( uint32 slotCount )
    {
        _listFlag.assign( slotCount, 0u );
        _listEnter.clear();
    }

    void GPUBatchMembershipPatch::markLeave( uint32 slot )
    {
        SW_ASSERT( slot < _listFlag.size() );
        _listFlag[slot] = static_cast<uint8>( _listFlag[slot] | kLeave );
    }

    void GPUBatchMembershipPatch::addEnter( uint32 batchIndex, uint32 slot )
    {
        SW_ASSERT( slot < _listFlag.size() );
        _listFlag[slot] = static_cast<uint8>( _listFlag[slot] | kEnter );
        _listEnter.push_back( GPUBatchMemberEntry{ batchIndex, slot } );
    }

    void GPUBatchMembershipPatch::rewriteOrder( GPUMeshBatch* pBatch, uint32 batchCount, uint32 freshBatchBegin, const vector<uint32>& listOldOrder,
                                                vector<uint32>& outListOrder, vector<uint32>& ioListSlotToOrder )
    {
        // 배치 번호 순으로 붙인다 — 같은 배치 안은 칸 순(결정적이다).
        VectorUtil::sort( _listEnter, []( const GPUBatchMemberEntry& entryA, const GPUBatchMemberEntry& entryB )
        { return entryA._batchIndex != entryB._batchIndex ? entryA._batchIndex < entryB._batchIndex : entryA._slot < entryB._slot; } );
        outListOrder.clear();
        outListOrder.reserve( listOldOrder.size() + _listEnter.size() );
        uint32 visibleBase  = 0;
        uint32 headCapacity = 0;
        size_t enterCursor  = 0;
        auto   appendMember = [&outListOrder, &ioListSlotToOrder]( uint32 slot )
        {
            ioListSlotToOrder[slot] = static_cast<uint32>( outListOrder.size() );
            outListOrder.push_back( slot );
        };
        for ( uint32 batchIndex = 0; batchIndex < batchCount; ++batchIndex )
        {
            GPUMeshBatch& batch    = pBatch[batchIndex];
            const bool    bFresh   = batchIndex >= freshBatchBegin; // 이번에 끝에 붙인 배치 — 지난 구성원이 없다
            const uint32  oldBase  = batch._instanceBase;
            const uint32  oldCount = bFresh ? 0u : batch._instanceCount;
            batch._instanceBase    = static_cast<uint32>( outListOrder.size() );
            for ( uint32 order = oldBase; order < oldBase + oldCount && order < listOldOrder.size(); ++order )
            {
                const uint32 slot = listOldOrder[order];
                if ( slot < _listFlag.size() && ( _listFlag[slot] & kLeave ) == 0u )
                    appendMember( slot );
            }
            for ( ; enterCursor < _listEnter.size() && _listEnter[enterCursor]._batchIndex == batchIndex; ++enterCursor )
                appendMember( _listEnter[enterCursor]._slot );
            batch._instanceCount = static_cast<uint32>( outListOrder.size() ) - batch._instanceBase;
            const bool bLODLevel = bFresh == false && oldCount == 0u && batch._lodLevelCount == 0u;
            if ( bLODLevel == false )
                headCapacity = batch._instanceCount;
            batch._visibleBase     = visibleBase;
            batch._visibleCapacity = bLODLevel ? headCapacity : batch._instanceCount;
            visibleBase += batch._visibleCapacity;
        }
        SW_ASSERT( enterCursor == _listEnter.size() ); // 들어오는 칸의 배치 번호가 범위 안이어야 모두 붙는다
    }
} // namespace sw
