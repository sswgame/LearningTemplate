#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandList.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

#include <vulkan/vulkan.h>

namespace sw
{
    VulkanRHICommandList::VulkanRHICommandList( VulkanRHIDevice* pDevice )
        : _pDevice{ pDevice }
        , _entry{ pDevice != nullptr ? pDevice->acquireCommandListEntry() : VulkanCommandListEntry{} }
        , _bEntryDirty{ 0 }
        , _state{}
        , _context{ pDevice, _entry._buffer, &_state }
    {
        _pContext = &_context;
    }

    VulkanRHICommandList::~VulkanRHICommandList()
    {
        if ( _pDevice != nullptr )
            _pDevice->recycleCommandListEntryDeferred( _entry );
    }

    void VulkanRHICommandList::beginCommandList()
    {
        _state = VulkanRecordingState{};
        if ( _pDevice == nullptr )
            return;

        // 리스트 객체는 프레임을 넘어 재사용된다(FrameRenderer::_frameCmd). 같은 버퍼를 다시 Reset
        // 하면 직전 프레임 커맨드를 GPU 가 아직 읽는 중일 수 있으므로, 두 번째 기록부터는 쌍을 통째로
        // 갈아 낀다 — 쓰던 쌍은 펜스 통과 후 반납하고(대기 없음) 새 쌍은 이미 통과한 것만 든 풀에서
        // 빌린다. DX12 의 커맨드 얼로케이터와 같은 계약이다.
        if ( _bEntryDirty != 0 )
        {
            _pDevice->recycleCommandListEntryDeferred( _entry );
            _entry = _pDevice->acquireCommandListEntry();
            _context.rebindCommandBuffer( _entry._buffer );
            _bEntryDirty = 0;
        }

        if ( _entry._buffer == VK_NULL_HANDLE )
            return;

        vkResetCommandBuffer( _entry._buffer, 0 );

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if ( vkBeginCommandBuffer( _entry._buffer, &beginInfo ) != VK_SUCCESS )
            return;

        _bEntryDirty = 1;

        // 새 버퍼라 동적 상태가 비어 있다 — 파이프라인이 뷰포트/시저를 동적으로 쓰므로 기본값을 깐다.
        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = static_cast<float32>( _pDevice->_swapChainExtentHeight );
        viewport.width    = static_cast<float32>( _pDevice->_swapChainExtentWidth );
        viewport.height   = -static_cast<float32>( _pDevice->_swapChainExtentHeight );
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport( _entry._buffer, 0, 1, &viewport );

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = { _pDevice->_swapChainExtentWidth, _pDevice->_swapChainExtentHeight };
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
        vkEndCommandBuffer( _entry._buffer );
    }
} // namespace sw
