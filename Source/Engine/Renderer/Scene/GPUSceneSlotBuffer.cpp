#include "pch.h"

#include "Engine/Renderer/Scene/GPUSceneSlotBuffer.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    GPUInstance GPUSceneSlotBuffer::makeHole()
    {
        GPUInstance hole{};
        hole._meshBatchIndex = kHoleBatchIndex;
        return hole;
    }

    void GPUSceneSlotBuffer::apply( const vector<GPUInstance>& listOrdered, const vector<uint32>* pListOrderToSlot, uint32 slotCount, bool bOrderChanged,
                                    const vector<GPUInstanceDirtyRange>& listDirtyRange, bool bAllDirty )
    {
        const uint32 oldCount = static_cast<uint32>( _listSlot.size() );
        if ( slotCount != oldCount )
        {
            _listSlot.resize( slotCount, makeHole() );
            _listDirtyFlag.resize( slotCount, 0u );
            if ( slotCount < oldCount )
            {
                // 버린 꼬리의 바뀐 칸 기록을 걷어 낸다.
                size_t keptCount = 0;
                for ( const uint32 slot : _listDirtySlot )
                {
                    if ( slot < slotCount )
                        _listDirtySlot[keptCount++] = slot;
                }
                _listDirtySlot.resize( keptCount );
            }
            // 새 칸의 GPU 내용은 모른다 — 구멍을 올리게 바뀐 칸으로 센다.
            for ( uint32 slot = oldCount; slot < slotCount; ++slot )
            {
                _listDirtyFlag[slot] = 1u;
                _listDirtySlot.push_back( slot );
            }
            bOrderChanged = true;
        }

        const uint32 orderedCount = static_cast<uint32>( listOrdered.size() );
        auto         toSlot       = [pListOrderToSlot]( uint32 orderIndex ) -> uint32
        {
            if ( pListOrderToSlot == nullptr )
                return orderIndex;
            return orderIndex < pListOrderToSlot->size() ? ( *pListOrderToSlot )[orderIndex] : kHoleBatchIndex;
        };

        const bool bScanAll = bOrderChanged || bAllDirty || listDirtyRange.empty();
        if ( bScanAll == false )
        {
            // 차례 목록이 그대로다 — 바뀐 차례만 그 칸으로 옮긴다. 구멍은 그대로다.
            for ( const GPUInstanceDirtyRange& range : listDirtyRange )
            {
                const uint32 end = MathUtil::min( range._start + range._count, orderedCount );
                for ( uint32 orderIndex = range._start; orderIndex < end; ++orderIndex )
                {
                    const uint32 slot = toSlot( orderIndex );
                    if ( slot < slotCount )
                        writeSlot( slot, listOrdered[orderIndex] );
                }
            }
            return;
        }

        // 모든 차례를 보고(비교만 — 내용이 같은 칸은 올리지 않는다), 차례 목록에 없는 칸은 구멍으로 만든다. 그리는 순서만 바뀐 프레임은 바뀐 칸이 없다.
        const bool bTrackSeen = pListOrderToSlot != nullptr;
        if ( bTrackSeen )
            _listSeenScratch.assign( slotCount, 0u );
        for ( uint32 orderIndex = 0; orderIndex < orderedCount; ++orderIndex )
        {
            const uint32 slot = toSlot( orderIndex );
            if ( slot >= slotCount )
                continue;
            if ( bTrackSeen )
                _listSeenScratch[slot] = 1u;
            writeSlot( slot, listOrdered[orderIndex] );
        }
        const GPUInstance hole = makeHole();
        for ( uint32 slot = bTrackSeen ? 0u : orderedCount; slot < slotCount; ++slot )
        {
            if ( bTrackSeen && _listSeenScratch[slot] != 0u )
                continue;
            writeSlot( slot, hole );
        }
    }

    void GPUSceneSlotBuffer::writeSlot( uint32 slot, const GPUInstance& instance )
    {
        // 비트로 견준다 — 엡실론 비교는 조금씩 움직이는 물체를 영원히 "안 바뀜" 으로 본다(`DrawCandidate::operator==` 와 같은 까닭). 패딩 칸도 0 으로 맞춰 둔 값이다.
        GPUInstance& stored = _listSlot[slot];
        if ( Memory::compare( &stored, &instance, sizeof( GPUInstance ) ) == 0 )
            return;
        stored = instance;
        if ( _listDirtyFlag[slot] != 0u )
            return;
        _listDirtyFlag[slot] = 1u;
        _listDirtySlot.push_back( slot );
    }

    bool GPUSceneSlotBuffer::buildRuns( uint32 maxRunCount, vector<GPUInstanceDirtyRange>& outListRun )
    {
        outListRun.clear();
        if ( _bFullUploadPending != SW_FALSE )
            return false;
        VectorUtil::sort( _listDirtySlot );
        for ( const uint32 slot : _listDirtySlot )
        {
            const bool bContinuesRun = outListRun.empty() == false && outListRun.back()._start + outListRun.back()._count == slot;
            if ( bContinuesRun )
            {
                ++outListRun.back()._count;
                continue;
            }
            if ( outListRun.size() >= maxRunCount )
            {
                outListRun.clear();
                return false;
            }
            outListRun.push_back( GPUInstanceDirtyRange{ slot, 1u } );
        }
        return true;
    }

    void GPUSceneSlotBuffer::clearPending()
    {
        for ( const uint32 slot : _listDirtySlot )
        {
            if ( slot < _listDirtyFlag.size() )
                _listDirtyFlag[slot] = 0u;
        }
        _listDirtySlot.clear();
        _bFullUploadPending = SW_FALSE;
    }

    void GPUSceneSlotBuffer::reset()
    {
        _listSlot.clear();
        _listDirtySlot.clear();
        _listDirtyFlag.clear();
        _listSeenScratch.clear();
        _bFullUploadPending = SW_TRUE;
    }
} // namespace sw
