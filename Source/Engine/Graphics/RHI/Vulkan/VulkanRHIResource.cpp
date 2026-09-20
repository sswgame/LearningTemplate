#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResource.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/Support/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include <vulkan/vulkan.h>

namespace sw
{
    SW_LOG_CALLER( "VulkanRHIResource" );

    namespace
    {
        /**
         * @class VulkanOneShotCommands
         * @brief 일회용 커맨드 버퍼 하나 — 할당·시작은 생성자가, **해제는 소멸자가** 합니다.
         *
         * @details 텍스처 업로드와 리드백이 같은 열다섯 줄을 각자 적고 있었다(할당 → begin → … →
         *          end → submit → waitIdle → free). 지금은 그 사이에 `return` 이 없어 새는 자리가
         *          없지만, **누군가 중간에 검사를 하나 더하는 날 커맨드 버퍼가 샌다** — 풀에서 조용히
         *          자라다가 나중에 할당이 실패한다. 해제를 소멸자에 두면 그 실수가 생길 수 없다.
         *
         * @note 장치 핸들 셋을 인자로 받는다. `VulkanRHIDevice` 의 그 멤버들은 private 이고
         *       `VulkanRHIResource` 만 friend 라, 이 클래스가 장치를 직접 알 수는 없다.
         */
        class VulkanOneShotCommands
        {
        public:
            /** @brief 커맨드 버퍼를 하나 할당하고 기록을 시작합니다. 실패하면 `isValid()` 가 false 입니다. */
            VulkanOneShotCommands( VkDevice device, VkCommandPool commandPool, VkQueue queue )
                : _device{ device }
                , _commandPool{ commandPool }
                , _queue{ queue }
                , _commandBuffer{ VK_NULL_HANDLE }
            {
                VkCommandBufferAllocateInfo allocInfo{};
                allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocInfo.commandPool        = _commandPool;
                allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocInfo.commandBufferCount = 1;
                if ( vkAllocateCommandBuffers( _device, &allocInfo, &_commandBuffer ) != VK_SUCCESS )
                {
                    _commandBuffer = VK_NULL_HANDLE;
                    return;
                }

                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkBeginCommandBuffer( _commandBuffer, &beginInfo );
            }

            ~VulkanOneShotCommands()
            {
                if ( _commandBuffer != VK_NULL_HANDLE )
                    vkFreeCommandBuffers( _device, _commandPool, 1, &_commandBuffer );
            }

            VulkanOneShotCommands( const VulkanOneShotCommands& )            = delete;
            VulkanOneShotCommands& operator=( const VulkanOneShotCommands& ) = delete;

            /** @brief 커맨드 버퍼를 얻었는지 여부입니다. */
            bool isValid() const { return _commandBuffer != VK_NULL_HANDLE; }
            /** @brief 기록 대상 커맨드 버퍼입니다. */
            VkCommandBuffer get() const { return _commandBuffer; }

            /**
             * @brief 기록을 끝내고 제출한 뒤 큐가 빌 때까지 기다립니다.
             * @details 로드·리드백 경로라 큐가 비기를 기다리는 값싼 동기 방식을 택했다
             *          (`executeCommandListImmediate` 와 같은 이유).
             */
            bool endSubmitAndWait()
            {
                if ( isValid() == false )
                    return false;

                vkEndCommandBuffer( _commandBuffer );

                VkSubmitInfo submitInfo{};
                submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers    = &_commandBuffer;
                if ( vkQueueSubmit( _queue, 1, &submitInfo, VK_NULL_HANDLE ) != VK_SUCCESS )
                    return false;

                vkQueueWaitIdle( _queue );
                return true;
            }

        private:
            VkDevice        _device;
            VkCommandPool   _commandPool;
            VkQueue         _queue;
            VkCommandBuffer _commandBuffer;
        };

