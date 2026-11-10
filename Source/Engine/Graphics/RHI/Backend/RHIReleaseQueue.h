/**
 * @file RHIReleaseQueue.h
 * @brief GPU 지연 해제 큐입니다.
 */
#pragma once
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Graphics/RHI/Backend/RHIRetireList.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class RHIReleaseQueue
     * @brief GPU 리소스 해제 콜백을 GPU 펜스(프레임 시리얼 · 전송 시리얼)가 지난 뒤에 실행합니다.
     * @details 키는 하나다 — GPU 가 끝낸 시리얼. CPU 프레임 수를 세어 짐작하는 길은 두지 않는다(그 길은 해제 지연과 프레임 인 플라이트 수가 우연히 같아야 안전했다).
     *          프레임 해제의 키는 `RHIFrameFenceRing::getRecordingSerial()`(어느 스레드에서나 원자 읽기)이다. 언리얼 `FRHIResource` 지연 삭제도 펜스 기준이다(기억에 의존한 비교).
     */
    class SW_API RHIReleaseQueue
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명: 소멸 시 flushAll
        // ------------------------------------------------------------------------------
        RHIReleaseQueue();
        /** @brief 대기 중인 해제를 모두 실행합니다. */
        ~RHIReleaseQueue();

        // ------------------------------------------------------------------------------
        // 2) enqueue, tick, flush
        // ------------------------------------------------------------------------------
        /** @brief GPU 펜스 값이 완료된 뒤에 해제합니다. */
        void enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate, uint64 fenceValue );

        /**
         * @brief 자원을 해제 큐에 쥐여 두었다가 펜스가 지난 뒤 놓고, 그때 추적기에서 줄입니다.
         * @details 키 공간은 템플릿 인자로 받아 람다가 델리게이트의 인라인 칸(24 바이트)에 들게 한다 — 자원 · 추적기 · id.
         *          추적기는 자원을 실제로 놓을 때 줄인다(해제 요청 시점이 아니라).
         */
        template <RHIMemoryKeySpace kSpace, class Owned>
        void enqueueGPUTrackedRelease( Owned owned, RHIMemoryTracker& tracker, uint64 id, uint64 fenceValue )
        {
            RHIMemoryTracker* pTracker = &tracker;
            enqueueGPURelease( RHIResourceReleaseDelegate::create( [owned, pTracker, id]()
            {
                (void)owned;
                pTracker->recordFree( RHIMemoryKey{ id, kSpace } );
            } ),
                               fenceValue );
        }

        /** @brief 펜스가 지날 때까지 자원을 쥐고 있다가 놓습니다(추적기에 오르지 않는 자원). */
        template <class Owned>
        void enqueueGPUKeepAlive( Owned owned, uint64 fenceValue )
        {
            enqueueGPURelease( RHIResourceReleaseDelegate::create( [owned]()
            { (void)owned; } ),
                               fenceValue );
        }

        /** @brief 완료된 GPU 펜스 이하의 해제 콜백을 실행합니다. */
        void tickCompleted( uint64 completedFence );

        /** @brief 대기 중인 해제를 모두 즉시 실행합니다. */
        void flushAll();

        /** @brief 아직 실행되지 않은 해제 항목 수를 반환합니다. */
        uint32 getPendingReleaseCount() const;

    private:
        RHIRetireList<RHIResourceReleaseDelegate> _listGPUEntry SW_GUARDED_BY( _spinLock ); ///< 키 = 지나야 할 GPU 펜스 값
        /// @brief tick 이 완료된 콜백을 옮겨 담는 임시 버퍼입니다. 잠금 밖에서 부르려고 옮깁니다. 프레임마다 다시 채워 용량이 남습니다.
        ///        `_spinLock` 안에서만 꺼내고 돌려놓습니다. tick 은 여러 스레드에서 올 수 있습니다(DX12 업로드 펜스 대기).
        vector<RHIResourceReleaseDelegate> _listReadyScratch SW_GUARDED_BY( _spinLock );
        mutable SpinLock                                     _spinLock;
    };
} // namespace sw
