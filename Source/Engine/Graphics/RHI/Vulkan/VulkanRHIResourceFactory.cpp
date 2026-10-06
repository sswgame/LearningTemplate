#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/Support/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Support/RHIBufferSize.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanOneShotCommands.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include <vulkan/vulkan.h>

namespace sw
{
    SW_LOG_CALLER( "VulkanRHIResourceFactory" );

    namespace
    {
        /**
         * @brief 2D 색 이미지의 **전체 밉 체인** 배리어 뼈대입니다. 레이아웃과 접근 마스크만 채우면 됩니다.
         * @details `transitionImageLayout` 은 밉 하나만 다루므로 업로드 · 리드백은 전체 밉 배리어를
         *          직접 씁니다. `aspectMask` 나 `layerCount` 를
         *          빠뜨린 새 배리어는 검증 계층이 잡아 주지만, **잡히는 곳이 배리어를 건 자리가 아니라
         *          그 뒤의 전이**라 읽기 나쁩니다. 고정값은 한 곳에 둡니다.
         */
        VkImageMemoryBarrier makeWholeImageBarrier( VkImage image, uint32 mipLevels, uint32 arrayLayers )
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
            barrier.subresourceRange.layerCount     = arrayLayers;
            return barrier;
        }
    } // namespace

    RHIBufferHandle VulkanRHIResourceFactory::createConstantBuffer( uint32 size )
    {
        const uint32          aligned = MathUtil::align( size, constant::kConstantBufferAlignment );
        const uint32          total   = aligned * constant::kMaxFrameCountInFlight;
        const RHIBufferHandle handle  = _pDevice->createVulkanBuffer( total, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr );
        if ( handle != 0 )
        {
            // 렌더 스레드가 드로우마다 이 표를 읽는다. 게임 스레드의 삽입(재배치)이 그와 겹치지 않게 배타 락으로 넣는다.
            std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
            _pDevice->_mapCbSlotSize[handle] = aligned;
        }
        return handle;
    }

    void VulkanRHIResourceFactory::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( buffer == 0 || pData == nullptr || size == 0 )
            return;

        // 이번 프레임 칸에 쓰고, 나머지 칸은 링이 그 칸으로 돌아올 때 채운다(`RHIConstantBufferMirror` — 값이 바뀔 때만 쓰는 머티리얼 버퍼가
        // 세 프레임 중 두 프레임을 옛 값으로 그리던 것). 링 상수버퍼가 아니면(칸 크기가 없다) 버퍼 앞에 그대로 쓴다.
        // 디스크립터는 여기서 손대지 않는다. 드로우 직전 슬롯 세트를 쓸 때(flushSlotSet) 이번 프레임 칸의 오프셋을 넣는다.
        std::shared_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        if ( _pDevice->_mapCbSlotSize.contains( buffer ) == false )
        {
            VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
            void*                                pMapped{ nullptr };
            if ( pRecord != nullptr && pRecord->_memory != VK_NULL_HANDLE &&
                 vkMapMemory( _pDevice->_device, pRecord->_memory, 0, size, 0, &pMapped ) == VK_SUCCESS )
            {
                Memory::copy( pMapped, pData, size );
                vkUnmapMemory( _pDevice->_device, pRecord->_memory );
            }
            return;
        }
        _pDevice->_constantBufferMirror.write( buffer, _pDevice->_currentFrame % constant::kMaxFrameCountInFlight, pData, size,
                                               [this]( RHIBufferHandle target, uint32 slot, const void* pBytes, uint32 byteCount )
        { _pDevice->writeConstantBufferSlot( target, slot, pBytes, byteCount ); } );
    }

    RHIBufferHandle VulkanRHIResourceFactory::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        if ( elementSize == 0 || elementCount == 0 )
            return 0;

        // 32비트 API 다. 담기지 않으면 만들지 않는다(RHIBufferSize 가 세 백엔드의 규칙 하나).
        uint32 totalBytes{ 0 };
        if ( RHIBufferSize::computeStructuredBytes( elementSize, elementCount, totalBytes ) == false )
            return 0;

        constexpr uint32 usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        return _pDevice->createVulkanBuffer( static_cast<uint32>( totalBytes ), usage, nullptr );
    }

    bool VulkanRHIResourceFactory::acquireStructuredUploadStaging( uint64 sizeBytes, uint64& outOffset, VkBuffer& outBuffer )
    {
        const uint32                           slotIndex = _pDevice->_currentFrame;
        VulkanRHIDevice::StructuredUploadSlot& slot      = _pDevice->_arrStructuredUploadSlot[slotIndex];

        // 슬롯이 다시 내 차례가 됐다는 것은 beginFrame 이 그 슬롯의 펜스를 기다렸다는 뜻이다. 오프셋을 되감는다.
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

            // 옛 스테이징은 이번 구간의 앞선 복사가 아직 읽고 있을 수 있다. 펜스 뒤에 놓아준다.
            if ( slot._buffer != VK_NULL_HANDLE )
            {
                VkDevice       dev = _pDevice->_device;
                VkBuffer       buf = slot._buffer;
                VkDeviceMemory mem = slot._memory;
                if ( slot._pMapped != nullptr )
                    vkUnmapMemory( dev, mem );
                RHIMemoryLedger* pLedger = &_pDevice->getMemoryLedger();
                _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, buf, mem, pLedger]()
                {
                    vkDestroyBuffer( dev, buf, nullptr );
                    vkFreeMemory( dev, mem, nullptr );
                    pLedger->recordFree( RHIMemoryKey::makeDeviceObject( mem ) );
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

            VkMemoryRequirements memoryRequirements{};
            vkGetBufferMemoryRequirements( _pDevice->_device, slot._buffer, &memoryRequirements );
            uint32 memoryTypeIndex{ 0 };
            if ( _pDevice->findMemoryType( memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex ) == false )
            {
                vkDestroyBuffer( _pDevice->_device, slot._buffer, nullptr );
                slot._buffer = VK_NULL_HANDLE;
                SW_LOG_ERROR( "acquireStructuredUploadStaging: no host visible memory type" );
                return false;
            }

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize  = memoryRequirements.size;
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
            _pDevice->getMemoryLedger().recordAllocation( RHIMemoryKey::makeDeviceObject( slot._memory ), RHIMemoryKind::Staging, memoryRequirements.size );
        }

        slot._uploadOffset = offset + sizeBytes;
        outOffset          = offset;
        outBuffer          = slot._buffer;
        return true;
    }

    void VulkanRHIResourceFactory::updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource,
                                                                  const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        // 스테이징 슬롯에 쓰고 복사를 프레임 커맨드버퍼에 기록한다. 목적 버퍼를 직접 vkMapMemory 해서 쓰면
        // "GPU 가 직전 프레임을 아직 읽는 중인 메모리를 CPU 가 덮어쓰는" 해저드가 생긴다. 큐 순서가 곧 해저드 해결이고,
        // "바뀐 게 없으면 업로드 생략" 같은 상위 로직도 단일 목적 버퍼 그대로 유효하다.
        VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
        if ( pRecord == nullptr || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0 ||
             pRecord->_buffer == VK_NULL_HANDLE || _pDevice->_device == VK_NULL_HANDLE || _pDevice->_graphicsQueue == VK_NULL_HANDLE )
            return;

        // **조각을 모두 한 스테이징에 모아 배리어 한 쌍 · 복사 한 번으로 끝낸다.** 조각마다 부르면
        // 스테이징 확보와 제출이 그만큼 되풀이된다(DX12 에서 호출당 ~3.3 us).
        // `vkCmdCopyBuffer` 는 영역 배열을 그대로 받으므로 여기서는 나눌 이유가 아예 없다.
        constexpr uint32     kCopyAlignment = 4;
        vector<VkBufferCopy> listRegion;
        listRegion.reserve( regionCount );
        uint32 totalSize = 0;
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( region._size == 0 || region._dstOffset >= pRecord->_size )
                continue;
            totalSize += MathUtil::align( region._size, kCopyAlignment );
        }
        if ( totalSize == 0 )
            return;

        uint64   stagingOffset{ 0 };
        VkBuffer stagingBuffer{ VK_NULL_HANDLE };
        if ( acquireStructuredUploadStaging( totalSize, stagingOffset, stagingBuffer ) == false )
            return;

        const uint8* pBase  = static_cast<const uint8*>( pBaseSource );
        uint8* const pStage = static_cast<uint8*>( _pDevice->_arrStructuredUploadSlot[_pDevice->_currentFrame]._pMapped );
        uint64       cursor{ stagingOffset };
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( region._size == 0 || region._dstOffset >= pRecord->_size )
                continue;

            // 클램프는 **오프셋을 포함해서** 해야 한다(크기만 보면 앞에서부터 쓸 때만 맞는다).
            uint32 copySize = region._size;
            if ( copySize > pRecord->_size - region._dstOffset )
                copySize = static_cast<uint32>( pRecord->_size - region._dstOffset );

            Memory::copy( pStage + cursor, pBase + region._srcOffset, copySize );

            VkBufferCopy vkRegion{};
            vkRegion.srcOffset = cursor;
            vkRegion.dstOffset = region._dstOffset;
            vkRegion.size      = copySize;
            listRegion.push_back( vkRegion );

            cursor += MathUtil::align( region._size, kCopyAlignment );
        }
        if ( listRegion.empty() )
            return;

        // 프레임 안이면 프레임 스트림에 기록한다(제출 순서상 이번 프레임의 패스 리스트보다 앞). 프레임 밖
        // (초기 업로드 · 테스트)이면 일회성 커맨드버퍼로 제출하고 큐가 비기를 기다린다.
        VkCommandBuffer cmd      = ( _pDevice->_bFrameStarted == SW_TRUE ) ? _pDevice->_activeFrameBuffer : VK_NULL_HANDLE;
        const bool      bOneShot = ( cmd == VK_NULL_HANDLE );

        // 프레임 밖이면 일회성 커맨드버퍼를 쓴다. **해제는 소멸자가 하므로** 나중에 이 사이에 검사가
        // 하나 더 생겨도 커맨드 버퍼가 새지 않는다.
        std::optional<VulkanOneShotCommands> oneShot;
        if ( bOneShot )
        {
            if ( _pDevice->_oneShotCommandPool == VK_NULL_HANDLE )
                return;

            oneShot.emplace( _pDevice->_device, _pDevice->_oneShotCommandPool, _pDevice->_oneShotMutex, _pDevice->_graphicsQueue, _pDevice->_queueMutex );
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

        // 앞 프레임의 읽기(셰이더 · 간접 인자 · 컴퓨트 쓰기) 가 끝난 뒤에 복사하고, 복사가 끝난 뒤에 이번
        // 프레임이 읽는다. 상태 추적(_state)은 건드리지 않는다. 보수적인 마스크로 양쪽을 다 덮는다.
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

        vkCmdCopyBuffer( cmd, stagingBuffer, pRecord->_buffer, static_cast<uint32_t>( listRegion.size() ), listRegion.data() );

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = kConsumerAccess;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kConsumerStage, 0, 0, nullptr, 1, &barrier, 0, nullptr );

        if ( oneShot.has_value() && oneShot->endSubmitAndWait() == false )
            SW_LOG_ERROR( "updateStructuredBuffer: vkQueueSubmit failed" );
    }

    RHIBufferHandle VulkanRHIResourceFactory::createIndexBuffer( const void* pData, uint32 sizeBytes, uint32 indexStride )
    {
        // 인덱스 크기는 걸 때(setIndexBuffer) 정한다. 구조버퍼에는 VK_BUFFER_USAGE_INDEX_BUFFER_BIT 가 없어
        // vkCmdBindIndexBuffer 에 거는 것이 용도 위반이므로 인덱스 용도로 따로 만든다.
        (void)indexStride;
        if ( pData == nullptr )
            return 0;
        return _pDevice->createVulkanBuffer( sizeBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, pData );
    }

    RHIBufferHandle VulkanRHIResourceFactory::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        // 인덱스 버퍼와 같은 경로다(디바이스의 `createVulkanBuffer` 가 용도 `_usage` 까지 기록에 남긴다).
        if ( pData == nullptr )
            return 0;
        return _pDevice->createVulkanBuffer( sizeBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, pData );
    }

    void VulkanRHIResourceFactory::destroyBuffer( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;
        // 렌더 스레드의 기록 상태(`_recordingState` 의 묶인 정점 · 인덱스 버퍼)는 여기서 지우지 않는다. 이 함수는 게임 스레드에서도 불리는데 그 값은
        // 렌더 스레드만 쓴다(여기서 쓰면 경쟁이다). 핸들은 세대가 있어 다시 쓰이지 않으므로 지운 핸들은 드로우에서 풀리지 않고,
        // 정점 버퍼는 풀스크린 버퍼로 떨어진다(bindVertexBuffers).
        {
            std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
            _pDevice->_mapCbSlotSize.erase( buffer );
            _pDevice->_constantBufferMirror.forget( buffer );
        }

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

        VkBuffer         buf     = owned._buffer;
        VkDeviceMemory   mem     = owned._memory;
        VkDevice         dev     = _pDevice->_device;
        RHIMemoryLedger* pLedger = &_pDevice->getMemoryLedger();
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, buf, mem, pLedger, buffer]()
        {
            if ( buf != VK_NULL_HANDLE )
                vkDestroyBuffer( dev, buf, nullptr );
            if ( mem != VK_NULL_HANDLE )
                vkFreeMemory( dev, mem, nullptr );
            // 장부는 메모리를 실제로 놓을 때 줄인다(해제 요청 시점이 아니라).
            pLedger->recordFree( RHIMemoryKey::makeBuffer( buffer ) );
        } ),
                                                   _pDevice->_frameFenceCounter + 1 );
    }

    RHITextureHandle VulkanRHIResourceFactory::createTexture2D( const RHITextureDesc& desc )
    {
        if ( _pDevice->_device == nullptr || desc._width == 0 || desc._height == 0 )
            return 0;
        if ( isRhiTextureShapeValid( desc ) == false )
        {
            SW_LOG_ERROR( "createTexture2D: dimension %# with %# slices (%#x%#) is not a valid texture shape", static_cast<uint32>( desc._dimension ),
                          desc._arraySize, desc._width, desc._height );
            return 0;
        }
        const bool bSliced = desc._dimension != RHITextureDimension::Texture2D;

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
        imageInfo.arrayLayers   = desc._arraySize;
        imageInfo.flags         = ( desc._dimension == RHITextureDimension::TextureCube ) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u;
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
        record._arrayLayers   = desc._arraySize;
        record._dimension     = desc._dimension;
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
        viewInfo.viewType                        = ( desc._dimension == RHITextureDimension::TextureCube )    ? VK_IMAGE_VIEW_TYPE_CUBE
                                                 : ( desc._dimension == RHITextureDimension::Texture2DArray ) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                                                                              : VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = format;
        viewInfo.subresourceRange.aspectMask     = aspect;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = imageInfo.mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = desc._arraySize;

        if ( vkCreateImageView( _pDevice->_device, &viewInfo, nullptr, &record._imageView ) != VK_SUCCESS )
        {
            vkDestroyImage( _pDevice->_device, record._image, nullptr );
            vkFreeMemory( _pDevice->_device, record._memory, nullptr );
            SW_LOG_ERROR( "Failed to create VkImageView for Texture2D." );
            return 0;
        }

        if ( desc._bIsDepthStencil != SW_FALSE )
        {
            // 샘플용 뷰는 aspect 가 하나여야 한다(DEPTH|STENCIL 뷰는 디스크립터에 못 쓴다). 그림자맵처럼
            // 깊이를 읽는 패스가 이 뷰로 bindless 등록된다. registerBindlessTexture 참고.
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if ( vkCreateImageView( _pDevice->_device, &viewInfo, nullptr, &record._sampleView ) != VK_SUCCESS )
            {
                record._sampleView = VK_NULL_HANDLE;
                SW_LOG_WARNING( "createTexture2D: depth sample view creation failed — texture cannot be sampled." );
            }
        }

        // 면이 여럿이면 렌더 패스 첨부는 면 하나짜리 2D 뷰여야 한다(배열 · 큐브 뷰는 첨부가 될 수 없다).
        if ( bSliced && ( desc._bIsRenderTarget || desc._bIsDepthStencil ) )
        {
            for ( uint32 slice = 0; slice < desc._arraySize; ++slice )
            {
                VkImageViewCreateInfo sliceInfo           = viewInfo;
                sliceInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
                sliceInfo.subresourceRange.aspectMask     = aspect;
                sliceInfo.subresourceRange.levelCount     = 1;
                sliceInfo.subresourceRange.baseArrayLayer = slice;
                sliceInfo.subresourceRange.layerCount     = 1;
                VkImageView sliceView{ VK_NULL_HANDLE };
                if ( vkCreateImageView( _pDevice->_device, &sliceInfo, nullptr, &sliceView ) != VK_SUCCESS )
                {
                    SW_LOG_ERROR( "createTexture2D: slice view %# creation failed", slice );
                    break;
                }
                record._listSliceView.push_back( sliceView );
            }
        }

        if ( record._bRenderTarget && _pDevice->createOffscreenFramebuffer( record ) == false )
            SW_LOG_WARNING( "createTexture2D: framebuffer creation failed — texture kept without offscreen pass." );

        // GPU 메모리 장부에 올리는 유일한 자리다. 크기는 드라이버가 요구한 할당 크기(정렬 포함)다.
        const RHITextureHandle handle = _pDevice->_gpuTextures.insert( record );
        _pDevice->getMemoryLedger().recordAllocation( RHIMemoryKey::makeTexture( handle ), RHIMemoryLedger::classifyTexture( desc ), allocInfo.allocationSize );
        return handle;
    }

    bool VulkanRHIResourceFactory::uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        VulkanRHIDevice::VulkanTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_image == VK_NULL_HANDLE || _pDevice->_device == VK_NULL_HANDLE ||
             _pDevice->_graphicsQueue == VK_NULL_HANDLE || _pDevice->_oneShotCommandPool == VK_NULL_HANDLE )
            return false;
        if ( pRecord->_bDepthStencil != SW_FALSE )
            return false;
        if ( desc._arraySlice >= pRecord->_arrayLayers )
        {
            SW_LOG_ERROR( "uploadTexture2D: slice %# is out of range (%# slices)", desc._arraySlice, pRecord->_arrayLayers );
            return false;
        }

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

        VulkanOneShotCommands oneShot{ _pDevice->_device, _pDevice->_oneShotCommandPool, _pDevice->_oneShotMutex, _pDevice->_graphicsQueue,
                                       _pDevice->_queueMutex };
        if ( oneShot.isValid() == false )
        {
            destroyBuffer( staging );
            SW_LOG_ERROR( "uploadTexture2D: failed to allocate the one-shot command buffer" );
            return false;
        }
        const VkCommandBuffer cmd = oneShot.get();

        // transitionImageLayout 은 밉 하나만 다루므로 여기서는 전체 밉 체인 배리어를 직접 쓴다.
        VkImageMemoryBarrier barrier = makeWholeImageBarrier( pRecord->_image, pRecord->_mipLevels, pRecord->_arrayLayers );

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
            region.imageSubresource.baseArrayLayer = desc._arraySlice;
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

    bool VulkanRHIResourceFactory::uploadTexture2DRegion( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        VulkanRHIDevice::VulkanTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_image == VK_NULL_HANDLE || _pDevice->_device == VK_NULL_HANDLE ||
             _pDevice->_graphicsQueue == VK_NULL_HANDLE || _pDevice->_oneShotCommandPool == VK_NULL_HANDLE || pRecord->_bDepthStencil != SW_FALSE )
            return false;
        uint32 rowBytes{ 0 };
        if ( validateTextureRegionUpload( static_cast<RHIFormat>( pRecord->_rhiFormat ), pRecord->_width, pRecord->_height, pRecord->_mipLevels,
                                          pRecord->_arrayLayers, desc, rowBytes ) == false )
            return false;

        // 전체 업로드와 같은 길: 빈틈없는 행 그대로 스테이징 → 일회성 커맨드 → 제출하고 기다린다(가끔 · 작게 바뀌는 텍스처용).
        const uint32                         usedBytes = rowBytes * desc._height;
        const RHIBufferHandle                staging   = _pDevice->createVulkanBuffer( usedBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, desc._pData );
        VulkanRHIDevice::VulkanBufferRecord* pStaging  = _pDevice->resolveAllocatedBuffer( staging );
        if ( pStaging == nullptr || pStaging->_buffer == VK_NULL_HANDLE )
        {
            destroyBuffer( staging );
            SW_LOG_ERROR( "uploadTexture2DRegion: failed to create the staging buffer (%# bytes)", usedBytes );
            return false;
        }

        VulkanOneShotCommands oneShot{ _pDevice->_device, _pDevice->_oneShotCommandPool, _pDevice->_oneShotMutex, _pDevice->_graphicsQueue,
                                       _pDevice->_queueMutex };
        if ( oneShot.isValid() == false )
        {
            destroyBuffer( staging );
            SW_LOG_ERROR( "uploadTexture2DRegion: failed to allocate the one-shot command buffer" );
            return false;
        }
        const VkCommandBuffer cmd = oneShot.get();

        // 레코드가 이미지 레이아웃 하나만 들므로 배리어도 이미지 전체다. 구간 밖 픽셀은 지금 레이아웃에서 보존된다(UNDEFINED 였다면 원래 정의되지 않았다).
        VkImageMemoryBarrier barrier = makeWholeImageBarrier( pRecord->_image, pRecord->_mipLevels, pRecord->_arrayLayers );
        barrier.oldLayout            = static_cast<VkImageLayout>( pRecord->_layout );
        barrier.newLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask        = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );

        VkBufferImageCopy region{};
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0; // 0 = 빈틈없는 행(imageExtent.width)
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = desc._mip;
        region.imageSubresource.baseArrayLayer = desc._arraySlice;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = { static_cast<int32>( desc._x ), static_cast<int32>( desc._y ), 0 };
        region.imageExtent                     = { desc._width, desc._height, 1 };
        vkCmdCopyBufferToImage( cmd, pStaging->_buffer, pRecord->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                              0, nullptr, 1, &barrier );

        const bool bSubmitted = oneShot.endSubmitAndWait();
        destroyBuffer( staging );
        if ( bSubmitted == false )
        {
            SW_LOG_ERROR( "uploadTexture2DRegion: vkQueueSubmit failed" );
            return false;
        }
        pRecord->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
        return true;
    }

    RHIFormat VulkanRHIResourceFactory::getTextureFormat( RHITextureHandle texture ) const
    {
        const VulkanRHIDevice::VulkanTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        return pRecord != nullptr ? static_cast<RHIFormat>( pRecord->_rhiFormat ) : RHIFormat::Unknown;
    }

    bool VulkanRHIResourceFactory::readbackTexture2D( RHITextureHandle texture, uint32 mip, uint32 arraySlice, vector<uint8>& outBytes, RHITextureMipSpan& outLayout )
    {
        // **여기의 실패는 모두 소리를 낸다**(형제인 uploadTexture2D 와 같다). 리드백은 오프스크린 렌더 문제가
        // 드러나는 통로라 "false 인데 이유가 없다" 가 곧 긴 추적이 된다.
        VulkanRHIDevice::VulkanTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_image == VK_NULL_HANDLE || _pDevice->_device == VK_NULL_HANDLE ||
             _pDevice->_graphicsQueue == VK_NULL_HANDLE || _pDevice->_oneShotCommandPool == VK_NULL_HANDLE )
        {
            SW_LOG_ERROR( "readbackTexture2D: texture %# or the device is not usable", texture );
            return false;
        }
        if ( pRecord->_bDepthStencil != SW_FALSE || mip >= pRecord->_mipLevels || arraySlice >= pRecord->_arrayLayers )
        {
            SW_LOG_ERROR( "readbackTexture2D: depth-stencil readback is unsupported, or mip %# / slice %# is out of range (%# mips, %# slices)",
                          mip, arraySlice, pRecord->_mipLevels, pRecord->_arrayLayers );
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

        VulkanOneShotCommands oneShot{ _pDevice->_device, _pDevice->_oneShotCommandPool, _pDevice->_oneShotMutex, _pDevice->_graphicsQueue,
                                       _pDevice->_queueMutex };
        if ( oneShot.isValid() == false )
        {
            destroyBuffer( staging );
            SW_LOG_ERROR( "readbackTexture2D: failed to allocate the one-shot command buffer" );
            return false;
        }
        const VkCommandBuffer cmd = oneShot.get();

        VkImageMemoryBarrier barrier = makeWholeImageBarrier( pRecord->_image, pRecord->_mipLevels, pRecord->_arrayLayers );
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
        region.imageSubresource.baseArrayLayer = arraySlice;
        region.imageSubresource.layerCount     = 1;
        region.imageExtent                     = { outLayout._width, outLayout._height, 1 };
        vkCmdCopyImageToBuffer( cmd, pRecord->_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pStaging->_buffer, 1, &region );

        // 읽기 뒤에는 샘플링 레이아웃으로 둔다. 한 번도 안 올린 텍스처(UNDEFINED)도 이제부터는 정의된 레이아웃을 갖는다.
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

    void VulkanRHIResourceFactory::destroyTexture( RHITextureHandle texture )
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
        VkDevice            dev        = _pDevice->_device;
        VkImageView         view       = owned._imageView;
        VkImageView         sampleView = owned._sampleView;
        VkImage             image      = owned._image;
        VkDeviceMemory      mem        = owned._memory;
        vector<VkImageView> listSliceView{ owned._listSliceView };
        RHIMemoryLedger*    pLedger = &_pDevice->getMemoryLedger();
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, view, sampleView, image, mem, listSliceView, pLedger, texture]()
        {
            for ( VkImageView sliceView : listSliceView )
                vkDestroyImageView( dev, sliceView, nullptr );
            if ( view != VK_NULL_HANDLE )
                vkDestroyImageView( dev, view, nullptr );
            if ( sampleView != VK_NULL_HANDLE )
                vkDestroyImageView( dev, sampleView, nullptr );
            if ( image != VK_NULL_HANDLE )
                vkDestroyImage( dev, image, nullptr );
            if ( mem != VK_NULL_HANDLE )
                vkFreeMemory( dev, mem, nullptr );
            // 장부는 메모리를 실제로 놓을 때 줄인다(해제 요청 시점이 아니라).
            pLedger->recordFree( RHIMemoryKey::makeTexture( texture ) );
        } ),
                                                   _pDevice->_frameFenceCounter + 1 );
    }
} // namespace sw
