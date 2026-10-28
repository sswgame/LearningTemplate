/**
 * @file VulkanOneShotCommands.h
 * @brief 프레임 밖 일회성 커맨드 버퍼(할당, 기록, 제출, 대기, 해제)입니다. Vulkan 백엔드 내부 전용입니다.
 */
#pragma once
#include "Core/Common/PlatformOSHeaders.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/mutex.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanResultCheck.h"
#include "Engine/Profiling/FrameProfiler.h"

#include <vulkan/vulkan.h>

namespace sw
{
    /**
     * @class VulkanOneShotCommands
     * @brief 일회용 커맨드 버퍼 하나입니다. 할당과 시작은 생성자가, **해제는 소멸자가** 합니다.
     *
     * @details 프레임 밖 구조 버퍼 갱신(`updateStructuredBufferRegions`)과 디바이스의 GPU 시계 읽기(`readGPUClockNanos`, 타임스탬프 하나를 내고 기다립니다)가 씁니다
     *          (할당 → begin → … → end → submit → 펜스 대기 → free). 텍스처 전송은 기다리지 않는 전송 작업(`VulkanRHIDevice::VulkanTransferJob`)을 쓴다.
     *          해제를 손으로 적으면 **중간에 검사(`return`)를 하나 더하는 날 커맨드 버퍼가 누수됩니다.** 풀에서 조용히
     *          자라다가 나중에 할당이 실패합니다. 해제를 소멸자에 두면 그 실수가 생길 수 없습니다.
     *
     * @note 장치 핸들과 락을 인자로 받습니다. `VulkanRHIDevice` 의 그 멤버들은 private 이고
     *       `VulkanRHIResourceFactory` 만 friend 라, 이 클래스가 장치를 직접 알 수는 없습니다.
     *       **일회성 풀 락은 이 객체가 사는 동안 잡고 있습니다**(풀은 외부 동기화 대상이고 게임 스레드와 로더 스레드가 함께 업로드합니다).
     *       큐 락은 제출과 대기 동안만 쥡니다.
     */
    class VulkanOneShotCommands
    {
    public:
        /** @brief 일회성 풀 락을 잡고 커맨드 버퍼를 하나 할당해 기록을 시작합니다. 실패하면 `isValid()` 가 false 입니다. */
        VulkanOneShotCommands( VkDevice device, VkCommandPool commandPool, mutex& poolMutex, VkQueue queue, mutex& queueMutex )
            : _poolLock{ poolMutex }
            , _queueMutex{ queueMutex }
            , _device{ device }
            , _commandPool{ commandPool }
            , _queue{ queue }
            , _commandBuffer{ VK_NULL_HANDLE }
        {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool        = _commandPool;
            allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            if ( vkAllocateCommandBuffers( _device, &allocInfo, &_commandBuffer ) != VK_SUCCESS )
            {
                _commandBuffer = VK_NULL_HANDLE;
                return;
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            // 시작하지 못한 버퍼에 기록하면 검증 오류 · 드라이버 크래시다 — 풀에 돌려주고 무효로 둔다(부르는 쪽은 isValid 로 거른다).
            if ( isVkSuccess( vkBeginCommandBuffer( _commandBuffer, &beginInfo ), "vkBeginCommandBuffer (one-shot)" ) == false )
            {
                vkFreeCommandBuffers( _device, _commandPool, 1, &_commandBuffer );
                _commandBuffer = VK_NULL_HANDLE;
            }
        }

        ~VulkanOneShotCommands()
        {
            if ( _commandBuffer != VK_NULL_HANDLE )
                vkFreeCommandBuffers( _device, _commandPool, 1, &_commandBuffer );
        }

        VulkanOneShotCommands( const VulkanOneShotCommands& )            = delete;
        VulkanOneShotCommands& operator=( const VulkanOneShotCommands& ) = delete;

        /** @brief 커맨드 버퍼를 얻었는지 여부입니다. */
        bool isValid() const { return _commandBuffer != VK_NULL_HANDLE; }
        /** @brief 기록 대상 커맨드 버퍼입니다. */
        VkCommandBuffer get() const { return _commandBuffer; }

        /**
         * @brief 기록을 끝내고 제출한 뒤 **이 제출의 펜스**가 신호할 때까지 기다립니다(RHIAsyncUploads U2).
         * @details 큐 전체를 비우는 `vkQueueWaitIdle` 대신 제출에 단 `VkFence` 를 기다린다. 대기 범위가 큐에 쌓인 이전 프레임 전체에서 내 제출로 줄고, 큐 락은 제출하는
         *          동안만 쥐며 대기는 락 밖에서 한다(다른 스레드의 프레임 제출을 막지 않는다). 일회성 풀 락은 커맨드 버퍼 기록과 해제에 필요하므로(Vulkan 사양의 외부 동기화 규칙)
         *          제출이 끝나면 놓고 대기한 뒤 다시 잡는다 — 해제는 소멸자가 한다.
         *          호출 스레드는 자기 제출이 끝날 때까지 기다린다.
         */
        bool endSubmitAndWait()
        {
            if ( isValid() == false )
                return false;

            // 프레임 밖 버퍼 갱신 · 시계 읽기가 제출을 기다리는 지점이다(대기 계측). 제출 락 대기와 펜스 대기를 함께 잰다.
            SW_PROFILE_SCOPE( "RT.Upload.waitIdle" );
            _lastResult = vkEndCommandBuffer( _commandBuffer );
            if ( isVkSuccess( _lastResult, "vkEndCommandBuffer (one-shot)" ) == false )
                return false;

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence fence{ VK_NULL_HANDLE };
            _lastResult = vkCreateFence( _device, &fenceInfo, nullptr, &fence );
            if ( _lastResult != VK_SUCCESS )
                return false;

            VkSubmitInfo submitInfo{};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &_commandBuffer;
            {
                ScopedLock<mutex> queueLock{ _queueMutex };
                _lastResult = vkQueueSubmit( _queue, 1, &submitInfo, fence );
            }
            if ( _lastResult == VK_SUCCESS )
            {
                _poolLock.unlock();
                _lastResult = vkWaitForFences( _device, 1, &fence, VK_TRUE, UINT64_MAX );
                _poolLock.lock();
            }
            vkDestroyFence( _device, fence, nullptr );
            return _lastResult == VK_SUCCESS;
        }

        /** @brief 마지막 `endSubmitAndWait` 가 본 `VkResult` 입니다(제출 실패의 이유이며 `VK_ERROR_DEVICE_LOST` 인지 가립니다). */
        [[nodiscard]] VkResult getLastResult() const { return _lastResult; }

    private:
        std::unique_lock<mutex> _poolLock;
        mutex&                  _queueMutex;
        VkDevice                _device;
        VkCommandPool           _commandPool;
        VkQueue                 _queue;
        VkCommandBuffer         _commandBuffer;
        VkResult                _lastResult{ VK_SUCCESS };
    };
} // namespace sw
