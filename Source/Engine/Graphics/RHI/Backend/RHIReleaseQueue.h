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
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    /**
     * @struct RHIReleasedResource
     * @brief 백엔드가 핸들 표에서 뺀 자원입니다(`IRHIResourceFactory::destroyBufferNative` · `destroyTextureNative` · `destroyPipelineStateNative`). 언제 놓을지는 장치가 정합니다(`IRHIDevice::releaseNow`).
     */
    struct RHIReleasedResource
    {
        RHIResourceReleaseDelegate _release{};           ///< GPU 가 다 쓴 뒤에 네이티브 객체를 놓는 일. 놓을 것이 없으면 비어 있다
        uint64                     _transferSerial{ 0 }; ///< 이 자원을 쓰는 마지막 전송의 시리얼 — 끝나기 전에는 놓지 않는다(전송 계약 6). 없으면 0
    };

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
        // 1) 수명: 소멸 시 abandonAll
        // ------------------------------------------------------------------------------
        RHIReleaseQueue();
        /** @brief 추적하는 자원(`enqueueResource`)을 놓을 때 줄일 추적기 @p tracker 와 함께 만듭니다. 추적기가 큐보다 오래 삽니다. */
        explicit RHIReleaseQueue( RHIMemoryTracker& tracker );
        /** @brief 대기 중인 해제를 모두 실행하고 통지는 `Abandoned` 로 보냅니다. */
        ~RHIReleaseQueue();

        // ------------------------------------------------------------------------------
        // 2) enqueue, tick, flush
        // ------------------------------------------------------------------------------
        /** @brief GPU 펜스 값이 완료된 뒤에 해제합니다. 해제는 결과를 묻지 않는다 — 기다리지 못한 비우기에서도 놓는다. */
        void enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate, uint64 fenceValue );
        /** @brief GPU 펜스 값이 완료되면 `Completed`, 그 전에 큐를 버리면 `Abandoned` 로 알립니다. */
        void enqueueCompletion( const RHIFrameCompletionDelegate& completionDelegate, uint64 fenceValue );

        /**
         * @brief 추적기에 오른 자원 @p key 를 펜스 값이 완료된 뒤에 놓고, 놓은 뒤 추적기에서 줄입니다(해제 요청 시점이 아니라).
         * @details 추적기를 줄이는 일이 해제 콜백 안에 있으면 백엔드마다 적어야 하고 빠뜨리면 그 종류의 바이트가 끝없이 는다 — 항목이 키를 든다. @p release 는 비어 있어도 된다(놓을 네이티브 객체가 없다).
         */
        void enqueueResource( const RHIResourceReleaseDelegate& release, const RHIMemoryKey& key, uint64 fenceValue );

        /** @brief 펜스가 지날 때까지 @p owned 를 쥐고 있기만 하는 해제입니다(COM 참조 — 놓는 것이 곧 해제다). */
        template <class Owned>
        static RHIResourceReleaseDelegate makeKeepAlive( Owned owned )
        {
            return SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [owned = std::move( owned )]()
            { (void)owned; } );
        }

        /** @brief 완료된 GPU 펜스 이하의 해제 콜백을 실행하고 통지를 `Completed` 로 보냅니다. */
        void releaseCompleted( uint64 completedFence );

        /** @brief GPU 가 쉬는 것을 확인했을 때(대기 성공) 남은 해제를 모두 실행하고 통지를 `Completed` 로 보냅니다. */
        void releaseAllCompleted();
        /**
         * @brief 끝났는지 모른 채 비울 때(잃은 장치 · 시간 초과 · 종료) 남은 해제를 모두 실행하고 통지를 `Abandoned` 로 보냅니다.
         * @details 해제는 놓아도 된다 — 잃은 장치는 GPU 가 더 돌지 않고, WebGPU 는 GPU 가 쓰는 객체를 놓아도 구현이 끝날 때까지 살린다(명세).
         */
        void abandonAll();

        /** @brief 아직 실행되지 않은 해제 · 통지 항목 수를 반환합니다. */
        uint32 getPendingReleaseCount() const;

    private:
        friend struct RHIDeviceTestAccess; ///< 테스트가 통지를 하나씩 부른다 — 배포 공개 API 에 꺼내는 길을 두지 않는다

        /** @brief 해제 하나입니다. */
        struct ReleaseEntry
        {
            RHIResourceReleaseDelegate _release{};
            RHIMemoryKey               _trackedKey{}; ///< 추적기에서 줄일 자원. `_id` 가 0 이면 추적하지 않는다(핸들 · 주소는 0 이 아니다)
        };

        /** @brief 남은 항목을 모두 꺼내 해제를 부르고 통지를 @p result 로 보냅니다. */
        void flushAll( RHIFrameCompletion result );
        /** @brief 항목을 넣은 순서대로 놓고 추적기에서 줄입니다(잠금 밖). */
        void runReleases( const vector<ReleaseEntry>& listEntry );

    private:
        RHIRetireList<ReleaseEntry> _listGPUEntry                      SW_GUARDED_BY( _spinLock ); ///< 키 = 지나야 할 GPU 펜스 값
        RHIRetireList<RHIFrameCompletionDelegate> _listCompletionEntry SW_GUARDED_BY( _spinLock ); ///< 키 = 지나야 할 GPU 펜스 값
        /// @brief tick 이 완료된 콜백을 옮겨 담는 임시 버퍼입니다. 잠금 밖에서 부르려고 옮깁니다. 프레임마다 다시 채워 용량이 남습니다.
        ///        `_spinLock` 안에서만 꺼내고 돌려놓습니다. tick 은 여러 스레드에서 올 수 있습니다(DX12 업로드 펜스 대기).
        vector<ReleaseEntry> _listReadyScratch                    SW_GUARDED_BY( _spinLock );
        vector<RHIFrameCompletionDelegate> _listCompletionScratch SW_GUARDED_BY( _spinLock ); ///< 통지 쪽 같은 용도의 임시 버퍼
        mutable SpinLock                                          _spinLock;
        RHIMemoryTracker*                                         _pTracker; ///< `enqueueResource` 가 줄일 추적기(없으면 null)
    };
} // namespace sw
