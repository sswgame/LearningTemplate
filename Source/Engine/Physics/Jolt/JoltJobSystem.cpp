#include "pch.h"

#include "Engine/Physics/Jolt/JoltJobSystem.h"

#include "Core/Log/Logger.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineServices.h"

#include <algorithm>
#include <cstdlib>
#include <thread>

namespace sw
{
    namespace
    {
        struct JoltJobSystemInternal
        {
            /** @brief 이 스레드가 지금 태스크로 잡을 실행 중이면 그 실행 칸 번호, 아니면 -1 입니다(잡 안에서 스텝을 기다릴 때 자기 칸을 건너뛴다). */
            static thread_local int32 t_executingSlot;
        };
        thread_local int32 JoltJobSystemInternal::t_executingSlot{ -1 };
    } // namespace
} // namespace sw

namespace sw
{
    JoltJobSystem::JoltJobSystem( uint32 maxJobCount, uint32 maxBarrierCount )
        : JPH::JobSystemWithBarrier{ maxBarrierCount }
        , _arrExecutorSlot{}
        , _jobs{}
        , _listQueuedJob{}
        , _queueMutex{}
        , _pTaskManager{ nullptr }
        , _jobTaskStage{}
        , _maxConcurrency{ 1 }
    {
        _jobs.Init( maxJobCount, maxJobCount );
        if ( engine::areEngineServicesBound() )
        {
            _pTaskManager = &engine::getSubsystem<TaskManager>();
            // 스테이지는 만들 때 한 번만 쓴다 — 이후 `addTask` 는 원자 연산이라 여러 스레드가 동시에 `QueueJob` 해도 된다.
            _jobTaskStage   = _pTaskManager->createStage( "JoltJobs" );
            _maxConcurrency = _pTaskManager->getWorkerCount() + 1;
        }
    }

    JoltJobSystem::~JoltJobSystem()
    {
        // 큐에 남은 잡을 여기서 비운다(이미 실행된 잡이면 Execute 가 건너뛴다). 그다음 이미 나간 태스크가 참조를 놓을 때까지 기다린다.
        for ( Job* pJob = popQueuedJob(); pJob != nullptr; pJob = popQueuedJob() )
        {
            pJob->Execute();
            pJob->Release();
        }
        // 이미 나간 태스크가 이 객체를 쥐고 있다. 끝날 때까지 기다린다(기한을 두고 포기하지 않는다 — 오래 걸리면 `waitStage` 가 주기적으로 남은 태스크를 오류로 남긴다).
        if ( _pTaskManager != nullptr )
            _pTaskManager->waitStage( _jobTaskStage, "JoltJobSystem" );
    }

    int32 JoltJobSystem::GetMaxConcurrency() const
    {
        return static_cast<int32>( _maxConcurrency );
    }

    JPH::JobHandle JoltJobSystem::CreateJob( const utf8* pJobName, JPH::ColorArg color, const JobFunction& jobFunction, JPH::uint32 dependencyCount )
    {
        // 잡 슬롯이 다 찼으면 다른 스레드가 잡을 끝내 슬롯을 돌려줄 때까지 양보하며 다시 확인한다(Jolt JobSystemThreadPool 과 같은 처리).
        JPH::uint32 index = JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex;
        for ( ;; )
        {
            index = _jobs.ConstructObject( pJobName, color, this, jobFunction, dependencyCount );
            if ( index != JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex )
                break;
            std::this_thread::yield();
        }
        Job* pJob = &_jobs.Get( index );

        // 핸들이 참조 하나를 가진다. 의존이 없으면 바로 큐에 넣는다.
        JobHandle handle{ pJob };
        if ( dependencyCount == 0 )
            QueueJob( pJob );
        return handle;
    }

