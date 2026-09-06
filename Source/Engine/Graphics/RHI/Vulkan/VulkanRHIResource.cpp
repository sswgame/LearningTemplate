#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResource.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/Support/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

#include <vulkan/vulkan.h>

namespace sw
{
    namespace
    {
        struct VulkanRHIResourceInternal
        {
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "VulkanRHIResource" );

    RHIBufferHandle VulkanRHIResource::createConstantBuffer( uint32 size )
    {
        const uint32          aligned = MathUtil::align( size, constant::kConstantBufferAlignment );
        const uint32          total   = aligned * constant::kMaxFrameCountInFlight;
        const RHIBufferHandle handle  = _pDevice->createVulkanBuffer( total, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr );
        if ( handle != 0 )
            _pDevice->_mapCbSlotSize[handle] = aligned;
        return handle;
    }

    void VulkanRHIResource::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
        if ( pRecord == nullptr || pData == nullptr || size == 0 )
            return;

        if ( pRecord->_memory == VK_NULL_HANDLE )
            return;

        uint32     slotSize = size;
        const auto slotIt   = _pDevice->_mapCbSlotSize.find( buffer );
        if ( slotIt != _pDevice->_mapCbSlotSize.end() )
            slotSize = slotIt->second;
        const uint32 offset = ( _pDevice->_currentFrame % constant::kMaxFrameCountInFlight ) * slotSize;

        void* pMapped{ nullptr };
        if ( vkMapMemory( _pDevice->_device, pRecord->_memory, offset, size, 0, &pMapped ) == VK_SUCCESS )
        {
            Memory::copy( pMapped, pData, size );
            vkUnmapMemory( _pDevice->_device, pRecord->_memory );
        }

        // 디스크립터는 여기서 손대지 않는다. 프레임 슬롯마다 전용 셋이 등록 시점에 자기 오프셋을
        // 가리키도록 한 번만 기록돼 있고(registerBindlessResource), 바인딩 때 현재 프레임 셋이
        // 선택된다(registeredDescriptorSetAt). 예전엔 셋 하나를 매 프레임 새 슬롯으로 다시 기록했는데,
        // 그 셋은 아직 실행 중인 직전 프레임 커맨드버퍼가 참조하고 있어 in-use 위반이었다.
        (void)slotSize;
        (void)offset;
    }

