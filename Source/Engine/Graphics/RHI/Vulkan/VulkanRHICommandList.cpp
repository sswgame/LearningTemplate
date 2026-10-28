#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandList.h"

#include "Core/Foundation/PlatformOSHeaders.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanResultCheck.h"

#include <vulkan/vulkan.h>

namespace sw
{
    VulkanRHICommandList::VulkanRHICommandList( VulkanRHIDevice* pDevice )
        : _pDevice{ pDevice }
        , _entry{ pDevice != nullptr ? pDevice->acquireCommandListEntry() : VulkanCommandListEntry{} }
        , _bEntryDirty{ SW_FALSE }
        , _state{}
        , _context{ pDevice, _entry._buffer, &_state, _entry._pDescriptorPoolSet }
    {
        _pContext = &_context;
        if ( _pDevice != nullptr )
            _pDevice->registerCommandList( this );
    }

    VulkanRHICommandList::~VulkanRHICommandList()
    {
        if ( _pDevice != nullptr )
        {
            _pDevice->unregisterCommandList( this );
            _pDevice->recycleCommandListEntryDeferred( _entry );
        }
    }

    void VulkanRHICommandList::detachFromDevice()
    {
        // 디바이스가 종료되는 중이다. 반납하지 않고 쌍을 지금 파괴한다. 디스크립터 풀 세트는 디바이스가 통째로 비운다.
        if ( _pDevice != nullptr )
            _pDevice->destroyCommandListEntryImmediate( _entry );
        _state = VulkanRecordingState{};
        _entry = VulkanCommandListEntry{};
        _context.rebindCommandBuffer( VK_NULL_HANDLE, nullptr );
        _pDevice  = nullptr;
        _pContext = nullptr;
    }

    void VulkanRHICommandList::writeTimestamp( uint32 slotIndex )
    {
        const VkQueryPool pool                  = ( _pDevice != nullptr ) ? _pDevice->getTimestampPool() : VK_NULL_HANDLE;
        const bool        bCannotWriteTimestamp = pool == VK_NULL_HANDLE || slotIndex >= constant::kMaxGPUTimestampSlot || _entry._buffer == VK_NULL_HANDLE;
        if ( bCannotWriteTimestamp )
            return;
        // BOTTOM_OF_PIPE 는 "여기까지 GPU 가 다 끝냈다"를 뜻한다. DX12 의 EndQuery 와 같은 의미라
        // 백엔드끼리 값을 그대로 비교할 수 있다.
        vkCmdWriteTimestamp( _entry._buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool,
                             _pDevice->getTimestampBase() + slotIndex );
    }

    void VulkanRHICommandList::beginCommandList()
    {
        _state = VulkanRecordingState{};
        if ( _pDevice == nullptr )
            return;

        // 리스트 객체는 프레임을 넘어 재사용된다(FrameRenderer::_frameCmd). 같은 버퍼를 다시 Reset
        // 하면 직전 프레임 커맨드를 GPU 가 아직 읽는 중일 수 있으므로, 두 번째 기록부터는 쌍을 통째로
        // 교체한다. 쓰던 쌍은 펜스 통과 후 반납하고(대기 없음) 새 쌍은 이미 통과한 것만 들어 있는 풀에서
        // 빌린다. DX12 의 커맨드 얼로케이터와 같은 계약이다.
        if ( _bEntryDirty != SW_FALSE )
        {
            _pDevice->recycleCommandListEntryDeferred( _entry );
            _entry = _pDevice->acquireCommandListEntry();
            _context.rebindCommandBuffer( _entry._buffer, _entry._pDescriptorPoolSet );
            _bEntryDirty = SW_FALSE;
        }

        if ( _entry._buffer == VK_NULL_HANDLE )
            return;

        // 리셋 · 시작 · 끝이 실패하면(장치 분실 · 메모리 부족) 이 리스트로 기록할 길이 없다 — 이어 가면 다음 제출이 검증 오류 · 크래시라 멈춘다.
        requireVk( vkResetCommandBuffer( _entry._buffer, 0 ), "vkResetCommandBuffer (command list)" );
        // 이 쌍의 슬롯 세트들은 GPU 가 다 읽었다(재사용 풀은 펜스 통과분만 담는다). 커맨드 버퍼처럼 풀 그룹도 통째로 비운다.
        if ( _entry._pDescriptorPoolSet != nullptr )
            _pDevice->resetDescriptorPoolSet( *_entry._pDescriptorPoolSet );

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        requireVk( vkBeginCommandBuffer( _entry._buffer, &beginInfo ), "vkBeginCommandBuffer (command list)" );

        _bEntryDirty = SW_TRUE;

        // 새 버퍼라 동적 상태가 비어 있다. 파이프라인이 뷰포트와 시저를 동적으로 쓰므로 기본값을 설정한다.
        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = static_cast<float32>( _pDevice->_swapChain.getExtentHeight() );
        viewport.width    = static_cast<float32>( _pDevice->_swapChain.getExtentWidth() );
        viewport.height   = -static_cast<float32>( _pDevice->_swapChain.getExtentHeight() );
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport( _entry._buffer, 0, 1, &viewport );

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = { _pDevice->_swapChain.getExtentWidth(), _pDevice->_swapChain.getExtentHeight() };
        vkCmdSetScissor( _entry._buffer, 0, 1, &scissor );
    }

    void VulkanRHICommandList::endCommandList()
    {
        if ( _entry._buffer == VK_NULL_HANDLE )
            return;

        if ( _state._bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( _entry._buffer );
            _state._bRenderPassActive = SW_FALSE;
        }
        requireVk( vkEndCommandBuffer( _entry._buffer ), "vkEndCommandBuffer (command list)" );
    }
} // namespace sw
