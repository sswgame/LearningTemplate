/**
 * @file VulkanMemoryAccount.h
 * @brief Vulkan 장치 메모리 할당 수의 계수입니다. Vulkan 헤더 없이 읽혀 테스트가 씁니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class VulkanMemoryAccount
     * @brief 살아 있는 `VkDeviceMemory` 수를 세고, 장치 한도(`maxMemoryAllocationCount`)에서 남겨 둘 칸(`EngineConfig::_rhi._allocationCountReserve`)을 넘는 할당을 거절합니다.
     * @details 한도를 넘는 `vkAllocateMemory` 는 명세상 실패(`VK_ERROR_TOO_MANY_OBJECTS`)이거나 드라이버에 따라 정의되지 않은 동작이다 — 데스크톱도 흔히 4096 이다(R-4).
     *          그 근처에 닿으면 조용히 깨지지 않고 오류로 멈춘다. 자원은 블록을 나눠 쓰므로(`VulkanMemoryAllocator`) 이 계수가 세는 것은 블록과 전용 할당이다.
     *          이 계수가 블록 수를 센다. 모든 할당 · 해제가 `VulkanRHIDevice::allocateDeviceMemory` · `freeDeviceMemory` 를 지나 이 계수를 거친다. 여러 스레드가 부른다.
     */
    class VulkanMemoryAccount
    {
    public:
        /** @brief 장치 한도 @p maxAllocationCount 와 남겨 둘 칸 @p reserve 를 정합니다. 쓸 수 있는 수는 그 차이입니다(남길 칸이 한도 이상이면 0). */
        void setLimit( uint32 maxAllocationCount, uint32 reserve ) { _usableCount = reserve < maxAllocationCount ? maxAllocationCount - reserve : 0u; }

        /** @brief 할당 하나를 셉니다. 쓸 수 있는 수에 닿았으면 세지 않고 false 입니다. */
        [[nodiscard]] bool tryAcquire()
        {
            uint32 liveCount = _liveCount.load();
            do
            {
                if ( liveCount >= _usableCount )
                    return false;
            } while ( _liveCount.compare_exchange_weak( liveCount, liveCount + 1u ) == false );
            return true;
        }

        /** @brief 해제 하나를 셉니다. */
        void release() { _liveCount.fetch_sub( 1u ); }

        /** @brief 살아 있는 할당 수입니다. */
        uint32 getLiveCount() const { return _liveCount.load(); }
        /** @brief 거절 없이 둘 수 있는 할당 수입니다. */
        uint32 getUsableCount() const { return _usableCount; }

    private:
        atomic<uint32> _liveCount{ 0 };
        uint32         _usableCount{ 0 }; ///< 장치 생성 때 한 번 정한다(그 뒤 읽기만)
    };
} // namespace sw
