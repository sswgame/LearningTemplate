#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Log/ScopedDeferredLog.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanMemoryAllocator.h"

#include <vulkan/vulkan.h>

// VulkanMemoryAllocator.cpp 와 같은 VMA 설정이다(구현은 그 파일이 짓는다 — 여기는 선언만 읽는다).
#define VMA_STATIC_VULKAN_FUNCTIONS  1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_VULKAN_VERSION           1002000
#define VMA_EXTERNAL_MEMORY_WIN32    0
#include <vk_mem_alloc.h>

namespace sw
{
    void VulkanPlacementLog::onFrameBeginInternal( uint64 frameSerial )
    {
        ScopedLock<mutex> lock{ _mutex };
        _currentFrame = frameSerial;
    }

    void VulkanPlacementLog::recordPlacementInternal( VmaAllocator allocator, const VulkanAllocation& allocation, const utf8* pName )
    {
        VmaAllocationInfo info{};
        vmaGetAllocationInfo( allocator, allocation._allocation, &info );
        Placement added{};
        added._memory = info.deviceMemory;
        added._offset = info.offset;
        added._size   = info.size;
        added._pName  = pName;
        const ScopedDeferredLog deferredLog;
        ScopedLock<mutex>       lock{ _mutex };
        added._createdFrame = _currentFrame;
        for ( const Placement& live : _listLive )
        {
            if ( live.overlaps( added ) )
                SW_LOG_ERROR_ID( "Vulkan.MemoryOverlap", "The allocation '%#' ([%#, +%#)) overlaps the live allocation '%#' ([%#, +%#), created in frame %#)", pName, added._offset, added._size,
                                 live._pName, live._offset, live._size, live._createdFrame );
        }
        _listLive.push_back( added );
    }

    void VulkanPlacementLog::recordReleaseInternal( VmaAllocator allocator, const VulkanAllocation& allocation )
    {
        if ( allocation.isValid() == false )
            return;
        VmaAllocationInfo info{};
        vmaGetAllocationInfo( allocator, allocation._allocation, &info );
        ScopedLock<mutex> lock{ _mutex };
        for ( size_t index = 0; index < _listLive.size(); ++index )
        {
            if ( _listLive[index]._memory != info.deviceMemory || _listLive[index]._offset != info.offset )
                continue;
            Placement freed   = _listLive[index];
            freed._freedFrame = _currentFrame;
            VectorUtil::removeAtSwap( _listLive, index );
            if ( _listFreed.size() < kFreedPlacementCount )
                _listFreed.push_back( freed );
            else
                _listFreed[_freedCursor] = freed;
            _freedCursor = ( _freedCursor + 1u ) % kFreedPlacementCount;
            return;
        }
    }

    void VulkanPlacementLog::reportInternal( VmaAllocator allocator, const VulkanAllocation& allocation ) const
    {
        if ( allocation.isValid() == false )
            return;
        VmaAllocationInfo info{};
        vmaGetAllocationInfo( allocator, allocation._allocation, &info );
        Placement own{};
        own._memory = info.deviceMemory;
        own._offset = info.offset;
        own._size   = info.size;
        const ScopedDeferredLog deferredLog;
        ScopedLock<mutex>       lock{ _mutex };
        for ( const vector<Placement>* pList : { &_listLive, &_listFreed } )
        {
            for ( const Placement& other : *pList )
            {
                if ( other.overlaps( own ) == false )
                    continue;
                SW_LOG_INFO_ID( "Vulkan.MemoryPlacement", "The memory [%#, +%#) is or was used by '%#' at byte %# (+%# bytes), created in frame %#, freed in frame %# (0 = live), now frame %#", own._offset,
                                own._size, other._pName, static_cast<int64>( other._offset ) - static_cast<int64>( own._offset ), other._size, other._createdFrame, other._freedFrame, _currentFrame );
            }
        }
    }
} // namespace sw
