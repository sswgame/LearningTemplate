#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/RHI/Support/RHIStagingRingUtil.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"

// 텍스처 전송(docs/plans/RhiAsyncUploads.md 2.2, U6): 영속 매핑 스테이징 링, 제출 뒤에도 사는 전송 작업(커맨드 버퍼 + 펜스), 펜스 줄의 연속 구간 워터마크.
// 전송은 그래픽스 큐 하나로 낸다 — 같은 큐라 큐 소유권 이전이 없고, 제출 순서와 배리어가 계약 3(앞선 프레임 뒤, 뒤의 프레임 앞)을 만든다.

namespace sw
{
    SW_LOG_CALLER( "Vulkan" );

    bool VulkanRHIDevice::initializeTransfers( const RHISwapChainDesc& desc )
    {
        if ( desc._stagingRingBytes == 0 || desc._readbackRingBytes == 0 )
        {
            SW_LOG_ERROR( "The Vulkan backend needs staging ring capacities (gv_rhiStagingRingMegabytes and gv_rhiReadbackRingMegabytes must be positive)" );
            return false;
        }

        // 작업을 다시 쓸 때 begin 이 버퍼를 리셋하므로 RESET_COMMAND_BUFFER 가 필요하다. 짧게 사는 기록이라 TRANSIENT 도 준다.
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = _graphicsQueueFamilyIndex;
        if ( vkCreateCommandPool( _device, &poolInfo, nullptr, &_transferCommandPool ) != VK_SUCCESS )
        {
            _transferCommandPool = VK_NULL_HANDLE;
            SW_LOG_ERROR( "Failed to create the Vulkan transfer command pool" );
            return false;
        }
        return createStagingRing( _uploadRing, desc._stagingRingBytes, desc._stagingSoloMaxBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "upload" ) &&
               createStagingRing( _readbackRing, desc._readbackRingBytes, desc._stagingSoloMaxBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "readback" );
    }

    void VulkanRHIDevice::shutdownTransfers()
    {
        // GPU 는 종료 2단계(waitIdle)에서 쉬었다. 잃은 장치면 끝나지 않은 작업이 남아 있을 수 있지만 더 기다리지 않는다(isDeviceLost 설명).
        _transferFenceQueue.retireAll( [this]( VulkanTransferJob& job )
        { recycleTransferJob( job ); } );
        _transferReleaseQueue.flushAll();
        {
            std::scoped_lock<mutex> jobLock{ _transferJobMutex };
            for ( const VulkanTransferJob& job : _listFreeTransferJob )
            {
                if ( job._fence != VK_NULL_HANDLE )
                    vkDestroyFence( _device, job._fence, nullptr );
            }
            _listFreeTransferJob.clear();
        }
        // 커맨드 버퍼는 풀과 함께 놓인다.
        if ( _transferCommandPool != VK_NULL_HANDLE )
        {
            vkDestroyCommandPool( _device, _transferCommandPool, nullptr );
            _transferCommandPool = VK_NULL_HANDLE;
        }
        // 가져가지 않은 읽기는 링과 함께 사라진다. 그 뒤의 가져가기 · 버리기는 업로드 티켓처럼 아무 일도 하지 않는다.
        _readbackTable.clear();
        destroyStagingRing( _uploadRing );
        destroyStagingRing( _readbackRing );
    }

    bool VulkanRHIDevice::createStagingRing( VulkanStagingRing& ring, uint64 capacityBytes, uint64 soloMaxBytes, uint32 usageFlags, const utf8* pName )
    {
        // 구간 시작 정렬. 복사 오프셋 규칙(텍셀 블록 바이트와 4 의 배수)은 제출 함수가 구간 안에서 맞추고, 여기서는 캐시 줄 경계에서 시작하도록 넉넉히 둔다(GL 링과 같다).
        constexpr uint64 kSpanAlignmentBytes = 256;
        if ( createMappedStagingBuffer( capacityBytes, usageFlags, pName, ring._buffer, ring._memory, ring._pMapped ) == false )
            return false;
        ring._ledger.initialize( capacityBytes, kSpanAlignmentBytes, soloMaxBytes );
        return true;
    }

    void VulkanRHIDevice::destroyStagingRing( VulkanStagingRing& ring )
    {
        {
            std::scoped_lock<mutex> soloLock{ ring._soloMutex };
            destroyMappedStagingBuffer( ring._soloBuffer, ring._soloMemory, ring._pSoloMapped );
        }
        destroyMappedStagingBuffer( ring._buffer, ring._memory, ring._pMapped );
        ring._ledger.shutdown();
    }

