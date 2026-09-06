/**
 * @file VulkanRHICommandList.h
 * @brief 소프트웨어 Cmd-vector 기록 없이 즉시 VulkanRHICommandContext를 호출하는 IRHICommandList
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/RHICommandListForward.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"

namespace sw
{
    class VulkanRHIDevice;

    /**
     * @class VulkanRHICommandList
     * @brief `RHIDeferredCommandList`(CPU `Cmd` 벡터에 기록 후 나중에 replay)를 대체하는 IRHICommandList.
     * @details 이 리스트는 **자기 커맨드 풀 + 커맨드 버퍼 + 기록 상태**를 소유한다. `VkCommandPool` 은
     *          외부 동기화 대상이라, 여러 리스트가 서로 다른 스레드에서 동시에 기록하려면 풀이
     *          리스트마다 따로여야 한다(DX12 의 얼로케이터와 같은 제약). 쌍은 디바이스 풀에서 빌리고
     *          다 쓰면 GPU 펜스 통과 후 돌려준다.
     *          `endCommandList` 로 버퍼를 닫고 `IRHIDevice::executeCommandList` 로 넘기면, 디바이스가
     *          프레임 스트림을 그 지점에서 잘라 [지금까지의 세그먼트][이 리스트][새 세그먼트] 순서로
     *          잇고 `endFrame` 에서 한 번에 제출한다 — 같은 큐의 제출 순서가 곧 실행 순서다.
     */
    class VulkanRHICommandList : public IRHICommandList
    {
    public:
        /** @brief 디바이스 풀에서 커맨드 풀 + 버퍼 쌍을 빌립니다. */
        explicit VulkanRHICommandList( VulkanRHIDevice* pDevice );
        /** @brief 빌린 쌍을 GPU 펜스 통과 후 풀로 돌려보냅니다. */
        ~VulkanRHICommandList() override;

        VulkanRHICommandList( const VulkanRHICommandList& )            = delete;
        VulkanRHICommandList& operator=( const VulkanRHICommandList& ) = delete;

        /** @brief `IRHIDevice::executeCommandList` 가 프레임 스트림에 끼워 넣는 네이티브 버퍼. */
        VkCommandBuffer nativeCommandBuffer() const { return _entry._buffer; }

        void beginCommandList() override;
        void endCommandList() override;

        SW_FORWARD_RHI_COMMAND_LIST( _context )

    private:
        VulkanRHIDevice*        _pDevice{ nullptr };
        VulkanCommandListEntry  _entry{};          ///< 이 리스트 전용 커맨드 풀 + 커맨드 버퍼
        uint8                   _bEntryDirty{ 0 }; ///< 현재 쌍에 이미 기록했는가(있으면 다음 begin 때 교체)
        VulkanRecordingState    _state{};
        VulkanRHICommandContext _context;
    };
} // namespace sw
