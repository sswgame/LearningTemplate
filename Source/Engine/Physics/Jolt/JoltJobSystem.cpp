#include "pch.h"

#include "Engine/Physics/Jolt/JoltJobSystem.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"

#include <algorithm>
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
        , _pendingTaskCount{ 0 }
        , _maxConcurrency{ 1 }
    {
        _jobs.Init( maxJobCount, maxJobCount );
        if ( engine::areEngineServicesBound() )
            _maxConcurrency = engine::getSubsystem<TaskManager>().getWorkerCount() + 1;
    }

    JoltJobSystem::~JoltJobSystem()
    {
        // 큐에 남은 잡을 여기서 비운다(이미 실행된 잡이면 Execute 가 건너뛴다). 그다음 이미 나간 태스크가 참조를 놓을 때까지 기다린다.
        for ( Job* pJob = popQueuedJob(); pJob != nullptr; pJob = popQueuedJob() )
        {
            pJob->Execute();
            pJob->Release();
        }
        while ( _pendingTaskCount.load( std::memory_order_acquire ) != 0 )
        {
            std::this_thread::yield();
        }
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
            std::scoped_lock<mutex> lock{ _queueMutex };
            _listQueuedJob.push_back( pJob );
        }
        if ( engine::areEngineServicesBound() == false )
        {
            runQueuedJob();
            return;
        }
        _pendingTaskCount.fetch_add( 1, std::memory_order_acq_rel );
        TaskManager& taskManager = engine::getSubsystem<TaskManager>();
        TaskHandle   handle      = taskManager.emplaceTask( "JoltJob", SW_DELEGATE_METHOD( TaskDelegate, &JoltJobSystem::runQueuedJob, this ) );
        if ( handle.isValid() == false )
        {
            // 태스크를 낼 수 없다(풀이 바닥났거나 종료 중이다). 여기서 실행한다.
            _pendingTaskCount.fetch_sub( 1, std::memory_order_acq_rel );
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
        taskManager.submit( handle );
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
            if ( engine::areEngineServicesBound() )
            {
                const uint32  slotIndex                = std::min( engine::getSubsystem<TaskManager>().getCurrentThreadScratchSlot(), kMaxExecutorSlotCount - 1 );
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
        if ( engine::areEngineServicesBound() )
            _pendingTaskCount.fetch_sub( 1, std::memory_order_acq_rel );
    }

    JPH::JobSystem::Job* JoltJobSystem::popQueuedJob()
    {
        std::scoped_lock<mutex> lock{ _queueMutex };
        if ( _listQueuedJob.empty() )
            return nullptr;
        Job* pJob = _listQueuedJob.front();
        _listQueuedJob.pop_front();
        return pJob;
    }
} // namespace sw
