/**
 * @file VulkanOneShotCommands.h
 * @brief 프레임 밖 일회성 커맨드 버퍼(할당, 기록, 제출, 대기, 해제)입니다. Vulkan 백엔드 내부 전용입니다.
 */
#pragma once
#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/mutex.h"

#include "Engine/Profiling/FrameProfiler.h"

#include <vulkan/vulkan.h>

namespace sw
{
    /**
     * @class VulkanOneShotCommands
     * @brief 일회용 커맨드 버퍼 하나입니다. 할당과 시작은 생성자가, **해제는 소멸자가** 합니다.
     *
     * @details 텍스처 업로드와 리드백이 같이 씁니다(할당 → begin → … → end → submit → waitIdle → free).
     *          디바이스의 GPU 시계 읽기(`readGPUClockNanos`, 타임스탬프 하나를 내고 기다립니다)도 씁니다.
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
            vkBeginCommandBuffer( _commandBuffer, &beginInfo );
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
         * @brief 기록을 끝내고 제출한 뒤 큐가 빌 때까지 기다립니다.
         * @details 로드 · 리드백 경로라 큐가 비기를 기다리는 값싼 동기 방식을 택했습니다
         *          (`executeCommandListImmediate` 와 같은 이유).
         */
        bool endSubmitAndWait()
        {
            if ( isValid() == false )
                return false;

            // 로드 · 리드백 업로드가 큐를 통째로 기다리는 지점이다(비동기 전환의 근거 계측). 제출 락 대기와 큐 비움 대기를 함께 잰다.
            SW_PROFILE_SCOPE( "RT.Upload.waitIdle" );
            vkEndCommandBuffer( _commandBuffer );

            VkSubmitInfo submitInfo{};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &_commandBuffer;
            std::scoped_lock<mutex> queueLock{ _queueMutex };
            _lastResult = vkQueueSubmit( _queue, 1, &submitInfo, VK_NULL_HANDLE );
            if ( _lastResult != VK_SUCCESS )
                return false;

            _lastResult = vkQueueWaitIdle( _queue );
            return _lastResult == VK_SUCCESS;
        }

        /** @brief 마지막 `endSubmitAndWait` 가 본 `VkResult` 입니다(제출 실패의 이유이며 `VK_ERROR_DEVICE_LOST` 인지 가립니다). */
        [[nodiscard]] VkResult getLastResult() const { return _lastResult; }

    private:
        std::scoped_lock<mutex> _poolLock;
        mutex&                  _queueMutex;
        VkDevice                _device;
        VkCommandPool           _commandPool;
        VkQueue                 _queue;
        VkCommandBuffer         _commandBuffer;
        VkResult                _lastResult{ VK_SUCCESS };
    };
} // namespace sw