        /**
         * @brief 2D 색 이미지의 **전체 밉 체인** 배리어 뼈대입니다. 레이아웃과 접근 마스크만 채우면 됩니다.
         * @details `transitionImageLayout` 은 밉 하나만 다루므로 업로드·리드백은 전체 밉 배리어를
         *          직접 쓴다. 그 뼈대 열 줄이 두 곳에 복사돼 있었다 — `aspectMask` 나 `layerCount` 를
         *          빠뜨린 새 배리어는 검증 계층이 잡아 주지만, **잡히는 곳이 배리어를 건 자리가 아니라
         *          그 뒤의 전이**라 읽기 나쁘다. 고정값은 한 곳에 둔다.
         */
        VkImageMemoryBarrier makeWholeImageBarrier( VkImage image, uint32 mipLevels )
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                           = image;
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;
            return barrier;
        }
    } // namespace

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

        // 디스크립터는 여기서 손대지 않는다. 드로우 직전 슬롯 세트를 쓸 때(flushSlotSet) 이번 프레임 슬롯의 오프셋을
        // 넣는다 — 세트는 프레임마다 새로 할당되므로 아직 실행 중인 직전 프레임의 세트를 덮어쓸 일이 없다.
        (void)slotSize;
        (void)offset;
    }

    RHIBufferHandle VulkanRHIResource::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        if ( elementSize == 0 || elementCount == 0 )
            return 0;

        // **64비트로 곱하고 담기지 않으면 거절한다.** `elementSize * elementCount` 를 uint32 로 곱하면
        // 넘쳐서 **조용히 작은 버퍼**가 만들어지고, 셰이더는 원래 개수만큼 쓰므로 그 밖으로 나간다.
        // DX12 는 이 함정을 이미 고쳤는데(그쪽은 `Width` 가 UINT64 라 넓히는 것으로 끝났다)
        // 나머지 백엔드로는 옮겨지지 않았다 — 여기서는 아래 API 가 전부 32비트 크기를 받으므로
        // 넓힐 수가 없다. 담기지 않으면 만들지 않는 것이 맞다.
        const uint64 totalBytes = static_cast<uint64>( elementSize ) * static_cast<uint64>( elementCount );
        if ( totalBytes > static_cast<uint64>( ~uint32{ 0 } ) )
        {
            SW_LOG_ERROR( "구조 버퍼가 32비트 크기에 담기지 않습니다 (%# x %# = %# 바이트).",
                          elementSize, elementCount, totalBytes );
            return 0;
        }

        constexpr uint32 usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        return _pDevice->createVulkanBuffer( static_cast<uint32>( totalBytes ), usage, nullptr );
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

        // 프레임 밖이면 일회성 커맨드버퍼를 쓴다. **해제는 소멸자가 하므로** 나중에 이 사이에 검사가
        // 하나 더 생겨도 커맨드 버퍼가 새지 않는다 (같은 절차가 이 파일에 세 벌 있었다).
        std::optional<VulkanOneShotCommands> oneShot;
        if ( bOneShot )
        {
            if ( _pDevice->_commandPool == VK_NULL_HANDLE )
                return;

            oneShot.emplace( _pDevice->_device, _pDevice->_commandPool, _pDevice->_graphicsQueue );
            if ( oneShot->isValid() == false )
            {
                SW_LOG_ERROR( "updateStructuredBuffer: failed to allocate the one-shot command buffer" );
                return;
            }
            cmd = oneShot->get();
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

        if ( oneShot.has_value() && oneShot->endSubmitAndWait() == false )
            SW_LOG_ERROR( "updateStructuredBuffer: vkQueueSubmit failed" );
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
        return _pDevice->_gpuBuffers.insert( record );
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

        // 아직 등록돼 있으면 인덱스를 반납한다 (unregister 를 안 부른 소유자 정리). 디스크립터는 드로우 시점 슬롯 세트에만
        // 있으므로 되돌릴 것이 없다.
        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        for ( size_t bufferIndex = 0; bufferIndex < _pDevice->_listBindlessSourceBuffer.size(); ++bufferIndex )
        {
            if ( _pDevice->_listBindlessSourceBuffer[bufferIndex] != buffer )
                continue;
            _pDevice->_listBindlessSourceBuffer[bufferIndex] = RHIBufferHandle{ 0 };
            deferFreeBufferIndex( static_cast<uint32>( bufferIndex ), false );
        }
        for ( size_t bufferIndex = 0; bufferIndex < _pDevice->_listUavSourceBuffer.size(); ++bufferIndex )
        {
            if ( _pDevice->_listUavSourceBuffer[bufferIndex] != buffer )
                continue;
            _pDevice->_listUavSourceBuffer[bufferIndex] = RHIBufferHandle{ 0 };
            deferFreeBufferIndex( static_cast<uint32>( bufferIndex ), true );
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
        if ( desc._bIsDepthStencil != SW_FALSE || desc._format == sw::RHIFormat::D24_UNORM_S8_UINT )
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
        if ( desc._bIsDepthStencil != SW_FALSE )
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

        if ( desc._bIsDepthStencil != SW_FALSE )
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

        return _pDevice->_gpuTextures.insert( record );
    }

    bool VulkanRHIResource::uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        VulkanRHIDevice::VulkanTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_image == VK_NULL_HANDLE || _pDevice->_device == VK_NULL_HANDLE ||
             _pDevice->_graphicsQueue == VK_NULL_HANDLE || _pDevice->_commandPool == VK_NULL_HANDLE )
            return false;
        if ( pRecord->_bDepthStencil != SW_FALSE )
            return false;

        RHITextureMipSpan arrMip[constant::kMaxTextureMipCount]{};
        const uint32      mipCount = resolveTextureUploadMips( desc, static_cast<RHIFormat>( pRecord->_rhiFormat ), pRecord->_width, pRecord->_height,
                                                               pRecord->_mipLevels, arrMip, constant::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR( "uploadTexture2D: unsupported format or not enough data (%# bytes for %#×%#, %# mips)",
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
            destroyBuffer( staging );
            SW_LOG_ERROR( "uploadTexture2D: failed to create the staging buffer (%# bytes)", usedBytes );
            return false;
        }

        VulkanOneShotCommands oneShot{ _pDevice->_device, _pDevice->_commandPool, _pDevice->_graphicsQueue };
        if ( oneShot.isValid() == false )
        {
            destroyBuffer( staging );
            SW_LOG_ERROR( "uploadTexture2D: failed to allocate the one-shot command buffer" );
            return false;
        }
        const VkCommandBuffer cmd = oneShot.get();

        // transitionImageLayout 은 밉 하나만 다루므로 여기서는 전체 밉 체인 배리어를 직접 쓴다.
        VkImageMemoryBarrier barrier = makeWholeImageBarrier( pRecord->_image, pRecord->_mipLevels );

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

        const bool bSubmitted = oneShot.endSubmitAndWait();
        destroyBuffer( staging );

        if ( bSubmitted == false )
        {
            SW_LOG_ERROR( "uploadTexture2D: vkQueueSubmit failed" );
            return false;
        }
        pRecord->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
        return true;
    }

    RHIFormat VulkanRHIResource::getTextureFormat( RHITextureHandle texture ) const
    {
        const VulkanRHIDevice::VulkanTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        return pRecord != nullptr ? static_cast<RHIFormat>( pRecord->_rhiFormat ) : RHIFormat::Unknown;
    }

    bool VulkanRHIResource::readbackTexture2D( RHITextureHandle texture, uint32 mip, vector<uint8>& outBytes, RHITextureMipSpan& outLayout )
    {
        // **여기의 실패는 전부 소리를 낸다.** 예전에는 네 자리가 말없이 false 를 돌려줬는데, 리드백은
        // 오프스크린 렌더 문제가 드러나는 통로라 "false 인데 이유가 없다" 가 곧 긴 추적이 된다
        // (형제인 uploadTexture2D 는 같은 자리에서 전부 로그를 남기고 있었다).
        VulkanRHIDevice::VulkanTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_image == VK_NULL_HANDLE || _pDevice->_device == VK_NULL_HANDLE ||
             _pDevice->_graphicsQueue == VK_NULL_HANDLE || _pDevice->_commandPool == VK_NULL_HANDLE )
        {
            SW_LOG_ERROR( "readbackTexture2D: texture %# or the device is not usable", texture );
            return false;
        }
        if ( pRecord->_bDepthStencil != SW_FALSE || mip >= pRecord->_mipLevels )
        {
            SW_LOG_ERROR( "readbackTexture2D: depth-stencil readback is unsupported, or mip %# is out of range (%# mips)",
                          mip, pRecord->_mipLevels );
            return false;
        }
        if ( computeRhiTextureMipLayout( static_cast<RHIFormat>( pRecord->_rhiFormat ), pRecord->_width, pRecord->_height, mip, outLayout ) == false )
        {
            SW_LOG_ERROR( "readbackTexture2D: unsupported format %# for %#x%# mip %#",
                          pRecord->_rhiFormat, pRecord->_width, pRecord->_height, mip );
            return false;
        }

        const RHIBufferHandle                staging  = _pDevice->createVulkanBuffer( outLayout._sizeBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr );
        VulkanRHIDevice::VulkanBufferRecord* pStaging = _pDevice->resolveAllocatedBuffer( staging );
        if ( pStaging == nullptr || pStaging->_buffer == VK_NULL_HANDLE )
        {
            destroyBuffer( staging );
            SW_LOG_ERROR( "readbackTexture2D: failed to create the staging buffer (%# bytes)", outLayout._sizeBytes );
            return false;
        }

        VulkanOneShotCommands oneShot{ _pDevice->_device, _pDevice->_commandPool, _pDevice->_graphicsQueue };
        if ( oneShot.isValid() == false )
        {
            destroyBuffer( staging );
            SW_LOG_ERROR( "readbackTexture2D: failed to allocate the one-shot command buffer" );
            return false;
        }
        const VkCommandBuffer cmd = oneShot.get();

        VkImageMemoryBarrier barrier = makeWholeImageBarrier( pRecord->_image, pRecord->_mipLevels );
        barrier.oldLayout            = static_cast<VkImageLayout>( pRecord->_layout );
        barrier.newLayout            = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask        = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask        = VK_ACCESS_TRANSFER_READ_BIT;
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

        const bool bSubmitted = oneShot.endSubmitAndWait();

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
            else
            {
                SW_LOG_ERROR( "readbackTexture2D: failed to map the staging memory (%# bytes)", outLayout._sizeBytes );
            }
            pRecord->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
        }
        else
        {
            SW_LOG_ERROR( "readbackTexture2D: vkQueueSubmit failed" );
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
