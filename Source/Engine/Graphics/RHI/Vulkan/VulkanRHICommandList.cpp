#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandList.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

#include <vulkan/vulkan.h>

namespace sw
{
    VulkanRHICommandList::VulkanRHICommandList( VulkanRHIDevice* pDevice )
        : _pDevice{ pDevice }
        , _entry{ pDevice != nullptr ? pDevice->acquireCommandListEntry() : VulkanCommandListEntry{} }
        , _state{}
        , _context{ pDevice, _entry._buffer, &_state }
    {
    }

    VulkanRHICommandList::~VulkanRHICommandList()
    {
        if ( _pDevice != nullptr )
            _pDevice->recycleCommandListEntryDeferred( _entry );
    }

    void VulkanRHICommandList::beginCommandList()
    {
        _state = VulkanRecordingState{};
        if ( _pDevice == nullptr || _entry._buffer == VK_NULL_HANDLE || _entry._pool == VK_NULL_HANDLE )
            return;

        // 이 리스트 전용 풀이라 다른 스레드가 동시에 건드리지 않는다. 풀에서 빌려온 시점에 이미 GPU
        // 펜스를 통과했으므로 곧바로 리셋해도 안전하다.
        if ( vkResetCommandPool( _pDevice->getDevice(), _entry._pool, 0 ) != VK_SUCCESS )
            return;

        // primary 버퍼다 — 자기 안에서 직접 vkCmdBeginRenderPass 를 하므로 secondary 일 수 없다.
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = nullptr;

        vkBeginCommandBuffer( _entry._buffer, &beginInfo );
    }

    void VulkanRHICommandList::endCommandList()
    {
        if ( _entry._buffer == VK_NULL_HANDLE )
            return;

        // 이 리스트가 연 렌더패스가 남아 있으면 닫는다 — secondary 버퍼는 자기 안에서 완결되어야 한다.
        if ( _state._bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( _entry._buffer );
            _state._bRenderPassActive = SW_FALSE;
        }
        vkEndCommandBuffer( _entry._buffer );
    }
} // namespace sw
