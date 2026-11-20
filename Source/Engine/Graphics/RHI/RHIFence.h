/**
 * @file RHIFence.h
 * @brief "이 자원은 펜스 값 N 이 끝나면 다시 쓴다" 를 형식으로 적는 렌더러 쪽 펜스입니다. `IRHIDevice::notifyFrameCompletion`(GPU 완료 통지) 위에 섭니다.
 * @details 렌더러의 구간 풀(`GPUMeshVertexPool` · `GPUMeshMorphPool` · `GPUVertexAnimationPool`)은 돌려받은 구간을 GPU 가 그 구간을 읽던 프레임을 끝낸 뒤에만
 *          다시 내준다. 예전에는 "호출 수 + 인플라이트 프레임 수" 로 짐작했다 — 그 짐작은 호출이 프레임마다 정확히 한 번이고 앞서가기 상한이 상수와 같아야만 맞았다.
 *          여기서는 `signal` 이 낸 펜스 값을 키로 두고(`SpanAllocator::freeDeferred`), 디바이스가 그 프레임의 GPU 작업을 끝냈다고 알려 준 값(`getCompletedValue`)까지만
 *          돌려받는다(`retireDeferred`). 언리얼 RHI 의 지연 삭제(펜스가 지난 뒤 `FRHIResource` 를 놓는 줄)와 D3D12 의 펜스 값 링(`Signal` 값 ≤ `GetCompletedValue` 인 칸만 재사용)과
 *          같은 모양이다(기억에 의존한 비교). 백엔드의 프레임 시리얼을 바깥에 드러내지 않고 계약 함수 하나만 쓴다 — 네 백엔드가 같은 약속을 이미 테스트로 건다.
 * @code
 *     const uint64 fenceValue = _fence.signal( *pDevice ); // 렌더 스레드, 프레임 기록 중
 *     _allocator.retireDeferred( _fence.getCompletedValue() );
 *     _allocator.freeDeferred( offset, count, fenceValue );
 * @endcode
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class RHIFence
     * @brief 펜스 값(1 부터)을 내고, GPU 가 끝낸 값을 원자적으로 기억합니다.
     * @details 완료 값은 공유 칸에 둔다 — 디바이스 해제 큐에 맡긴 콜백이 풀보다 오래 살 수 있고(종료 · 장치 교체) 풀이 이동해도 콜백이 같은 칸을 가리킨다.
     *          `signal` 은 `notifyFrameCompletion` 과 같이 렌더 스레드(프레임을 기록하는 스레드)에서만 부른다. 완료 값은 어느 스레드에서 읽어도 된다.
     */
    class SW_API RHIFence
    {
    public:
        RHIFence();
        ~RHIFence() = default;
        // 복사하면 두 펜스가 완료 칸 하나를 나누면서 낸 값은 따로 세어, 한쪽의 완료가 다른 쪽의 값을 끝냈다고 잘못 알린다 — 이동만 둔다(풀이 이동한다).
        RHIFence( const RHIFence& )                = delete;
        RHIFence& operator=( const RHIFence& )     = delete;
        RHIFence( RHIFence&& ) noexcept            = default;
        RHIFence& operator=( RHIFence&& ) noexcept = default;

        /**
         * @brief 새 펜스 값을 내고, 지금 기록 중인(프레임 밖이면 다음에 낼) 프레임의 GPU 작업이 끝나면 완료 값을 그 값으로 올리게 디바이스에 맡깁니다.
         * @return 낸 펜스 값입니다. 이 호출 앞에 GPU 에 낸 작업이 읽던 자원은 이 값이 끝나면 다시 써도 된다.
         */
        [[nodiscard]] uint64 signal( IRHIDevice& device );
        /** @brief GPU 가 끝낸 가장 큰 펜스 값입니다(아직 없으면 0). 뒤로 가지 않습니다. */
        uint64 getCompletedValue() const { return _pCompletedValue->load( std::memory_order_acquire ); }
        /** @brief 마지막으로 낸 펜스 값입니다(아직 없으면 0). */
        uint64 getLastSignalValue() const { return _lastSignalValue; }

    private:
        shared_ptr<atomic<uint64>> _pCompletedValue; ///< 완료 통지(`Completed`)가 올린다(fetch_max) — 콜백은 백엔드가 완료를 확인하는 스레드에서 불린다
        uint64                     _lastSignalValue{ 0 };
    };
} // namespace sw
