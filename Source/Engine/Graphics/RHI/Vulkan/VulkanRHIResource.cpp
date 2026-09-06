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

    void VulkanRHIResource::updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        // updateConstantBuffer 로 넘기면 안 된다. 그쪽은 `(frame % kMaxFrameCountInFlight) * slotSize`
        // 링 오프셋에 쓰는데, 그 링은 createConstantBuffer 가 `size * kMaxFrameCountInFlight` 로
        // 잡아 준 버퍼에만 존재한다. createStructuredBuffer 는 정확히 한 프레임 크기만 잡으므로
        // 프레임 1부터 버퍼 밖을 매핑한다 — 인스턴스 버퍼 400 x 96 B = 0x9600 에서
        // "offset 0x9600 plus size 0x9600 oversteps total array size 0x9600" 이 그것이고,
        // 그 덮어쓰기가 메모리를 깨뜨려 vkAcquireNextImageKHR 가 매 프레임 DEVICE_LOST(-4) 를 냈다.
        // 매 프레임 구조버퍼를 갱신하는 씬이 없어서 한 번도 드러나지 않았을 뿐이다.
        //
        // 구조버퍼는 링이 아니므로 항상 오프셋 0 이다. (GPU 가 직전 프레임을 아직 읽고 있을 수 있는
        // CPU/GPU 해저드는 남는다 — 그건 프레임 간 찢어짐이지 메모리 손상이 아니다. 진짜 해결은
        // 구조버퍼도 링으로 잡고 바인딩 쪽이 프레임 슬롯을 가리키게 하는 것인데, bindless SRV 가
        // 버퍼 전체를 한 번 등록하는 구조라 별도 설계가 필요하다.)
        VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
        if ( pRecord == nullptr || pData == nullptr || size == 0 || pRecord->_memory == VK_NULL_HANDLE )
            return;

        void* pMapped{ nullptr };
        if ( vkMapMemory( _pDevice->_device, pRecord->_memory, 0, size, 0, &pMapped ) == VK_SUCCESS )
        {
            Memory::copy( pMapped, pData, size );
            vkUnmapMemory( _pDevice->_device, pRecord->_memory );
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

        if ( record._bRenderTarget && _pDevice->createOffscreenFramebuffer( record ) == false )
            SW_LOG_WARNING( "createTexture2D: framebuffer creation failed — texture kept without offscreen pass." );

        return _pDevice->_gpuTextures.insert( std::move( record ) );
    }

    void VulkanRHIResource::destroyTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return;

        VulkanRHIDevice::VulkanTextureRecord* pSlot = _pDevice->resolveTexture( texture );
        if ( pSlot == nullptr )
            return;

        _pDevice->destroyCompositeFramebuffersUsing( texture );

        if ( pSlot->_bindlessIndex != kInvalidDescriptorIndex )
        {
            std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
            const RHIDescriptorIndex            index = pSlot->_bindlessIndex;
            if ( _pDevice->_bindlessTextureSet != VK_NULL_HANDLE && index < _pDevice->_listRegisteredTexture.size() &&
                 _pDevice->_listRegisteredTexture[index] == _pDevice->_bindlessTextureSet )
            {
                _pDevice->writeBindlessTextureSlot( index, _pDevice->_bindlessDummyView );
                releaseFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree, index, VkDescriptorSet{ VK_NULL_HANDLE } );
            }
            else if ( index < _pDevice->_listRegisteredTexture.size() && _pDevice->_listRegisteredTexture[index] != VK_NULL_HANDLE )
            {
                VkDevice              dev  = _pDevice->_device;
                VkDescriptorPool      pool = _pDevice->_descriptorPool;
                const VkDescriptorSet set  = releaseFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree,
                                                                   index, VkDescriptorSet{ VK_NULL_HANDLE } );
                _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pool, set]()
                {
                    vkFreeDescriptorSets( dev, pool, 1, &set );
                } ),
                                                           _pDevice->_frameFenceCounter + 1 );
            }
            pSlot->_bindlessIndex = kInvalidDescriptorIndex;
        }

        _pDevice->destroyOffscreenFramebuffer( *pSlot );
        VulkanRHIDevice::VulkanTextureRecord owned;
        if ( _pDevice->_gpuTextures.take( texture, owned ) == false )
            return;
        VkDevice       dev   = _pDevice->_device;
        VkImageView    view  = owned._imageView;
        VkImage        image = owned._image;
        VkDeviceMemory mem   = owned._memory;
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, view, image, mem]()
        {
            if ( view != VK_NULL_HANDLE )
                vkDestroyImageView( dev, view, nullptr );
            if ( image != VK_NULL_HANDLE )
                vkDestroyImage( dev, image, nullptr );
            if ( mem != VK_NULL_HANDLE )
                vkFreeMemory( dev, mem, nullptr );
        } ),
                                                   _pDevice->_frameFenceCounter + 1 );
    }
} // namespace sw