    RHIBufferHandle VulkanRHIResource::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        if ( elementSize == 0 || elementCount == 0 )
            return 0;
        constexpr uint32 usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        return _pDevice->createVulkanBuffer( elementSize * elementCount, usage, nullptr );
    }

    bool VulkanRHIResource::acquireStructuredUploadStaging( uint64 sizeBytes, uint64& outOffset, VkBuffer& outBuffer )
    {
        const uint32                           slotIndex = _pDevice->_currentFrame;
        VulkanRHIDevice::StructuredUploadSlot& slot      = _pDevice->_arrStructuredUploadSlot[slotIndex];

        // 슬롯이 다시 내 차례가 됐다는 건 beginFrame 이 그 슬롯의 펜스를 기다렸다는 뜻 — 오프셋을 되감는다.
        // 같은 펜스 구간(같은 프레임) 안의 두 번째 호출은 앞선 복사가 아직 스테이징을 읽을 수 있으므로 이어 쓴다.
        if ( slot._resetFence != _pDevice->_frameFenceCounter )
        {
            slot._uploadOffset = 0;
            slot._resetFence   = _pDevice->_frameFenceCounter;
        }

        uint64 offset = MathUtil::align( slot._uploadOffset, static_cast<uint64>( constant::kConstantBufferAlignment ) );
        if ( slot._buffer == VK_NULL_HANDLE || slot._capacity < offset + sizeBytes )
        {
            const uint64 newCapacity = MathUtil::align( ( offset + sizeBytes ) * 2, 65536ull );

            // 옛 스테이징은 이번 구간의 앞선 복사가 아직 읽고 있을 수 있다 — 펜스 뒤에 놓아준다.
            if ( slot._buffer != VK_NULL_HANDLE )
            {
                VkDevice       dev = _pDevice->_device;
                VkBuffer       buf = slot._buffer;
                VkDeviceMemory mem = slot._memory;
                if ( slot._pMapped != nullptr )
                    vkUnmapMemory( dev, mem );
                _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, buf, mem]()
                {
                    vkDestroyBuffer( dev, buf, nullptr );
                    vkFreeMemory( dev, mem, nullptr );
                } ),
                                                           _pDevice->_frameFenceCounter + 1 );
                slot._buffer   = VK_NULL_HANDLE;
                slot._memory   = VK_NULL_HANDLE;
                slot._pMapped  = nullptr;
                slot._capacity = 0;
                offset         = 0;
            }

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size        = newCapacity;
            bufferInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if ( vkCreateBuffer( _pDevice->_device, &bufferInfo, nullptr, &slot._buffer ) != VK_SUCCESS )
            {
                SW_LOG_ERROR( "acquireStructuredUploadStaging: failed to create the staging buffer (%# bytes)", newCapacity );
                slot._buffer = VK_NULL_HANDLE;
                return false;
            }

            VkMemoryRequirements memReq{};
            vkGetBufferMemoryRequirements( _pDevice->_device, slot._buffer, &memReq );
            uint32 memoryTypeIndex{ 0 };
            if ( _pDevice->findMemoryType( memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex ) == false )
            {
                vkDestroyBuffer( _pDevice->_device, slot._buffer, nullptr );
                slot._buffer = VK_NULL_HANDLE;
                SW_LOG_ERROR( "acquireStructuredUploadStaging: no host visible memory type" );
                return false;
            }

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize  = memReq.size;
            allocInfo.memoryTypeIndex = memoryTypeIndex;
            if ( vkAllocateMemory( _pDevice->_device, &allocInfo, nullptr, &slot._memory ) != VK_SUCCESS ||
                 vkBindBufferMemory( _pDevice->_device, slot._buffer, slot._memory, 0 ) != VK_SUCCESS ||
                 vkMapMemory( _pDevice->_device, slot._memory, 0, VK_WHOLE_SIZE, 0, &slot._pMapped ) != VK_SUCCESS )
            {
                if ( slot._memory != VK_NULL_HANDLE )
                    vkFreeMemory( _pDevice->_device, slot._memory, nullptr );
                vkDestroyBuffer( _pDevice->_device, slot._buffer, nullptr );
                slot             = VulkanRHIDevice::StructuredUploadSlot{};
                slot._resetFence = _pDevice->_frameFenceCounter;
                SW_LOG_ERROR( "acquireStructuredUploadStaging: failed to allocate/map the staging memory" );
                return false;
            }
            slot._capacity = newCapacity;
        }

        slot._uploadOffset = offset + sizeBytes;
        outOffset          = offset;
        outBuffer          = slot._buffer;
        return true;
    }

    void VulkanRHIResource::updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        // 예전엔 목적 버퍼를 직접 vkMapMemory 해서 썼다. 링 오프셋 버그(71cd9755)를 걷어낸 뒤에도
        // "GPU 가 직전 프레임을 아직 읽는 중인 메모리를 CPU 가 덮어쓰는" 해저드가 남아 있었다.
        // 지금은 스테이징 슬롯에 쓰고 복사를 프레임 커맨드버퍼에 기록한다 — 큐 순서가 곧 해저드 해결이고,
        // "바뀐 게 없으면 업로드 생략" 같은 상위 로직도 단일 목적 버퍼 그대로 유효하다.
        VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
        if ( pRecord == nullptr || pData == nullptr || size == 0 || pRecord->_buffer == VK_NULL_HANDLE ||
             _pDevice->_device == VK_NULL_HANDLE || _pDevice->_graphicsQueue == VK_NULL_HANDLE )
            return;
        if ( size > pRecord->_size )
            size = pRecord->_size;

        uint64   stagingOffset{ 0 };
        VkBuffer stagingBuffer{ VK_NULL_HANDLE };
        if ( acquireStructuredUploadStaging( size, stagingOffset, stagingBuffer ) == false )
            return;
        Memory::copy( static_cast<uint8*>( _pDevice->_arrStructuredUploadSlot[_pDevice->_currentFrame]._pMapped ) + stagingOffset, pData, size );

        // 프레임 안이면 프레임 스트림에 기록한다(제출 순서상 이번 프레임의 패스 리스트보다 앞). 프레임 밖
        // (초기 업로드·테스트)이면 일회성 커맨드버퍼로 제출하고 큐가 비기를 기다린다.
        VkCommandBuffer cmd      = ( _pDevice->_bFrameStarted == SW_TRUE ) ? _pDevice->_activeFrameBuffer : VK_NULL_HANDLE;
        const bool      bOneShot = ( cmd == VK_NULL_HANDLE );
        if ( bOneShot )
        {
            if ( _pDevice->_commandPool == VK_NULL_HANDLE )
                return;
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool        = _pDevice->_commandPool;
            allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            if ( vkAllocateCommandBuffers( _pDevice->_device, &allocInfo, &cmd ) != VK_SUCCESS )
                return;
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer( cmd, &beginInfo );
        }
        else if ( _pDevice->_recordingState._bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( cmd );
            _pDevice->_recordingState._bRenderPassActive = SW_FALSE;
        }

        // 앞 프레임의 읽기(셰이더·간접 인자·컴퓨트 쓰기) 가 끝난 뒤에 복사하고, 복사가 끝난 뒤에 이번
        // 프레임이 읽는다. 상태 추적(_state)은 건드리지 않는다 — 보수적인 마스크로 양쪽을 다 덮는다.
        constexpr VkAccessFlags        kConsumerAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        constexpr VkPipelineStageFlags kConsumerStage  = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;

        VkBufferMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = pRecord->_buffer;
        barrier.offset              = 0;
        barrier.size                = VK_WHOLE_SIZE;

        barrier.srcAccessMask = kConsumerAccess;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier( cmd, kConsumerStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr );

        VkBufferCopy region{};
        region.srcOffset = stagingOffset;
        region.dstOffset = 0;
        region.size      = size;
        vkCmdCopyBuffer( cmd, stagingBuffer, pRecord->_buffer, 1, &region );

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = kConsumerAccess;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kConsumerStage, 0, 0, nullptr, 1, &barrier, 0, nullptr );

        if ( bOneShot )
        {
            vkEndCommandBuffer( cmd );
            VkSubmitInfo submitInfo{};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &cmd;
            if ( vkQueueSubmit( _pDevice->_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE ) == VK_SUCCESS )
                vkQueueWaitIdle( _pDevice->_graphicsQueue );
            vkFreeCommandBuffers( _pDevice->_device, _pDevice->_commandPool, 1, &cmd );
        }
    }

    RHIBufferHandle VulkanRHIResource::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        if ( _pDevice->_device == nullptr || pData == nullptr || sizeBytes == 0 )
            return 0;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = sizeBytes;
        bufferInfo.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer buffer = VK_NULL_HANDLE;
        if ( vkCreateBuffer( _pDevice->_device, &bufferInfo, nullptr, &buffer ) != VK_SUCCESS )
        {
            SW_LOG_ERROR( "Failed to create VkBuffer for Vertex Buffer!" );
            return 0;
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements( _pDevice->_device, buffer, &memRequirements );

        uint32 memoryTypeIndex{ 0 };
        if ( _pDevice->findMemoryType( memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex ) == false )
        {
            vkDestroyBuffer( _pDevice->_device, buffer, nullptr );
            SW_LOG_ERROR( "Failed to find a host visible memory type for Vertex Buffer!" );
            return 0;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        VkDeviceMemory memory = VK_NULL_HANDLE;
        if ( vkAllocateMemory( _pDevice->_device, &allocInfo, nullptr, &memory ) != VK_SUCCESS )
        {
            vkDestroyBuffer( _pDevice->_device, buffer, nullptr );
            SW_LOG_ERROR( "Failed to allocate memory for Vertex Buffer!" );
            return 0;
        }

        vkBindBufferMemory( _pDevice->_device, buffer, memory, 0 );

        void* pMapped{ nullptr };
        if ( vkMapMemory( _pDevice->_device, memory, 0, sizeBytes, 0, &pMapped ) == VK_SUCCESS && pMapped != nullptr )
        {
            Memory::copy( pMapped, pData, sizeBytes );
            vkUnmapMemory( _pDevice->_device, memory );
        }

        VulkanRHIDevice::VulkanBufferRecord record{};
        record._buffer = buffer;
        record._memory = memory;
        record._size   = sizeBytes;
        return _pDevice->_gpuBuffers.insert( std::move( record ) );
    }

    void VulkanRHIResource::destroyBuffer( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;
        if ( buffer == _pDevice->_recordingState._boundMeshVb )
            _pDevice->_recordingState._boundMeshVb = 0;
        if ( buffer == _pDevice->_recordingState._boundIndexBuffer )
            _pDevice->_recordingState._boundIndexBuffer = 0;
        _pDevice->_mapCbSlotSize.erase( buffer );

        VulkanRHIDevice::VulkanBufferRecord owned;
        if ( _pDevice->_gpuBuffers.take( buffer, owned ) == false )
            return;

        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        for ( size_t bufferIndex = 0; bufferIndex < _pDevice->_listBindlessSourceBuffer.size(); ++bufferIndex )
        {
            if ( _pDevice->_listBindlessSourceBuffer[bufferIndex] != buffer )
                continue;
            if ( bufferIndex < _pDevice->_listRegisteredDescriptorSet.size() && _pDevice->_listRegisteredDescriptorSet[bufferIndex] != VK_NULL_HANDLE )
            {
                VkDevice         dev  = _pDevice->_device;
                VkDescriptorPool pool = _pDevice->_descriptorPool;
                VkDescriptorSet  set  = _pDevice->_listRegisteredDescriptorSet[bufferIndex];
                _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pool, set]()
                {
                    vkFreeDescriptorSets( dev, pool, 1, &set );
                } ),
                                                           _pDevice->_frameFenceCounter + 1 );
                _pDevice->_listRegisteredDescriptorSet[bufferIndex] = VK_NULL_HANDLE;
            }
            releaseFreeListIndex( _pDevice->_listBindlessSourceBuffer, _pDevice->_listBindlessFree,
                                  static_cast<uint32>( bufferIndex ), RHIBufferHandle{ 0 } );
        }
        for ( size_t bufferIndex = 0; bufferIndex < _pDevice->_listUavSourceBuffer.size(); ++bufferIndex )
        {
            if ( _pDevice->_listUavSourceBuffer[bufferIndex] != buffer )
                continue;
            if ( bufferIndex < _pDevice->_listRegisteredUAV.size() && _pDevice->_listRegisteredUAV[bufferIndex] != VK_NULL_HANDLE )
            {
                VkDevice         dev  = _pDevice->_device;
                VkDescriptorPool pool = _pDevice->_descriptorPool;
                VkDescriptorSet  set  = _pDevice->_listRegisteredUAV[bufferIndex];
                _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pool, set]()
                {
                    vkFreeDescriptorSets( dev, pool, 1, &set );
                } ),
                                                           _pDevice->_frameFenceCounter + 1 );
                _pDevice->_listRegisteredUAV[bufferIndex] = VK_NULL_HANDLE;
            }
            releaseFreeListIndex( _pDevice->_listUavSourceBuffer, _pDevice->_listUavFree,
                                  static_cast<uint32>( bufferIndex ), RHIBufferHandle{ 0 } );
        }

        VkBuffer       buf = owned._buffer;
        VkDeviceMemory mem = owned._memory;
        VkDevice       dev = _pDevice->_device;
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, buf, mem]()
        {
            if ( buf != VK_NULL_HANDLE )
                vkDestroyBuffer( dev, buf, nullptr );
            if ( mem != VK_NULL_HANDLE )
                vkFreeMemory( dev, mem, nullptr );
        } ),
                                                   _pDevice->_frameFenceCounter + 1 );
    }

    RHITextureHandle VulkanRHIResource::createTexture2D( const RHITextureDesc& desc )
    {
        if ( _pDevice->_device == nullptr || desc._width == 0 || desc._height == 0 )
            return 0;

        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if ( desc._bIsRenderTarget )
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if ( desc._bIsDepthStencil )
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if ( desc._bIsUnorderedAccess )
            usage |= VK_IMAGE_USAGE_STORAGE_BIT;

        const VkFormat requested = VulkanRHIDeviceInternal::toVulkanTextureFormat( desc._format );
        VkFormat       format    = requested;
        if ( desc._bIsDepthStencil != 0 || desc._format == sw::RHIFormat::D24_UNORM_S8_UINT )
        {
            if ( _pDevice->_depthFormat == 0 )
            {
                SW_LOG_ERROR( "createTexture2D: depth format not selected." );
                return 0;
            }
            format = static_cast<VkFormat>( _pDevice->_depthFormat );
        }

        VkImageCreateInfo imageInfo{};
        imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width  = desc._width;
        imageInfo.extent.height = desc._height;
        imageInfo.extent.depth  = 1;
        imageInfo.mipLevels     = desc._mipLevels > 0 ? desc._mipLevels : 1;
        imageInfo.arrayLayers   = 1;
        imageInfo.format        = format;
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage         = usage;
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

        VulkanRHIDevice::VulkanTextureRecord record{};
        record._width         = desc._width;
        record._height        = desc._height;
        record._format        = static_cast<uint32>( format );
        record._rhiFormat     = static_cast<uint32>( desc._format );
        record._mipLevels     = imageInfo.mipLevels;
        record._layout        = static_cast<uint32>( VK_IMAGE_LAYOUT_UNDEFINED );
        record._bRenderTarget = desc._bIsRenderTarget ? 1 : 0;
        record._bDepthStencil = desc._bIsDepthStencil ? 1 : 0;
        record._bindlessIndex = kInvalidDescriptorIndex;

        if ( vkCreateImage( _pDevice->_device, &imageInfo, nullptr, &record._image ) != VK_SUCCESS )
        {
            SW_LOG_ERROR( "Failed to create VkImage for Texture2D." );
            return 0;
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements( _pDevice->_device, record._image, &memRequirements );

        uint32 memoryTypeIndex{ 0 };
        if ( _pDevice->findMemoryType( memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex ) == false )
        {
            vkDestroyImage( _pDevice->_device, record._image, nullptr );
            SW_LOG_ERROR( "Failed to find a device local memory type for Texture2D." );
            return 0;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        if ( vkAllocateMemory( _pDevice->_device, &allocInfo, nullptr, &record._memory ) != VK_SUCCESS )
        {
            vkDestroyImage( _pDevice->_device, record._image, nullptr );
            SW_LOG_ERROR( "Failed to allocate memory for Texture2D." );
            return 0;
        }

        vkBindImageMemory( _pDevice->_device, record._image, record._memory, 0 );

        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        if ( desc._bIsDepthStencil != 0 )
            aspect = static_cast<VkImageAspectFlags>( _pDevice->depthAspectMask() );

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = record._image;
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = format;
        viewInfo.subresourceRange.aspectMask     = aspect;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = imageInfo.mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;

        if ( vkCreateImageView( _pDevice->_device, &viewInfo, nullptr, &record._imageView ) != VK_SUCCESS )
        {
            vkDestroyImage( _pDevice->_device, record._image, nullptr );
            vkFreeMemory( _pDevice->_device, record._memory, nullptr );
            SW_LOG_ERROR( "Failed to create VkImageView for Texture2D." );
            return 0;
        }

        if ( desc._bIsDepthStencil != 0 )
        {
            // 샘플용 뷰는 aspect 가 하나여야 한다 (DEPTH|STENCIL 뷰는 디스크립터에 못 쓴다). 그림자맵처럼
            // 깊이를 읽는 패스가 이 뷰로 bindless 등록된다 — registerBindlessTexture 참고.
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if ( vkCreateImageView( _pDevice->_device, &viewInfo, nullptr, &record._sampleView ) != VK_SUCCESS )
            {
                record._sampleView = VK_NULL_HANDLE;
                SW_LOG_WARNING( "createTexture2D: depth sample view creation failed — texture cannot be sampled." );
            }
        }

        if ( record._bRenderTarget && _pDevice->createOffscreenFramebuffer( record ) == false )
            SW_LOG_WARNING( "createTexture2D: framebuffer creation failed — texture kept without offscreen pass." );

        return _pDevice->_gpuTextures.insert( std::move( record ) );
    }

    bool VulkanRHIResource::uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        VulkanRHIDevice::VulkanTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_image == VK_NULL_HANDLE || _pDevice->_device == VK_NULL_HANDLE ||
             _pDevice->_graphicsQueue == VK_NULL_HANDLE || _pDevice->_commandPool == VK_NULL_HANDLE )
            return false;
        if ( pRecord->_bDepthStencil != 0 )
            return false;

        RHITextureMipSpan arrMip[constant::kMaxTextureMipCount]{};
        const uint32      mipCount = resolveTextureUploadMips( desc, static_cast<RHIFormat>( pRecord->_rhiFormat ), pRecord->_width, pRecord->_height,
                                                               pRecord->_mipLevels, arrMip, constant::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR( "uploadTexture2D: unsupported format or not enough data (%# bytes for %#x%#, %# mips)",
                          desc._sizeBytes, pRecord->_width, pRecord->_height, pRecord->_mipLevels );
            return false;
        }
        const uint32 usedBytes = arrMip[mipCount - 1]._offsetBytes + arrMip[mipCount - 1]._sizeBytes;

        // 호스트 가시 스테이징 버퍼에 통째로 올린 뒤 일회성 커맨드버퍼로 밉마다 복사한다. 로드 시점 경로라
        // 큐가 비기를 기다리는 값싼 동기 방식을 택했다(executeCommandListImmediate 와 같은 이유).
        const RHIBufferHandle                staging  = _pDevice->createVulkanBuffer( usedBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, desc._pData );
        VulkanRHIDevice::VulkanBufferRecord* pStaging = _pDevice->resolveAllocatedBuffer( staging );
        if ( pStaging == nullptr || pStaging->_buffer == VK_NULL_HANDLE )
        {
            SW_LOG_ERROR( "uploadTexture2D: failed to create the staging buffer (%# bytes)", usedBytes );
            return false;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = _pDevice->_commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer cmd{ VK_NULL_HANDLE };
        if ( vkAllocateCommandBuffers( _pDevice->_device, &allocInfo, &cmd ) != VK_SUCCESS )
        {
            destroyBuffer( staging );
            SW_LOG_ERROR( "uploadTexture2D: failed to allocate the one-shot command buffer" );
            return false;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer( cmd, &beginInfo );

        // transitionImageLayout 은 밉 하나만 다루므로 여기서는 전체 밉 체인 배리어를 직접 쓴다.
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = pRecord->_image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = pRecord->_mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;

        barrier.oldLayout     = static_cast<VkImageLayout>( pRecord->_layout );
        barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );

        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            const RHITextureMipSpan& span = arrMip[mip];
            VkBufferImageCopy        region{};
            region.bufferOffset                    = span._offsetBytes;
            region.bufferRowLength                 = 0; // 0 = 빈틈없는 행
            region.bufferImageHeight               = 0;
            region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel       = span._mip;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount     = 1;
            region.imageExtent                     = { span._width, span._height, 1 };
            vkCmdCopyBufferToImage( cmd, pStaging->_buffer, pRecord->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
        }

        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &barrier );
        vkEndCommandBuffer( cmd );

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &cmd;
        const bool bSubmitted         = ( vkQueueSubmit( _pDevice->_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE ) == VK_SUCCESS );
        if ( bSubmitted )
            vkQueueWaitIdle( _pDevice->_graphicsQueue );
        vkFreeCommandBuffers( _pDevice->_device, _pDevice->_commandPool, 1, &cmd );
        destroyBuffer( staging );

        if ( bSubmitted == false )
        {
            SW_LOG_ERROR( "uploadTexture2D: vkQueueSubmit failed" );
            return false;
        }
        pRecord->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
        return true;
    }

    bool VulkanRHIResource::readbackTexture2D( RHITextureHandle texture, uint32 mip, vector<uint8>& outBytes, RHITextureMipSpan& outLayout )
    {
        VulkanRHIDevice::VulkanTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_image == VK_NULL_HANDLE || _pDevice->_device == VK_NULL_HANDLE ||
             _pDevice->_graphicsQueue == VK_NULL_HANDLE || _pDevice->_commandPool == VK_NULL_HANDLE )
            return false;
        if ( pRecord->_bDepthStencil != 0 || mip >= pRecord->_mipLevels )
            return false;
        if ( computeRHITextureMipLayout( static_cast<RHIFormat>( pRecord->_rhiFormat ), pRecord->_width, pRecord->_height, mip, outLayout ) == false )
            return false;

        const RHIBufferHandle                staging  = _pDevice->createVulkanBuffer( outLayout._sizeBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr );
        VulkanRHIDevice::VulkanBufferRecord* pStaging = _pDevice->resolveAllocatedBuffer( staging );
        if ( pStaging == nullptr || pStaging->_buffer == VK_NULL_HANDLE )
            return false;

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = _pDevice->_commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer cmd{ VK_NULL_HANDLE };
        if ( vkAllocateCommandBuffers( _pDevice->_device, &allocInfo, &cmd ) != VK_SUCCESS )
        {
            destroyBuffer( staging );
            return false;
        }
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer( cmd, &beginInfo );

        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = pRecord->_image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = pRecord->_mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.oldLayout                       = static_cast<VkImageLayout>( pRecord->_layout );
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask                   = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );

        VkBufferImageCopy region{};
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = mip;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageExtent                     = { outLayout._width, outLayout._height, 1 };
        vkCmdCopyImageToBuffer( cmd, pRecord->_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pStaging->_buffer, 1, &region );

        // 읽기 뒤에는 샘플링 레이아웃으로 둔다 — 한 번도 안 올린 텍스처(UNDEFINED)도 이제부터는 정의된 레이아웃을 갖는다.
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );
        vkEndCommandBuffer( cmd );

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &cmd;
        const bool bSubmitted         = ( vkQueueSubmit( _pDevice->_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE ) == VK_SUCCESS );
        if ( bSubmitted )
            vkQueueWaitIdle( _pDevice->_graphicsQueue );
        vkFreeCommandBuffers( _pDevice->_device, _pDevice->_commandPool, 1, &cmd );

        bool bOk = false;
        if ( bSubmitted )
        {
            void* pMapped{ nullptr };
            if ( vkMapMemory( _pDevice->_device, pStaging->_memory, 0, outLayout._sizeBytes, 0, &pMapped ) == VK_SUCCESS && pMapped != nullptr )
            {
                outBytes.assign( outLayout._sizeBytes, 0 );
                Memory::copy( outBytes.data(), pMapped, outLayout._sizeBytes );
                vkUnmapMemory( _pDevice->_device, pStaging->_memory );
                bOk = true;
            }
            pRecord->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
        }
        destroyBuffer( staging );
        return bOk;
    }

    void VulkanRHIResource::destroyTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return;

        VulkanRHIDevice::VulkanTextureRecord* pSlot = _pDevice->resolveTexture( texture );
        if ( pSlot == nullptr )
            return;

        _pDevice->destroyCompositeFramebuffersUsing( texture );

        releaseTextureBindlessSlot( *pSlot );

        _pDevice->destroyOffscreenFramebuffer( *pSlot );
        VulkanRHIDevice::VulkanTextureRecord owned;
        if ( _pDevice->_gpuTextures.take( texture, owned ) == false )
            return;
        VkDevice       dev        = _pDevice->_device;
        VkImageView    view       = owned._imageView;
        VkImageView    sampleView = owned._sampleView;
        VkImage        image      = owned._image;
        VkDeviceMemory mem        = owned._memory;
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, view, sampleView, image, mem]()
        {
            if ( view != VK_NULL_HANDLE )
                vkDestroyImageView( dev, view, nullptr );
            if ( sampleView != VK_NULL_HANDLE )
                vkDestroyImageView( dev, sampleView, nullptr );
            if ( image != VK_NULL_HANDLE )
                vkDestroyImage( dev, image, nullptr );
            if ( mem != VK_NULL_HANDLE )
                vkFreeMemory( dev, mem, nullptr );
        } ),
                                                   _pDevice->_frameFenceCounter + 1 );
    }
} // namespace sw
