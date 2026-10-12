/**
 * @file RHIReleaseQueue.h
 * @brief GPU 지연 해제 큐입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/KeyedPendingList.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Graphics/RHI/RHITypes.h"

// 프레임 상수(kGPUReleaseFrameLatency)는 EngineDefines 가 아니라 RHITypes 의 constant 블록에 있다.
// 백엔드 간 계약 상수와 같은 자리에 모아 두기 때문이다(EngineDefines.h 의 주석 참고).

namespace sw
{
    /**
     * @class RHIReleaseQueue
     * @brief GPU 리소스 해제 콜백을 frameLatency 프레임 뒤, 또는 GPU 펜스가 지난 뒤에 실행합니다.
     */
    class SW_API RHIReleaseQueue
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 — frameLatency 뒤 해제, 소멸 시 flushAll
        // ------------------------------------------------------------------------------
        /** @brief frameLatency 프레임 뒤 해제를 수행하는 큐를 만듭니다. */
        explicit RHIReleaseQueue( uint32 frameLatency = constant::kGPUReleaseFrameLatency );
        /** @brief 대기 중인 해제를 모두 실행합니다. */
        ~RHIReleaseQueue();

        // ------------------------------------------------------------------------------
        // 2) enqueue · tick · flush
        // ------------------------------------------------------------------------------
        /** @brief GPU 리소스 해제 콜백을 지연 큐에 넣습니다. CPU 프레임 지연입니다. */
        void enqueueRelease( const RHIResourceReleaseDelegate& releaseDelegate );

        /** @brief GPU 펜스 값이 완료된 뒤에 해제합니다. */
        void enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate, uint64 fenceValue );

        /** @brief 프레임을 진행하고 만기된 해제 콜백을 실행합니다. */
        void tickFrame();

        /** @brief 완료된 GPU 펜스 이하의 해제 콜백을 실행합니다. */
        void tickCompleted( uint64 completedFence );

        /** @brief 대기 중인 해제를 모두 즉시 실행합니다. */
        void flushAll();

        /** @brief 아직 실행되지 않은 해제 항목 수를 반환합니다. */
        uint32 getPendingReleaseCount() const;

    private:
        /** @brief 키 이하가 된 해제를 꺼내 잠금 밖에서 실행합니다(tickFrame · tickCompleted 공통). */
        void releaseReady( KeyedPendingList<RHIResourceReleaseDelegate>& list, uint64 threshold );

        KeyedPendingList<RHIResourceReleaseDelegate> _listFrameEntry; ///< 키 = 해제할 CPU 프레임 번호
        KeyedPendingList<RHIResourceReleaseDelegate> _listGPUEntry;   ///< 키 = 지나야 할 GPU 펜스 값
        /// @brief tick 이 완료된 콜백을 옮겨 담는 자리입니다. 잠금 밖에서 부르려고 옮깁니다. 프레임마다 다시 채워 용량이 남습니다.
        ///        `_spinLock` 안에서만 꺼내고 돌려놓습니다 — tick 은 여러 스레드에서 올 수 있습니다(DX12 업로드 펜스 대기).
        vector<RHIResourceReleaseDelegate> _listReadyScratch;
        mutable SpinLock                   _spinLock;
        uint64                             _currentFrame;
        uint32                             _frameLatency;
    };
} // namespace sw
