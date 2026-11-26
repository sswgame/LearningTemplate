/**
 * @file RHITimestampRing.h
 * @brief GPU 타임스탬프의 칸 표입니다 — 이번 프레임에 적은 슬롯(어느 기록 스레드에서나)과, 프레임 칸마다 굳힌 슬롯 마스크를 한 곳에서 지킵니다.
 * @details 다섯 백엔드가 같은 표(칸마다 마스크 배열 · 제출 표지 + 이번 프레임 원자 마스크 + 자기 칸 카운터)를 따로 들던 것을 모은다.
 *          칸은 프레임 펜스 링의 칸(`RHIFrameFenceRing::getRecordingSlot` · DX12 `FrameResourceRing::currentIndex`)을 그대로 쓴다 — 백엔드마다 따로 돌던 칸 카운터가 없다.
 *          네이티브 쿼리(쿼리 힙 · 풀 · GL 쿼리 이름)와 결과 읽기는 백엔드 몫이다. 언리얼 `FRHIRenderQueryPool` 의 프레임별 묶음과 같은 자리다(기억에 의존한 비교).
 *          WebGPU 도 칸마다 되읽기 버퍼 하나(풀이 → 복사 → 매핑을 그 프레임 완료에 넣는다)라 같은 표를 쓴다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/Diagnostics/GPUTimestampSlotMask.h"

namespace sw
{
    /**
     * @class RHITimestampRing
     * @brief 프레임 칸마다 "그 프레임이 적은 타임스탬프 슬롯" 마스크입니다. 빈 마스크면 그 칸에 읽을 것이 없다.
     * @code
     *     _timestampRing.markWritten( slotIndex );                       // 기록(어느 스레드)
     *     (void)_timestampRing.closeFrame( ringSlot );                   // 제출 직전
     *     const GPUTimestampSlotMask mask = _timestampRing.getSlotMask( ringSlot ); // 칸을 다시 쓰기 직전(앞 프레임이 끝났다) — 읽는다
     *     _timestampRing.beginFrame( ringSlot );                          // 그 뒤 비운다
     * @endcode
     */
    class RHITimestampRing
    {
    public:
        static constexpr uint32 kSlotCount = constant::render::kMaxFrameCountInFlight;

        RHITimestampRing()
            : _currentMask{}
            , _currentComputeQueueMask{}
            , _arrSlotMask{}
            , _arrComputeQueueMask{}
        {
        }

        /** @brief 모든 칸 · 이번 프레임 마스크를 비웁니다(장치를 세우거나 놓을 때). */
        void reset()
        {
            for ( uint32 ringSlot = 0; ringSlot < kSlotCount; ++ringSlot )
            {
                beginFrame( ringSlot );
            }
        }

        /** @brief 이번 프레임에 슬롯 @p timestampSlot 을 적었습니다. 병렬 기록 스레드에서 불러도 된다(슬롯은 패스 인덱스로 고정이다). */
        void markWritten( uint32 timestampSlot ) { _currentMask.set( timestampSlot ); }

        /** @brief 이번 프레임에 비동기 컴퓨트 큐 리스트가 슬롯 @p timestampSlot 을 적었습니다(`markWritten` 을 겸한다). 그래픽스 큐와 시간이 겹치는 슬롯이라 따로 가린다. */
        void markComputeQueueWritten( uint32 timestampSlot )
        {
            _currentMask.set( timestampSlot );
            _currentComputeQueueMask.set( timestampSlot );
        }

        /** @brief 이번 프레임의 마스크를 칸 @p ringSlot 에 굳히고 돌려줍니다(제출 직전 — 기록이 모두 끝났다). */
        GPUTimestampSlotMask closeFrame( uint32 ringSlot )
        {
            SW_CHECK_INDEX( ringSlot, kSlotCount );
            const GPUTimestampSlotMask mask = _currentMask.load();
            _arrSlotMask[ringSlot]          = mask;
            _arrComputeQueueMask[ringSlot]  = _currentComputeQueueMask.load();
            return mask;
        }

        /** @brief 칸 @p ringSlot 에 굳힌 마스크입니다(그 칸의 앞 프레임이 적은 슬롯). */
        const GPUTimestampSlotMask& getSlotMask( uint32 ringSlot ) const
        {
            SW_CHECK_INDEX( ringSlot, kSlotCount );
            return _arrSlotMask[ringSlot];
        }

        /** @brief 칸 @p ringSlot 에 굳힌 슬롯 가운데 비동기 컴퓨트 큐 리스트가 적은 것입니다(`getSlotMask` 의 부분 집합). */
        const GPUTimestampSlotMask& getComputeQueueMask( uint32 ringSlot ) const
        {
            SW_CHECK_INDEX( ringSlot, kSlotCount );
            return _arrComputeQueueMask[ringSlot];
        }

        /** @brief 칸 @p ringSlot 을 다시 씁니다 — 지난 결과를 읽은 뒤 그 칸 마스크와 이번 프레임 마스크를 비운다. */
        void beginFrame( uint32 ringSlot )
        {
            SW_CHECK_INDEX( ringSlot, kSlotCount );
            _arrSlotMask[ringSlot]         = GPUTimestampSlotMask{};
            _arrComputeQueueMask[ringSlot] = GPUTimestampSlotMask{};
            _currentMask.clear();
            _currentComputeQueueMask.clear();
        }

    private:
        GPUTimestampSlotAtomicMask _currentMask;                     ///< 이번 프레임에 적은 슬롯(기록 스레드가 올린다)
        GPUTimestampSlotAtomicMask _currentComputeQueueMask;         ///< 그 가운데 비동기 컴퓨트 큐 리스트가 적은 슬롯
        GPUTimestampSlotMask       _arrSlotMask[kSlotCount];         ///< 칸마다 굳힌 마스크(렌더 스레드만)
        GPUTimestampSlotMask       _arrComputeQueueMask[kSlotCount]; ///< 칸마다 굳힌 컴퓨트 큐 마스크(렌더 스레드만)
    };
} // namespace sw
