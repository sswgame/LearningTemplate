#include "pch.h"

#include "Engine/Renderer/Scene/GPUSceneDirtySlots.h"

#include "Core/Algorithm/VectorUtil.h"

namespace sw
{
    void GPUSceneDirtySlots::resize( uint32 slotCount )
    {
        const uint32 oldCount = static_cast<uint32>( _listFlag.size() );
        _listFlag.resize( slotCount, 0u );
        if ( slotCount >= oldCount )
            return;
        size_t keptCount = 0;
        for ( const uint32 slot : _listSlot )
        {
            if ( slot < slotCount )
                _listSlot[keptCount++] = slot;
        }
        _listSlot.resize( keptCount );
    }

    void GPUSceneDirtySlots::mark( uint32 slot )
    {
        if ( _listFlag[slot] != 0u )
            return;
        _listFlag[slot] = 1u;
        _listSlot.push_back( slot );
    }

    bool GPUSceneDirtySlots::collectRuns( uint32 maxRunCount, vector<GPUInstanceDirtyRange>& outListRun )
    {
        outListRun.clear();
        if ( _bFullUploadPending != SW_FALSE )
            return false;
        VectorUtil::sort( _listSlot );
        for ( const uint32 slot : _listSlot )
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

    void GPUSceneDirtySlots::clear()
    {
        for ( const uint32 slot : _listSlot )
        {
            _listFlag[slot] = 0u;
        }
        _listSlot.clear();
        _bFullUploadPending = SW_FALSE;
    }

    void GPUSceneDirtySlots::reset()
    {
        _listSlot.clear();
        _listFlag.clear();
        _bFullUploadPending = SW_TRUE;
    }
} // namespace sw
