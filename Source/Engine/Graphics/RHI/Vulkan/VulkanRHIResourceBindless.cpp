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
        _pDevice->checkRegistryMutableNow( "registerBindlessTexture" );
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
        _pDevice->checkRegistryMutableNow( "registerBindlessResource" );
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
        _pDevice->checkRegistryMutableNow( "unregisterBindlessResource" );
        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        if ( index >= _pDevice->_listRegisteredDescriptorSet.size() )
            return;
        // 버퍼가 소유하지 않는 인덱스(텍스처 SRV 인덱스가 잘못 넘어왔거나 이중 해제)를 프리리스트에
        // 넣으면 다음 registerBindlessResource 가 살아 있는 다른 버퍼의 슬롯을 덮어쓴다 — 실제로
        // 트랜지언트 텍스처 인덱스 0·1·2 가 여기로 와서 패스 CB 슬롯 2 에 인스턴스 STORAGE 세트가
        // 들어갔고, 그래픽스 set 0 이 UNIFORM 레이아웃과 어긋난다는 검증 에러가 매 프레임 났다.
        if ( index >= _pDevice->_listBindlessSourceBuffer.size() || _pDevice->_listBindlessSourceBuffer[index] == 0 )
        {
            SW_LOG_ERROR( "Bindless buffer index %# is not owned by any buffer; ignoring the release.", index );
            return;
        }
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

    void VulkanRHIResource::unregisterBindlessTexture( RHIDescriptorIndex index )
    {
        _pDevice->checkRegistryMutableNow( "unregisterBindlessTexture" );
        if ( index == kInvalidDescriptorIndex )
            return;
        // 텍스처 레코드가 자기 인덱스를 들고 있으므로(destroyTexture 가 그걸로 정리한다) 레코드 쪽도
        // 같이 지워야 나중의 destroyTexture 가 같은 슬롯을 두 번 반납하지 않는다.
        bool bFound = false;
        _pDevice->_gpuTextures.forEach( [this, index, &bFound]( VulkanRHIDevice::VulkanTextureRecord& record )
        {
            if ( record._bindlessIndex != index )
                return;
            releaseTextureBindlessSlot( record );
            bFound = true;
        } );
        if ( bFound == false )
            SW_LOG_ERROR( "Bindless texture index %# is not owned by any texture; ignoring the release.", index );
    }

    void VulkanRHIResource::releaseTextureBindlessSlot( VulkanRHIDevice::VulkanTextureRecord& record )
    {
        if ( record._bindlessIndex == kInvalidDescriptorIndex )
            return;
        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex            index = record._bindlessIndex;
        if ( _pDevice->_bindlessTextureSet != VK_NULL_HANDLE && index < _pDevice->_listRegisteredTexture.size() &&
             _pDevice->_listRegisteredTexture[index] == _pDevice->_bindlessTextureSet )
        {
            // 공유 배열 세트의 슬롯 하나 — 더미 뷰로 되돌려 두면 GPU 가 아직 읽고 있어도 안전하다.
            _pDevice->writeBindlessTextureSlot( index, _pDevice->_bindlessDummyView );
            releaseFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree, index, VkDescriptorSet{ VK_NULL_HANDLE } );
        }
        else if ( index < _pDevice->_listRegisteredTexture.size() && _pDevice->_listRegisteredTexture[index] != VK_NULL_HANDLE )
        {
            // 텍스처별 전용 세트(슬롯 바인드 폴백) — 프레임 펜스 뒤에 반납한다.
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
        record._bindlessIndex = kInvalidDescriptorIndex;
    }

    RHIDescriptorIndex VulkanRHIResource::registerBindlessUAV( RHIBufferHandle buffer )
    {
        _pDevice->checkRegistryMutableNow( "registerBindlessUAV" );
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
        _pDevice->checkRegistryMutableNow( "unregisterBindlessUAV" );
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