    void JoltJobSystem::QueueJob( Job* pJob )
    {
        // 큐가 참조 하나를 가진다. 태스크가 실행한 뒤에 해제한다.
        pJob->AddRef();
        {
            ScopedLock<mutex> lock{ _queueMutex };
            _listQueuedJob.push_back( pJob );
        }
        if ( _pTaskManager == nullptr )
        {
            runQueuedJob();
            return;
        }
        TaskHandle handle = _pTaskManager->emplaceTask( "JoltJob", SW_DELEGATE_METHOD( TaskDelegate, &JoltJobSystem::runQueuedJob, this ) );
        if ( handle.isValid() == false )
        {
            // 태스크를 낼 수 없다(풀이 바닥났거나 종료 중이다). 여기서 실행한다.
            Job* pQueued = popQueuedJob();
            if ( pQueued != nullptr )
            {
                pQueued->Execute();
                pQueued->Release();
            }
            return;
        }
        // 스텝을 호출한 스레드가 장벽에서 곧바로 기다린다. 게임 스레드의 대량 잡 뒤에 줄을 서지 않게 High 레인으로 낸다.
        handle.setPriority( TaskPriority::High );
        _jobTaskStage.addTask( handle ); // 제출 전에 넣는다(스테이지 계약)
        _pTaskManager->submit( handle );
    }

    void JoltJobSystem::QueueJobs( Job** ppJob, JPH::uint jobCount )
    {
        for ( JPH::uint jobIndex = 0; jobIndex < jobCount; ++jobIndex )
        {
            QueueJob( ppJob[jobIndex] );
        }
    }

    void JoltJobSystem::FreeJob( Job* pJob )
    {
        _jobs.DestructObject( pJob );
    }

    void JoltJobSystem::WaitForJobs( Barrier* pBarrier )
    {
        JPH::JobSystemWithBarrier::WaitForJobs( pBarrier );
        // 칸별 수를 스테이지 대기로 바꾸지 않는다 — 스테이지는 다른 스텝의 잡 태스크까지 세고, 잡 안에서 부르면 자기 태스크를 기다린다(Unity JobHandle 도 장벽 하나만 기다린다).
        // 장벽에 붙기 전에 끝난 잡은 장벽이 기다리지 않는다. 지금 실행 중인 칸마다 그 실행이 나올 때까지 기다려 선후를 맺는다(파일 머리말 주의).
        for ( uint32 slotIndex = 0; slotIndex < kMaxExecutorSlotCount; ++slotIndex )
        {
            if ( static_cast<int32>( slotIndex ) == JoltJobSystemInternal::t_executingSlot )
                continue;
            const ExecutorSlot& slot         = _arrExecutorSlot[slotIndex];
            const uint64        enteredCount = slot._enterCount.load( std::memory_order_acquire );
            while ( slot._exitCount.load( std::memory_order_acquire ) < enteredCount )
            {
                std::this_thread::yield();
            }
        }
    }

    void JoltJobSystem::runQueuedJob()
    {
        Job* pJob = popQueuedJob();
        if ( pJob != nullptr )
        {
            if ( _pTaskManager != nullptr )
            {
                const uint32  slotIndex                = std::min( _pTaskManager->getCurrentThreadScratchSlot(), kMaxExecutorSlotCount - 1 );
                ExecutorSlot& slot                     = _arrExecutorSlot[slotIndex];
                const int32   previousSlot             = JoltJobSystemInternal::t_executingSlot;
                JoltJobSystemInternal::t_executingSlot = static_cast<int32>( slotIndex );
                slot._enterCount.fetch_add( 1, std::memory_order_acq_rel );
                pJob->Execute();
                slot._exitCount.fetch_add( 1, std::memory_order_release );
                JoltJobSystemInternal::t_executingSlot = previousSlot;
            }
            else
            {
                pJob->Execute();
            }
            pJob->Release();
        }
    }

    JPH::JobSystem::Job* JoltJobSystem::popQueuedJob()
    {
        ScopedLock<mutex> lock{ _queueMutex };
        if ( _listQueuedJob.empty() )
            return nullptr;
        Job* pJob = _listQueuedJob.front();
        _listQueuedJob.pop_front();
        return pJob;
    }
} // namespace sw
