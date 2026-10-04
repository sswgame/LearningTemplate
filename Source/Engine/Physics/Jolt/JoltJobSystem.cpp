#include "pch.h"

#include "Engine/Physics/Jolt/JoltJobSystem.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    JoltJobSystem::JoltJobSystem( uint32 maxJobCount, uint32 maxBarrierCount )
        : JPH::JobSystemWithBarrier{ maxBarrierCount }
        , _jobs{}
        , _listQueuedJob{}
        , _queueMutex{}
        , _pendingTaskCount{ 0 }
        , _maxConcurrency{ 1 }
    {
        _jobs.Init( maxJobCount, maxJobCount );
        if ( engine::areEngineServicesBound() )
            _maxConcurrency = engine::getTaskManager().getWorkerCount() + 1;
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
            std::this_thread::yield();
    }

    int32 JoltJobSystem::GetMaxConcurrency() const
    {
        return static_cast<int32>( _maxConcurrency );
    }

    JPH::JobHandle JoltJobSystem::CreateJob( const utf8* pJobName, JPH::ColorArg color, const JobFunction& jobFunction, JPH::uint32 dependencyCount )
    {
        // 잡 자리가 다 찼으면 다른 스레드가 잡을 끝내 자리를 돌려줄 때까지 양보하며 다시 묻는다(Jolt JobSystemThreadPool 과 같은 처리).
        JPH::uint32 index = JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex;
        for ( ;; )
        {
            index = _jobs.ConstructObject( pJobName, color, this, jobFunction, dependencyCount );
            if ( index != JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex )
                break;
            std::this_thread::yield();
        }
        Job* pJob = &_jobs.Get( index );

        // 핸들이 참조 하나를 든다. 의존이 없으면 바로 큐에 넣는다.
        JobHandle handle{ pJob };
        if ( dependencyCount == 0 )
            QueueJob( pJob );
        return handle;
    }

    void JoltJobSystem::QueueJob( Job* pJob )
    {
        // 큐가 참조 하나를 든다 — 태스크가 실행 뒤에 놓는다.
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
        TaskManager& taskManager = engine::getTaskManager();
        TaskHandle   handle      = taskManager.emplaceTask( "JoltJob", SW_DELEGATE_METHOD( TaskDelegate, &JoltJobSystem::runQueuedJob, this ) );
        if ( handle.isValid() == false )
        {
            // 태스크를 낼 수 없다(풀이 바닥났거나 내리는 중) — 여기서 실행한다.
            _pendingTaskCount.fetch_sub( 1, std::memory_order_acq_rel );
            Job* pQueued = popQueuedJob();
            if ( pQueued != nullptr )
            {
                pQueued->Execute();
                pQueued->Release();
            }
            return;
        }
        // 스텝을 부른 스레드가 장벽에서 곧바로 기다린다 — 게임 스레드의 대량 잡 뒤에 줄을 서지 않게 High 레인으로 낸다.
        handle.setPriority( TaskPriority::High );
        taskManager.submit( handle );
    }

    void JoltJobSystem::QueueJobs( Job** ppJob, JPH::uint jobCount )
    {
        for ( JPH::uint jobIndex = 0; jobIndex < jobCount; ++jobIndex )
            QueueJob( ppJob[jobIndex] );
    }

    void JoltJobSystem::FreeJob( Job* pJob )
    {
        _jobs.DestructObject( pJob );
    }

    void JoltJobSystem::runQueuedJob()
    {
        Job* pJob = popQueuedJob();
        if ( pJob != nullptr )
        {
            pJob->Execute();
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
