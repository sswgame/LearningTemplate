/**
 * @file VulkanRHIResourceBindless.cpp
 * @brief Vulkan 의 bindless 등록 — 리소스를 셰이더가 인덱스로 접근할 수 있게 올린다
 * @details `VulkanRHIResource` 의 일부다. DX12/Vulkan 은 디스크립터 힙/배열에 쓰고, DX11/GL 은 슬롯
 *          기반이라 인덱스만 흉내 낸다 — 네 백엔드를 나란히 비교하기 좋은 지점이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/Support/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResource.h"
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

    RHIDescriptorIndex VulkanRHIResource::registerBindlessTexture( RHITextureHandle texture )
    {
        if ( texture == 0 || _pDevice->_descriptorPool == VK_NULL_HANDLE || _pDevice->_defaultSampler == VK_NULL_HANDLE )
            return kInvalidDescriptorIndex;

        VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( texture );
        if ( pResolved == nullptr || pResolved->_imageView == VK_NULL_HANDLE )
            return kInvalidDescriptorIndex;

        // DEPTH|STENCIL image views cannot be written as sampled descriptors.
        if ( pResolved->_bDepthStencil != 0 )
            return kInvalidDescriptorIndex;

        VulkanRHIDevice::VulkanTextureRecord& record = *pResolved;
        if ( record._bindlessIndex != kInvalidDescriptorIndex )
            return record._bindlessIndex;

        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex            descriptorIndex = resolveFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree );

        if ( descriptorIndex >= _pDevice->kBindlessTextureCount )
        {
            SW_LOG_ERROR( "Bindless texture table full." );
            return kInvalidDescriptorIndex;
        }

        if ( _pDevice->_bindlessTextureSet != VK_NULL_HANDLE )
        {
            _pDevice->writeBindlessTextureSlot( descriptorIndex, record._imageView );
            if ( descriptorIndex >= _pDevice->_listRegisteredTexture.size() )
                _pDevice->_listRegisteredTexture.resize( descriptorIndex + 1 );
            _pDevice->_listRegisteredTexture[descriptorIndex] = _pDevice->_bindlessTextureSet; // shared array set
            record._bindlessIndex                             = descriptorIndex;
            return descriptorIndex;
        }

        // Fallback: one descriptor set per texture (slot bind path).
        if ( _pDevice->_textureDescriptorSetLayout == VK_NULL_HANDLE )
            return kInvalidDescriptorIndex;

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = _pDevice->_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &_pDevice->_textureDescriptorSetLayout;

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        if ( vkAllocateDescriptorSets( _pDevice->_device, &allocInfo, &descriptorSet ) != VK_SUCCESS )
        {
            SW_LOG_ERROR( "Failed to allocate VkDescriptorSet for bindless texture!" );
            return kInvalidDescriptorIndex;
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler     = _pDevice->_defaultSampler;
        imageInfo.imageView   = record._imageView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = descriptorSet;
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imageInfo;
        vkUpdateDescriptorSets( _pDevice->_device, 1, &write, 0, nullptr );

        if ( descriptorIndex >= _pDevice->_listRegisteredTexture.size() )
            _pDevice->_listRegisteredTexture.resize( descriptorIndex + 1 );
        _pDevice->_listRegisteredTexture[descriptorIndex] = descriptorSet;
        record._bindlessIndex                             = descriptorIndex;
        return descriptorIndex;
    }

    RHIDescriptorIndex VulkanRHIResource::registerBindlessResource( RHIBufferHandle buffer )
    {
        const VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
        if ( pRecord == nullptr || _pDevice->_descriptorPool == VK_NULL_HANDLE || _pDevice->_descriptorSetLayout == VK_NULL_HANDLE )
            return kInvalidDescriptorIndex;
        const bool            bIsStorage = ( pRecord->_usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT ) != 0;
        VkDescriptorSetLayout setLayout  = bIsStorage ? _pDevice->_uavDescriptorSetLayout : _pDevice->_descriptorSetLayout;

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = _pDevice->_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &setLayout;

        const auto   slotIt   = _pDevice->_mapCbSlotSize.find( buffer );
        const bool   bRingCb  = ( bIsStorage == false ) && ( slotIt != _pDevice->_mapCbSlotSize.end() );
        const uint32 slotSize = bRingCb ? slotIt->second : pRecord->_size;

        // 링 상수버퍼(createConstantBuffer 가 프레임 슬롯 수만큼 잡아둔 버퍼)는 슬롯마다 전용 셋을
        // 만들어 각자 자기 오프셋을 가리키게 굳혀 둔다 - 그래야 매 프레임 디스크립터를 다시 쓸 일이
        // 없고(in-use 위반 제거), 드로우 경로에서 vkUpdateDescriptorSets 가 아예 사라진다.
        const uint32    ringCount = bRingCb ? constant::kMaxFrameCountInFlight : 1u;
        VkDescriptorSet arrSet[constant::kMaxFrameCountInFlight]{};
        for ( uint32 ringIndex = 0; ringIndex < ringCount; ++ringIndex )
        {
            if ( vkAllocateDescriptorSets( _pDevice->_device, &allocInfo, &arrSet[ringIndex] ) != VK_SUCCESS )
                return kInvalidDescriptorIndex;

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = pRecord->_buffer;
            bufferInfo.offset = bRingCb ? static_cast<VkDeviceSize>( ringIndex ) * slotSize : 0;
            bufferInfo.range  = slotSize;

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet          = arrSet[ringIndex];
            descriptorWrite.dstBinding      = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType  = bIsStorage ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo     = &bufferInfo;
            vkUpdateDescriptorSets( _pDevice->_device, 1, &descriptorWrite, 0, nullptr );
        }

        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex            descriptorIndex = resolveFreeListIndex( _pDevice->_listRegisteredDescriptorSet, _pDevice->_listBindlessFree );

        if ( descriptorIndex >= _pDevice->_listRegisteredDescriptorSet.size() )
        {
            _pDevice->_listRegisteredDescriptorSet.resize( descriptorIndex + 1 );
            _pDevice->_listBindlessSourceBuffer.resize( descriptorIndex + 1 );
        }
        const size_t ringBase = static_cast<size_t>( descriptorIndex ) * constant::kMaxFrameCountInFlight;
        if ( ringBase + constant::kMaxFrameCountInFlight > _pDevice->_listRegisteredCbSetRing.size() )
            _pDevice->_listRegisteredCbSetRing.resize( ringBase + constant::kMaxFrameCountInFlight, VK_NULL_HANDLE );
        for ( uint32 ringIndex = 0; ringIndex < constant::kMaxFrameCountInFlight; ++ringIndex )
            _pDevice->_listRegisteredCbSetRing[ringBase + ringIndex] = bRingCb ? arrSet[ringIndex] : VK_NULL_HANDLE;

        _pDevice->_listRegisteredDescriptorSet[descriptorIndex] = arrSet[0];
        _pDevice->_listBindlessSourceBuffer[descriptorIndex]    = buffer;
        return descriptorIndex;
    }

    void VulkanRHIResource::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        if ( index >= _pDevice->_listRegisteredDescriptorSet.size() )
            return;
        const VkDescriptorSet set = releaseFreeListIndex( _pDevice->_listRegisteredDescriptorSet, _pDevice->_listBindlessFree,
                                                          index, VkDescriptorSet{ VK_NULL_HANDLE } );

        VkDevice         dev         = _pDevice->_device;
        VkDescriptorPool pool        = _pDevice->_descriptorPool;
        auto             enqueueFree = [this, dev, pool]( VkDescriptorSet target )
        {
            _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pool, target]()
            {
                vkFreeDescriptorSets( dev, pool, 1, &target );
            } ),
                                                       _pDevice->_frameFenceCounter + 1 );
        };

        // 프레임 슬롯별 셋도 같이 반납한다. arrSet[0] 은 _listRegisteredDescriptorSet 과 같은 객체라
        // 링이 있는 인덱스에서는 set 을 따로 해제하지 않는다(이중 해제 방지).
        const size_t ringBase = static_cast<size_t>( index ) * constant::kMaxFrameCountInFlight;
        bool         bHadRing = false;
        if ( ringBase + constant::kMaxFrameCountInFlight <= _pDevice->_listRegisteredCbSetRing.size() )
        {
            for ( uint32 ringIndex = 0; ringIndex < constant::kMaxFrameCountInFlight; ++ringIndex )
            {
                VkDescriptorSet& ringSet = _pDevice->_listRegisteredCbSetRing[ringBase + ringIndex];
                if ( ringSet == VK_NULL_HANDLE )
                    continue;
                bHadRing = true;
                enqueueFree( ringSet );
                ringSet = VK_NULL_HANDLE;
            }
        }
        if ( set != VK_NULL_HANDLE && bHadRing == false )
            enqueueFree( set );
        if ( index < _pDevice->_listBindlessSourceBuffer.size() )
            _pDevice->_listBindlessSourceBuffer[index] = 0;
    }

    RHIDescriptorIndex VulkanRHIResource::registerBindlessUAV( RHIBufferHandle buffer )
    {
        const VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
        if ( pRecord == nullptr || _pDevice->_descriptorPool == VK_NULL_HANDLE || _pDevice->_uavDescriptorSetLayout == VK_NULL_HANDLE )
            return kInvalidDescriptorIndex;

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = _pDevice->_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &_pDevice->_uavDescriptorSetLayout;

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        if ( vkAllocateDescriptorSets( _pDevice->_device, &allocInfo, &descriptorSet ) != VK_SUCCESS )
        {
            SW_LOG_ERROR( "Failed to allocate VkDescriptorSet for UAV!" );
            return kInvalidDescriptorIndex;
        }

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = pRecord->_buffer;
        bufferInfo.offset = 0;
        bufferInfo.range  = pRecord->_size;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet          = descriptorSet;
        descriptorWrite.dstBinding      = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo     = &bufferInfo;

        vkUpdateDescriptorSets( _pDevice->_device, 1, &descriptorWrite, 0, nullptr );

        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex            descriptorIndex = resolveFreeListIndex( _pDevice->_listRegisteredUAV, _pDevice->_listUavFree );

        if ( descriptorIndex >= _pDevice->_listRegisteredUAV.size() )
        {
            _pDevice->_listRegisteredUAV.resize( descriptorIndex + 1 );
            _pDevice->_listUavSourceBuffer.resize( descriptorIndex + 1 );
        }
        _pDevice->_listRegisteredUAV[descriptorIndex]   = descriptorSet;
        _pDevice->_listUavSourceBuffer[descriptorIndex] = buffer;
        return descriptorIndex;
    }

    void VulkanRHIResource::unregisterBindlessUAV( RHIDescriptorIndex index )
    {
        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        if ( index >= _pDevice->_listRegisteredUAV.size() )
            return;
        const VkDescriptorSet set = releaseFreeListIndex( _pDevice->_listRegisteredUAV, _pDevice->_listUavFree,
                                                          index, VkDescriptorSet{ VK_NULL_HANDLE } );
        if ( set != VK_NULL_HANDLE )
        {
            VkDevice         dev  = _pDevice->_device;
            VkDescriptorPool pool = _pDevice->_descriptorPool;
            _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pool, set]()
            {
                vkFreeDescriptorSets( dev, pool, 1, &set );
            } ),
                                                       _pDevice->_frameFenceCounter + 1 );
        }
        if ( index < _pDevice->_listUavSourceBuffer.size() )
            _pDevice->_listUavSourceBuffer[index] = 0;
    }
} // namespace sw
