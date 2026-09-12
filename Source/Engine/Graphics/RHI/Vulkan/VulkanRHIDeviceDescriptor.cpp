/**
 * @file VulkanRHIDeviceDescriptor.cpp
 * @brief VulkanRHIDevice 의 디스크립터 자원 — 슬롯 세트(set 0) 레이아웃·프레임별 풀, 텍스처 배열 세트(set 1), 파이프라인 레이아웃
 * @details 언리얼 Vulkan RHI 와 같은 방식이다: 셰이더는 register(b#/t#/u#) 로 선언하고(binding = 종류별 시프트 + 번호,
 *          bindingslots.hlsli 6), 드로우/디스패치 직전에 바인딩 상태가 바뀌었으면 프레임 풀에서 세트를 하나 할당해
 *          쓴 뒤 건다(VulkanRHICommandContext::flushSlotSet). 텍스처 배열은 별도 세트(set 1)에 한 번 채워 두고
 *          커맨드버퍼마다 한 번 바인딩한다.
 */
#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    SW_LOG_CALLER( "Vulkan" );

    namespace
    {
        constexpr VkDeviceSize kDummyUboSize = 256;
    } // namespace

    RHIBufferHandle VulkanRHIDevice::bindlessSourceBufferAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        return index < _listBindlessSourceBuffer.size() ? _listBindlessSourceBuffer[index] : RHIBufferHandle{ 0 };
    }

    RHIBufferHandle VulkanRHIDevice::uavSourceBufferAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        return index < _listUavSourceBuffer.size() ? _listUavSourceBuffer[index] : RHIBufferHandle{ 0 };
    }

    bool VulkanRHIDevice::createDescriptorResources()
    {
        if ( _device == VK_NULL_HANDLE )
            return false;

        namespace vk                       = shaderslot::vk;
        namespace bindless                 = shaderslot::bindless;
        const VkShaderStageFlags allStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

        // 1) 정적 샘플러 세트 s0..s7 (bindingslots.hlsli 4) — set 1 binding SW_VK_SAMPLER_BINDING 의 immutable sampler 배열(0..6)
        //    + binding SW_VK_SHADOW_SAMPLER_BINDING 의 비교 샘플러(7). DX12 의 정적 샘플러와 같은 표.
        {
            struct SamplerSpec
            {
                VkFilter             _filter;
                VkSamplerAddressMode _address;
                VkBool32             _bCompare;
                float32              _anisotropy;
            };
            const SamplerSpec arrSpec[shaderslot::kStaticSamplerCount] = {
                { VK_FILTER_LINEAR,          VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_FALSE, 1.0f}, // LINEAR_WRAP
                { VK_FILTER_LINEAR,   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_FALSE, 1.0f}, // LINEAR_CLAMP
                {VK_FILTER_NEAREST,          VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_FALSE, 1.0f}, // POINT_WRAP
                {VK_FILTER_NEAREST,   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_FALSE, 1.0f}, // POINT_CLAMP
                { VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, VK_FALSE, 1.0f}, // LINEAR_MIRROR
                { VK_FILTER_LINEAR,          VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_FALSE, 8.0f}, // ANISO_WRAP
                {VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_FALSE, 1.0f}, // POINT_BORDER
                { VK_FILTER_LINEAR,   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,  VK_TRUE, 1.0f}, // SHADOW_CMP
            };
            for ( uint32 samplerIndex = 0; samplerIndex < shaderslot::kStaticSamplerCount; ++samplerIndex )
            {
                VkSamplerCreateInfo samplerInfo{};
                samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                samplerInfo.magFilter    = arrSpec[samplerIndex]._filter;
                samplerInfo.minFilter    = arrSpec[samplerIndex]._filter;
                samplerInfo.mipmapMode   = arrSpec[samplerIndex]._filter == VK_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
                samplerInfo.addressModeU = arrSpec[samplerIndex]._address;
                samplerInfo.addressModeV = arrSpec[samplerIndex]._address;
                samplerInfo.addressModeW = arrSpec[samplerIndex]._address;
                // 기능이 꺼져 있으면(samplerAnisotropy 미지원) 이방성만 끈다 — 세트 생성 자체가 실패하면 아무것도 못 그린다.
                const bool bAniso            = arrSpec[samplerIndex]._anisotropy > 1.0f && _bSamplerAnisotropy != 0;
                samplerInfo.anisotropyEnable = bAniso ? VK_TRUE : VK_FALSE;
                samplerInfo.maxAnisotropy    = bAniso ? arrSpec[samplerIndex]._anisotropy : 1.0f;
                samplerInfo.compareEnable    = arrSpec[samplerIndex]._bCompare;
                samplerInfo.compareOp        = VK_COMPARE_OP_LESS_OR_EQUAL;
                samplerInfo.borderColor      = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                samplerInfo.maxLod           = 1000.0f;
                if ( vkCreateSampler( _device, &samplerInfo, nullptr, &_arrStaticSampler[samplerIndex] ) != VK_SUCCESS )
                    return false;
            }
        }

        // 2) set 0 = 슬롯 세트. binding 0..15 = b# (UBO), 16..31 = t# (SSBO), 32..47 = u# (SSBO). 전부 partially-bound —
        //    드로우마다 실제로 쓰인 슬롯만 쓴다(셰이더가 정적으로 참조하는 슬롯은 반드시 걸려 있어야 한다: b0/b1 은 더미로 채운다).
        {
            VkDescriptorSetLayoutBinding arrBinding[vk::kSlotBindingCount]{};
            VkDescriptorBindingFlags     arrFlags[vk::kSlotBindingCount]{};
            for ( uint32 bindingIndex = 0; bindingIndex < vk::kSlotBindingCount; ++bindingIndex )
            {
                arrBinding[bindingIndex].binding         = bindingIndex;
                arrBinding[bindingIndex].descriptorType  = ( bindingIndex < vk::kTShift ) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                arrBinding[bindingIndex].descriptorCount = 1;
                arrBinding[bindingIndex].stageFlags      = allStages;
                arrFlags[bindingIndex]                   = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
            }
            VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
            bindingFlagsInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bindingFlagsInfo.bindingCount  = vk::kSlotBindingCount;
            bindingFlagsInfo.pBindingFlags = arrFlags;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.pNext        = &bindingFlagsInfo;
            layoutInfo.bindingCount = vk::kSlotBindingCount;
            layoutInfo.pBindings    = arrBinding;
            if ( vkCreateDescriptorSetLayout( _device, &layoutInfo, nullptr, &_slotSetLayout ) != VK_SUCCESS )
            {
                SW_LOG_ERROR( "슬롯 세트 레이아웃 생성 실패 (descriptor indexing / partially-bound 미지원?)." );
                return false;
            }
        }

        // 3) set 1 = 텍스처 배열(binding 0, update-after-bind) + immutable sampler 배열(binding 1) + 비교 샘플러(binding 2)
        //    + RW 텍스처 배열(binding 3, STORAGE_IMAGE, update-after-bind).
        {
            constexpr uint32             kBindingCount = 4;
            VkDescriptorSetLayoutBinding arrBinding[kBindingCount]{};
            arrBinding[0].binding            = bindless::kVkTextureBinding;
            arrBinding[0].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            arrBinding[0].descriptorCount    = kBindlessTextureCount;
            arrBinding[0].stageFlags         = allStages;
            arrBinding[1].binding            = bindless::kVkSamplerBinding;
            arrBinding[1].descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLER;
            arrBinding[1].descriptorCount    = shaderslot::kStaticSamplerArrayCount;
            arrBinding[1].stageFlags         = allStages;
            arrBinding[1].pImmutableSamplers = _arrStaticSampler;
            arrBinding[2].binding            = bindless::kVkShadowSamplerBinding;
            arrBinding[2].descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLER;
            arrBinding[2].descriptorCount    = 1;
            arrBinding[2].stageFlags         = allStages;
            arrBinding[2].pImmutableSamplers = &_arrStaticSampler[shaderslot::kSamplerShadowCmp];
            arrBinding[3].binding            = bindless::kVkRwTextureBinding;
            arrBinding[3].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            arrBinding[3].descriptorCount    = kBindlessStorageImageCount;
            arrBinding[3].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorBindingFlags arrFlags[kBindingCount] = {
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, 0, 0,
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT };
            VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
            bindingFlagsInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bindingFlagsInfo.bindingCount  = kBindingCount;
            bindingFlagsInfo.pBindingFlags = arrFlags;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.pNext        = &bindingFlagsInfo;
            layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            layoutInfo.bindingCount = kBindingCount;
            layoutInfo.pBindings    = arrBinding;
            if ( vkCreateDescriptorSetLayout( _device, &layoutInfo, nullptr, &_textureSetLayout ) != VK_SUCCESS )
            {
                SW_LOG_ERROR( "텍스처 배열 세트 레이아웃 생성 실패 (update-after-bind 미지원?)." );
                return false;
            }
        }

        // 4) 파이프라인 레이아웃 — set 0 슬롯, set 1 텍스처 배열, 푸시 상수(setComputeRootConstants, 16 dword).
        {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = allStages;
            pushRange.offset     = 0;
            pushRange.size       = kMaxComputeRootConstantDwords * sizeof( uint32 );

            VkDescriptorSetLayout arrSetLayout[2] = { _slotSetLayout, _textureSetLayout };

            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount         = 2;
            pipelineLayoutInfo.pSetLayouts            = arrSetLayout;
            pipelineLayoutInfo.pushConstantRangeCount = 1;
            pipelineLayoutInfo.pPushConstantRanges    = &pushRange;
            if ( vkCreatePipelineLayout( _device, &pipelineLayoutInfo, nullptr, &_pipelineLayout ) != VK_SUCCESS )
                return false;
        }

        // 5) 풀 — 텍스처 세트용 하나(update-after-bind), 슬롯 세트용은 프레임 링마다 하나(beginFrame 이 통째로 리셋).
        {
            VkDescriptorPoolSize arrPoolSize[] = {
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,           kBindlessTextureCount},
                {               VK_DESCRIPTOR_TYPE_SAMPLER, shaderslot::kStaticSamplerCount},
                {         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,      kBindlessStorageImageCount},
            };
            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
            poolInfo.maxSets       = 1;
            poolInfo.poolSizeCount = static_cast<uint32>( sizeof( arrPoolSize ) / sizeof( arrPoolSize[0] ) );
            poolInfo.pPoolSizes    = arrPoolSize;
            if ( vkCreateDescriptorPool( _device, &poolInfo, nullptr, &_descriptorPool ) != VK_SUCCESS )
                return false;
        }
        for ( VulkanDescriptorPoolSet& poolSet : _arrFrameDescriptorPoolSet )
        {
            const VkDescriptorPool pool = createSlotPool();
            if ( pool == VK_NULL_HANDLE )
                return false;
            poolSet._listPool.push_back( pool );
            poolSet._cursor = 0;
        }

        // 6) 셰이더가 정적으로 참조하지만 엔진이 안 건 b# 슬롯(픽스처의 MaterialCB 등)이 가리킬 0 채운 더미 UBO.
        {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size        = kDummyUboSize;
            bufferInfo.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if ( vkCreateBuffer( _device, &bufferInfo, nullptr, &_dummyUBO ) != VK_SUCCESS )
                return false;
            VkMemoryRequirements memReq{};
            vkGetBufferMemoryRequirements( _device, _dummyUBO, &memReq );
            uint32 memoryTypeIndex{ 0 };
            if ( findMemoryType( memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex ) == false )
                return false;
            VkMemoryAllocateInfo allocMem{};
            allocMem.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocMem.allocationSize  = memReq.size;
            allocMem.memoryTypeIndex = memoryTypeIndex;
            if ( vkAllocateMemory( _device, &allocMem, nullptr, &_dummyUBOMemory ) != VK_SUCCESS )
                return false;
            vkBindBufferMemory( _device, _dummyUBO, _dummyUBOMemory, 0 );
            void* pMapped{ nullptr };
            if ( vkMapMemory( _device, _dummyUBOMemory, 0, kDummyUboSize, 0, &pMapped ) == VK_SUCCESS && pMapped != nullptr )
            {
                Memory::set( pMapped, 0, static_cast<size_t>( kDummyUboSize ) );
                vkUnmapMemory( _device, _dummyUBOMemory );
            }
        }

        return true;
    }

    bool VulkanRHIDevice::ensureTextureSet()
    {
        if ( _textureSet != VK_NULL_HANDLE )
            return true;
        if ( _device == VK_NULL_HANDLE || _descriptorPool == VK_NULL_HANDLE || _defaultSampler == VK_NULL_HANDLE || _textureSetLayout == VK_NULL_HANDLE )
            return false;

        // 1x1 더미 이미지 — 안 쓰는 텍스처 원소가 유효하도록.
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
        allocInfo.pSetLayouts        = &_textureSetLayout;
        if ( vkAllocateDescriptorSets( _device, &allocInfo, &_textureSet ) != VK_SUCCESS )
        {
            SW_LOG_ERROR( "텍스처 배열 세트 할당 실패." );
            return false;
        }

        vector<VkDescriptorImageInfo> listImage( kBindlessTextureCount );
        for ( VkDescriptorImageInfo& info : listImage )
        {
            info.sampler     = _defaultSampler;
            info.imageView   = _bindlessDummyView;
            info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = _textureSet;
        write.dstBinding      = shaderslot::bindless::kVkTextureBinding;
        write.dstArrayElement = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = kBindlessTextureCount;
        write.pImageInfo      = listImage.data();
        vkUpdateDescriptorSets( _device, 1, &write, 0, nullptr );
        SW_LOG_INFO( "Bindless texture array ready (%# slots).", kBindlessTextureCount );
        return true;
    }

    void VulkanRHIDevice::writeBindlessTextureSlot( RHIDescriptorIndex index, VkImageView view, uint32 imageLayout )
    {
        if ( _textureSet == VK_NULL_HANDLE || view == VK_NULL_HANDLE || index >= kBindlessTextureCount )
            return;

        // 레이아웃은 샘플 시점에 이미지가 실제로 있을 레이아웃과 같아야 한다 — 컬러는 SHADER_READ_ONLY,
        // 깊이는 prepareTextureForShaderRead 가 옮기는 DEPTH_STENCIL_READ_ONLY (호출자가 고른다).
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler     = _defaultSampler;
        imageInfo.imageView   = view;
        imageInfo.imageLayout = static_cast<VkImageLayout>( imageLayout );

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = _textureSet;
        write.dstBinding      = shaderslot::bindless::kVkTextureBinding;
        write.dstArrayElement = index;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imageInfo;
        vkUpdateDescriptorSets( _device, 1, &write, 0, nullptr );
    }

    void VulkanRHIDevice::writeBindlessStorageImageSlot( RHIDescriptorIndex index, VkImageView view )
    {
        if ( _textureSet == VK_NULL_HANDLE || view == VK_NULL_HANDLE || index >= kBindlessStorageImageCount )
            return;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView   = view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // 스토리지 이미지는 GENERAL — prepareTextureForUnorderedAccess 가 맞춘다

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = _textureSet;
        write.dstBinding      = shaderslot::bindless::kVkRwTextureBinding;
        write.dstArrayElement = index;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.descriptorCount = 1;
        write.pImageInfo      = &imageInfo;
        vkUpdateDescriptorSets( _device, 1, &write, 0, nullptr );
    }

    VkDescriptorPool VulkanRHIDevice::createSlotPool()
    {
        // 세트 하나가 b/t/u 슬롯을 전부 걸 수 있게 잡는다 — 예전엔 SSBO 를 세트당 6개로 잡아 컴퓨트 패스가 많으면 maxSets 전에 바닥났다.
        VkDescriptorPoolSize arrPoolSize[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,                             kSlotSetsPerPool * shaderslot::kConstantBufferSlotCount},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kSlotSetsPerPool * ( shaderslot::kSrvSlotCount + shaderslot::kComputeUavSlotCount )},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets       = kSlotSetsPerPool;
        poolInfo.poolSizeCount = static_cast<uint32>( sizeof( arrPoolSize ) / sizeof( arrPoolSize[0] ) );
        poolInfo.pPoolSizes    = arrPoolSize;
        VkDescriptorPool pool{ VK_NULL_HANDLE };
        if ( vkCreateDescriptorPool( _device, &poolInfo, nullptr, &pool ) != VK_SUCCESS )
            return VK_NULL_HANDLE;
        return pool;
    }

    VkDescriptorSet VulkanRHIDevice::allocateSlotSet( VulkanDescriptorPoolSet& poolSet )
    {
        // 락이 없다 — 풀 묶음은 커맨드 버퍼 하나의 것이고, 그 버퍼는 한 스레드만 기록한다(VkDescriptorPool 은 외부 동기화 대상).
        if ( _slotSetLayout == VK_NULL_HANDLE )
            return VK_NULL_HANDLE;

        vector<VkDescriptorPool>& listPool = poolSet._listPool;
        uint32&                   cursor   = poolSet._cursor;
        while ( true )
        {
            if ( cursor >= listPool.size() )
            {
                // 언리얼처럼 풀이 차면 하나 더 만든다. 만든 풀은 묶음이 사는 동안 유지된다(리셋만 한다).
                if ( listPool.size() >= kMaxPoolsPerDescriptorPoolSet )
                {
                    if ( poolSet._bExhaustedLogged == SW_FALSE )
                    {
                        poolSet._bExhaustedLogged = SW_TRUE;
                        SW_LOG_ERROR( "슬롯 세트 풀 묶음이 상한(%#×%#)에 닿았습니다 — 이 버퍼의 나머지 드로우는 이전 세트로 그립니다.", kMaxPoolsPerDescriptorPoolSet, kSlotSetsPerPool );
                    }
                    return VK_NULL_HANDLE;
                }
                const VkDescriptorPool pool = createSlotPool();
                if ( pool == VK_NULL_HANDLE )
                    return VK_NULL_HANDLE;
                listPool.push_back( pool );
            }

            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool     = listPool[cursor];
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts        = &_slotSetLayout;
            VkDescriptorSet set{ VK_NULL_HANDLE };
            const VkResult  result = vkAllocateDescriptorSets( _device, &allocInfo, &set );
            if ( result == VK_SUCCESS )
                return set;
            if ( result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL )
                return VK_NULL_HANDLE;
            ++cursor; // 이 풀은 찼다 — 다음 풀로
        }
    }

    void VulkanRHIDevice::resetDescriptorPoolSet( VulkanDescriptorPoolSet& poolSet )
    {
        if ( _device == VK_NULL_HANDLE )
            return;
        for ( VkDescriptorPool pool : poolSet._listPool )
        {
            if ( pool != VK_NULL_HANDLE )
                vkResetDescriptorPool( _device, pool, 0 );
        }
        poolSet._cursor = 0;
    }

    void VulkanRHIDevice::destroyDescriptorPoolSet( VulkanDescriptorPoolSet& poolSet )
    {
        if ( _device != VK_NULL_HANDLE )
        {
            for ( VkDescriptorPool pool : poolSet._listPool )
            {
                if ( pool != VK_NULL_HANDLE )
                    vkDestroyDescriptorPool( _device, pool, nullptr );
            }
        }
        poolSet._listPool.clear();
        poolSet._cursor           = 0;
        poolSet._bExhaustedLogged = SW_FALSE;
    }
} // namespace sw