    bool VulkanRHIDevice::createMappedStagingBuffer( uint64 capacityBytes, uint32 usageFlags, const utf8* pName, VkBuffer& outBuffer, VkDeviceMemory& outMemory,
                                                     uint8*& outMapped )
    {
        outBuffer = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        outMapped = nullptr;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = capacityBytes;
        bufferInfo.usage       = usageFlags;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if ( vkCreateBuffer( _device, &bufferInfo, nullptr, &outBuffer ) != VK_SUCCESS )
        {
            outBuffer = VK_NULL_HANDLE;
            SW_LOG_ERROR( "Failed to create the Vulkan %# staging buffer (%# bytes, gv_rhiStagingRingMegabytes · gv_rhiReadbackRingMegabytes · gv_rhiStagingSingleMaxMegabytes)",
                          pName, capacityBytes );
            return false;
        }

        // 호스트 일관 메모리라 제출 전의 memcpy 는 flush 없이 보이고, 되읽기는 복사 뒤의 HOST 배리어와 펜스만으로 읽힌다(invalidate 가 필요 없다).
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements( _device, outBuffer, &requirements );
        uint32 memoryTypeIndex{ 0 };
        void*  pMapped{ nullptr };
        bool   bMemoryReady = findMemoryType( requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex );
        if ( bMemoryReady )
        {
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize  = requirements.size;
            allocInfo.memoryTypeIndex = memoryTypeIndex;
            bMemoryReady              = vkAllocateMemory( _device, &allocInfo, nullptr, &outMemory ) == VK_SUCCESS &&
                           vkBindBufferMemory( _device, outBuffer, outMemory, 0 ) == VK_SUCCESS &&
                           vkMapMemory( _device, outMemory, 0, VK_WHOLE_SIZE, 0, &pMapped ) == VK_SUCCESS && pMapped != nullptr;
        }
        if ( bMemoryReady == false )
        {
            SW_LOG_ERROR( "The Vulkan %# staging buffer (%# bytes) could not be allocated or mapped in host visible memory", pName, capacityBytes );
            destroyMappedStagingBuffer( outBuffer, outMemory, outMapped );
            return false;
        }

        outMapped = static_cast<uint8*>( pMapped );
        getMemoryLedger().recordAllocation( RHIMemoryKey::makeDeviceObject( outMemory ), RHIMemoryKind::Staging, requirements.size );
        return true;
    }

    void VulkanRHIDevice::destroyMappedStagingBuffer( VkBuffer& rBuffer, VkDeviceMemory& rMemory, uint8*& pMapped )
    {
        // 메모리를 놓으면 매핑도 풀린다. 장부에는 매핑까지 끝난 버퍼만 올렸다.
        if ( pMapped != nullptr )
            getMemoryLedger().recordFree( RHIMemoryKey::makeDeviceObject( rMemory ) );
        if ( rBuffer != VK_NULL_HANDLE )
            vkDestroyBuffer( _device, rBuffer, nullptr );
        if ( rMemory != VK_NULL_HANDLE )
            vkFreeMemory( _device, rMemory, nullptr );
        rBuffer = VK_NULL_HANDLE;
        rMemory = VK_NULL_HANDLE;
        pMapped = nullptr;
    }

    RHISubmitResult VulkanRHIDevice::allocateStagingSpan( VulkanStagingRing& ring, uint64 sizeBytes, uint32 usageFlags, const utf8* pCaller, RHIStagingSpan& outSpan )
    {
        RHIStagingAllocateResult result = ring._ledger.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        if ( result == RHIStagingAllocateResult::Full )
        {
            retireFinishedTransfers();
            result = ring._ledger.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        }
        // 결과 옮기기 · 못 받은 구간 놓기 · 이유 로그는 세 백엔드가 같이 쓰는 한 곳이다(잃은 장치면 Full 도 Rejected, 계약 2).
        const RHISubmitResult submitResult = RHIStagingRingUtil::settleAllocation( ring._ledger, _transferSerials, result, sizeBytes, "Vulkan", pCaller, outSpan );
        if ( submitResult != RHISubmitResult::Accepted || outSpan._bSolo == SW_FALSE )
            return submitResult;
        // 장부가 단독 구간을 내줬다면 앞선 단독 구간은 이미 돌아왔다 — 남은 버퍼(아직 회수가 지우지 않은 것)를 GPU 가 더 쓰지 않는다.
        std::scoped_lock<mutex> soloLock{ ring._soloMutex };
        destroyMappedStagingBuffer( ring._soloBuffer, ring._soloMemory, ring._pSoloMapped );
        if ( createMappedStagingBuffer( outSpan._sizeBytes, usageFlags, "single transfer", ring._soloBuffer, ring._soloMemory, ring._pSoloMapped ) )
            return RHISubmitResult::Accepted;
        return RHIStagingRingUtil::rejectSoloBufferFailure( ring._ledger, outSpan, "Vulkan", pCaller );
    }

    void VulkanRHIDevice::destroyIdleSoloBuffer( VulkanStagingRing& ring )
    {
        std::scoped_lock<mutex> soloLock{ ring._soloMutex };
        if ( ring._soloBuffer == VK_NULL_HANDLE )
            return;
        // 판정은 락 안에서 한다 — 밖에서 보면 그 사이 다른 스레드가 새 단독 구간을 받아 버퍼를 만들 수 있다.
        ring._ledger.freeCompleted( _transferSerials.getWatermark() );
        if ( ring._ledger.hasLiveSolo() )
            return;
        destroyMappedStagingBuffer( ring._soloBuffer, ring._soloMemory, ring._pSoloMapped );
    }

