/**
 * @file JoltJobSystem.h
 * @brief Jolt 의 잡을 엔진 태스크 시스템(`TaskManager`)으로 실행하는 잡 시스템입니다. Jolt 헤더를 include 하므로 `Physics/Jolt` 밖에서는 보지 않습니다.
 * @details Jolt 는 스텝 하나를 의존 관계가 있는 잡 그래프로 나누고, 스텝을 호출한 스레드는 장벽(`Barrier`)에서 **남은 잡을 직접 실행하며** 기다린다
 *          (`JobSystemWithBarrier`). 잡끼리 서로를 바쁘게 기다리지 않으므로(`JobSystemSingleThreaded` 로도 돈다) 엔진의 공유 워커 풀에 잡을 넘겨도
 *          교착이 없다. 워커가 모두 다른 일을 해도 호출한 스레드가 혼자 끝낸다. 그래서 물리 전용 스레드를 따로 띄우지 않는다.
 *
 *          잡 하나를 큐에 넣을 때마다 엔진 태스크 하나를 낸다. 태스크는 큐에서 잡 하나를 꺼내 실행하고(이미 장벽이 실행했으면 Jolt 가 건너뛴다)
 *          참조를 해제한다. 태스크 풀이 바닥나면 그 자리에서 실행한다.
 *
 *          주의: Jolt 는 잡을 장벽에 붙이기 전에 끝난 잡을 relaxed 원자 연산으로만 알아채고 기다리지 않는다(`Job::SetBarrier`). 그 잡을 실행한
 *          워커와 스텝을 부른 스레드 사이에 선후(happens-before)가 없어, 다음 스텝이 같은 임시 메모리에 `Step` 을 만들면 앞 잡의 읽기와 겹친다
 *          (TSan 이 `PhysicsUpdateContext::Step` 생성자로 보고). 그래서 `WaitForJobs` 는 장벽을 기다린 뒤 그때 잡을 실행 중이던 스레드마다
 *          그 실행이 끝나기를 기다린다(슬롯별 들어감 · 나옴 수, 나옴은 release).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Task/TaskTypes.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

namespace sw
{
    class TaskManager;

    /** @class JoltJobSystem @brief 엔진 태스크로 도는 Jolt 잡 시스템입니다. 파일 머리말 참고. */
    class JoltJobSystem final : public JPH::JobSystemWithBarrier
    {
    public:
        /** @brief 동시에 살아 있을 수 있는 잡과 장벽의 상한을 받아 만듭니다. */
        JoltJobSystem( uint32 maxJobCount, uint32 maxBarrierCount );
        /** @brief 남은 잡 태스크가 모두 끝날 때까지 기다립니다(큐에 남은 것은 여기서 실행한다). */
        ~JoltJobSystem() override;

        int32     GetMaxConcurrency() const override;
        JobHandle CreateJob( const utf8* pJobName, JPH::ColorArg color, const JobFunction& jobFunction, JPH::uint32 dependencyCount = 0 ) override;
        /** @brief 장벽을 기다리고, 그때 잡을 실행 중이던 태스크가 모두 끝나기를 기다립니다(파일 머리말 주의). */
        void WaitForJobs( Barrier* pBarrier ) override;

    protected:
        void QueueJob( Job* pJob ) override;
        void QueueJobs( Job** ppJob, JPH::uint jobCount ) override;
        void FreeJob( Job* pJob ) override;

    private:
        /** @brief 엔진 태스크 하나의 본문입니다. 큐에서 잡 하나를 꺼내 실행하고 참조를 해제합니다. */
        void runQueuedJob();
        /** @brief 큐에서 잡 하나를 꺼냅니다. 비었으면 nullptr 입니다. */
        Job* popQueuedJob();

        /** @brief 태스크를 실행하는 스레드 하나(스크래치 슬롯)의 잡 실행 수입니다. 나옴 수는 release 로 올려 기다리는 쪽이 그 실행과 선후를 맺는다. */
        struct alignas( 64 ) ExecutorSlot
        {
            atomic<uint64> _enterCount; ///< 기본 생성이 0 이다
            atomic<uint64> _exitCount;
        };
        static constexpr uint32 kMaxExecutorSlotCount = 64; ///< 넘는 슬롯 번호는 마지막 칸을 나눠 쓴다

        ExecutorSlot                _arrExecutorSlot[kMaxExecutorSlotCount];
        JPH::FixedSizeFreeList<Job> _jobs;
        deque<Job*> _listQueuedJob  SW_GUARDED_BY( _queueMutex );
        mutable mutex               _queueMutex;
        TaskManager*                _pTaskManager; ///< 엔진 서비스가 묶여 있지 않으면 nullptr 이다(잡을 그 자리에서 실행한다)
        /** @brief 이 잡 시스템이 낸 엔진 태스크 전부입니다. 소멸이 `waitStage` 로 기다린다(따로 센 수를 양보하며 돌지 않는다 — 태스크 매니저의 대기 · 진단을 쓴다). */
        TaskStageHandle _jobTaskStage;
        uint32          _maxConcurrency;
    };
} // namespace sw
