/**
 * @file VulkanRHIDeviceDescriptor.cpp
 * @brief VulkanRHIDevice 의 디스크립터 자원 — 세트 레이아웃, 파이프라인 레이아웃, bindless 텍스처 배열
 * @details Vulkan 은 세트 단위로 바인딩하므로 슬롯 배치가 곧 파이프라인 레이아웃이다.
 *          셰이더(common.hlsli 의 SW_VK_CB_SET_*)와 값이 맞아야 하는 코드가 여기 모여 있다.
 */
#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"

namespace sw
{
    SW_LOG_CALLER( "Vulkan" );

    size_t VulkanRHIDevice::registeredDescriptorSetCount() const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        return _listRegisteredDescriptorSet.size();
    }

    VkDescriptorSet VulkanRHIDevice::registeredDescriptorSetAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredDescriptorSet.size() )
            return VK_NULL_HANDLE;

        // 링 상수버퍼는 프레임 슬롯마다 전용 셋을 갖는다. 예전엔 셋 하나를 매 프레임 새 슬롯으로
        // 다시 기록했는데, 그 셋은 직전 프레임 커맨드버퍼가 아직 쓰고 있어서 in-use 위반이었다
        // (오프스크린 블로킹 제출이 사라지기 전까지는 그 스톨이 가려주고 있었다).
        const size_t ringBase = static_cast<size_t>( index ) * constant::kMaxFrameCountInFlight;
        if ( ringBase + _currentFrame < _listRegisteredCbSetRing.size() )
        {
            const VkDescriptorSet ringSet = _listRegisteredCbSetRing[ringBase + _currentFrame];
            if ( ringSet != VK_NULL_HANDLE )
                return ringSet;
        }
        return _listRegisteredDescriptorSet[index];
    }

    VkDescriptorSet VulkanRHIDevice::registeredUavSetAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredUAV.size() )
            return VK_NULL_HANDLE;
        return _listRegisteredUAV[index];
    }

    VkDescriptorSet VulkanRHIDevice::registeredTextureSetAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredTexture.size() )
            return VK_NULL_HANDLE;
        return _listRegisteredTexture[index];
    }

    bool VulkanRHIDevice::createDescriptorResources()
    {
        if ( _device == VK_NULL_HANDLE )
            return false;

        auto createSimpleSetLayout = [this]( VkDescriptorType type, VkShaderStageFlags stages, VkDescriptorSetLayout& outLayout ) -> bool
        {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding         = 0;
            binding.descriptorType  = type;
            binding.descriptorCount = 1;
            binding.stageFlags      = stages;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings    = &binding;
            return vkCreateDescriptorSetLayout( _device, &layoutInfo, nullptr, &outLayout ) == VK_SUCCESS;
        };

        const VkShaderStageFlags allStages =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

        if ( createSimpleSetLayout( VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, allStages, _descriptorSetLayout ) == false )
            return false;
        if ( createSimpleSetLayout( VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, allStages, _textureDescriptorSetLayout ) == false )
            return false;
        if ( createSimpleSetLayout( VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, allStages, _uavDescriptorSetLayout ) == false )
            return false;

        // set 4 = 정적 샘플러(g_SwSamplerLinearWrap). binding.hlsli 는 standalone SamplerState 로 선언하므로
        // (COMBINED_IMAGE_SAMPLER 가 아니라) VK_DESCRIPTOR_TYPE_SAMPLER 전용 레이아웃이 필요하다.
        // immutable sampler 로 굽기 때문에 vkUpdateDescriptorSets 없이 매 드로우 바인딩만 하면 된다.
        {
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter     = VK_FILTER_LINEAR;
            samplerInfo.minFilter     = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.borderColor   = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            samplerInfo.maxLod        = 1000.0f;
            if ( vkCreateSampler( _device, &samplerInfo, nullptr, &_staticSamplerLinearWrap ) != VK_SUCCESS )
                return false;

            VkDescriptorSetLayoutBinding binding{};
            binding.binding            = 0;
            binding.descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount    = 1;
            binding.stageFlags         = allStages;
            binding.pImmutableSamplers = &_staticSamplerLinearWrap;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings    = &binding;
            if ( vkCreateDescriptorSetLayout( _device, &layoutInfo, nullptr, &_samplerSetLayout ) != VK_SUCCESS )
                return false;
        }

        // Bindless texture array layout (set 1) — ensureBindlessTextureArray가 세트를 할당.
        {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding         = 0;
            binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            binding.descriptorCount = kBindlessTextureCount;
            binding.stageFlags      = allStages;

            VkDescriptorBindingFlags bindingFlags =
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
            bindingFlagsInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bindingFlagsInfo.bindingCount  = 1;
            bindingFlagsInfo.pBindingFlags = &bindingFlags;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.pNext        = &bindingFlagsInfo;
            layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings    = &binding;
            if ( vkCreateDescriptorSetLayout( _device, &layoutInfo, nullptr, &_bindlessTextureArrayLayout ) != VK_SUCCESS )
                return false;
        }

        // Descriptor set layout (matches VulkanRHICommandContext binds):
        //   0: Pass/Material UBO
        //   1: Bindless texture array (native sampling)
        //   2,3,5: Explicit single-texture SRV slots (DX11-style emulation, 텍스처 슬롯 0/1/3)
        //   4: 정적 샘플러(g_SwSamplerLinearWrap, immutable) — 텍스처 슬롯 2 는 이 set 을 쓰지 않는다
        //      (bindShaderResource 는 native bindless 에서 아예 호출 안 됨 — Vulkan 은 항상 native).
        //   6..9: 컴퓨트 읽기전용 구조버퍼(t0..t3, bindComputeShaderResource) / GPUScene 인스턴스 구조버퍼
        //   7..9: 위 4개 set 중 뒤쪽 3개는 컴퓨트 UAV(u0..u2, bindComputeUAV) 와도 공유한다 —
        //         한 디스패치에서 t 슬롯과 u 슬롯을 동시에 쓸 때는 서로 다른 인덱스를 사용해야 한다.
        //   10: MaterialCB(b1). 세트 단위 바인딩이라 상수 버퍼 슬롯마다 세트가 하나씩 필요하다 —
        //       common.hlsli 의 SW_VK_CB_SET_1 과 같은 값이어야 한다.
        // 세트 번호는 bindingslots.hlsli(shaderslot::vk) 가 정본이다 — HLSL 의 [[vk::binding(slot, set)]] 과 같은 파일.
        // 예약·미사용 세트(2,3,5)는 텍스처 레이아웃으로 채워 두기만 한다(어떤 셰이더도 참조하지 않는다 — 계약 검증이 막는다).
        namespace vkslot = shaderslot::vk;
        VkDescriptorSetLayout arrSetLayout[kBoundDescriptorSetCount]{};
        for ( uint32 setIndex = 0; setIndex < kBoundDescriptorSetCount; ++setIndex )
            arrSetLayout[setIndex] = _textureDescriptorSetLayout;
        arrSetLayout[vkslot::kSetPassCb]          = _descriptorSetLayout;
        arrSetLayout[vkslot::kSetBindlessTexture] = _bindlessTextureArrayLayout;
        arrSetLayout[vkslot::kSetStaticSampler]   = _samplerSetLayout;
        for ( uint32 storageIndex = 0; storageIndex < vkslot::kStorageSetCount; ++storageIndex )
            arrSetLayout[vkslot::kSetStorage0 + storageIndex] = _uavDescriptorSetLayout;
        arrSetLayout[vkslot::kSetMaterialCb] = _descriptorSetLayout;

        // 세트를 11개 요구한다. Vulkan 이 보장하는 최소값은 4 라서 기기에 따라 부족할 수 있는데,
        // 예전엔 확인 없이 만들고 vkCreatePipelineLayout 실패만 남겨 원인을 알 수 없었다.
        {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties( _physicalDevice, &props );
            if ( props.limits.maxBoundDescriptorSets < kBoundDescriptorSetCount )
            {
                SW_LOG_ERROR( "이 기기는 디스크립터 세트를 %#개까지만 바인딩할 수 있는데 엔진은 %#개를 요구합니다.",
                              props.limits.maxBoundDescriptorSets, kBoundDescriptorSetCount );
                return false;
            }
        }

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = allStages;
        pushRange.offset     = 0;
        pushRange.size       = kMaxComputeRootConstantDwords * sizeof( uint32 );

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount         = kBoundDescriptorSetCount;
        pipelineLayoutInfo.pSetLayouts            = arrSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges    = &pushRange;
        if ( vkCreatePipelineLayout( _device, &pipelineLayoutInfo, nullptr, &_pipelineLayout ) != VK_SUCCESS )
            return false;

        VkDescriptorPoolSize arrPoolSize[] = {
            {        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16384},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32768},
            {        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16384},
            {               VK_DESCRIPTOR_TYPE_SAMPLER,     4},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets       = 32768;
        poolInfo.poolSizeCount = static_cast<uint32>( sizeof( arrPoolSize ) / sizeof( arrPoolSize[0] ) );
        poolInfo.pPoolSizes    = arrPoolSize;
        if ( vkCreateDescriptorPool( _device, &poolInfo, nullptr, &_descriptorPool ) != VK_SUCCESS )
            return false;

        // 정적 샘플러 set 4 를 한 번 할당해 둔다 (immutable sampler라 vkUpdateDescriptorSets 불필요).
        {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool     = _descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts        = &_samplerSetLayout;
            if ( vkAllocateDescriptorSets( _device, &allocInfo, &_staticSamplerSet ) != VK_SUCCESS )
                return false;
        }

        return true;
    }

    bool VulkanRHIDevice::ensureBindlessTextureArray()
    {
        if ( _bindlessTextureSet != VK_NULL_HANDLE )
            return true;
        if ( _device == VK_NULL_HANDLE || _descriptorPool == VK_NULL_HANDLE || _defaultSampler == VK_NULL_HANDLE )
            return false;
        if ( _bindlessTextureArrayLayout == VK_NULL_HANDLE )
            return false;

        // 1x1 dummy image so unbound slots are valid.
        VkImageCreateInfo imageInfo{};
        imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent        = { 1, 1, 1 };
        imageInfo.mipLevels     = 1;
        imageInfo.arrayLayers   = 1;
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if ( vkCreateImage( _device, &imageInfo, nullptr, &_bindlessDummyImage ) != VK_SUCCESS )
            return false;

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements( _device, _bindlessDummyImage, &memReq );
        uint32 memoryTypeIndex{ 0 };
        if ( findMemoryType( memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex ) == false )
        {
            SW_LOG_ERROR( "Failed to find a device local memory type for the bindless dummy image." );
            return false;
        }

        VkMemoryAllocateInfo allocMem{};
        allocMem.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocMem.allocationSize  = memReq.size;
        allocMem.memoryTypeIndex = memoryTypeIndex;
        if ( vkAllocateMemory( _device, &allocMem, nullptr, &_bindlessDummyMemory ) != VK_SUCCESS )
            return false;
        vkBindImageMemory( _device, _bindlessDummyImage, _bindlessDummyMemory, 0 );

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                       = _bindlessDummyImage;
        viewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                      = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if ( vkCreateImageView( _device, &viewInfo, nullptr, &_bindlessDummyView ) != VK_SUCCESS )
            return false;

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = _descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &_bindlessTextureArrayLayout;

        if ( vkAllocateDescriptorSets( _device, &allocInfo, &_bindlessTextureSet ) != VK_SUCCESS )
            return false;

        vector<VkDescriptorImageInfo> infos( kBindlessTextureCount );
        vector<VkWriteDescriptorSet>  writes( kBindlessTextureCount );
        for ( uint32 slotIndex = 0; slotIndex < kBindlessTextureCount; ++slotIndex )
        {
            infos[slotIndex].sampler          = _defaultSampler;
            infos[slotIndex].imageView        = _bindlessDummyView;
            infos[slotIndex].imageLayout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[slotIndex].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[slotIndex].dstSet          = _bindlessTextureSet;
            writes[slotIndex].dstBinding      = 0;
            writes[slotIndex].dstArrayElement = slotIndex;
            writes[slotIndex].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[slotIndex].descriptorCount = 1;
            writes[slotIndex].pImageInfo      = &infos[slotIndex];
        }
        vkUpdateDescriptorSets( _device, kBindlessTextureCount, writes.data(), 0, nullptr );
        SW_LOG_INFO( "Bindless texture array ready (%# slots).", kBindlessTextureCount );
        return true;
    }

    bool VulkanRHIDevice::ensureDefaultDescriptorSet()
    {
        if ( _descriptorSet != VK_NULL_HANDLE )
            return true;
        if ( _device == VK_NULL_HANDLE || _descriptorPool == VK_NULL_HANDLE || _descriptorSetLayout == VK_NULL_HANDLE )
            return false;

        // set 0(Pass/Material UBO)에 바인딩할 게 없을 때 쓰는 기본 세트.
        // 그래픽스 셰이더는 set 0 을 정적으로 참조하므로 Vulkan 검증상 모든 draw 에서
        // set 0 이 바인딩돼 있어야 한다(머티리얼 CBV 가 없는 드로우·인다이렉트 드로우 포함).
        constexpr VkDeviceSize kDummyUboSize = 256;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = kDummyUboSize;
        bufferInfo.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if ( vkCreateBuffer( _device, &bufferInfo, nullptr, &_dummyUBO ) != VK_SUCCESS )
            return false;

        // 실패 지점마다 여기까지 만든 것을 되돌린다. (핸들을 남기면 재시도 시 그대로 누수된다)
        const auto destroyDummyUbo = [this]()
        {
            if ( _dummyUBOMemory != VK_NULL_HANDLE )
            {
                vkFreeMemory( _device, _dummyUBOMemory, nullptr );
                _dummyUBOMemory = VK_NULL_HANDLE;
            }
            if ( _dummyUBO != VK_NULL_HANDLE )
            {
                vkDestroyBuffer( _device, _dummyUBO, nullptr );
                _dummyUBO = VK_NULL_HANDLE;
            }
        };

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements( _device, _dummyUBO, &memReq );
        uint32 memoryTypeIndex{ 0 };
        if ( findMemoryType( memReq.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             memoryTypeIndex ) == false )
        {
            destroyDummyUbo();
            return false;
        }

        VkMemoryAllocateInfo allocMem{};
        allocMem.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocMem.allocationSize  = memReq.size;
        allocMem.memoryTypeIndex = memoryTypeIndex;
        if ( vkAllocateMemory( _device, &allocMem, nullptr, &_dummyUBOMemory ) != VK_SUCCESS )
        {
            _dummyUBOMemory = VK_NULL_HANDLE;
            destroyDummyUbo();
            return false;
        }
        vkBindBufferMemory( _device, _dummyUBO, _dummyUBOMemory, 0 );

        void* pMapped{ nullptr };
        if ( vkMapMemory( _device, _dummyUBOMemory, 0, kDummyUboSize, 0, &pMapped ) == VK_SUCCESS && pMapped != nullptr )
        {
            Memory::set( pMapped, 0, static_cast<size_t>( kDummyUboSize ) );
            vkUnmapMemory( _device, _dummyUBOMemory );
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = _descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &_descriptorSetLayout;
        if ( vkAllocateDescriptorSets( _device, &allocInfo, &_descriptorSet ) != VK_SUCCESS )
        {
            _descriptorSet = VK_NULL_HANDLE;
            destroyDummyUbo();
            SW_LOG_ERROR( "Failed to allocate the default (set 0) descriptor set." );
            return false;
        }

        VkDescriptorBufferInfo dbInfo{};
        dbInfo.buffer = _dummyUBO;
        dbInfo.offset = 0;
        dbInfo.range  = kDummyUboSize;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = _descriptorSet;
        write.dstBinding      = 0;
        write.dstArrayElement = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo     = &dbInfo;
        vkUpdateDescriptorSets( _device, 1, &write, 0, nullptr );
        return true;
    }

    void VulkanRHIDevice::writeBindlessTextureSlot( RHIDescriptorIndex index, VkImageView view, uint32 imageLayout )
    {
        if ( _bindlessTextureSet == VK_NULL_HANDLE || view == VK_NULL_HANDLE || index >= kBindlessTextureCount )
            return;

        // 레이아웃은 샘플 시점에 이미지가 실제로 있을 레이아웃과 같아야 한다 — 컬러는 SHADER_READ_ONLY,
        // 깊이는 prepareTextureForShaderRead 가 옮기는 DEPTH_STENCIL_READ_ONLY (호출자가 고른다).
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler     = _defaultSampler;
        imageInfo.imageView   = view;
        imageInfo.imageLayout = static_cast<VkImageLayout>( imageLayout );

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = _bindlessTextureSet;
        write.dstBinding      = 0;
        write.dstArrayElement = index;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imageInfo;
        vkUpdateDescriptorSets( _device, 1, &write, 0, nullptr );
    }

} // namespace sw
