/**
 * @file GPUFrameClock.h
 * @brief 프레임 하나의 GPU 시간(첫 명령 ~ 마지막 패스 끝)을 늘 재는 시계입니다. Shipping 에도 들어갑니다.
 * @details 자동 렌더 배율(`RenderScaleAdjuster`)의 입력이고, GPU 타임스탬프 계측 스위치(`IRHITimestampProvider::setTimestampEnabled`)의 주인입니다.
 *          켜는 조건은 "프로파일러 기록 중 또는 자동 배율이 원함" 이고, 읽은 `RHITimestampFrame` 한 벌을 프로파일러(`GPUPassTimeline::reportFrame`)도 받습니다 — 쿼리는 한 벌입니다.
 *          언리얼 `FDynamicResolutionHeuristicProxy` 의 GPU 프레임 시간, 유니티 `FrameTimingManager`(`FrameTiming.gpuFrameTime`)에 해당합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/Diagnostics/RHITimestampFrame.h"

#include <atomic>

namespace sw
{
    class IRHICommandList;
    class IRHITimestampProvider;

    /** @brief 끝난 프레임 하나의 GPU 시간입니다. 2 ~ 3 프레임 늦고 겹치거나 건너뛸 수 있어, 쓰는 쪽은 순번이 바뀐 표본만 셉니다. */
    struct GPUFrameSample
    {
        uint32  _serial{ 0 }; ///< 읽은 순번(1 부터, 0 은 "아직 없음")
        float32 _gpuMs{ 0.0f };
    };

    /** @brief 시계의 상태입니다(게임 스레드가 락 없이 읽는다). */
    enum class GPUFrameClockState : uint8
    {
        Off,         ///< 아무도 원하지 않아 타임스탬프를 쓰지 않는다
        Unsupported, ///< 디바이스에 타임스탬프가 없다(제공자 없음, 또는 켠 뒤에도 슬롯이 열리지 않음)
        WarmingUp,   ///< 켰지만 아직 읽은 표본이 없다
        Ready        ///< 표본을 읽고 있다
    };

    /**
     * @class GPUFrameClock
     * @brief 렌더 스레드가 프레임 시작 · 끝 타임스탬프를 쓰고 끝난 프레임을 읽으며, 게임 스레드가 원자값 하나로 최신 표본을 읽습니다.
     */
    class SW_API GPUFrameClock
    {
    public:
        /// @brief 켠 뒤 슬롯이 열리기를 기다리는 프레임 수입니다. 백엔드는 켠 다음 프레임에 자원을 만들고(프레임 링 ≤ 3), 그보다 넉넉히 기다린 뒤 "없음" 으로 정한다.
        static constexpr uint32 kSlotOpenWaitFrames = 8;

        GPUFrameClock();

        GPUFrameClock( const GPUFrameClock& )            = delete;
        GPUFrameClock& operator=( const GPUFrameClock& ) = delete;

        /** @brief 자동 렌더 배율이 시계를 원하는지 정합니다. 게임 스레드에서 부릅니다(원자). */
        void setWanted( bool bWanted );
        /** @brief 프레임의 첫 GPU 명령 자리에 시작 타임스탬프를 씁니다(렌더 스레드). 이번 프레임에 재지 않으면 아무것도 하지 않습니다. */
        void writeFrameBegin( IRHICommandList& cmd ) const;
        /** @brief 마지막 패스 뒤에 끝 타임스탬프를 씁니다(렌더 스레드). */
        void writeFrameEnd( IRHICommandList& cmd ) const;
        /** @brief 이번 프레임에 시작 · 끝 타임스탬프를 쓰는지입니다(렌더 스레드). 끝 타임스탬프를 쓸 리스트를 마련할지 정할 때 봅니다. */
        bool isWritingFrame() const { return _bWritingFrame != SW_FALSE; }

        /**
         * @brief 프레임마다 한 번(제출 뒤) 부릅니다. 계측 스위치를 맞추고 끝난 프레임을 읽어 표본을 냅니다.
         * @param pTimestamps 디바이스의 타임스탬프 능력(`IRHIDevice::findTimestampProvider`). nullptr 이면 시계를 원할 때 `Unsupported` 입니다.
         * @param bProfilerRecording 프로파일러가 패스 시간을 기록 중이면 true 입니다(스위치를 켜는 다른 이유).
         * @return 이번에 읽은 프레임(프로파일러가 이어 씁니다), 읽지 못했으면 nullptr 입니다. 다음 `readBack` 까지 유효합니다.
         */
        const RHITimestampFrame* readBack( IRHITimestampProvider* pTimestamps, bool bProfilerRecording );
        /** @brief 이미 읽은 프레임에서 표본을 냅니다. 시작 · 끝 슬롯이 기록되지 않았으면(음수 · 거꾸로) 표본을 내지 않고 false 입니다. GPU 없이 테스트합니다. */
        bool publishFrame( const RHITimestampFrame& frame );

        /** @brief 디바이스를 잊습니다(백엔드 교체). 새 디바이스의 슬롯이 열렸는지 다시 볼 때까지 타임스탬프를 쓰지 않습니다. 표본 순번은 이어 갑니다. */
        void clearDevice();

        /** @brief 시계의 상태입니다(게임 스레드, 락 없음). */
        [[nodiscard]] GPUFrameClockState getState() const;
        /** @brief 마지막으로 읽은 표본입니다(게임 스레드, 락 없음). 아직 없으면 순번 0 입니다. */
        [[nodiscard]] GPUFrameSample getLatest() const;

    private:
        void setState( GPUFrameClockState state );

    private:
        /// @brief 마지막으로 읽은 프레임입니다(렌더 스레드). `readBack` 이 돌려주는 포인터가 가리킨다.
        RHITimestampFrame _frame;
        /// @brief 상위 32 비트가 순번, 하위 32 비트가 GPU ms(float 비트)입니다. 두 값을 한 번에 싣기 위해 64 비트 하나로 둔다.
        std::atomic<uint64> _packedSample;
        std::atomic<uint8>  _state;
        std::atomic<uint8>  _bWanted;
        /// @brief 켠 뒤 슬롯이 열리지 않은 프레임 수입니다(렌더 스레드).
        uint32 _closedSlotFrames;
        /// @brief 다음 표본의 순번입니다(렌더 스레드).
        uint32 _nextSerial;
        /// @brief 이번 프레임에 시작 · 끝 타임스탬프를 쓰는지입니다(렌더 스레드, `readBack` 이 정한다).
        uint8 _bWritingFrame : 1;
    };
} // namespace sw
