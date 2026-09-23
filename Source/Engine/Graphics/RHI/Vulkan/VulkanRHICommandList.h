/**
 * @file VulkanRHICommandList.h
 * @brief CPU 기록 벡터 없이 VulkanRHICommandContext 를 곧바로 부르는 IRHICommandList 입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/RHICommandListForwarder.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

namespace sw
{
    class VulkanRHIDevice;

    /**
     * @class VulkanRHICommandList
     * @brief 예전의 `RHIDeferredCommandList`(CPU `Cmd` 벡터에 기록한 뒤 나중에 재생)를 대신하는 IRHICommandList 입니다.
     * @details 이 리스트는 **자기 커맨드 풀 + 커맨드 버퍼 + 기록 상태**를 소유합니다. `VkCommandPool` 은
     *          외부 동기화 대상이라, 여러 리스트가 서로 다른 스레드에서 동시에 기록하려면 풀이
     *          리스트마다 따로여야 합니다(DX12 의 얼로케이터와 같은 제약). 쌍은 디바이스 풀에서 빌리고
     *          다 쓰면 GPU 펜스 통과 후 돌려줍니다.
     *          `endCommandList` 로 버퍼를 닫고 `IRHIDevice::executeCommandList` 로 넘기면, 디바이스가
     *          프레임 스트림을 그 지점에서 잘라 [지금까지의 세그먼트][이 리스트][새 세그먼트] 순서로
     *          잇고 `endFrame` 에서 한 번에 제출합니다. 같은 큐의 제출 순서가 곧 실행 순서입니다.
     */
    class VulkanRHICommandList : public RHICommandListForwarder<VulkanRHICommandContext>
    {
    public:
        /** @brief 디바이스 풀에서 커맨드 풀 + 버퍼 쌍을 빌립니다. */
        explicit VulkanRHICommandList( VulkanRHIDevice* pDevice );
        /** @brief 빌린 쌍을 GPU 펜스 통과 후 풀로 돌려보냅니다. */
        ~VulkanRHICommandList() override;

        VulkanRHICommandList( const VulkanRHICommandList& )            = delete;
        VulkanRHICommandList& operator=( const VulkanRHICommandList& ) = delete;

        /** @brief `IRHIDevice::executeCommandList` 가 프레임 스트림에 끼워 넣는 네이티브 버퍼입니다. */
        VkCommandBuffer nativeCommandBuffer() const { return _entry._buffer; }

        void beginCommandList() override;
        void endCommandList() override;
        void writeTimestamp( uint32 slotIndex ) override;
        /** @brief 디바이스가 내려갈 때 쌍을 지금 부수고 연결을 끊습니다(디바이스 종료가 부릅니다). */
        void detachFromDevice();

    private:
        VulkanRHIDevice*        _pDevice;
        VulkanCommandListEntry  _entry;       ///< 이 리스트 전용 커맨드 풀 + 커맨드 버퍼
        uint8                   _bEntryDirty; ///< 현재 쌍에 이미 기록했는가(있으면 다음 begin 때 교체)
        VulkanRecordingState    _state;
        VulkanRHICommandContext _context;
    };
} // namespace sw
