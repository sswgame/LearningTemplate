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

    void GPUSceneSlotBuffer::apply( const vector<GPUInstance>& listSlotInstance, const vector<GPUInstanceDirtyRange>& listDirtyRange, bool bAllDirty )
    {
        const uint32 slotCount = static_cast<uint32>( listSlotInstance.size() );
        const uint32 oldCount  = static_cast<uint32>( _listSlot.size() );
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
            // 새 칸의 GPU 내용은 모른다 — 바뀐 칸으로 세고 내용을 받는다(구간에 없어도).
            for ( uint32 slot = oldCount; slot < slotCount; ++slot )
            {
                _listSlot[slot]      = listSlotInstance[slot];
                _listDirtyFlag[slot] = 1u;
                _listDirtySlot.push_back( slot );
            }
        }

        // 구간이 비고 "전체" 가 아니면 바뀐 칸이 없다(빌더는 바뀐 칸을 모두 구간으로 싣고, 넘치면 "전체" 로 보낸다 — 카메라만 움직인 빌드).
        if ( bAllDirty )
        {
            // 비교만 한다 — 내용이 같은 칸은 올리지 않는다(배치를 다시 나눠 배치 번호가 그대로인 칸).
            for ( uint32 slot = 0; slot < slotCount; ++slot )
                writeSlot( slot, listSlotInstance[slot] );
            return;
        }
        for ( const GPUInstanceDirtyRange& range : listDirtyRange )
        {
            const uint32 end = MathUtil::min( range._start + range._count, slotCount );
            for ( uint32 slot = range._start; slot < end; ++slot )
                writeSlot( slot, listSlotInstance[slot] );
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
        _bFullUploadPending = SW_TRUE;
    }
} // namespace sw
