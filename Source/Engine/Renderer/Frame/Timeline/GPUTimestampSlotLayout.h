/**
 * @file GPUTimestampSlotLayout.h
 * @brief GPU 타임스탬프 슬롯(`constant::render::kMaxGPUTimestampSlot` 개)을 뷰 패스 · 프레임 패스 · 프레임 전체가 나눠 쓰는 배치입니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct GPUTimestampSlotLayout
     * @brief 타임스탬프 슬롯 번호의 배치입니다 — 쓰는 쪽(`FramePassExecutor` · `GPUFrameClock`)과 읽는 쪽(`GPUPassTimeline` · `GPUTimelineExporter`)이 같은 번호를 본다.
     * @details [뷰 패스 쌍 × kTimedPassCapacity][프레임 패스 쌍 × kTimedFramePassCapacity][빈 칸][예약 넷]. 뷰 패스는 앞에서부터 인덱스 × 2 쌍,
     *          프레임 패스(`_listFramePass`)는 그 뒤에서부터 인덱스 × 2 쌍을 쓴다(병렬 기록이라 흐르는 카운터가 아니라 고정 슬롯).
     *          프레임 전체와 프레임 그래프는 **뒤쪽 네 슬롯**을 쓴다. 백엔드는 가장 이른 시각을 기준점으로 삼으므로 뒤쪽 슬롯이 먼저 기록되어도 값이 잘리지 않는다.
     *          프레임 시작 · 끝 쌍은 `GPUFrameClock` 이 늘(Shipping 포함) 쓰고, 프레임 그래프 쌍은 프로파일러가 기록 중일 때만 쓴다.
     */
    struct GPUTimestampSlotLayout
    {
        static constexpr uint32 kFrameEnd     = constant::render::kMaxGPUTimestampSlot - 4u;
        static constexpr uint32 kComputeBegin = constant::render::kMaxGPUTimestampSlot - 3u;
        static constexpr uint32 kComputeEnd   = constant::render::kMaxGPUTimestampSlot - 2u;
        static constexpr uint32 kFrameBegin   = constant::render::kMaxGPUTimestampSlot - 1u;
        /// @brief GPU 시간을 측정할 수 있는 뷰 패스 수입니다(패스마다 슬롯 둘). 넘는 패스는 시간이 기록되지 않으며, 파이프라인을 로드할 때 경고합니다.
        static constexpr uint32 kTimedPassCapacity = 40;
        /// @brief 뷰 패스 쌍이 쓰는 슬롯의 끝(미포함) = 프레임 패스 쌍의 첫 슬롯입니다.
        static constexpr uint32 kPassSlotEnd        = kTimedPassCapacity * 2u;
        static constexpr uint32 kFramePassSlotBegin = kPassSlotEnd;
        /// @brief 예약 슬롯(프레임 끝 · 프레임 그래프 쌍 · 프레임 시작)의 첫 번호입니다.
        static constexpr uint32 kReservedSlotBegin = kFrameEnd;
        /// @brief GPU 시간을 측정할 수 있는 프레임 패스 수입니다(프레임 패스 종류는 열하나 — `isFramePassType`). 위 예약 슬롯과 겹치지 않게 합니다.
        static constexpr uint32 kTimedFramePassCapacity = ( kReservedSlotBegin - kFramePassSlotBegin ) / 2u;

        /** @brief 프레임 패스 @p framePassIndex(`_listFramePass` 의 번호)의 시작 슬롯입니다(끝은 + 1). 측정 칸 밖이면 `constant::invalid_index::kUint32` 입니다. */
        static constexpr uint32 findFramePassSlot( size_t framePassIndex )
        {
            return framePassIndex < kTimedFramePassCapacity ? kFramePassSlotBegin + static_cast<uint32>( framePassIndex ) * 2u : constant::invalid_index::kUint32;
        }

        /** @brief 패스가 `passCount` 개인 파이프라인에서 GPU 시간이 기록되지 않는 패스 수입니다. 앞에서부터 `kTimedPassCapacity` 개만 측정합니다. */
        static constexpr uint32 countUntimedPass( size_t passCount )
        {
            return passCount > kTimedPassCapacity ? static_cast<uint32>( passCount - kTimedPassCapacity ) : 0u;
        }
        static_assert( kFrameBegin < constant::render::kMaxGPUTimestampSlot, "타임스탬프 예약 칸이 백엔드 칸 수를 넘는다" );
    };
} // namespace sw
