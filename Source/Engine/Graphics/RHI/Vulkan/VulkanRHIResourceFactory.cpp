#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/IndexFreeList.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Backend/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Backend/RHIBufferSizeUtil.h"
#include "Engine/Graphics/RHI/Backend/RHIShaderRequest.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanOneShotCommands.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIUtil.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#include <vulkan/vulkan.h>

namespace sw
{
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

        /**
         * @brief 버퍼 ↔ 이미지 복사에서 버퍼 오프셋이 맞춰야 하는 단위입니다.
         * @details 사양은 컬러 포맷이면 텍셀 블록 바이트의 배수이자 4 의 배수, 깊이 aspect 면 4 의 배수를 요구한다. 블록 바이트는 1 · 4 · 8 · 12 · 16 이라
         *          둘의 최소공배수가 이 값이다(12 바이트 텍셀은 2 의 거듭제곱이 아니라서 링의 구간 정렬만으로는 맞지 않는다).
         */
        uint64 computeCopyOffsetAlignment( RHIFormat format )
        {
            const uint32 blockBytes = getRHIFormatBlockInfo( format )._blockBytes;
            return ( blockBytes != 0 && blockBytes % 4 == 0 ) ? blockBytes : 4;
        }

        /** @brief @p value 를 @p alignment 의 배수로 올립니다. 정렬이 2 의 거듭제곱이 아니어도 된다(12 바이트 텍셀). */
        uint64 alignUpToMultiple( uint64 value, uint64 alignment )
        {
            return ( ( value + alignment - 1 ) / alignment ) * alignment;
        }