    bool VulkanRHIDevice::beginTransferJob( VulkanTransferJob& outJob )
    {
        outJob = VulkanTransferJob{};
        {
            std::scoped_lock<mutex> jobLock{ _transferJobMutex };
            if ( _listFreeTransferJob.empty() == false )
            {
                outJob = _listFreeTransferJob.back();
                _listFreeTransferJob.pop_back();
            }
        }

        VkResult result{ VK_SUCCESS };
        if ( outJob._commandBuffer == VK_NULL_HANDLE )
        {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool        = _transferCommandPool;
            allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            result                       = vkAllocateCommandBuffers( _device, &allocInfo, &outJob._commandBuffer );
            if ( result != VK_SUCCESS )
            {
                SW_LOG_ERROR( "Failed to allocate a Vulkan transfer command buffer (VkResult=%#)", static_cast<int32>( result ) );
                return false;
            }
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            result          = vkCreateFence( _device, &fenceInfo, nullptr, &outJob._fence );
            if ( result != VK_SUCCESS )
            {
                vkFreeCommandBuffers( _device, _transferCommandPool, 1, &outJob._commandBuffer );
                SW_LOG_ERROR( "Failed to create a Vulkan transfer fence (VkResult=%#)", static_cast<int32>( result ) );
                return false;
            }
        }
        else
        {
            // 다시 쓰는 작업의 펜스는 지난 제출에서 신호됐다(제출하지 못하고 돌아온 작업이면 신호되지 않은 채다 — 리셋은 둘 다 받는다).
            result = vkResetFences( _device, 1, &outJob._fence );
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if ( result == VK_SUCCESS )
            result = vkBeginCommandBuffer( outJob._commandBuffer, &beginInfo ); // 풀이 RESET_COMMAND_BUFFER 라 지난 기록을 함께 지운다
        if ( result == VK_SUCCESS )
            return true;
        SW_LOG_ERROR( "Failed to begin a Vulkan transfer command buffer (VkResult=%#)", static_cast<int32>( result ) );
        recycleTransferJob( outJob );
        return false;
    }

    uint64 VulkanRHIDevice::submitTransferJob( const VulkanTransferJob& job, const utf8* pWhere )
    {
        VkResult result = vkEndCommandBuffer( job._commandBuffer );
        uint64   serial{ 0 };
        if ( result == VK_SUCCESS )
        {
            VkSubmitInfo submitInfo{};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &job._commandBuffer;
            std::scoped_lock<mutex> queueLock{ _queueMutex };
            // 기록하는 사이에 장치를 잃었으면(다른 스레드의 손실 · rhi.removeDevice) 내지 않는다.
            result = isDeviceLost() ? VK_ERROR_DEVICE_LOST : vkQueueSubmit( _graphicsQueue, 1, &submitInfo, job._fence );
            // 시리얼은 큐에 낸 직후 같은 락 안에서 매긴다 — 락 밖에서 매기면 시리얼 순서가 큐 순서와 어긋나 워터마크가 끝나지 않은 제출을 덮는다(계약 5).
            if ( result == VK_SUCCESS )
                serial = _transferFenceQueue.enqueue( job );
        }
        if ( serial != 0 )
            return serial;
        reportQueueResult( static_cast<int32>( result ), pWhere );
        recycleTransferJob( job );
        return 0;
    }

    void VulkanRHIDevice::recycleTransferJob( const VulkanTransferJob& job )
    {
        std::scoped_lock<mutex> jobLock{ _transferJobMutex };
        _listFreeTransferJob.push_back( job );
    }

    void VulkanRHIDevice::retireFinishedTransfers()
    {
        // 잃은 장치의 펜스는 묻지 않는다(isDeviceLost 설명). 워터마크가 멈추고 남은 티켓은 Failed 다.
        if ( _device != VK_NULL_HANDLE && isDeviceLost() == false )
        {
            _transferFenceQueue.retireDone(
                [this]( const VulkanTransferJob& job )
            {
                const VkResult status = vkGetFenceStatus( _device, job._fence );
                if ( status == VK_ERROR_DEVICE_LOST )
                    reportQueueResult( static_cast<int32>( status ), "retireFinishedTransfers: vkGetFenceStatus" );
                return status == VK_SUCCESS;
            },
                [this]( VulkanTransferJob& job )
            { recycleTransferJob( job ); } );
        }
        // 워터마크가 지난 전송을 기다리던 텍스처 해제를 프레임 해제 큐로 넘긴다(그 다음 프레임 펜스 뒤에 놓인다).
        _transferReleaseQueue.tickCompleted( _transferSerials.getWatermark() );
        // 장부가 단독 구간을 돌려받았으면 그 일회성 버퍼를 지운다(GL 과 같은 규칙, 호스트 가시 메모리를 링 하나 크기 넘게 쥐고 있지 않는다).
        destroyIdleSoloBuffer( _uploadRing );
        destroyIdleSoloBuffer( _readbackRing );
    }
} // namespace sw
