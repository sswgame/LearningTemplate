/**
 * @file RHIFrameFenceRing.h
 * @brief 프레임 시리얼 발급 · GPU 완료 워터마크 · 앞서가기 상한(`kMaxFrameCountInFlight`)을 한 곳에서 지키는 프레임 펜스 링입니다.
 * @details DX11(이벤트 쿼리) · GL(`glFenceSync`) · Vulkan(`VkFence`) · WebGPU(작업 완료 콜백)이 같은 링을 쓴다. 네이티브 펜스 객체는 백엔드가 칸(`getSlot`)마다 들고,
 *          링은 "어느 시리얼이 어느 칸에 있고 어디까지 끝났나" 만 안다. 해제 키는 `getRecordingSerial` 하나다 — 어느 스레드에서 읽어도 원자적이라
 *          전송 완료 콜백(폴링 스레드)이 읽어도 경쟁이 아니다. DX12 는 큐 펜스 값 자체가 시리얼이라(업로드도 같은 펜스에 Signal 한다) `FrameResourceRing` 을 그대로 쓴다.
 *          언리얼 RHI 의 프레임 펜스(`FRHIGPUFence` 링 + `MaxFramesInFlight`)와 Godot `RenderingDevice` 의 프레임 링(`frames[frame_count]` + 칸 펜스 대기)과 같은 자리다(기억에 의존한 비교).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class RHIFrameFenceRing
     * @brief 프레임 시리얼(1 부터)과 완료 워터마크입니다. 시리얼 s 는 칸 `( s - 1 ) % kSlotCount` 에 놓입니다.
     * @details 쓰기(`markSubmitted` · `retire` · `waitForRecordingSlot` · `markCompleted`)는 프레임을 내는 스레드(렌더 스레드)만 한다. 읽기는 어느 스레드에서나 된다.
     * @code
     *     _frameFenceRing.waitForRecordingSlot( [&]( uint32 slot, uint64 serial ) { return waitNative( slot ); } ); // beginFrame
     *     signalNative( _frameFenceRing.getRecordingSlot() );
     *     (void)_frameFenceRing.markSubmitted();                                                                       // endFrame
     *     _releaseQueue.tickCompleted( _frameFenceRing.retire( [&]( uint32 slot, uint64 serial ) { return isNativeDone( slot ); } ) );
     * @endcode
     */
    class RHIFrameFenceRing
    {
    public:
        static constexpr uint32 kSlotCount = constant::render::kMaxFrameCountInFlight;

        RHIFrameFenceRing()
            : _recordingSerial{ 1 }
            , _completedSerial{ 0 }
        {
        }

        /** @brief 처음 상태(기록 시리얼 1, 끝난 것 없음)로 되돌립니다. 장치를 세우거나 놓을 때만 부릅니다. */
        void reset()
        {
            _recordingSerial.store( 1, std::memory_order_release );
            _completedSerial.store( 0, std::memory_order_release );
        }

        // ------------------------------------------------------------------------------
        // 1) 읽기 — 어느 스레드에서나
        // ------------------------------------------------------------------------------
        /** @brief 지금 기록 중인(프레임 밖이면 다음에 낼) 프레임의 시리얼입니다. 지연 해제의 키는 이 값 하나다(`RHIReleaseQueue::enqueueGPURelease`). */
        uint64 getRecordingSerial() const { return _recordingSerial.load( std::memory_order_acquire ); }
        /** @brief GPU 가 끝낸 것이 확인된 가장 큰 시리얼입니다. */
        uint64 getCompletedSerial() const { return _completedSerial.load( std::memory_order_acquire ); }
        /** @brief 마지막으로 낸 프레임의 시리얼입니다(아직 없으면 0). */
        uint64 getLastSubmittedSerial() const { return getRecordingSerial() - 1; }
        /** @brief 낸 프레임이 모두 끝났는지 봅니다. */
        bool isIdle() const { return getCompletedSerial() >= getLastSubmittedSerial(); }
        /** @brief 시리얼 @p serial 의 칸입니다. */
        static uint32 getSlot( uint64 serial ) { return static_cast<uint32>( ( serial - 1 ) % kSlotCount ); }
        /** @brief 기록 중인 프레임의 칸입니다(프레임별 자원 · 네이티브 펜스를 고른다). */
        uint32 getRecordingSlot() const { return getSlot( getRecordingSerial() ); }
        /** @brief 기록 칸에 아직 끝나지 않은 앞 프레임이 있으면 그 시리얼, 없으면 0 입니다. */
        uint64 getRecordingSlotBusySerial() const
        {
            const uint64 recording = getRecordingSerial();
            if ( recording <= kSlotCount )
                return 0;
            const uint64 occupant = recording - kSlotCount;
            return occupant > getCompletedSerial() ? occupant : 0;
        }

        // ------------------------------------------------------------------------------
        // 2) 진행 — 렌더 스레드
        // ------------------------------------------------------------------------------
        /**
         * @brief 기록 칸을 앞 프레임이 아직 쓰면 그 프레임을 기다립니다(앞서가기 상한). @p fnWait( slot, serial ) 은 그 프레임이 끝났거나 장치를 잃었으면 true 를 돌려줍니다.
         * @return 칸이 비었으면 true 입니다. 대기가 실패하면 false 이고 워터마크는 그대로입니다.
         */
        template <typename FnWait>
        bool waitForRecordingSlot( FnWait&& fnWait )
        {
            const uint64 busySerial = getRecordingSlotBusySerial();
            if ( busySerial == 0 )
                return true;
            if ( fnWait( getSlot( busySerial ), busySerial ) == false )
                return false;
            markCompleted( busySerial );
            return true;
        }

        /** @brief 기록 중인 프레임을 냈습니다. 낸 시리얼을 돌려주고 다음 시리얼로 넘어갑니다(네이티브 Signal 뒤에 부른다). */
        uint64 markSubmitted()
        {
            const uint64 submitted = _recordingSerial.load( std::memory_order_relaxed );
            _recordingSerial.store( submitted + 1, std::memory_order_release );
            return submitted;
        }

        /** @brief 오래된 순서로 @p fnIsDone( slot, serial ) 이 true 인 프레임을 끝난 것으로 거둡니다. 새 워터마크를 돌려줍니다. */
        template <typename FnIsDone>
        uint64 retire( FnIsDone&& fnIsDone )
        {
            uint64       completed = getCompletedSerial();
            const uint64 last      = getLastSubmittedSerial();
            while ( completed < last && fnIsDone( getSlot( completed + 1 ), completed + 1 ) )
            {
                ++completed;
            }
            markCompleted( completed );
            return completed;
        }

        /** @brief GPU 가 @p serial 까지 끝냈습니다(대기 · waitIdle 뒤). 워터마크는 뒤로 가지 않습니다. */
        void markCompleted( uint64 serial )
        {
            if ( serial > _completedSerial.load( std::memory_order_relaxed ) )
                _completedSerial.store( serial, std::memory_order_release );
        }
        /** @brief 낸 프레임이 모두 끝났습니다(GPU 유휴 대기 뒤). */
        void markAllCompleted() { markCompleted( getLastSubmittedSerial() ); }

    private:
        atomic<uint64> _recordingSerial; ///< 렌더 스레드만 올린다(release) — 다른 스레드는 해제 키로 읽는다
        atomic<uint64> _completedSerial; ///< 렌더 스레드만 올린다
    };
} // namespace sw
