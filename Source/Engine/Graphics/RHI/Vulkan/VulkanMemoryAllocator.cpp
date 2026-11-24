#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanMemoryAllocator.h"

#include "Engine/Graphics/RHI/Backend/RHIAllocationLeakUtil.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIRequiredVersion.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanResultCheck.h"

#include <vulkan/vulkan.h>

// VMA 의 구현은 이 파일 하나가 짓는다(헤더는 SYSTEM include 라 그 안의 경고는 나지 않는다).
// 함수는 로더에 링크한 것을 그대로 쓴다(엔진의 다른 Vulkan 호출과 같다). 판은 장치가 요구하는 1.2 로 묶는다 — 더 높은 판은 Android libvulkan 이 내보내지 않는 함수를 링크한다.
// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html
#define VMA_STATIC_VULKAN_FUNCTIONS  1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_VULKAN_VERSION           1002000
#define VMA_EXTERNAL_MEMORY_WIN32    0 // 다른 API 와 메모리를 나누지 않는다
#define VMA_ASSERT( expr )           SW_ASSERT( expr )
#define VMA_ASSERT_LEAK( expr )      static_cast<void>( 0 ) // 남은 할당은 `shutdown` 이 이름과 함께 오류로 남긴다
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace sw
{
    bool VulkanMemoryAllocator::initialize( VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, bool bMemoryBudget )
    {
        static_assert( constant::render::kVulkanRequiredMajor == 1u && constant::render::kVulkanRequiredMinor == 2u, "VMA_VULKAN_VERSION must follow the required Vulkan version" );
        VmaAllocatorCreateInfo createInfo{};
        createInfo.flags            = bMemoryBudget ? static_cast<VmaAllocatorCreateFlags>( VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT ) : VmaAllocatorCreateFlags{ 0 };
        createInfo.physicalDevice   = physicalDevice;
        createInfo.device           = device;
        createInfo.instance         = instance;
        createInfo.vulkanApiVersion = VulkanRHIRequiredVersion::kRequiredAPIVersion;
        if ( isVkSuccess( vmaCreateAllocator( &createInfo, &_allocator ), "vmaCreateAllocator" ) == false )
        {
            _allocator = nullptr;
            return false;
        }
        _device        = device;
        _bMemoryBudget = bMemoryBudget ? SW_TRUE : SW_FALSE;
        return true;
    }

    void VulkanMemoryAllocator::shutdown()
    {
        if ( _allocator == nullptr )
            return;
        VmaTotalStatistics statistics{};
        vmaCalculateStatistics( _allocator, &statistics );
        if ( statistics.total.statistics.allocationCount != 0 )
        {
            utf8* pStats{ nullptr };
            vmaBuildStatsString( _allocator, &pStats, VK_TRUE );
            RHIAllocationLeakUtil::report( "Vulkan", statistics.total.statistics.allocationCount, pStats );
            vmaFreeStatsString( _allocator, pStats );
        }
        vmaDestroyAllocator( _allocator );
        _allocator = nullptr;
        _device    = nullptr;
    }

    void VulkanMemoryAllocator::onFrameBegin( uint64 frameSerial )
    {
        if ( _allocator != nullptr )
            vmaSetCurrentFrameIndex( _allocator, static_cast<uint32>( frameSerial ) );
    }

    bool VulkanMemoryAllocator::createBuffer( const VkBufferCreateInfo& info, VulkanBufferMemory placement, const utf8* pName, VkBuffer& outBuffer, VulkanAllocation& outAllocation )
    {
        outBuffer     = nullptr;
        outAllocation = VulkanAllocation{};
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        switch ( placement )
        {
            case VulkanBufferMemory::HostWrite:
                allocationInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
                break;
            case VulkanBufferMemory::HostRead:
                allocationInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
                break;
            case VulkanBufferMemory::HostCoherent:
                allocationInfo.flags         = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
                allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                break;
            case VulkanBufferMemory::DeviceLocal:
                allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                break;
        }
        VmaAllocationInfo allocated{};
        if ( isVkSuccess( vmaCreateBuffer( _allocator, &info, &allocationInfo, &outBuffer, &outAllocation._allocation, &allocated ), "vmaCreateBuffer" ) == false )
        {
            outBuffer     = nullptr;
            outAllocation = VulkanAllocation{};
            return false;
        }
        outAllocation._pMapped   = static_cast<uint8*>( allocated.pMappedData );
        outAllocation._sizeBytes = allocated.size;
        vmaSetAllocationName( _allocator, outAllocation._allocation, pName );
        if ( placement != VulkanBufferMemory::DeviceLocal && outAllocation._pMapped == nullptr )
        {
            SW_LOG_ERROR_ID( "Vulkan.BufferNotMapped", "The host visible buffer '%#' (%# bytes) was not mapped", pName, static_cast<uint64>( info.size ) );
            destroyBuffer( outBuffer, outAllocation );
            outBuffer     = nullptr;
            outAllocation = VulkanAllocation{};
            return false;
        }
        return true;
    }

    bool VulkanMemoryAllocator::createImage( const VkImageCreateInfo& info, const utf8* pName, VkImage& outImage, VulkanAllocation& outAllocation )
    {
        outImage      = nullptr;
        outAllocation = VulkanAllocation{};
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VmaAllocationInfo allocated{};
        if ( isVkSuccess( vmaCreateImage( _allocator, &info, &allocationInfo, &outImage, &outAllocation._allocation, &allocated ), "vmaCreateImage" ) == false )
        {
            outImage      = nullptr;
            outAllocation = VulkanAllocation{};
            return false;
        }
        outAllocation._sizeBytes = allocated.size;
        vmaSetAllocationName( _allocator, outAllocation._allocation, pName );
        return true;
    }

    void VulkanMemoryAllocator::destroyBuffer( VkBuffer buffer, const VulkanAllocation& allocation )
    {
        if ( _allocator != nullptr )
            vmaDestroyBuffer( _allocator, buffer, allocation._allocation );
    }

    void VulkanMemoryAllocator::destroyImage( VkImage image, const VulkanAllocation& allocation )
    {
        if ( _allocator != nullptr )
            vmaDestroyImage( _allocator, image, allocation._allocation );
    }

    bool VulkanMemoryAllocator::allocateHeap( const VkMemoryRequirements& requirements, const utf8* pName, VulkanAllocation& outAllocation )
    {
        outAllocation = VulkanAllocation{};
        VmaAllocationCreateInfo allocationInfo{};
        // 이미지 하나에 묶이지 않는 전용 블록이다 — CAN_ALIAS 가 `VkMemoryDedicatedAllocateInfo` 를 붙이지 않게 한다.
        // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html
        allocationInfo.flags         = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
        allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        VmaAllocationInfo allocated{};
        if ( isVkSuccess( vmaAllocateMemory( _allocator, &requirements, &allocationInfo, &outAllocation._allocation, &allocated ), "vmaAllocateMemory (heap)" ) == false )
        {
            outAllocation = VulkanAllocation{};
            return false;
        }
        outAllocation._sizeBytes = allocated.size;
        vmaSetAllocationName( _allocator, outAllocation._allocation, pName );
        return true;
    }

    void VulkanMemoryAllocator::freeHeap( const VulkanAllocation& allocation )
    {
        if ( _allocator != nullptr && allocation.isValid() )
            vmaFreeMemory( _allocator, allocation._allocation );
    }

    bool VulkanMemoryAllocator::createAliasingImage( const VulkanAllocation& heap, uint64 offsetBytes, const VkImageCreateInfo& info, VkImage& outImage, uint64& outBytes )
    {
        outImage = nullptr;
        outBytes = 0;
        // `vmaCreateAliasingImage2` 는 만들고 곧바로 묶는다 — 힙이 받지 않는 이미지를 검증 레이어 없이도 거절하려면 묶기 전에 요구를 봐야 해서 같은 일을 두 호출로 한다.
        // 종류 · 정렬 · 크기는 vkBindImageMemory 의 유효 사용(VUID-VkBindImageMemoryInfo-memory-01612 · memoryOffset-01613 · memory-01614)이다.
        VkImage image{ VK_NULL_HANDLE };
        if ( isVkSuccess( vkCreateImage( _device, &info, nullptr, &image ), "vkCreateImage (aliasing)" ) == false )
            return false;
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements( _device, image, &requirements );
        VmaAllocationInfo heapInfo{};
        vmaGetAllocationInfo( _allocator, heap._allocation, &heapInfo );
        const bool bTypeAccepted = ( requirements.memoryTypeBits & ( 1u << heapInfo.memoryType ) ) != 0;
        const bool bAligned      = requirements.alignment == 0 || ( heapInfo.offset + offsetBytes ) % requirements.alignment == 0;
        const bool bFits         = offsetBytes <= heapInfo.size && heapInfo.size - offsetBytes >= requirements.size;
        if ( bTypeAccepted == false || bAligned == false || bFits == false )
        {
            vkDestroyImage( _device, image, nullptr );
            SW_LOG_ERROR_ID( "Vulkan.AliasingImageRejected", "A %#x%# image does not fit the heap (type accepted %#, offset %# aligned %#, size %# of %#)", info.extent.width, info.extent.height, bTypeAccepted,
                             offsetBytes, bAligned, static_cast<uint64>( requirements.size ), static_cast<uint64>( heapInfo.size ) );
            return false;
        }
        if ( isVkSuccess( vmaBindImageMemory2( _allocator, heap._allocation, offsetBytes, image, nullptr ), "vmaBindImageMemory2" ) == false )
        {
            vkDestroyImage( _device, image, nullptr );
            return false;
        }
        outImage = image;
        outBytes = requirements.size;
        return true;
    }

    void VulkanMemoryAllocator::setName( const VulkanAllocation& allocation, const utf8* pName )
    {
        if ( _allocator != nullptr && allocation.isValid() )
            vmaSetAllocationName( _allocator, allocation._allocation, pName );
    }

    void VulkanMemoryAllocator::flush( const VulkanAllocation& allocation, uint64 offsetBytes, uint64 sizeBytes )
    {
        if ( allocation.isValid() )
            (void)isVkSuccess( vmaFlushAllocation( _allocator, allocation._allocation, offsetBytes, sizeBytes ), "vmaFlushAllocation" ); // 실패는 안에서 로그로 남는다
    }

    void VulkanMemoryAllocator::invalidate( const VulkanAllocation& allocation, uint64 offsetBytes, uint64 sizeBytes )
    {
        if ( allocation.isValid() )
            (void)isVkSuccess( vmaInvalidateAllocation( _allocator, allocation._allocation, offsetBytes, sizeBytes ), "vmaInvalidateAllocation" ); // 실패는 안에서 로그로 남는다
    }

    bool VulkanMemoryAllocator::queryBudget( RHIMemoryBudget& outBudget ) const
    {
        if ( _allocator == nullptr || _bMemoryBudget == SW_FALSE )
            return false;
        const VkPhysicalDeviceMemoryProperties* pProperties{ nullptr };
        vmaGetMemoryProperties( _allocator, &pProperties );
        VmaBudget arrBudget[VK_MAX_MEMORY_HEAPS]{};
        vmaGetHeapBudgets( _allocator, arrBudget );

        // 사용량은 모든 힙(로컬 + 호스트 가시)의 합이다 — 추적기가 호스트 가시 버퍼도 센다. 예산 · 남은 양은 로컬 힙만 본다.
        uint64 usageBytes{ 0 };
        uint64 localUsageBytes{ 0 };
        uint64 localBudgetBytes{ 0 };
        for ( uint32 heapIndex = 0; heapIndex < pProperties->memoryHeapCount; ++heapIndex )
        {
            usageBytes += arrBudget[heapIndex].usage;
            if ( ( pProperties->memoryHeaps[heapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ) == 0 )
                continue;
            localUsageBytes += arrBudget[heapIndex].usage;
            localBudgetBytes += arrBudget[heapIndex].budget;
        }
        outBudget._usageBytes      = usageBytes;
        outBudget._budgetBytes     = localBudgetBytes;
        outBudget._availableBytes  = localBudgetBytes > localUsageBytes ? localBudgetBytes - localUsageBytes : 0;
        outBudget._scope           = RHIMemoryScope::Process;
        outBudget._bUsageKnown     = SW_TRUE;
        outBudget._bBudgetKnown    = SW_TRUE;
        outBudget._bAvailableKnown = SW_TRUE;
        return true;
    }

    uint32 VulkanMemoryAllocator::computeBlockCount() const
    {
        if ( _allocator == nullptr )
            return 0;
        VmaBudget arrBudget[VK_MAX_MEMORY_HEAPS]{}; // 장치에 없는 힙의 칸은 0 으로 남는다
        vmaGetHeapBudgets( _allocator, arrBudget );
        uint32 blockCount{ 0 };
        for ( const VmaBudget& budget : arrBudget )
        {
            blockCount += budget.statistics.blockCount;
        }
        return blockCount;
    }
} // namespace sw
