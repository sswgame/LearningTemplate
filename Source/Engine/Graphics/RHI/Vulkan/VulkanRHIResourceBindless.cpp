/**
 * @file VulkanRHIResourceBindless.cpp
 * @brief Vulkan 의 bindless 등록 — 텍스처는 세트 1 배열의 원소에 쓰고, 버퍼는 인덱스 → 버퍼 핸들 표에 올린다
 * @details `VulkanRHIResource` 의 일부다. 버퍼 인덱스는 드로우/디스패치 직전 슬롯 세트를 쓸 때
 *          (VulkanRHICommandContext::flushSlotSet) VkBuffer 로 풀린다 — 등록 시점에 디스크립터를 쓰지 않는다.
 *          텍스처·버퍼(SRV/CB)·UAV 는 각자의 인덱스 공간이다. DX12 는 힙 하나라 인덱스 공간도 하나다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/Support/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResource.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include <vulkan/vulkan.h>

namespace sw
{
    SW_LOG_CALLER( "VulkanRHIResource" );

    RHIDescriptorIndex VulkanRHIResource::registerBindlessTexture( RHITextureHandle texture )
    {
        _pDevice->checkRegistryMutableNow( "registerBindlessTexture" );
        if ( texture == 0 || _pDevice->_textureSet == VK_NULL_HANDLE || _pDevice->_defaultSampler == VK_NULL_HANDLE )
            return kInvalidDescriptorIndex;

        VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( texture );
        if ( pResolved == nullptr || pResolved->_imageView == VK_NULL_HANDLE )
            return kInvalidDescriptorIndex;

        // 깊이 텍스처는 DEPTH|STENCIL 두 aspect 뷰로는 샘플 디스크립터를 못 만든다 — DEPTH 단일 aspect 뷰
        // (_sampleView) 를 쓰고, 샘플 시점 레이아웃(prepareTextureForShaderRead 가 옮기는
        // DEPTH_STENCIL_READ_ONLY_OPTIMAL) 을 디스크립터에도 같이 적는다. 예전엔 여기서 그냥 거부했다 —
        // 그러면 g_ShadowMapIndex 가 INVALID 가 되고 bindless 배열 범위 밖 읽기가 0 을 돌려줘, Vulkan 만
        // 모든 픽셀이 "완전 그림자"(x0.56) 로 어두웠다(검증 에러 없음, 큐브는 다 보인다).
        const bool        bDepth       = pResolved->_bDepthStencil != SW_FALSE;
        const VkImageView sampleView   = bDepth ? pResolved->_sampleView : pResolved->_imageView;
        const uint32      sampleLayout = static_cast<uint32>( bDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                                     : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
        if ( sampleView == VK_NULL_HANDLE )
            return kInvalidDescriptorIndex;

        VulkanRHIDevice::VulkanTextureRecord& record = *pResolved;
        if ( record._bindlessIndex != kInvalidDescriptorIndex )
            return record._bindlessIndex;

        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex            descriptorIndex = resolveFreeListIndex( _pDevice->_listTextureUsed, _pDevice->_listTextureFree );
        if ( descriptorIndex >= _pDevice->kBindlessTextureCount )
        {
            SW_LOG_ERROR( "Bindless texture table full." );
            return kInvalidDescriptorIndex;
        }

        _pDevice->writeBindlessTextureSlot( descriptorIndex, sampleView, sampleLayout );
        if ( descriptorIndex >= _pDevice->_listTextureUsed.size() )
            _pDevice->_listTextureUsed.resize( descriptorIndex + 1, 0 );
        _pDevice->_listTextureUsed[descriptorIndex] = 1;
        record._bindlessIndex                       = descriptorIndex;
        return descriptorIndex;
    }

    RHIDescriptorIndex VulkanRHIResource::registerBindlessResource( RHIBufferHandle buffer )
    {
        _pDevice->checkRegistryMutableNow( "registerBindlessResource" );
        if ( _pDevice->resolveAllocatedBuffer( buffer ) == nullptr )
            return kInvalidDescriptorIndex;

        // 버퍼는 인덱스 → 핸들 표에만 올린다. 실제 VkBuffer/오프셋은 슬롯 세트를 쓰는 순간(flushSlotSet)에 푼다 —
        // 링 상수버퍼의 프레임 오프셋도 그때 더한다. 예전엔 여기서 프레임 슬롯마다 세트를 만들었다.
        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        return allocateFreeListIndex( _pDevice->_listBindlessSourceBuffer, _pDevice->_listBindlessFree, buffer );
    }

    void VulkanRHIResource::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        _pDevice->checkRegistryMutableNow( "unregisterBindlessResource" );
        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        // 버퍼가 소유하지 않는 인덱스(텍스처 SRV 인덱스가 잘못 넘어왔거나 이중 해제)를 프리리스트에
        // 넣으면 다음 registerBindlessResource 가 살아 있는 다른 버퍼의 슬롯을 덮어쓴다 — 실제로
        // 트랜지언트 텍스처 인덱스 0·1·2 가 여기로 와서 패스 CB 슬롯 2 에 인스턴스 버퍼가 들어갔다.
        if ( index >= _pDevice->_listBindlessSourceBuffer.size() || _pDevice->_listBindlessSourceBuffer[index] == 0 )
        {
            SW_LOG_ERROR( "Bindless buffer index %# is not owned by any buffer; ignoring the release.", index );
            return;
        }
        _pDevice->_listBindlessSourceBuffer[index] = RHIBufferHandle{ 0 };
        deferFreeBufferIndex( index, false );
    }

    void VulkanRHIResource::deferFreeBufferIndex( RHIDescriptorIndex index, bool bUav )
    {
        // 인덱스는 이 프레임의 커맨드가 끝난 뒤에 재사용한다 — 같은 프레임에 등록된 새 버퍼가 아직 실행 중인
        // 세트가 가리키던 자리를 받지 않도록 (언리얼의 지연 해제와 같다).
        VulkanRHIDevice* pDevice = _pDevice;
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pDevice, index, bUav]()
        {
            std::unique_lock<std::shared_mutex> lock{ pDevice->_bindlessMutex };
            ( bUav ? pDevice->_listUavFree : pDevice->_listBindlessFree ).push_back( index );
        } ),
                                                   _pDevice->_frameFenceCounter + 1 );
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
        if ( index < _pDevice->_listTextureUsed.size() && _pDevice->_listTextureUsed[index] != 0 )
        {
            // 원소를 더미로 되돌리는 것도, 인덱스 재사용도 GPU 펜스 뒤에 한다 — update-after-bind 라도 실행 중인
            // 커맨드버퍼가 쓰는 디스크립터를 덮어쓰는 건 정의되지 않은 동작이고, 같은 프레임에 새 텍스처가
            // 이 인덱스를 받으면 이전 프레임이 새 텍스처를 샘플한다. 이미지 자체도 같은 펜스 뒤에 파괴된다(destroyTexture).
            _pDevice->_listTextureUsed[index] = 2; // 2 = 해제 대기 (등록 불가, 프리리스트에도 아직 없음)
            VulkanRHIDevice* pDevice          = _pDevice;
            _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pDevice, index]()
            {
                std::unique_lock<std::shared_mutex> lock{ pDevice->_bindlessMutex };
                pDevice->writeBindlessTextureSlot( index, pDevice->_bindlessDummyView, static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ) );
                releaseFreeListIndex( pDevice->_listTextureUsed, pDevice->_listTextureFree, index, uint8{ 0 } );
            } ),
                                                       _pDevice->_frameFenceCounter + 1 );
        }
        record._bindlessIndex = kInvalidDescriptorIndex;
    }

    RHIDescriptorIndex VulkanRHIResource::registerBindlessUAV( RHIBufferHandle buffer )
    {
        _pDevice->checkRegistryMutableNow( "registerBindlessUAV" );
        if ( _pDevice->resolveAllocatedBuffer( buffer ) == nullptr )
            return kInvalidDescriptorIndex;
        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex            index = allocateFreeListIndex( _pDevice->_listUavSourceBuffer, _pDevice->_listUavFree, buffer );
        if ( index >= _pDevice->_listUavSourceTexture.size() )
            _pDevice->_listUavSourceTexture.resize( index + 1, RHITextureHandle{ 0 } );
        _pDevice->_listUavSourceTexture[index] = RHITextureHandle{ 0 };
        return index;
    }

    RHIDescriptorIndex VulkanRHIResource::registerBindlessTextureUAV( RHITextureHandle texture )
    {
        _pDevice->checkRegistryMutableNow( "registerBindlessTextureUAV" );
        if ( texture == 0 || _pDevice->_textureSet == VK_NULL_HANDLE )
            return kInvalidDescriptorIndex;
        VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( texture );
        if ( pResolved == nullptr || pResolved->_imageView == VK_NULL_HANDLE || pResolved->_bDepthStencil != SW_FALSE )
            return kInvalidDescriptorIndex;

        // 버퍼 UAV 와 같은 인덱스 공간 — 인덱스가 곧 RW 텍스처 배열(set 1 binding 3)의 원소다.
        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex            index = allocateFreeListIndex( _pDevice->_listUavSourceBuffer, _pDevice->_listUavFree, RHIBufferHandle{ 0 } );
        if ( index >= _pDevice->kBindlessStorageImageCount )
        {
            SW_LOG_ERROR( "RW 텍스처 배열(%#)이 가득 찼습니다.", _pDevice->kBindlessStorageImageCount );
            releaseFreeListIndex( _pDevice->_listUavSourceBuffer, _pDevice->_listUavFree, index, RHIBufferHandle{ 0 } );
            return kInvalidDescriptorIndex;
        }
        if ( index >= _pDevice->_listUavSourceTexture.size() )
            _pDevice->_listUavSourceTexture.resize( index + 1, RHITextureHandle{ 0 } );
        _pDevice->_listUavSourceTexture[index] = texture;
        _pDevice->writeBindlessStorageImageSlot( index, pResolved->_imageView );
        return index;
    }

    void VulkanRHIResource::unregisterBindlessUAV( RHIDescriptorIndex index )
    {
        _pDevice->checkRegistryMutableNow( "unregisterBindlessUAV" );
        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const bool                          bBuffer  = index < _pDevice->_listUavSourceBuffer.size() && _pDevice->_listUavSourceBuffer[index] != 0;
        const bool                          bTexture = index < _pDevice->_listUavSourceTexture.size() && _pDevice->_listUavSourceTexture[index] != 0;
        if ( bBuffer == false && bTexture == false )
            return;
        if ( bBuffer )
            _pDevice->_listUavSourceBuffer[index] = RHIBufferHandle{ 0 };
        if ( bTexture )
            _pDevice->_listUavSourceTexture[index] = RHITextureHandle{ 0 };
        deferFreeBufferIndex( index, true );
    }
} // namespace sw