        /** @brief 정점 속성 형식을 VkFormat 으로 옮깁니다. `default:` 를 두지 않는다 — 형식을 더하면 `-Werror=switch` 가 여기를 가리킨다. */
        VkFormat toVkVertexFormat( RHIVertexAttributeFormat format )
        {
            switch ( format )
            {
                case RHIVertexAttributeFormat::Float2:
                    return VK_FORMAT_R32G32_SFLOAT;
                case RHIVertexAttributeFormat::Float3:
                    return VK_FORMAT_R32G32B32_SFLOAT;
                case RHIVertexAttributeFormat::Float4:
                    return VK_FORMAT_R32G32B32A32_SFLOAT;
                case RHIVertexAttributeFormat::Uint1:
                    return VK_FORMAT_R32_UINT;
                case RHIVertexAttributeFormat::Snorm8x4:
                    return VK_FORMAT_R8G8B8A8_SNORM;
            }
            SW_LOG_ASSERT( false, "Unsupported RHIVertexAttributeFormat: %#", static_cast<uint32>( format ) );
            return VK_FORMAT_UNDEFINED;
        }
    } // namespace

    RHIBufferHandle VulkanRHIResourceFactory::createConstantBuffer( uint32 size )
    {
        const uint32          aligned = MathUtil::align( size, constant::render::kConstantBufferAlignment );
        const uint32          total   = aligned * constant::render::kMaxFrameCountInFlight;
        const RHIBufferHandle handle  = _pDevice->createVulkanBuffer( total, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr );
        if ( handle != 0 )
        {
            // 렌더 스레드가 드로우마다 이 표를 읽는다. 게임 스레드의 삽입(재배치)이 그와 겹치지 않게 배타 락으로 넣는다.
            UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
            _pDevice->_mapCbSlotSize[handle] = aligned;
        }
        return handle;
    }

    void VulkanRHIResourceFactory::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        const bool bNothingToUpdate = buffer == 0 || pData == nullptr || size == 0;
        if ( bNothingToUpdate )
            return;

        // 이번 프레임 칸에 쓰고, 나머지 칸은 링이 그 칸으로 돌아올 때 채운다(`RHIConstantBufferMirror` — 값이 바뀔 때만 쓰는 머티리얼 버퍼가
        // 세 프레임 중 두 프레임을 옛 값으로 그리던 것). 링 상수버퍼가 아니면(칸 크기가 없다) 버퍼 앞에 그대로 쓴다.
        // 디스크립터는 여기서 손대지 않는다. 드로우 직전 슬롯 세트를 쓸 때(flushSlotSet) 이번 프레임 칸의 오프셋을 넣는다.
        ScopedSharedLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        if ( _pDevice->_mapCbSlotSize.contains( buffer ) == false )
        {
            VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
            void*                                pMapped{ nullptr };
            // Map 실패는 오류로 남긴다(isVkSuccess) — 조용히 건너뛰면 옛 상수로 그린다.
            const bool bCanMapBufferMemory = pRecord != nullptr && pRecord->_memory != VK_NULL_HANDLE &&
                                             isVkSuccess( vkMapMemory( _pDevice->_device, pRecord->_memory, 0, size, 0, &pMapped ), "vkMapMemory (constant buffer)" ) &&
                                             pMapped != nullptr;
            if ( bCanMapBufferMemory )
            {
                Memory::copy( pMapped, pData, size );
                vkUnmapMemory( _pDevice->_device, pRecord->_memory );
            }
            return;
        }
        _pDevice->_constantBufferMirror.write( buffer, _pDevice->_currentFrame % constant::render::kMaxFrameCountInFlight, pData, size,
                                               [this]( RHIBufferHandle target, uint32 slot, const void* pBytes, uint32 byteCount )
        { _pDevice->writeConstantBufferSlot( target, slot, pBytes, byteCount ); } );
    }

    RHIBufferHandle VulkanRHIResourceFactory::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        if ( elementSize == 0 || elementCount == 0 )
            return 0;

        // 32비트 API 다. 담기지 않으면 만들지 않는다(RHIBufferSizeUtil 가 세 백엔드의 규칙 하나).
        uint32 totalBytes{ 0 };
        if ( RHIBufferSizeUtil::computeStructuredBytes( elementSize, elementCount, totalBytes ) == false )
            return 0;

        // TRANSFER_SRC: 어느 구조 버퍼든 비동기 되읽기(`copyBufferToReadback`)의 원본이 될 수 있다 — IRHICommandList 계약이 원본을 가리지 않는다(다른 백엔드는 복사 원본 제한이 없다).
        constexpr uint32 usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
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

        uint64 offset = MathUtil::align( slot._uploadOffset, static_cast<uint64>( constant::render::kConstantBufferAlignment ) );
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
                RHIMemoryTracker* pTracker = &_pDevice->getMemoryTracker();
                _pDevice->_releaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, buf, mem, pTracker]()
                {
                    vkDestroyBuffer( dev, buf, nullptr );
                    vkFreeMemory( dev, mem, nullptr );
                    pTracker->recordFree( RHIMemoryKey::makeDeviceObject( mem ) );
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
                SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.AcquireStructuredUploadStagingFailedCreateStaging", "acquireStructuredUploadStaging: failed to create the staging buffer (%# bytes)", newCapacity );
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
                SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.AcquireStructuredUploadStagingNoHostVisible", "acquireStructuredUploadStaging: no host visible memory type" );
                return false;
            }

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType                   = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize          = memoryRequirements.size;
            allocInfo.memoryTypeIndex         = memoryTypeIndex;
            const bool bSlotMemorySetupFailed = vkAllocateMemory( _pDevice->_device, &allocInfo, nullptr, &slot._memory ) != VK_SUCCESS ||
                                                vkBindBufferMemory( _pDevice->_device, slot._buffer, slot._memory, 0 ) != VK_SUCCESS ||
                                                vkMapMemory( _pDevice->_device, slot._memory, 0, VK_WHOLE_SIZE, 0, &slot._pMapped ) != VK_SUCCESS;
            if ( bSlotMemorySetupFailed )
            {
                if ( slot._memory != VK_NULL_HANDLE )
                    vkFreeMemory( _pDevice->_device, slot._memory, nullptr );
                vkDestroyBuffer( _pDevice->_device, slot._buffer, nullptr );
                slot             = VulkanRHIDevice::StructuredUploadSlot{};
                slot._resetFence = _pDevice->_frameFenceCounter;
                SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.AcquireStructuredUploadStagingFailedAllocateMap", "acquireStructuredUploadStaging: failed to allocate/map the staging memory" );
                return false;
            }
            slot._capacity = newCapacity;
            _pDevice->getMemoryTracker().recordAllocation( RHIMemoryKey::makeDeviceObject( slot._memory ), RHIMemoryKind::Staging, memoryRequirements.size );
        }

        slot._uploadOffset = offset + sizeBytes;
        outOffset          = offset;
        outBuffer          = slot._buffer;
        return true;
    }

    // 길이 유지(긴 함수 정리 기준): 스테이징 확보 · 복사 · 장벽 · 제출의 순서가 곧 의미라 나누지 않는다.
    void VulkanRHIResourceFactory::updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource,
                                                                  const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        _pDevice->requireNoOpenRenderPassForUpload( "updateStructuredBufferRegions" );
        // 스테이징 슬롯에 쓰고 복사를 프레임 커맨드버퍼에 기록한다. 목적 버퍼를 직접 vkMapMemory 해서 쓰면
        // "GPU 가 직전 프레임을 아직 읽는 중인 메모리를 CPU 가 덮어쓰는" 해저드가 생긴다. 큐 순서가 곧 해저드 해결이고,
        // "바뀐 게 없으면 업로드 생략" 같은 상위 로직도 단일 목적 버퍼 그대로 유효하다.
        VulkanRHIDevice::VulkanBufferRecord* pRecord                    = _pDevice->resolveAllocatedBuffer( buffer );
        const bool                           bInvalidBufferRegionUpload = pRecord == nullptr || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0 ||
                                                pRecord->_buffer == VK_NULL_HANDLE || _pDevice->_device == VK_NULL_HANDLE || _pDevice->_graphicsQueue == VK_NULL_HANDLE;
        if ( bInvalidBufferRegionUpload )
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
            if ( region._sizeBytes == 0 || region._dstOffset >= pRecord->_sizeBytes )
                continue;
            totalSize += MathUtil::align( region._sizeBytes, kCopyAlignment );
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
            if ( region._sizeBytes == 0 || region._dstOffset >= pRecord->_sizeBytes )
                continue;

            // 클램프는 **오프셋을 포함해서** 해야 한다(크기만 보면 앞에서부터 쓸 때만 맞는다).
            uint32 copySize = region._sizeBytes;
            if ( copySize > pRecord->_sizeBytes - region._dstOffset )
                copySize = static_cast<uint32>( pRecord->_sizeBytes - region._dstOffset );

            Memory::copy( pStage + cursor, pBase + region._srcOffset, copySize );

            VkBufferCopy vkRegion{};
            vkRegion.srcOffset = cursor;
            vkRegion.dstOffset = region._dstOffset;
            vkRegion.size      = copySize;
            listRegion.push_back( vkRegion );

            cursor += MathUtil::align( region._sizeBytes, kCopyAlignment );
        }
        if ( listRegion.empty() )
            return;

        // 프레임 안이면 프레임 스트림에 기록한다(제출 순서상 이번 프레임의 패스 리스트보다 앞). 프레임 밖
        // (초기 업로드 · 테스트)이면 일회성 커맨드버퍼로 제출하고 큐가 비기를 기다린다.
        VkCommandBuffer cmd      = ( _pDevice->_bFrameStarted == SW_TRUE ) ? _pDevice->_activeFrameBuffer : VK_NULL_HANDLE;
        const bool      bOneShot = ( cmd == VK_NULL_HANDLE );

        // 프레임 밖이면 일회성 커맨드버퍼를 쓴다. **해제는 소멸자가 하므로** 나중에 이 사이에 검사가
        // 하나 더 생겨도 커맨드 버퍼가 새지 않는다.
        optional<VulkanOneShotCommands> oneShot;
        if ( bOneShot )
        {
            if ( _pDevice->_oneShotCommandPool == VK_NULL_HANDLE )
                return;

            oneShot.emplace( _pDevice->_device, _pDevice->_oneShotCommandPool, _pDevice->_oneShotMutex, _pDevice->_graphicsQueue, _pDevice->_queueMutex );
            if ( oneShot->isValid() == false )
            {
                SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.UpdateStructuredBufferFailedAllocateOne", "updateStructuredBuffer: failed to allocate the one-shot command buffer" );
                return;
            }
            cmd = oneShot->get();
        }
        else
        {
            // 패스 안에는 복사를 기록할 수 없다. 렌더 스레드의 위반은 위 판정(requireNoOpenRenderPassForUpload)이 알리고 닫았다 — 남은 것은 공용 상태와 함께 닫는다.
            _pDevice->_frameStreamContext->endRenderPass();
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
            _pDevice->reportQueueResult( static_cast<int32>( oneShot->getLastResult() ), "updateStructuredBuffer: one-shot submit" );
    }

    RHIBufferHandle VulkanRHIResourceFactory::createIndexBuffer( const void* pData, uint32 sizeBytes )
    {
        // 인덱스 크기는 걸 때(setIndexBuffer) 정한다. 구조버퍼에는 VK_BUFFER_USAGE_INDEX_BUFFER_BIT 가 없어
        // vkCmdBindIndexBuffer 에 거는 것이 용도 위반이므로 인덱스 용도로 따로 만든다.
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

    void VulkanRHIResourceFactory::updateGeometryBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        _pDevice->requireNoOpenRenderPassForUpload( "updateGeometryBufferRegions" );
        const bool bInvalidVertexRegionUpdate = buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0 || _pDevice->_device == VK_NULL_HANDLE;
        if ( bInvalidVertexRegionUpdate )
            return;
        // 정점 버퍼는 HOST_VISIBLE | HOST_COHERENT 메모리다(createVulkanBuffer). 매핑해 CPU 가 직접 쓴다 — 앞 프레임이 읽는 조각이 아님은
        // 부르는 쪽이 지킨다(IRHIResourceFactory::updateGeometryBufferRegions). 정점 · 인덱스 버퍼만 받는다: 링 상수 버퍼처럼 늘 매핑해 둔 메모리를
        // 다시 매핑하면 검증 레이어 오류다.
        constexpr VkBufferUsageFlags         kGeometryUsage  = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        VulkanRHIDevice::VulkanBufferRecord* pRecord         = _pDevice->resolveAllocatedBuffer( buffer );
        const bool                           bGeometryBuffer = pRecord != nullptr && pRecord->_memory != VK_NULL_HANDLE && ( pRecord->_usage & kGeometryUsage ) != 0;
        if ( bGeometryBuffer == false )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.UpdateGeometryBufferRegionsHandleNotGeometry", "updateGeometryBufferRegions: the handle is not a vertex or index buffer" );
            return;
        }
        void* pMapped{ nullptr };
        if ( vkMapMemory( _pDevice->_device, pRecord->_memory, 0, VK_WHOLE_SIZE, 0, &pMapped ) != VK_SUCCESS || pMapped == nullptr )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.UpdateGeometryBufferRegionsFailedMapVertex", "updateGeometryBufferRegions: failed to map the vertex or index buffer memory" );
            return;
        }
        const uint8* pBase = static_cast<const uint8*>( pBaseSource );
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( static_cast<uint64>( region._dstOffset ) + region._sizeBytes > pRecord->_sizeBytes )
            {
                SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.UpdateGeometryBufferRegionsRegionOutsideByte", "updateGeometryBufferRegions: region [%#, +%#) is outside the %#-byte vertex or index buffer; skipped", region._dstOffset, region._sizeBytes, pRecord->_sizeBytes );
                continue;
            }
            Memory::copy( static_cast<uint8*>( pMapped ) + region._dstOffset, pBase + region._srcOffset, region._sizeBytes );
        }
        vkUnmapMemory( _pDevice->_device, pRecord->_memory );
    }

    void VulkanRHIResourceFactory::destroyBuffer( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;
        // 렌더 스레드의 기록 상태(`_recordingState` 의 묶인 정점 · 인덱스 버퍼)는 여기서 지우지 않는다. 이 함수는 게임 스레드에서도 불리는데 그 값은
        // 렌더 스레드만 쓴다(여기서 쓰면 경쟁이다). 핸들은 세대가 있어 다시 쓰이지 않으므로 지운 핸들은 드로우에서 풀리지 않고,
        // 그 드로우는 버리고 알린다(`bindMeshVertexBufferForDraw` · `RHIDrawDiagnostics`).
        {
            UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
            _pDevice->_mapCbSlotSize.erase( buffer );
            _pDevice->_constantBufferMirror.remove( buffer );
        }

        VulkanRHIDevice::VulkanBufferRecord owned;
        if ( _pDevice->_gpuBuffers.take( buffer, owned ) == false )
            return;

        // 아직 등록돼 있으면 인덱스를 반납한다 (unregister 를 안 부른 소유자 정리). 디스크립터는 드로우 시점 슬롯 세트에만
        // 있으므로 되돌릴 것이 없다.
        UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        for ( size_t bufferIndex = 0; bufferIndex < _pDevice->_listBindlessSourceBuffer.size(); ++bufferIndex )
        {
            if ( _pDevice->_listBindlessSourceBuffer[bufferIndex] != buffer )
                continue;
            _pDevice->_listBindlessSourceBuffer[bufferIndex] = RHIBufferHandle{ 0 };
            deferFreeBufferIndex( static_cast<uint32>( bufferIndex ), false );
        }
        for ( size_t bufferIndex = 0; bufferIndex < _pDevice->_listUAVSourceBuffer.size(); ++bufferIndex )
        {
            if ( _pDevice->_listUAVSourceBuffer[bufferIndex] != buffer )
                continue;
            _pDevice->_listUAVSourceBuffer[bufferIndex] = RHIBufferHandle{ 0 };
            deferFreeBufferIndex( static_cast<uint32>( bufferIndex ), true );
        }

        VkBuffer          buf      = owned._buffer;
        VkDeviceMemory    mem      = owned._memory;
        VkDevice          dev      = _pDevice->_device;
        RHIMemoryTracker* pTracker = &_pDevice->getMemoryTracker();
        _pDevice->_releaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, buf, mem, pTracker, buffer]()
        {
            if ( buf != VK_NULL_HANDLE )
                vkDestroyBuffer( dev, buf, nullptr );
            if ( mem != VK_NULL_HANDLE )
                vkFreeMemory( dev, mem, nullptr );
            // 추적기는 메모리를 실제로 놓을 때 줄인다(해제 요청 시점이 아니라).
            pTracker->recordFree( RHIMemoryKey::makeBuffer( buffer ) );
        } ),
                                                   _pDevice->_frameFenceCounter + 1 );
    }

    void VulkanRHIResourceFactory::setTextureDebugName( RHITextureHandle texture, const utf8* pName )
    {
        const VulkanRHIDevice::VulkanTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_image == VK_NULL_HANDLE || pName == nullptr || _pDevice->_instance == VK_NULL_HANDLE )
            return;
        // VK_EXT_debug_utils 가 켜진 인스턴스에서만 함수가 있다(이벤트 표지 vkCmdBeginDebugUtilsLabelEXT 와 같은 조회) — 없으면 이름을 붙이지 않는다.
        // https://docs.vulkan.org/refpages/latest/refpages/source/vkSetDebugUtilsObjectNameEXT.html
        const PFN_vkSetDebugUtilsObjectNameEXT pfnSetName =
            reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>( vkGetInstanceProcAddr( _pDevice->_instance, "vkSetDebugUtilsObjectNameEXT" ) );
        if ( pfnSetName == nullptr )
            return;
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
        // VkImage 는 64 비트에서는 포인터, 32 비트에서는 uint64 다 — 바이트를 그대로 옮겨 두 경우를 함께 받는다.
        uint64 imageHandle = 0;
        Memory::copy( &imageHandle, &pRecord->_image, sizeof( pRecord->_image ) );
        nameInfo.objectHandle = imageHandle;
        nameInfo.pObjectName  = pName;
        (void)pfnSetName( _pDevice->_device, &nameInfo ); // 진단 이름이다 — 실패해도 그리기는 그대로다
    }

    // 길이 유지(긴 함수 정리 기준): 서술 채우기 · 이미지 생성 · 메모리 · 뷰 · 초기 데이터 업로드의 순서가 곧 의미라 나누지 않는다.
    RHITextureHandle VulkanRHIResourceFactory::createTexture2D( const RHITextureDesc& desc )
    {
        const bool bInvalidTextureRequest = _pDevice->_device == nullptr || desc._width == 0 || desc._height == 0;
        if ( bInvalidTextureRequest )
            return 0;
        if ( isRHITextureShapeValid( desc ) == false )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreateTexture2DDimensionSlicesX", "createTexture2D: dimension %# with %# slices (%#x%#) is not a valid texture shape", static_cast<uint32>( desc._dimension ),
                             desc._arraySize, desc._width, desc._height );
            return 0;
        }
        // 이 장치가 샘플링하지 못하는 압축 형식(데스크톱의 ASTC · ETC2)은 드라이버에 넘기지 않는다 — 넘기면 D3D12 는 실패, Vulkan 드라이버는 0 나누기로 죽었다.
        if ( isRHIFormatBlockCompressed( desc._format ) && _pDevice->supportsTextureFormat( desc._format ) == false )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreateTexture2DFormatUnsupported", "createTexture2D: this device cannot sample format %# (%#x%#) - the texture is not created", static_cast<uint32>( desc._format ), desc._width, desc._height );
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

        const VkFormat requested = VulkanRHIUtil::toVulkanTextureFormat( desc._format );
        VkFormat       format    = requested;
        if ( desc._bIsDepthStencil != SW_FALSE || desc._format == sw::RHIFormat::D24_UNORM_S8_UINT )
        {
            if ( _pDevice->_depthFormat == 0 )
            {
                SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreateTexture2DDepthFormatNot", "createTexture2D: depth format not selected." );
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
        record._listBindlessIndex.clear();

        if ( vkCreateImage( _pDevice->_device, &imageInfo, nullptr, &record._image ) != VK_SUCCESS )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.FailedCreateVkImageTexture2D", "Failed to create VkImage for Texture2D." );
            return 0;
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements( _pDevice->_device, record._image, &memRequirements );

        uint32 memoryTypeIndex{ 0 };
        if ( _pDevice->findMemoryType( memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex ) == false )
        {
            vkDestroyImage( _pDevice->_device, record._image, nullptr );
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.FailedFindDeviceLocal", "Failed to find a device local memory type for Texture2D." );
            return 0;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        if ( vkAllocateMemory( _pDevice->_device, &allocInfo, nullptr, &record._memory ) != VK_SUCCESS )
        {
            vkDestroyImage( _pDevice->_device, record._image, nullptr );
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.FailedAllocateMemoryTexture2D", "Failed to allocate memory for Texture2D." );
            return 0;
        }

        if ( isVkSuccess( vkBindImageMemory( _pDevice->_device, record._image, record._memory, 0 ), "vkBindImageMemory (Texture2D)" ) == false )
        {
            vkDestroyImage( _pDevice->_device, record._image, nullptr );
            vkFreeMemory( _pDevice->_device, record._memory, nullptr );
            return 0;
        }

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
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.FailedCreateVkImageViewTexture2D", "Failed to create VkImageView for Texture2D." );
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
                SW_LOG_WARNING_ID( "VulkanRHIResourceFactory.CreateTexture2DDepthSampleView", "createTexture2D: depth sample view creation failed — texture cannot be sampled." );
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
                    SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreateTexture2DSliceViewCreation", "createTexture2D: slice view %# creation failed", slice );
                    break;
                }
                record._listSliceView.push_back( sliceView );
            }
        }

        if ( record._bRenderTarget && _pDevice->createOffscreenFramebuffer( record ) == false )
            SW_LOG_WARNING_ID( "VulkanRHIResourceFactory.CreateTexture2DFramebufferCreationFailed", "createTexture2D: framebuffer creation failed — texture kept without offscreen pass." );

        // GPU 메모리 추적기에 올리는 유일한 자리다. 크기는 드라이버가 요구한 할당 크기(정렬 포함)다.
        const RHITextureHandle handle = _pDevice->_gpuTextures.insert( record );
        _pDevice->getMemoryTracker().recordAllocation( RHIMemoryKey::makeTexture( handle ), RHIMemoryTracker::classifyTexture( desc ), allocInfo.allocationSize );
        return handle;
    }

    // 전송(RHIAsyncUploads U6): 소스를 업로드 링에 복사하고, 전송 작업 하나에 기록해 펜스와 함께 낸 뒤 기다리지 않고 티켓을 준다. 완료는 pollTransfer 가 펜스로 본다.
    // 길이 유지(긴 함수 정리 기준): 검사 · 링 복사 · 기록 · 제출 · 레코드 갱신의 순서가 곧 의미라 나누지 않는다.
    RHITransferSubmit VulkanRHIResourceFactory::submitTextureUpload( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        VulkanRHIDevice::VulkanTextureRecord* pRecord                   = _pDevice->resolveTexture( texture );
        const bool                            bTextureUploadUnavailable = pRecord == nullptr || pRecord->_image == VK_NULL_HANDLE || _pDevice->_device == VK_NULL_HANDLE ||
                                               _pDevice->_graphicsQueue == VK_NULL_HANDLE || _pDevice->_transferCommandPool == VK_NULL_HANDLE || _pDevice->isDeviceLost();
        if ( bTextureUploadUnavailable )
        {
            // 장치를 잃었으면 진입에서 Rejected 다(계약 2) — 잃은 장치의 큐는 제출을 받지 못하고 펜스는 끝나지 않는다.
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.SubmitTextureUploadTextureDeviceNot", "submitTextureUpload: texture %# or the device is not usable (uninitialized or lost)", texture );
            return RHITransferSubmit::rejected();
        }
        if ( pRecord->_bDepthStencil != SW_FALSE )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.SubmitTextureUploadTextureDepthTexture", "submitTextureUpload: texture %# is a depth texture", texture );
            return RHITransferSubmit::rejected();
        }
        if ( desc._arraySlice >= pRecord->_arrayLayers )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.SubmitTextureUploadSliceOutRange", "submitTextureUpload: slice %# is out of range (%# slices)", desc._arraySlice, pRecord->_arrayLayers );
            return RHITransferSubmit::rejected();
        }

        RHITextureMipSpan arrMip[constant::render::kMaxTextureMipCount]{};
        const RHIFormat   format   = static_cast<RHIFormat>( pRecord->_rhiFormat );
        const uint32      mipCount = resolveTextureUploadMips( desc, format, pRecord->_width, pRecord->_height, pRecord->_mipLevels, arrMip, constant::render::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.SubmitTextureUploadUnsupportedFormatNot", "submitTextureUpload: unsupported format or not enough data (%# bytes for %#×%#, %# mips)",
                             desc._sizeBytes, pRecord->_width, pRecord->_height, pRecord->_mipLevels );
            return RHITransferSubmit::rejected();
        }

        // 밉마다 복사 오프셋 정렬에 맞춰 링에 놓는다. 소스는 밉을 빈틈없이 잇기 때문에 R8 의 작은 밉은 4 의 배수가 아닌 곳에서 시작할 수 있다 — 링에서 다시 맞춘다.
        const uint64 copyAlignment = computeCopyOffsetAlignment( format );
        uint64       ringBytes{ 0 };
        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            ringBytes += arrMip[mip]._sizeBytes + copyAlignment; // 정렬로 밀리는 몫까지 넉넉히
        }
        VulkanRHIDevice::VulkanStagingRing& ring = _pDevice->_uploadRing;
        RHIStagingSpan                      span{};
        const RHISubmitResult               allocateResult = _pDevice->allocateStagingSpan( ring, ringBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "submitTextureUpload", span );
        if ( allocateResult != RHISubmitResult::Accepted )
            return RHITransferSubmit::fromUnaccepted( allocateResult );

        // 소스 복사는 전송 락 밖에서 한다(계약 1: 반환 전에 소스를 복사한다).
        VkBufferImageCopy arrRegion[constant::render::kMaxTextureMipCount]{};
        uint64            ringOffset = span._offsetBytes;
        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            const RHITextureMipSpan& mipSpan = arrMip[mip];
            ringOffset                       = alignUpToMultiple( ringOffset, copyAlignment );
            Memory::copy( ring.getSpanMappedBase( span ) + ringOffset, mipSpan._pData, mipSpan._sizeBytes );

            VkBufferImageCopy& region              = arrRegion[mip];
            region.bufferOffset                    = ringOffset;
            region.bufferRowLength                 = 0; // 0 = 빈틈없는 행
            region.bufferImageHeight               = 0;
            region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel       = mipSpan._mip;
            region.imageSubresource.baseArrayLayer = desc._arraySlice;
            region.imageSubresource.layerCount     = 1;
            region.imageExtent                     = { mipSpan._width, mipSpan._height, 1 };
            ringOffset += mipSpan._sizeBytes;
        }

        ScopedLock<mutex>                  transferLock{ _pDevice->_transferMutex };
        VulkanRHIDevice::VulkanTransferJob job{};
        if ( _pDevice->beginTransferJob( job ) == false )
        {
            ring._allocator.free( span );
            return RHITransferSubmit::rejected();
        }
        uint32 layoutBefore{ 0 };
        {
            ScopedLock<mutex> layoutLock{ _pDevice->_imageLayoutMutex };
            layoutBefore = pRecord->_layout;
        }

        // 앞선 모든 큐 작업(이미 낸 프레임 · 전송)의 접근 뒤에 쓴다. transitionImageLayout 은 밉 하나만 다루므로 전체 밉 체인 배리어를 직접 쓴다.
        VkImageMemoryBarrier barrier = makeWholeImageBarrier( pRecord->_image, pRecord->_mipLevels, pRecord->_arrayLayers );
        barrier.oldLayout            = static_cast<VkImageLayout>( layoutBefore );
        barrier.newLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask        = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier( job._commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );

        vkCmdCopyBufferToImage( job._commandBuffer, ring.getSpanBuffer( span ), pRecord->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipCount, arrRegion );

        // 뒤에 낼 프레임의 셰이더 읽기가 이 쓰기를 본다(배리어의 두 번째 범위는 제출 순서상 뒤의 모든 명령이다 — 계약 3).
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier( job._commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &barrier );

        const uint64 serial = _pDevice->submitTransferJob( job, "submitTextureUpload: vkQueueSubmit" );
        if ( serial == 0 )
        {
            ring._allocator.free( span );
            return RHITransferSubmit::rejected();
        }
        ring._allocator.markSubmitted( span, serial );
        // 레이아웃 기록은 제출 순서를 따른다 — 뒤에 기록 · 제출하는 명령은 이 전송 뒤에 실행되므로 지금 바꿔 둔다.
        {
            ScopedLock<mutex> layoutLock{ _pDevice->_imageLayoutMutex };
            pRecord->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
        }
        pRecord->_uploadSerials.recordWrite( serial );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    // 길이 유지(긴 함수 정리 기준): 전체 업로드와 같은 순서(검사 · 링 복사 · 기록 · 제출 · 레코드 갱신)라 나누지 않는다.
    RHITransferSubmit VulkanRHIResourceFactory::submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        VulkanRHIDevice::VulkanTextureRecord* pRecord                         = _pDevice->resolveTexture( texture );
        const bool                            bTextureRegionUploadUnavailable = pRecord == nullptr || pRecord->_image == VK_NULL_HANDLE || _pDevice->_device == VK_NULL_HANDLE ||
                                                     _pDevice->_graphicsQueue == VK_NULL_HANDLE || _pDevice->_transferCommandPool == VK_NULL_HANDLE ||
                                                     pRecord->_bDepthStencil != SW_FALSE || _pDevice->isDeviceLost();
        if ( bTextureRegionUploadUnavailable )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.SubmitTextureRegionUploadTextureMissingDepth", "submitTextureRegionUpload: texture %# is missing, a depth texture, or the device is not usable (uninitialized or lost)", texture );
            return RHITransferSubmit::rejected();
        }
        const RHIFormat format = static_cast<RHIFormat>( pRecord->_rhiFormat );
        uint32          rowBytes{ 0 };
        if ( validateTextureRegionUpload( format, pRecord->_width, pRecord->_height, pRecord->_mipLevels, pRecord->_arrayLayers, desc, rowBytes ) == false )
            return RHITransferSubmit::rejected();

        // 빈틈없는 행 그대로 업로드 링에 놓는다(가끔 · 작게 바뀌는 텍스처 — 글리프 아틀라스).
        const uint64                        usedBytes     = static_cast<uint64>( rowBytes ) * desc._height;
        const uint64                        copyAlignment = computeCopyOffsetAlignment( format );
        VulkanRHIDevice::VulkanStagingRing& ring          = _pDevice->_uploadRing;
        RHIStagingSpan                      span{};
        const RHISubmitResult               allocateResult = _pDevice->allocateStagingSpan( ring, usedBytes + copyAlignment, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "submitTextureRegionUpload", span );
        if ( allocateResult != RHISubmitResult::Accepted )
            return RHITransferSubmit::fromUnaccepted( allocateResult );
        const uint64 ringOffset = alignUpToMultiple( span._offsetBytes, copyAlignment );
        Memory::copy( ring.getSpanMappedBase( span ) + ringOffset, desc._pData, static_cast<size_t>( usedBytes ) );

        ScopedLock<mutex>                  transferLock{ _pDevice->_transferMutex };
        VulkanRHIDevice::VulkanTransferJob job{};
        if ( _pDevice->beginTransferJob( job ) == false )
        {
            ring._allocator.free( span );
            return RHITransferSubmit::rejected();
        }
        uint32 layoutBefore{ 0 };
        {
            ScopedLock<mutex> layoutLock{ _pDevice->_imageLayoutMutex };
            layoutBefore = pRecord->_layout;
        }

        // 레코드가 이미지 레이아웃 하나만 들므로 배리어도 이미지 전체다. 구간 밖 픽셀은 지금 레이아웃에서 보존된다(UNDEFINED 였다면 원래 정의되지 않았다).
        VkImageMemoryBarrier barrier = makeWholeImageBarrier( pRecord->_image, pRecord->_mipLevels, pRecord->_arrayLayers );
        barrier.oldLayout            = static_cast<VkImageLayout>( layoutBefore );
        barrier.newLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask        = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier( job._commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );

        VkBufferImageCopy region{};
        region.bufferOffset                    = ringOffset;
        region.bufferRowLength                 = 0; // 0 = 빈틈없는 행(imageExtent.width)
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = desc._mip;
        region.imageSubresource.baseArrayLayer = desc._arraySlice;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = { static_cast<int32>( desc._x ), static_cast<int32>( desc._y ), 0 };
        region.imageExtent                     = { desc._width, desc._height, 1 };
        vkCmdCopyBufferToImage( job._commandBuffer, ring.getSpanBuffer( span ), pRecord->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier( job._commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                              0, nullptr, 1, &barrier );

        const uint64 serial = _pDevice->submitTransferJob( job, "submitTextureRegionUpload: vkQueueSubmit" );
        if ( serial == 0 )
        {
            ring._allocator.free( span );
            return RHITransferSubmit::rejected();
        }
        ring._allocator.markSubmitted( span, serial );
        {
            ScopedLock<mutex> layoutLock{ _pDevice->_imageLayoutMutex };
            pRecord->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
        }
        pRecord->_uploadSerials.recordWrite( serial );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    RHITransferStatus VulkanRHIResourceFactory::pollTransfer( RHITransferTicket ticket )
    {
        // 판정은 공용 부품이 한다. 아직이면 끝난 펜스를 한 번 걷고 다시 본다 — 프레임을 돌리지 않아도 되풀이하면 끝에 이른다(계약 7, 제출은 이미 큐에 났다).
        const RHITransferStatus status = _pDevice->_transferSerials.classify( ticket );
        if ( status != RHITransferStatus::Pending )
            return status;
        _pDevice->retireFinishedTransfers();
        return _pDevice->_transferSerials.classify( ticket );
    }

    bool VulkanRHIResourceFactory::takeTextureReadback( RHITransferTicket ticket, vector<uint8>& outBytes )
    {
        if ( pollTransfer( ticket ) != RHITransferStatus::Complete )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.TakeTextureReadbackTicketNotCompleted", "takeTextureReadback: ticket %# has not completed", ticket._serial );
            return false;
        }
        VulkanRHIDevice::VulkanPendingReadback pending{};
        if ( _pDevice->_readbackTable.take( ticket._serial, pending ) == false )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.TakeTextureReadbackTicketNotPending", "takeTextureReadback: ticket %# is not a pending texture readback (an upload ticket, or already taken or discarded)", ticket._serial );
            return false;
        }
        // 펜스를 지났고 복사 뒤에 HOST 배리어를 걸었다 — 호스트 일관 메모리라 그대로 읽는다. 읽은 뒤 구간을 돌려준다.
        VulkanRHIDevice::VulkanStagingRing& ring = _pDevice->_readbackRing;
        outBytes.resize( pending._sizeBytes );
        Memory::copy( outBytes.data(), ring.getSpanMappedBase( pending._span ) + pending._dataOffsetBytes, pending._sizeBytes );
        ring._allocator.free( pending._span );
        if ( pending._depth24TexelCount != 0 )
            convertRHIDepth24ToFloat( outBytes.data(), pending._depth24TexelCount );
        return true;
    }

    void VulkanRHIResourceFactory::discardTextureReadback( RHITransferTicket ticket )
    {
        if ( ticket._deviceGeneration != _pDevice->_transferSerials.getGeneration() )
            return;
        VulkanRHIDevice::VulkanPendingReadback pending{};
        if ( _pDevice->_readbackTable.take( ticket._serial, pending ) == false )
            return; // 업로드 티켓이거나 이미 가져감 · 버림이다 — 계약상 아무 일도 하지 않는다
        // 끝나지 않은 복사는 아직 이 구간에 쓴다. 시리얼을 붙여 워터마크가 지난 뒤에 돌아오게 한다(다음 되읽기가 같은 자리를 받지 않는다).
        VulkanRHIDevice::VulkanStagingRing& ring = _pDevice->_readbackRing;
        if ( _pDevice->_transferSerials.isComplete( ticket._serial ) )
            ring._allocator.free( pending._span );
        else
            ring._allocator.markSubmitted( pending._span, ticket._serial );
    }

    RHIFormat VulkanRHIResourceFactory::getTextureFormat( RHITextureHandle texture ) const
    {
        const VulkanRHIDevice::VulkanTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        return pRecord != nullptr ? static_cast<RHIFormat>( pRecord->_rhiFormat ) : RHIFormat::Unknown;
    }

    // 길이 유지(긴 함수 정리 기준): 스테이징 생성 · 전이 · 복사 · 대기 · 매핑의 순서가 곧 의미라 나누지 않는다.
    RHIBufferHandle VulkanRHIResourceFactory::createReadbackBuffer( uint32 elementSize, uint32 elementCount )
    {
        uint32     totalBytes{ 0 };
        const bool bInvalidReadbackRequest = elementSize == 0 || elementCount == 0 || RHIBufferSizeUtil::computeStructuredBytes( elementSize, elementCount, totalBytes ) == false;
        if ( bInvalidReadbackRequest )
            return 0;
        // 모든 Vulkan 버퍼가 HOST_VISIBLE | HOST_COHERENT 메모리다(createVulkanBuffer) — 복사 도착지 용도로 만들면 맵해 읽을 수 있다.
        return _pDevice->createVulkanBuffer( totalBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr );
    }

    bool VulkanRHIResourceFactory::readReadbackBuffer( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination )
    {
        VulkanRHIDevice::VulkanBufferRecord* pRecord     = _pDevice->resolveAllocatedBuffer( readback );
        const bool                           bUnreadable = pRecord == nullptr || pRecord->_buffer == VK_NULL_HANDLE || pDestination == nullptr || sizeBytes == 0 || sizeBytes > pRecord->_sizeBytes;
        if ( bUnreadable )
            return false;
        // 읽기 버퍼는 복사 도착지 용도 하나로만 만든다(createReadbackBuffer). 다른 용도의 버퍼는 매핑되더라도 읽기 버퍼가 아니다. 네 백엔드가 같이 거절한다.
        const bool bNotReadback = pRecord->_usage != VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if ( bNotReadback )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.ReadReadbackBufferBufferNotReadback", "readReadbackBuffer: buffer %# is not a readback buffer", readback );
            return false;
        }
        // 프레임 펜스를 기다린 뒤(해제 콜백)라 GPU 쓰기가 호스트에 보인다.
        void* pMapped{ nullptr };
        if ( vkMapMemory( _pDevice->_device, pRecord->_memory, 0, sizeBytes, 0, &pMapped ) != VK_SUCCESS || pMapped == nullptr )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.ReadReadbackBufferFailedMapReadback", "readReadbackBuffer: failed to map the readback memory (%# bytes)", sizeBytes );
            return false;
        }
        Memory::copy( pDestination, pMapped, sizeBytes );
        vkUnmapMemory( _pDevice->_device, pRecord->_memory );
        return true;
    }

    // 전송(RHIAsyncUploads U6): 되읽기 링의 구간에 복사를 기록해 펜스와 함께 내고 기다리지 않는다. 결과는 takeTextureReadback 이 링에서 복사해 간다.
    // 길이 유지(긴 함수 정리 기준): 검사 · 링 할당 · 기록 · 제출 · 보관의 순서가 곧 의미라 나누지 않는다.
    RHITransferSubmit VulkanRHIResourceFactory::submitTextureReadback( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout )
    {
        // **여기의 실패는 모두 소리를 낸다**(형제인 submitTextureUpload 와 같다). 리드백은 오프스크린 렌더 문제가
        // 드러나는 경로라 "거절인데 이유가 없다" 가 곧 긴 추적이 된다.
        VulkanRHIDevice::VulkanTextureRecord* pRecord                     = _pDevice->resolveTexture( texture );
        const bool                            bTextureReadbackUnavailable = pRecord == nullptr || pRecord->_image == VK_NULL_HANDLE || _pDevice->_device == VK_NULL_HANDLE ||
                                                 _pDevice->_graphicsQueue == VK_NULL_HANDLE || _pDevice->_transferCommandPool == VK_NULL_HANDLE || _pDevice->isDeviceLost();
        if ( bTextureReadbackUnavailable )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.SubmitTextureReadbackTextureDeviceNot", "submitTextureReadback: texture %# or the device is not usable (uninitialized or lost)", texture );
            return RHITransferSubmit::rejected();
        }
        if ( mip >= pRecord->_mipLevels || arraySlice >= pRecord->_arrayLayers )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.SubmitTextureReadbackMipSliceOut", "submitTextureReadback: mip %# / slice %# is out of range (%# mips, %# slices)", mip, arraySlice, pRecord->_mipLevels, pRecord->_arrayLayers );
            return RHITransferSubmit::rejected();
        }
        // 깊이는 깊이 aspect 만 복사한다(텍셀마다 4 바이트). D24 는 아래 24비트가 깊이라 가져갈 때 0..1 float32 로 바꾸고, D32 계열은 이미 float32 다(kRHIDepthReadbackFormat).
        const bool      bDepth       = pRecord->_bDepthStencil != SW_FALSE;
        const RHIFormat layoutFormat = bDepth ? kRHIDepthReadbackFormat : static_cast<RHIFormat>( pRecord->_rhiFormat );
        if ( computeRHITextureMipLayout( layoutFormat, pRecord->_width, pRecord->_height, mip, outLayout ) == false )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.SubmitTextureReadbackUnsupportedFormatX", "submitTextureReadback: unsupported format %# for %#x%# mip %#",
                             pRecord->_rhiFormat, pRecord->_width, pRecord->_height, mip );
            return RHITransferSubmit::rejected();
        }

        const uint64                        copyAlignment = computeCopyOffsetAlignment( layoutFormat );
        VulkanRHIDevice::VulkanStagingRing& ring          = _pDevice->_readbackRing;
        RHIStagingSpan                      span{};
        const RHISubmitResult               allocateResult = _pDevice->allocateStagingSpan( ring, outLayout._sizeBytes + copyAlignment, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "submitTextureReadback", span );
        if ( allocateResult != RHISubmitResult::Accepted )
            return RHITransferSubmit::fromUnaccepted( allocateResult );
        const uint64 ringOffset = alignUpToMultiple( span._offsetBytes, copyAlignment );

        ScopedLock<mutex>                  transferLock{ _pDevice->_transferMutex };
        VulkanRHIDevice::VulkanTransferJob job{};
        if ( _pDevice->beginTransferJob( job ) == false )
        {
            ring._allocator.free( span );
            return RHITransferSubmit::rejected();
        }
        uint32 layoutBeforeValue{ 0 };
        {
            ScopedLock<mutex> layoutLock{ _pDevice->_imageLayoutMutex };
            layoutBeforeValue = pRecord->_layout;
        }

        // 읽기는 요청 시점까지 낸 프레임의 쓰기를 본다(ALL_COMMANDS 배리어 — 계약 3).
        const VkImageLayout  layoutBefore = static_cast<VkImageLayout>( layoutBeforeValue );
        VkImageMemoryBarrier barrier      = makeWholeImageBarrier( pRecord->_image, pRecord->_mipLevels, pRecord->_arrayLayers );
        if ( bDepth )
            barrier.subresourceRange.aspectMask = _pDevice->depthAspectMask();
        barrier.oldLayout     = layoutBefore;
        barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier( job._commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );

        VkBufferImageCopy region{};
        region.bufferOffset                    = ringOffset;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = bDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = mip;
        region.imageSubresource.baseArrayLayer = arraySlice;
        region.imageSubresource.layerCount     = 1;
        region.imageExtent                     = { outLayout._width, outLayout._height, 1 };
        vkCmdCopyImageToBuffer( job._commandBuffer, pRecord->_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ring.getSpanBuffer( span ), 1, &region );

        // 읽기 뒤에는 샘플링 레이아웃으로 둔다. 한 번도 안 올린 텍스처(UNDEFINED)도 이제부터는 정의된 레이아웃을 갖는다.
        // 깊이는 읽기 전 레이아웃으로 되돌린다 — 렌더 패스 · 그림자 샘플링이 추적 레이아웃에서 출발한다(UNDEFINED 였으면 샘플링 레이아웃).
        const VkImageLayout layoutAfter = ( bDepth && layoutBefore != VK_IMAGE_LAYOUT_UNDEFINED ) ? layoutBefore : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.oldLayout               = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout               = layoutAfter;
        barrier.srcAccessMask           = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask           = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier( job._commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );

        // 복사가 쓴 바이트를 호스트가 읽게 한다. 펜스 신호만으로는 호스트 읽기가 두 번째 범위에 들지 않는다.
        VkBufferMemoryBarrier hostBarrier{};
        hostBarrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        hostBarrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostBarrier.dstAccessMask       = VK_ACCESS_HOST_READ_BIT;
        hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.buffer              = ring.getSpanBuffer( span );
        hostBarrier.offset              = ringOffset;
        hostBarrier.size                = outLayout._sizeBytes;
        vkCmdPipelineBarrier( job._commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hostBarrier, 0, nullptr );

        const uint64 serial = _pDevice->submitTransferJob( job, "submitTextureReadback: vkQueueSubmit" );
        if ( serial == 0 )
        {
            ring._allocator.free( span );
            return RHITransferSubmit::rejected();
        }
        {
            ScopedLock<mutex> layoutLock{ _pDevice->_imageLayoutMutex };
            pRecord->_layout = static_cast<uint32>( layoutAfter );
        }
        pRecord->_lastReadbackSerial.store( serial );

        // 구간에는 시리얼을 붙이지 않는다 — 가져가거나 버릴 때까지 결과가 링에 남아야 한다.
        VulkanRHIDevice::VulkanPendingReadback pending{};
        pending._span              = span;
        pending._dataOffsetBytes   = ringOffset;
        pending._sizeBytes         = outLayout._sizeBytes;
        pending._depth24TexelCount = ( bDepth && static_cast<VkFormat>( pRecord->_format ) == VK_FORMAT_D24_UNORM_S8_UINT ) ? outLayout._width * outLayout._height : 0u;
        _pDevice->_readbackTable.add( serial, pending );
        return _pDevice->_transferSerials.makeAccepted( serial );
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
        VkDevice                         dev        = _pDevice->_device;
        VkImageView                      view       = owned._imageView;
        VkImageView                      sampleView = owned._sampleView;
        VkImage                          image      = owned._image;
        VkDeviceMemory                   mem        = owned._memory;
        vector<VkImageView>              listSliceView{ owned._listSliceView };
        RHIMemoryTracker*                pTracker = &_pDevice->getMemoryTracker();
        const RHIResourceReleaseDelegate releaseDelegate =
            SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, view, sampleView, image, mem, listSliceView, pTracker, texture]()
        {
            for ( VkImageView sliceView : listSliceView )
            {
                vkDestroyImageView( dev, sliceView, nullptr );
            }
            if ( view != VK_NULL_HANDLE )
                vkDestroyImageView( dev, view, nullptr );
            if ( sampleView != VK_NULL_HANDLE )
                vkDestroyImageView( dev, sampleView, nullptr );
            if ( image != VK_NULL_HANDLE )
                vkDestroyImage( dev, image, nullptr );
            if ( mem != VK_NULL_HANDLE )
                vkFreeMemory( dev, mem, nullptr );
            // 추적기는 메모리를 실제로 놓을 때 줄인다(해제 요청 시점이 아니라).
            pTracker->recordFree( RHIMemoryKey::makeTexture( texture ) );
        } );

        // 전송 중인 텍스처의 해제는 전송이 끝난 뒤로 미룬다(전송 계약 6). 끝나면 그때의 다음 프레임 펜스까지 한 번 더 미뤄 이 텍스처를 그린 프레임도 지나게 한다.
        // 지금은 전송과 프레임이 같은 큐라 다음 프레임 펜스도 앞서 낸 전송을 덮지만, 해제가 그 큐 공유에 기대지 않게 전송 완료를 직접 본다(DX12 와 같은 규칙).
        const uint64 lastWriteSerial     = owned._uploadSerials._lastSerial.load();
        const uint64 lastReadSerial      = owned._lastReadbackSerial.load();
        const uint64 lastSerial          = lastWriteSerial > lastReadSerial ? lastWriteSerial : lastReadSerial;
        const bool   bUnfinishedTransfer = lastSerial != 0 && _pDevice->_transferSerials.isComplete( lastSerial ) == false;
        if ( bUnfinishedTransfer )
        {
            VulkanRHIDevice* pDevice = _pDevice;
            _pDevice->_transferReleaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pDevice, releaseDelegate]()
            { pDevice->_releaseQueue.enqueueGPURelease( releaseDelegate, pDevice->_frameFenceCounter + 1 ); } ),
                                                               lastSerial );
            return;
        }
        _pDevice->_releaseQueue.enqueueGPURelease( releaseDelegate, _pDevice->_frameFenceCounter + 1 );
    }

    bool VulkanRHIResourceFactory::isTextureReadyToSample( RHITextureHandle texture )
    {
        const VulkanRHIDevice::VulkanTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        return pRecord != nullptr && _pDevice->_transferSerials.isFirstUploadComplete( pRecord->_uploadSerials );
    }

    RHIDescriptorIndex VulkanRHIResourceFactory::registerBindlessTexture( RHITextureHandle texture )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessTexture" );
        const bool bCannotRegisterTexture = texture == 0 || _pDevice->_textureSet == VK_NULL_HANDLE || _pDevice->_defaultSampler == VK_NULL_HANDLE;
        if ( bCannotRegisterTexture )
            return kInvalidDescriptorIndex;

        VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( texture );
        if ( pResolved == nullptr || pResolved->_imageView == VK_NULL_HANDLE )
            return kInvalidDescriptorIndex;
        // 텍스처 배열 세트(set 1 binding 0)는 sampler2D[] 다 — 배열 · 큐브 뷰는 넣을 수 없다(RHITextureDimension 참고).
        if ( pResolved->_dimension != RHITextureDimension::Texture2D )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.RegisterBindlessTextureTextureTextureOnly", "registerBindlessTexture: texture %# is a %# texture — only Texture2D can be bound to the bindless texture set yet", texture,
                             static_cast<uint32>( pResolved->_dimension ) );
            return kInvalidDescriptorIndex;
        }

        // 깊이 텍스처는 DEPTH|STENCIL 두 aspect 뷰로는 샘플 디스크립터를 못 만든다. DEPTH 단일 aspect 뷰
        // (_sampleView) 를 쓰고, 샘플 시점 레이아웃(prepareTextureForShaderRead 가 옮기는
        // DEPTH_STENCIL_READ_ONLY_OPTIMAL) 을 디스크립터에도 같이 적는다. 주의: 여기서 거부하면 g_ShadowMapIndex 가
        // INVALID 가 되고 bindless 배열 범위 밖 읽기가 0 을 반환해, Vulkan 만 모든 픽셀이 "완전 그림자"(x0.56) 로
        // 어두워진다(검증 에러 없음, 큐브는 다 보인다).
        const bool        bDepth       = pResolved->_bDepthStencil != SW_FALSE;
        const VkImageView sampleView   = bDepth ? pResolved->_sampleView : pResolved->_imageView;
        const uint32      sampleLayout = static_cast<uint32>( bDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                                     : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
        if ( sampleView == VK_NULL_HANDLE )
            return kInvalidDescriptorIndex;

        VulkanRHIDevice::VulkanTextureRecord& record = *pResolved;
        // 등록마다 새 번호다(DX11 · DX12 · GL 과 같다). 같은 텍스처의 번호를 나눠 주면 한쪽의 해제(에디터의 ImGui 등록)가 다른 쪽(엔진)이 쓰는 번호를 놓는다.
        // 첫 업로드가 끝나지 않은 텍스처는 등록하지 않는다(소비자는 pollTransfer 가 Complete 를 준 뒤에 등록한다 — RHITextureUploadSerials). 뒤의 구간 갱신은 막지 않는다.
        if ( _pDevice->_transferSerials.isTextureReadyToRegister( record._uploadSerials, texture ) == false )
            return kInvalidDescriptorIndex;

        UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex descriptorIndex = _pDevice->_freeTexture.reserve( _pDevice->_listTextureUsed );
        if ( descriptorIndex >= _pDevice->kBindlessTextureCount )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.BindlessTextureTableFull", "Bindless texture table full." );
            return kInvalidDescriptorIndex;
        }

        _pDevice->writeBindlessTextureSlot( descriptorIndex, sampleView, sampleLayout );
        if ( descriptorIndex >= _pDevice->_listTextureUsed.size() )
            _pDevice->_listTextureUsed.resize( descriptorIndex + 1, 0 );
        _pDevice->_listTextureUsed[descriptorIndex] = 1;
        record._listBindlessIndex.push_back( descriptorIndex );
        return descriptorIndex;
    }

    RHIDescriptorIndex VulkanRHIResourceFactory::registerBindlessBufferSRV( RHIBufferHandle buffer )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessBufferSRV" );
        if ( _pDevice->resolveAllocatedBuffer( buffer ) == nullptr )
            return kInvalidDescriptorIndex;

        // 버퍼는 인덱스 → 핸들 표에만 올린다. 실제 VkBuffer · 오프셋은 슬롯 세트를 쓰는 순간(flushSlotSet)에 푼다.
        // 링 상수버퍼의 프레임 오프셋도 그때 더한다.
        UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        return _pDevice->_freeBindless.allocate( _pDevice->_listBindlessSourceBuffer, buffer );
    }

    void VulkanRHIResourceFactory::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        _pDevice->requireRegistryMutableNow( "unregisterBindlessResource" );
        UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        // 버퍼가 소유하지 않는 인덱스(텍스처 SRV 인덱스가 잘못 넘어왔거나 이중 해제)를 프리리스트에
        // 넣으면 다음 registerBindlessBufferSRV 가 살아 있는 다른 버퍼의 슬롯을 덮어쓴다. 실제로
        // 트랜지언트 텍스처 인덱스 0 · 1 · 2 가 여기로 와서 패스 CB 슬롯 2 에 인스턴스 버퍼가 들어갔다.
        if ( index >= _pDevice->_listBindlessSourceBuffer.size() || _pDevice->_listBindlessSourceBuffer[index] == 0 )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.BindlessBufferIndexNot", "Bindless buffer index %# is not owned by any buffer; ignoring the release.", index );
            return;
        }
        _pDevice->_listBindlessSourceBuffer[index] = RHIBufferHandle{ 0 };
        deferFreeBufferIndex( index, false );
    }

    void VulkanRHIResourceFactory::deferFreeBufferIndex( RHIDescriptorIndex index, bool bUAV )
    {
        // 인덱스는 이 프레임의 커맨드가 끝난 뒤에 재사용한다. 같은 프레임에 등록된 새 버퍼가 아직 실행 중인
        // 세트가 가리키던 자리를 받지 않도록 한다(언리얼의 지연 해제와 같다).
        VulkanRHIDevice* pDevice = _pDevice;
        _pDevice->_releaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pDevice, index, bUAV]()
        {
            UniqueLock<shared_mutex> lock{ pDevice->_bindlessMutex };
            ( bUAV ? pDevice->_freeUAV : pDevice->_freeBindless ).push( index );
        } ),
                                                   _pDevice->_frameFenceCounter + 1 );
    }

    void VulkanRHIResourceFactory::unregisterBindlessTexture( RHIDescriptorIndex index )
    {
        _pDevice->requireRegistryMutableNow( "unregisterBindlessTexture" );
        if ( index == kInvalidDescriptorIndex )
            return;
        // 텍스처 레코드가 자기 번호들을 들고 있으므로(destroyTexture 가 그걸로 정리한다) 레코드 쪽에서도
        // 같이 지워야 나중의 destroyTexture 가 같은 번호를 두 번 반납하지 않는다.
        bool bFound = false;
        _pDevice->_gpuTextures.forEach( [this, index, &bFound]( VulkanRHIDevice::VulkanTextureRecord& record )
        {
            const auto it = std::find( record._listBindlessIndex.begin(), record._listBindlessIndex.end(), index );
            if ( it == record._listBindlessIndex.end() )
                return;
            record._listBindlessIndex.erase( it );
            UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
            releaseTextureBindlessIndex( index );
            bFound = true;
        } );
        if ( bFound == false )
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.BindlessTextureIndexNot", "Bindless texture index %# is not owned by any texture; ignoring the release.", index );
    }

    void VulkanRHIResourceFactory::releaseTextureBindlessSlot( VulkanRHIDevice::VulkanTextureRecord& record )
    {
        if ( record._listBindlessIndex.empty() )
            return;
        UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        for ( const RHIDescriptorIndex index : record._listBindlessIndex )
        {
            releaseTextureBindlessIndex( index );
        }
        record._listBindlessIndex.clear();
    }

    void VulkanRHIResourceFactory::releaseTextureBindlessIndex( RHIDescriptorIndex index )
    {
        if ( index < _pDevice->_listTextureUsed.size() && _pDevice->_listTextureUsed[index] != 0 )
        {
            // 원소를 더미로 되돌리는 것도, 인덱스 재사용도 GPU 펜스 뒤에 한다. update-after-bind 라도 실행 중인
            // 커맨드버퍼가 쓰는 디스크립터를 덮어쓰는 것은 정의되지 않은 동작이고, 같은 프레임에 새 텍스처가
            // 이 인덱스를 받으면 이전 프레임이 새 텍스처를 샘플한다. 이미지 자체도 같은 펜스 뒤에 파괴된다(destroyTexture).
            _pDevice->_listTextureUsed[index] = 2; // 2 = 해제 대기 (등록 불가, 프리리스트에도 아직 없음)
            VulkanRHIDevice* pDevice          = _pDevice;
            _pDevice->_releaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pDevice, index]()
            {
                UniqueLock<shared_mutex> lock{ pDevice->_bindlessMutex };
                pDevice->writeBindlessTextureSlot( index, pDevice->_bindlessDummyView, static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ) );
                pDevice->_freeTexture.release( pDevice->_listTextureUsed, index, uint8{ 0 } );
            } ),
                                                       _pDevice->_frameFenceCounter + 1 );
        }
    }

    RHIDescriptorIndex VulkanRHIResourceFactory::registerBindlessBufferUAV( RHIBufferHandle buffer )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessBufferUAV" );
        if ( _pDevice->resolveAllocatedBuffer( buffer ) == nullptr )
            return kInvalidDescriptorIndex;
        UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex index = _pDevice->_freeUAV.allocate( _pDevice->_listUAVSourceBuffer, buffer );
        if ( index >= _pDevice->_listUAVSourceTexture.size() )
            _pDevice->_listUAVSourceTexture.resize( index + 1, RHITextureHandle{ 0 } );
        _pDevice->_listUAVSourceTexture[index] = RHITextureHandle{ 0 };
        return index;
    }

    RHIDescriptorIndex VulkanRHIResourceFactory::registerBindlessTextureUAV( RHITextureHandle texture )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessTextureUAV" );
        if ( texture == 0 || _pDevice->_textureSet == VK_NULL_HANDLE )
            return kInvalidDescriptorIndex;
        VulkanRHIDevice::VulkanTextureRecord* pResolved                 = _pDevice->resolveTexture( texture );
        const bool                            bTextureNotStorageCapable = pResolved == nullptr || pResolved->_imageView == VK_NULL_HANDLE || pResolved->_bDepthStencil != SW_FALSE;
        if ( bTextureNotStorageCapable )
            return kInvalidDescriptorIndex;

        // 버퍼 UAV 와 같은 인덱스 공간이다. 인덱스가 곧 RW 텍스처 배열(set 1 binding 3)의 원소다.
        UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex index = _pDevice->_freeUAV.allocate( _pDevice->_listUAVSourceBuffer, RHIBufferHandle{ 0 } );
        if ( index >= _pDevice->kBindlessStorageImageCount )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.RWTextureArrayFull", "The RW texture array (%#) is full.", _pDevice->kBindlessStorageImageCount );
            _pDevice->_freeUAV.release( _pDevice->_listUAVSourceBuffer, index, RHIBufferHandle{ 0 } );
            return kInvalidDescriptorIndex;
        }
        if ( index >= _pDevice->_listUAVSourceTexture.size() )
            _pDevice->_listUAVSourceTexture.resize( index + 1, RHITextureHandle{ 0 } );
        _pDevice->_listUAVSourceTexture[index] = texture;
        _pDevice->writeBindlessStorageImageSlot( index, pResolved->_imageView );
        return index;
    }

    void VulkanRHIResourceFactory::unregisterBindlessUAV( RHIDescriptorIndex index )
    {
        _pDevice->requireRegistryMutableNow( "unregisterBindlessUAV" );
        UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const bool               bBuffer  = index < _pDevice->_listUAVSourceBuffer.size() && _pDevice->_listUAVSourceBuffer[index] != 0;
        const bool               bTexture = index < _pDevice->_listUAVSourceTexture.size() && _pDevice->_listUAVSourceTexture[index] != 0;
        if ( bBuffer == false && bTexture == false )
            return;
        if ( bBuffer )
            _pDevice->_listUAVSourceBuffer[index] = RHIBufferHandle{ 0 };
        if ( bTexture )
            _pDevice->_listUAVSourceTexture[index] = RHITextureHandle{ 0 };
        deferFreeBufferIndex( index, true );
    }

    // 길이 유지(긴 함수 정리 기준): 파이프라인 상태 서술을 단계 순서로 채운 뒤 만드는 선형 목록이라 나누지 않는다.
    RHIPipelineStateHandle VulkanRHIResourceFactory::createPipelineStateNative( const RHIPipelineStateDesc& desc )
    {
        // 와이어프레임 채우기는 디바이스 기능 fillModeNonSolid 가 있어야 한다. 없으면 솔리드로 바꾸지 않고 셰이더 모듈을 만들기 전에 거절한다(아래 래스터 상태 주석).
        const RHIRenderState& renderState = desc._renderState;
        if ( renderState._fillMode == RHIFillMode::Wireframe && _pDevice->_bFillModeNonSolid == 0 )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreatePipelineStateWireframeFillNeeds", "createPipelineState: wireframe fill needs the fillModeNonSolid device feature ('%#')", desc._vertexShaderPath );
            return 0;
        }
        // 깊이 클립을 끄는 것(depthClampEnable)도 장치 기능이다. 없으면 켠 채로 그리지 않고 거절한다 — 다른 그림이 통과하면 안 된다.
        if ( renderState._bEnableDepthClip == SW_FALSE && _pDevice->_bDepthClamp == 0 )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreatePipelineStateDepthClampNeeds", "createPipelineState: turning depth clip off needs the depthClamp device feature ('%#')", desc._vertexShaderPath );
            return 0;
        }
        // 서술체 해석(진입점 기본값 · define · 깊이 전용 판정 · RT 수)은 RHIShaderRequest 하나가 한다. 백엔드는 받기만 한다.
        // 백엔드가 desc 를 직접 읽으면 define(SW_FORWARD · 머티리얼 퍼뮤테이션 · SW_VIEWMODE_UNLIT)을 빠뜨려도 조용히 어긋난다.
        const RHIGraphicsShaderRequest request         = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::SPIRV_Vulkan );
        const ShaderCompileDesc&       vsDesc          = request._vertex;
        const ShaderCompileDesc&       psDesc          = request._pixel;
        const bool                     bHasPixelShader = request._bHasPixelShader != SW_FALSE;
        ShaderCompileResult            vsResult{};
        ShaderCompileResult            psResult{};
        if ( RHIShaderRequest::compileGraphics( request, vsResult, psResult ) == false )
        {
            SW_LOG_WARNING_ID( "VulkanRHIResourceFactory.CreatePipelineStateShaderCompileFailed", "createPipelineState: shader compile failed (vs=%# ps=%#)",
                               vsResult._bSuccess, psResult._bSuccess );
            return 0;
        }

        if ( _pDevice->_pipelineLayout == VK_NULL_HANDLE )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreatePipelineStatePipelineLayoutNull", "createPipelineState: pipeline layout is null" );
            return 0;
        }

        VkShaderModuleCreateInfo vsInfo{};
        vsInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vsInfo.codeSize = vsResult._bytecode.size();
        vsInfo.pCode    = reinterpret_cast<const uint32*>( vsResult._bytecode.data() );
        // 모듈을 못 만들면 PSO 도 만들지 않는다(핸들 0). 결과를 보지 않으면 빈 모듈로 파이프라인을 만들려다 검증 오류 · 드라이버 크래시가 된다.
        VkShaderModule vertShaderModule{ VK_NULL_HANDLE };
        if ( vkCreateShaderModule( _pDevice->_device, &vsInfo, nullptr, &vertShaderModule ) != VK_SUCCESS || vertShaderModule == VK_NULL_HANDLE )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreatePipelineStateVkCreateShaderModuleFailedVertex", "createPipelineState: vkCreateShaderModule failed for the vertex shader '%#'", desc._vertexShaderPath );
            return 0;
        }

        VkShaderModule fragShaderModule{ VK_NULL_HANDLE };
        if ( bHasPixelShader )
        {
            VkShaderModuleCreateInfo psInfo{};
            psInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            psInfo.codeSize = psResult._bytecode.size();
            psInfo.pCode    = reinterpret_cast<const uint32*>( psResult._bytecode.data() );
            if ( vkCreateShaderModule( _pDevice->_device, &psInfo, nullptr, &fragShaderModule ) != VK_SUCCESS || fragShaderModule == VK_NULL_HANDLE )
            {
                vkDestroyShaderModule( _pDevice->_device, vertShaderModule, nullptr );
                SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreatePipelineStateVkCreateShaderModuleFailedPixel", "createPipelineState: vkCreateShaderModule failed for the pixel shader '%#'", desc._pixelShaderPath );
                return 0;
            }
        }

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        // 컴파일에 쓴 진입점과 같은 이름이어야 한다. "VSMain" 으로 박으면 파이프라인 XML 이 다른 진입점을 쓰는 순간
        // Vulkan 만 파이프라인 생성에 실패한다(desc 는 이 함수가 끝날 때까지 살아 있다).
        vertShaderStageInfo.pName = vsDesc._entryPoint.c_str();

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        if ( bHasPixelShader )
        {
            fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragShaderStageInfo.module = fragShaderModule;
            fragShaderStageInfo.pName  = psDesc._entryPoint.c_str();
        }

        // 선택 스테이지(지오메트리 · 헐 · 도메인). 진입점 글은 요청이 이 함수 끝까지 들고 있다.
        ShaderCompileResult             gsResult{};
        ShaderCompileResult             hsResult{};
        ShaderCompileResult             dsResult{};
        const bool                      bOptionalUnsupported = ( request._bHasGeometryShader != SW_FALSE && _pDevice->_bGeometryShader == 0 ) || ( request._bHasTessellation != SW_FALSE && _pDevice->_bTessellationShader == 0 );
        VkPipelineShaderStageCreateInfo arrShaderStage[5]{};
        uint32                          shaderStageCount{ 0 };
        arrShaderStage[shaderStageCount++] = vertShaderStageInfo;
        if ( bHasPixelShader )
            arrShaderStage[shaderStageCount++] = fragShaderStageInfo;
        VkShaderModule arrOptionalModule[3]{ VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
        uint32         optionalModuleCount{ 0 };
        const auto     destroyShaderModules = [&]()
        {
            vkDestroyShaderModule( _pDevice->_device, vertShaderModule, nullptr );
            if ( fragShaderModule != VK_NULL_HANDLE )
                vkDestroyShaderModule( _pDevice->_device, fragShaderModule, nullptr );
            for ( uint32 moduleIndex = 0; moduleIndex < optionalModuleCount; ++moduleIndex )
            {
                vkDestroyShaderModule( _pDevice->_device, arrOptionalModule[moduleIndex], nullptr );
            }
        };
        const auto addOptionalStage = [&]( VkShaderStageFlagBits stage, const ShaderCompileResult& result, const ShaderCompileDesc& stageDesc )
        {
            VkShaderModuleCreateInfo moduleInfo{};
            moduleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            moduleInfo.codeSize = result._bytecode.size();
            moduleInfo.pCode    = reinterpret_cast<const uint32*>( result._bytecode.data() );
            VkShaderModule shaderModule{ VK_NULL_HANDLE };
            if ( vkCreateShaderModule( _pDevice->_device, &moduleInfo, nullptr, &shaderModule ) != VK_SUCCESS )
                return false;
            arrOptionalModule[optionalModuleCount++]   = shaderModule;
            VkPipelineShaderStageCreateInfo& stageInfo = arrShaderStage[shaderStageCount++];
            stageInfo.sType                            = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage                            = stage;
            stageInfo.module                           = shaderModule;
            stageInfo.pName                            = stageDesc._entryPoint.c_str();
            return true;
        };
        bool bOptionalReady = bOptionalUnsupported == false && RHIShaderRequest::compileOptionalStages( request, gsResult, hsResult, dsResult );
        if ( bOptionalReady && request._bHasGeometryShader != SW_FALSE )
            bOptionalReady = addOptionalStage( VK_SHADER_STAGE_GEOMETRY_BIT, gsResult, request._geometry );
        if ( bOptionalReady && request._bHasTessellation != SW_FALSE )
            bOptionalReady = addOptionalStage( VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, hsResult, request._hull ) && addOptionalStage( VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, dsResult, request._domain );
        if ( bOptionalReady == false )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreatePipelineStateOptionalStageFailed", "createPipelineState: optional stage unsupported by the device or failed to compile (%#)", desc._vertexShaderPath.c_str() );
            destroyShaderModules();
            return 0;
        }

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        // 정점 속성은 **공용 표**(메시 정점 형식의 표 — constant::render::getMeshVertexAttributes)에서 만든다. DX11 · DX12 · GL 과 같은 표다. 셰이더가 읽지 않는 속성(컴파일러가
        // 뗀 입력)은 빼고 건다 — 걸어 두면 검증 레이어가 "not consumed" 를 PSO 마다 낸다. 바인딩도 읽는 슬롯만 선언한다 — 정점 입력이 없는 셰이더는
        // 바인딩이 없어 정점 버퍼 없이 그린다. stride 는 슬롯의 원소 크기(위치 · 인스턴스 슬롯 · 형식의 속성), 인스턴스 슬롯 스트림만 인스턴스 스텝이다.
        const uint32                    consumedMask    = RHIShaderRequest::computeConsumedVertexAttributeMask( vsResult._bytecode, ShaderTargetFormat::SPIRV_Vulkan );
        const uint32                    vertexSlotMask  = RHIShaderRequest::toVertexSlotMask( consumedMask );
        const RHIVertexAttribute* const pAttributeTable = constant::render::getMeshVertexAttributes( desc._meshVertexFormat );
        VkVertexInputBindingDescription arrBindingDescription[constant::render::kVertexBufferSlotCount]{};
        uint32                          bindingCount{ 0 };
        for ( uint32 slot = 0; slot < constant::render::kVertexBufferSlotCount; ++slot )
        {
            if ( ( vertexSlotMask & ( 1u << slot ) ) == 0 )
                continue;
            const VkVertexInputRate inputRate     = slot == constant::render::kInstanceSlotStreamSlot ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
            arrBindingDescription[bindingCount++] = VkVertexInputBindingDescription{ slot, constant::render::getVertexSlotStride( desc._meshVertexFormat, slot ), inputRate };
        }
        VkVertexInputAttributeDescription arrAttributeDescription[constant::render::kVertexAttributeCount]{};
        uint32                            attributeCount{ 0 };
        for ( uint32 attributeIndex = 0; attributeIndex < constant::render::kVertexAttributeCount; ++attributeIndex )
        {
            if ( ( consumedMask & ( 1u << attributeIndex ) ) == 0 )
                continue;
            const RHIVertexAttribute&          attribute   = pAttributeTable[attributeIndex];
            VkVertexInputAttributeDescription& description = arrAttributeDescription[attributeCount++];
            description.binding                            = attribute._inputSlot;
            description.location                           = attribute._location;
            description.format                             = toVkVertexFormat( attribute._format );
            description.offset                             = attribute._byteOffset;
        }

        vertexInputInfo.vertexBindingDescriptionCount   = bindingCount;
        vertexInputInfo.pVertexBindingDescriptions      = arrBindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = attributeCount;
        vertexInputInfo.pVertexAttributeDescriptions    = arrAttributeDescription;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology               = request._bHasTessellation != SW_FALSE ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable        = renderState._bEnableDepthClip != SW_FALSE ? VK_FALSE : VK_TRUE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        // 기능을 못 켠 디바이스에서 LINE 을 요청하면 솔리드로 바꾸지 않고 거절한다 — 다른 그림이 와이어프레임으로 통과하면 안 된다.
        // 렌더러는 능력(RHICapabilities::_bWireframeFill)을 보고 와이어프레임 보기 모드를 받지 않으므로 여기 닿으면 결함이다.
        const bool bUseWireframe = renderState._fillMode == RHIFillMode::Wireframe;
        rasterizer.polygonMode   = bUseWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth     = 1.0f;
        rasterizer.cullMode      = ( renderState._cullMode == RHICullMode::Front ) ? VK_CULL_MODE_FRONT_BIT : ( ( renderState._cullMode == RHICullMode::Back ) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE );
        rasterizer.frontFace     = renderState._frontFace == RHIFrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
        // 깊이 바이어스 — 다른 백엔드와 같은 상수 · 기울기 두 칸이다(RHIFixedRenderState::_depthBias). LINE 다각형 모드에도 걸린다.
        rasterizer.depthBiasEnable         = ( renderState._depthBias != 0 || renderState._slopeScaledDepthBias != 0.0f ) ? VK_TRUE : VK_FALSE;
        rasterizer.depthBiasConstantFactor = static_cast<float32>( renderState._depthBias );
        rasterizer.depthBiasSlopeFactor    = renderState._slopeScaledDepthBias;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable  = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        const uint32 numRT      = request._numRenderTargets;
        const uint32 blendCount = ( numRT > kMaxColorAttachments ) ? kMaxColorAttachments : numRT;

        VkPipelineColorBlendAttachmentState arrColorBlendAttachment[kMaxColorAttachments]{};
        for ( uint32 blendIndex = 0; blendIndex < blendCount; ++blendIndex )
        {
            const RHIBlendTargetState& source                  = renderState._arrBlendTarget[blendIndex];
            arrColorBlendAttachment[blendIndex].colorWriteMask = static_cast<VkColorComponentFlags>( source._colorWriteMask ); // 비트가 같다(RHIColorWriteMask)
            // 블렌드 지원은 형식마다 선택이다(R16G16_SNORM 등, lavapipe 는 없다). 못 섞는 첨부는 덮어쓴다 - 겹침 보기가 G버퍼 전체에 블렌드를 켜도 값을 보는 것은 알베도 칸이다.
            // 형식이 없으면 ensurePipelineRenderPass 가 R8G8B8A8_UNORM 으로 두며 그 형식은 블렌드가 필수다.
            const VkFormat     attachmentFormat = VulkanRHIUtil::toVulkanTextureFormat( desc._arrRTVFormat[blendIndex] );
            VkFormatProperties formatProperties{};
            vkGetPhysicalDeviceFormatProperties( _pDevice->_physicalDevice, attachmentFormat, &formatProperties );
            const bool bBlendable = attachmentFormat == VK_FORMAT_UNDEFINED || ( formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT ) != 0;

            arrColorBlendAttachment[blendIndex].blendEnable         = ( source._bEnableBlend != SW_FALSE && bBlendable ) ? VK_TRUE : VK_FALSE;
            arrColorBlendAttachment[blendIndex].srcColorBlendFactor = VulkanRHIUtil::toVulkanBlendFactor( source._srcColor );
            arrColorBlendAttachment[blendIndex].dstColorBlendFactor = VulkanRHIUtil::toVulkanBlendFactor( source._dstColor );
            arrColorBlendAttachment[blendIndex].colorBlendOp        = VulkanRHIUtil::toVulkanBlendOp( source._colorOp );
            arrColorBlendAttachment[blendIndex].srcAlphaBlendFactor = VulkanRHIUtil::toVulkanBlendFactor( source._srcAlpha );
            arrColorBlendAttachment[blendIndex].dstAlphaBlendFactor = VulkanRHIUtil::toVulkanBlendFactor( source._dstAlpha );
            arrColorBlendAttachment[blendIndex].alphaBlendOp        = VulkanRHIUtil::toVulkanBlendOp( source._alphaOp );
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable   = VK_FALSE;
        colorBlending.attachmentCount = blendCount;
        colorBlending.pAttachments    = blendCount > 0 ? arrColorBlendAttachment : nullptr;

        VkRenderPass pipelineRp = _pDevice->ensurePipelineRenderPass( desc );
        if ( pipelineRp == VK_NULL_HANDLE )
            pipelineRp = _pDevice->_renderPass;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable       = renderState._bEnableDepthTest != SW_FALSE ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable      = ( depthStencil.depthTestEnable != VK_FALSE && renderState._bEnableDepthWrite != SW_FALSE ) ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp        = VulkanRHIUtil::toVulkanCompareOp( renderState._depthCompare );
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable     = renderState._bEnableStencil != SW_FALSE ? VK_TRUE : VK_FALSE;
        depthStencil.front.failOp          = VulkanRHIUtil::toVulkanStencilOp( renderState._stencilFrontFail );
        depthStencil.front.depthFailOp     = VulkanRHIUtil::toVulkanStencilOp( renderState._stencilFrontDepthFail );
        depthStencil.front.passOp          = VulkanRHIUtil::toVulkanStencilOp( renderState._stencilFrontPass );
        depthStencil.front.compareOp       = VulkanRHIUtil::toVulkanCompareOp( renderState._stencilFrontCompare );
        depthStencil.front.compareMask     = renderState._stencilReadMask;
        depthStencil.front.writeMask       = renderState._stencilWriteMask;
        depthStencil.front.reference       = renderState._stencilRef;
        depthStencil.back.failOp           = VulkanRHIUtil::toVulkanStencilOp( renderState._stencilBackFail );
        depthStencil.back.depthFailOp      = VulkanRHIUtil::toVulkanStencilOp( renderState._stencilBackDepthFail );
        depthStencil.back.passOp           = VulkanRHIUtil::toVulkanStencilOp( renderState._stencilBackPass );
        depthStencil.back.compareOp        = VulkanRHIUtil::toVulkanCompareOp( renderState._stencilBackCompare );
        depthStencil.back.compareMask      = renderState._stencilReadMask;
        depthStencil.back.writeMask        = renderState._stencilWriteMask;
        depthStencil.back.reference        = renderState._stencilRef;

        vector<VkDynamicState>           listDynamicState = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32>( listDynamicState.size() );
        dynamicState.pDynamicStates    = listDynamicState.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = shaderStageCount;
        VkPipelineTessellationStateCreateInfo tessellationState{};
        tessellationState.sType              = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
        tessellationState.patchControlPoints = 3;
        pipelineInfo.pTessellationState      = request._bHasTessellation != SW_FALSE ? &tessellationState : nullptr;
        pipelineInfo.pStages                 = arrShaderStage;
        pipelineInfo.pVertexInputState       = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState     = &inputAssembly;
        pipelineInfo.pViewportState          = &viewportState;
        pipelineInfo.pRasterizationState     = &rasterizer;
        pipelineInfo.pMultisampleState       = &multisampling;
        pipelineInfo.pDepthStencilState      = &depthStencil;
        pipelineInfo.pColorBlendState        = &colorBlending;
        pipelineInfo.pDynamicState           = &dynamicState;
        pipelineInfo.layout                  = _pDevice->_pipelineLayout;
        pipelineInfo.renderPass              = pipelineRp;
        pipelineInfo.subpass                 = 0;
        pipelineInfo.basePipelineHandle      = VK_NULL_HANDLE;

        VkPipeline     newPipeline{ VK_NULL_HANDLE };
        const VkResult createResult = vkCreateGraphicsPipelines( _pDevice->_device, _pDevice->_pipelineCache, 1, &pipelineInfo, nullptr, &newPipeline );

        destroyShaderModules();

        // 실패하면 PSO 를 만들지 않는다(핸들 0, 다른 세 백엔드와 같은 규칙). 빈 파이프라인을 등록해 두면 드로우가 다른 것으로 대신 그려진다.
        if ( createResult != VK_SUCCESS || newPipeline == VK_NULL_HANDLE )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreatePipelineStateVkCreateGraphicsPipelinesFailed", "createPipelineState: vkCreateGraphicsPipelines failed (%#) '%#'", static_cast<int32>( createResult ), desc._vertexShaderPath );
            return 0;
        }

        VulkanRHIDevice::VulkanPipelineStateRecord record{};
        record._pipeline     = newPipeline;
        record._vertexLayout = RHIVertexLayout{ vertexSlotMask, desc._meshVertexFormat };
        return _pDevice->_pipelineStates.insert( record );
    }

    RHIPipelineStateHandle VulkanRHIResourceFactory::createComputePipelineStateNative( string_view shaderPath, string_view entryPoint, const vector<string>& listDefine )
    {
        const ShaderCompileDesc csDesc   = RHIShaderRequest::resolveCompute( shaderPath, entryPoint, listDefine, ShaderTargetFormat::SPIRV_Vulkan );
        ShaderCompileResult     csResult = RHIShaderRequest::compile( csDesc );

        if ( csResult._bSuccess == false )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreateComputePipelineStateShaderCompileFailed", "createComputePipelineState: shader compile failed '%#'", shaderPath );
            return 0;
        }

        VkShaderModuleCreateInfo csInfo{};
        csInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        csInfo.codeSize = csResult._bytecode.size();
        csInfo.pCode    = reinterpret_cast<const uint32*>( csResult._bytecode.data() );
        // 핸들을 초기화하고 결과를 검사한다. 실패를 그냥 쓰면 쓰레기 값을 파이프라인 생성과 vkDestroyShaderModule 에 넘긴다.
        VkShaderModule compShaderModule{ VK_NULL_HANDLE };
        if ( vkCreateShaderModule( _pDevice->_device, &csInfo, nullptr, &compShaderModule ) != VK_SUCCESS ||
             compShaderModule == VK_NULL_HANDLE )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreateComputePipelineStateVkCreateShaderModuleFailed", "createComputePipelineState: vkCreateShaderModule failed" );
            return 0;
        }

        VkPipelineShaderStageCreateInfo compShaderStageInfo{};
        compShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        compShaderStageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        compShaderStageInfo.module = compShaderModule;
        compShaderStageInfo.pName  = csDesc._entryPoint.c_str();

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.layout = _pDevice->_pipelineLayout;
        pipelineInfo.stage  = compShaderStageInfo;

        VkPipeline newPipeline{ VK_NULL_HANDLE };
        if ( vkCreateComputePipelines( _pDevice->_device, _pDevice->_pipelineCache, 1, &pipelineInfo, nullptr, &newPipeline ) != VK_SUCCESS )
            newPipeline = VK_NULL_HANDLE;

        vkDestroyShaderModule( _pDevice->_device, compShaderModule, nullptr );
        if ( newPipeline == VK_NULL_HANDLE )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreateComputePipelineStateVkCreateComputePipelinesFailed", "createComputePipelineState: vkCreateComputePipelines failed '%#'", shaderPath );
            return 0;
        }

        VulkanRHIDevice::VulkanPipelineStateRecord record{};
        record._pipeline = newPipeline;
        return _pDevice->_pipelineStates.insert( record );
    }

    void VulkanRHIResourceFactory::destroyPipelineState( RHIPipelineStateHandle pso )
    {
        VulkanRHIDevice::VulkanPipelineStateRecord record{};
        if ( _pDevice->_pipelineStates.take( pso, record ) == false )
            return;
        if ( record._pipeline != VK_NULL_HANDLE )
        {
            VkDevice   dev  = _pDevice->_device;
            VkPipeline pipe = record._pipeline;
            _pDevice->_releaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pipe]()
            {
                vkDestroyPipeline( dev, pipe, nullptr );
            } ),
                                                       _pDevice->_frameFenceCounter + 1 );
        }
        if ( _pDevice->_recordingState._activeGraphicsPSO == pso )
            _pDevice->_recordingState._activeGraphicsPSO = 0;
    }

    RHIRenderPassHandle VulkanRHIResourceFactory::createRenderPass( const RHIRenderPassDesc& desc )
    {

        if ( desc._listColorAttachment.empty() && desc._bHasDepthStencil == SW_FALSE )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreateRenderPassRequiresColorDepth", "createRenderPass requires color or depth attachments." );
            return 0;
        }

        VulkanRHIRenderPassCache::RenderPassSpec spec{};
        const uint32                             colorCount =
            desc._listColorAttachment.size() > kMaxColorAttachments
                                            ? kMaxColorAttachments
                                            : static_cast<uint32>( desc._listColorAttachment.size() );
        for ( uint32 colorIndex = 0; colorIndex < colorCount; ++colorIndex )
        {
            const RHIRenderPassAttachment& attachment = desc._listColorAttachment[colorIndex];
            spec.addColor( static_cast<uint32>( VulkanRHIUtil::toVulkanTextureFormat( attachment._format ) ), VulkanRHIUtil::toVulkanLoadOp( attachment._loadOp ),
                           ( attachment._loadOp == RHIRenderPassLoadOp::Clear ) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
            spec._arrColorStoreOp[colorIndex] = VulkanRHIUtil::toVulkanStoreOp( attachment._storeOp );
        }
        if ( desc._bHasDepthStencil != SW_FALSE )
            spec.setDepth( _pDevice->_depthFormat, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );

        if ( spec._colorCount == 0 && spec._depthFormat == 0 )
            return _pDevice->_renderPassCache.addRenderPassRecord( _pDevice->_renderPass, false ); // 스왑체인 RP 별칭. 소유하지 않는다

        VkRenderPass created = _pDevice->createRenderPassFromSpec( spec );
        if ( created == VK_NULL_HANDLE )
        {
            SW_LOG_ERROR_ID( "VulkanRHIResourceFactory.CreateRenderPassDescFailed", "createRenderPass(desc) failed" );
            return 0;
        }

        return _pDevice->_renderPassCache.addRenderPassRecord( created, true );
    }

    void VulkanRHIResourceFactory::destroyRenderPass( RHIRenderPassHandle pass )
    {
        _pDevice->_renderPassCache.destroyRenderPassRecord( _pDevice->_device, pass, _pDevice->_renderPass );
    }
} // namespace sw
