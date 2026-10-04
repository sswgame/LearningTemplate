/**
 * @file JoltJobSystem.h
 * @brief Jolt 의 잡을 엔진 태스크 시스템(`TaskManager`)으로 돌리는 잡 시스템입니다. Jolt 헤더를 include 하므로 `Physics/Jolt` 밖에서는 보지 않습니다.
 * @details Jolt 는 스텝 하나를 의존 관계가 있는 잡 그래프로 나누고, 스텝을 부른 스레드는 장벽(`Barrier`)에서 **남은 잡을 직접 실행하며** 기다립니다
 *          (`JobSystemWithBarrier`). 잡끼리 서로를 바쁘게 기다리지 않으므로(`JobSystemSingleThreaded` 로도 돈다) 엔진의 공유 워커 풀에 잡을 넘겨도
 *          교착이 없습니다 — 워커가 모두 다른 일을 해도 부른 스레드가 혼자 끝냅니다. 그래서 물리 전용 스레드를 따로 띄우지 않습니다.
 *
 *          잡 하나를 큐에 넣을 때마다 엔진 태스크 하나를 냅니다. 태스크는 큐에서 잡 하나를 꺼내 실행하고(이미 장벽이 실행했으면 Jolt 가 건너뛴다)
 *          참조를 놓습니다. 태스크 풀이 바닥나면 그 자리에서 실행합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

namespace sw
{
    /** @class JoltJobSystem @brief 엔진 태스크로 도는 Jolt 잡 시스템입니다. 파일 머리말 참고. */
    class JoltJobSystem final : public JPH::JobSystemWithBarrier
    {
    public:
        /** @brief 동시에 살아 있을 수 있는 잡 · 장벽의 상한으로 만듭니다. */
        JoltJobSystem( uint32 maxJobCount, uint32 maxBarrierCount );
        /** @brief 남은 잡 태스크가 모두 끝날 때까지 기다립니다(큐에 남은 것은 여기서 실행한다). */
        ~JoltJobSystem() override;

        int32     GetMaxConcurrency() const override;
        JobHandle CreateJob( const utf8* pJobName, JPH::ColorArg color, const JobFunction& jobFunction, JPH::uint32 dependencyCount = 0 ) override;

    protected:
        void QueueJob( Job* pJob ) override;
        void QueueJobs( Job** ppJob, JPH::uint jobCount ) override;
        void FreeJob( Job* pJob ) override;

    private:
        /** @brief 엔진 태스크 하나의 본문 — 큐에서 잡 하나를 꺼내 실행하고 참조를 놓습니다. */
        void runQueuedJob();
        /** @brief 큐에서 잡 하나를 꺼냅니다. 비었으면 nullptr 입니다. */
        Job* popQueuedJob();

        JPH::FixedSizeFreeList<Job> _jobs;
        deque<Job*>                 _listQueuedJob;
        mutable mutex               _queueMutex;
        atomic<uint32>              _pendingTaskCount; ///< 내고 아직 끝나지 않은 엔진 태스크 수(소멸이 기다린다)
        uint32                      _maxConcurrency;
    };
} // namespace sw
