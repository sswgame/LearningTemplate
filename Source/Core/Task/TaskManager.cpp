#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/Futex.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/formatString.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/ThreadCrashStack.h"
#include "Core/String/fixed_string.h"
#include "Core/Task/TaskNode.h"
#include "Core/Task/TaskNodePool.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    SW_LOG_CALLER( "TaskManager" );

    namespace
    {
        thread_local int32  t_currentWorkerIndex = -1; ///< 현재 워커 스레드의 인덱스. 워커가 아니면 -1
        thread_local int32  t_helperScratchSlot  = -1; ///< 워커가 아닌 스레드가 받은 도우미 스크래치 번호(0..). 아직 없으면 -1
        thread_local uint32 t_helperSlotOwnerID  = 0;  ///< 위 번호를 준 매니저의 `_instanceID`. 매니저를 다시 만들면 옛 번호를 쓰지 않는다
        /// @brief 매니저 인스턴스 번호를 나눠 주는 카운터입니다(0 은 "없음").
        atomic<uint32>         s_nextInstanceID{ 1 };
        thread_local TaskNode* t_pCurrentRunningTask = nullptr; ///< 현재 스레드에서 실행 중인 태스크 노드
#if !defined( SW_SHIPPING )
        /// @brief 태스크 · 스테이지 대기 구간을 내보낼 곳입니다(`TaskManager::setProfileHook`). 없으면 nullptr.
        atomic<const TaskProfileHook*> s_pProfileHook{ nullptr };
#endif

        /// @brief 대기 함수(waitStage/waitAll/runParallel)가 잠들기 전에 도는 `cpuPause` 횟수(약 2 us)입니다. 기다리는 동안에는 다른 일을 돕습니다.
        constexpr uint32 kIdleSpinCount = 64;
        /**
         * @brief 워커가 잠들기 전에 일감을 기다리며 스핀하는 시간(마이크로초)입니다. 짧게 둡니다(측정해 본 결과입니다).
         * @details 스핀은 `_workEpoch` 캐시 라인 하나만 읽으므로 큐 경합은 없습니다. 그런데도 길게(50 us) 돌리면 손해입니다. 워커가
         *          SMT 형제 코어를 차지해 게임 · 렌더 스레드가 느려집니다. 렌더 그래프 병렬 기록이 150~192 us(2 us 스핀) 대
         *          214~242 us(50 us 스핀)였습니다. 일부 워커만 길게 돌리는 hot pool(2 · 4개)도 이득이 없었습니다. 이 엔진의 프레임
         *          (~0.7 ms)은 잡 사이의 빈틈이 워커 수만큼의 코어를 깨어 있게 유지할 만큼 길지 않습니다. 상용 엔진의 8~16 ms
         *          프레임에서는 답이 달라질 수 있으므로 상수로 남깁니다.
         */
        constexpr int64 kWorkerIdleSpinMicro = 2;
        /**
         * @brief 워커 수를 정할 때 하드웨어 스레드 수에서 빼 두는 수입니다(게임 스레드와 렌더 스레드 몫).
         * @details 하드웨어 스레드 수만큼 워커를 만들면 워커가 일하는 동안 게임 · 렌더 스레드가 코어를 나눠 써야 해서, 게임 스레드가
         *          잡을 돌릴 때 렌더 스레드의 병렬 기록이 밀립니다. 상용 엔진도 전용 스레드 몫을 뺍니다.
         */
        constexpr uint32 kReservedThreadCount = 2;
        /**
         * @brief 병렬 그룹을 참여 스레드(워커 + 호출 스레드) 하나당 몇 청크로 나눌지 정합니다.
         * @details 청크는 노드가 아니라 원자 카운터 연산 한 번이라 잘게 나눠도 비용이 거의 없습니다. 잘게 나누면 먼저 끝난 스레드가
         *          남은 청크를 이어서 가져가 꼬리가 짧아집니다(청크 하나가 곧 꼬리 길이의 상한입니다). 너무 잘게 나누면 청크마다 캐시
         *          라인 하나(`_nextChunkStart`)가 코어 사이를 오갑니다. 4개가 균형점입니다.
         */
        constexpr uint32 kChunksPerThread = 4;
        /** @brief 같은 스테이지를 두 번째로 기다리는 스레드가 브로드캐스트를 기다리며 잠들 때의 폴링 간격(밀리초)입니다. 드문 경로입니다. */
        constexpr uint32 kSecondWaiterPollMilli = 1;
        /** @brief 큐 항목의 태그 비트입니다. 켜져 있으면 `ParallelGroup*` 티켓, 아니면 `TaskNode*` 입니다. 둘 다 8바이트 정렬이라 하위 비트가 비어 있습니다. */
        constexpr uintptr_t kTicketTagBit = 1;

        /**
         * @brief 병렬 그룹의 본문을 도는 동안 "지금 실행 중인 태스크" 를 비워 두는 RAII 스코프입니다. 끝나면 바깥 값을 되돌립니다.
         * @details 병렬 본문은 어느 태스크의 본문도 아니다. 이 스레드가 **돕고 있던** 태스크를 그대로 물려받으면 본문에서 만든 태스크가
         *          엉뚱한 태스크의 자식이 된다(씬 로드 태스크가 게임 틱의 소리 재생 태스크를 기다리는 식 — 어느 스레드가 티켓을 가져가느냐에
         *          따라 부모가 달라진다). 본문에서 만든 태스크는 최상위다. 티켓 하나(청크 여러 개)마다 한 번 세우므로 청크당 비용은 없습니다.
         *
         *          "병렬 본문 안인가" 표시는 두지 않는다. 틱 중 구조 변경은 단언이 아니라 구조 변경 큐로 미룬다(`GameObjectManager::deferStructuralChange`).
         */
        struct ParallelTaskScope
        {
            ParallelTaskScope()
                : _pPreviousRunningTask{ t_pCurrentRunningTask }
            {
                t_pCurrentRunningTask = nullptr;
            }
            ~ParallelTaskScope()
            {
                t_pCurrentRunningTask = _pPreviousRunningTask;
            }
            ParallelTaskScope( const ParallelTaskScope& )            = delete;
            ParallelTaskScope& operator=( const ParallelTaskScope& ) = delete;

            TaskNode* _pPreviousRunningTask;
        };

        /** @brief 큐 항목 하나입니다. 태스크 노드 포인터이거나, 태그 비트가 켜진 병렬 그룹 포인터입니다. */
        struct TaskQueueItem
        {
            static uintptr_t      fromNode( TaskNode* pNode ) { return reinterpret_cast<uintptr_t>( pNode ); }
            static uintptr_t      fromTicket( ParallelGroup* pGroup ) { return reinterpret_cast<uintptr_t>( pGroup ) | kTicketTagBit; }
            static bool           isTicket( uintptr_t item ) { return ( item & kTicketTagBit ) != 0; }
            static TaskNode*      toNode( uintptr_t item ) { return reinterpret_cast<TaskNode*>( item ); }
            static ParallelGroup* toGroup( uintptr_t item ) { return reinterpret_cast<ParallelGroup*>( item & ~kTicketTagBit ); }
        };

        /** @brief 태스크 본문을 호출합니다. 호출 대상 variant 의 분기마다 하나씩 있습니다. */
        struct TaskExecutionVisitor
        {
            void operator()( std::monostate& ) const {}

            void operator()( TaskDelegate& taskDelegate ) const
            {
                if ( taskDelegate.isBound() )
                    taskDelegate();
            }

            void operator()( TaskArgsPayload& payload ) const
            {
                if ( payload._delegate.isBound() )
                    payload._delegate( payload._args );
            }

            /** @brief 병렬 그룹 띄우기는 `executeTask` 가 먼저 가로챈다(매니저가 필요하다). 여기까지 오지 않는다. */
            void operator()( ParallelGroupLaunch& ) const {}
        };

        /** @brief 병렬 그룹을 어떻게 나눌지(청크 크기와 큐에 넣을 티켓 수)입니다. */
        struct ParallelSplit
        {
            uint32 _chunkSize{ 1 };
            uint32 _ticketCount{ 0 };

            /**
             * @brief @p count 개를 워커 @p workerCount 개(+ 호출 스레드)가 나눠 갖도록 자릅니다.
             * @param bCallerRuns 호출 스레드가 바로 함께 실행하는지 여부(`runParallel`). 그렇다면 티켓은 나머지 참여자 몫만 넣습니다.
             */
            static ParallelSplit compute( uint32 count, uint32 workerCount, bool bCallerRuns )
            {
                ParallelSplit split{};
                if ( count == 0 )
                    return split;
                const uint32 participantCount = workerCount + 1;
                uint32       chunkCount       = MathUtil::min( count, participantCount * kChunksPerThread );
                split._chunkSize              = ( count + chunkCount - 1 ) / chunkCount;
                if ( split._chunkSize == 0 )
                    split._chunkSize = 1;
                chunkCount = ( count + split._chunkSize - 1 ) / split._chunkSize;
                // 티켓 하나는 스레드 하나의 몫이다. 청크가 티켓보다 적으면 남는 티켓은 가져가자마자 끝난다(헛된 깨우기).
                const uint32 requestedTicketCount = bCallerRuns ? ( chunkCount > 0 ? chunkCount - 1 : 0 ) : chunkCount;
                split._ticketCount                = MathUtil::min( requestedTicketCount, workerCount );
                return split;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 수명 — 생성 · 초기화 · 종료 · 강제 정리
    // ------------------------------------------------------------------------------
    TaskManager::TaskManager()
        : _bInitialized{ false }
        , _mainThreadID{}
        , _bStop{ false }
        , _listWorkerSlot{}
        , _helperSlotCount{ 0 }
        , _helperSlotFreeMask{ 0 }
        , _instanceID{ s_nextInstanceID.fetch_add( 1, std::memory_order_relaxed ) }
        , _idleWorkerMask{ 0 }
        , _workEpoch{ 0 }
        , _wakeSignalCount{ 0 }
        , _completionEpoch{ 0 }
        , _broadcastWaiterCount{ 0 }
        , _mainThreadParkedSlot{ -1 }
        , _globalHighQueue{}
        , _globalWorkerQueue{}
        , _globalLowQueue{}
        , _queueMainThread{}
        , _arrHelperWaiter{}
        , _activeTaskCount{ 0 }
        , _clearEpoch{ 0 }
        , _nodePool{ sw::make_unique<TaskNodePool>() }
    {
    }

    // 소멸자에서 예외가 빠져나가면 프로그램이 종료된다. `shutdown()` 이 잠그는 뮤텍스는 시스템 오류일 때만 예외를 던지는데,
    // 그 상황은 소멸자에서 복구할 방법이 없으므로 종료가 맞는 동작이다.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    TaskManager::~TaskManager()
    {
        shutdown();
    }

    bool TaskManager::initialize( uint32 threadCount )
    {
        if ( _bInitialized )
            return false;

        constexpr uint32 kDefaultThreadCount = 4;
        if ( threadCount == 0 )
        {
            threadCount = std::thread::hardware_concurrency();
            if ( threadCount == 0 )
                threadCount = kDefaultThreadCount;
            else if ( threadCount > kReservedThreadCount + 1 )
                threadCount -= kReservedThreadCount;
            else
                threadCount = 1;
        }
        if ( threadCount > kMaxWorkerCount )
            threadCount = kMaxWorkerCount;

        _mainThreadID = std::this_thread::get_id();
        _bStop        = false;
        _idleWorkerMask.store( 0, std::memory_order_relaxed );
        _mainThreadParkedSlot.store( -1, std::memory_order_relaxed );
        // 자리를 모두 만든 **뒤에** 스레드를 띄운다. 워커는 서로의 데크를 훔치므로, 도는 동안 목록의 크기가 바뀌면 안 된다.
        _listWorkerSlot.reserve( threadCount );
        for ( uint32 workerIndex = 0; workerIndex < threadCount; ++workerIndex )
        {
            _listWorkerSlot.push_back( make_unique<WorkerSlot>() );
        }
        for ( uint32 workerIndex = 0; workerIndex < threadCount; ++workerIndex )
        {
            // 워커가 이미 목록을 읽고 있으므로 const 로 꺼낸다(비 const 첨자는 레이스 탐지기에 쓰기로 잡힌다).
            std::as_const( _listWorkerSlot )[workerIndex]->_thread = std::thread( &TaskManager::workerLoop, this, workerIndex );
        }

        _bInitialized = true;
        SW_LOG_INFO( "Initialized with %# worker threads.", threadCount );
        return true;
    }

    void TaskManager::shutdown()
    {
        if ( _bInitialized == false )
            return;

        BLOCK( "Wait For Active Tasks" )
        {
            waitAll();
        }

        BLOCK( "Stop Worker Threads" )
        {
            _bStop.store( true, std::memory_order_seq_cst );
            // 잠든 워커는 자기 워드로, 스핀 중인 워커는 세대로 알아챈다. 모두 깨운다.
            wakeSleepingWorkers();

            for ( const unique_ptr<WorkerSlot>& slot : std::as_const( _listWorkerSlot ) )
            {
                if ( slot->_thread.joinable() )
                    slot->_thread.join();
            }
            _listWorkerSlot.clear();
        }

        BLOCK( "Cleanup Resources" )
        {
            clear();
            _mainThreadID = {};
            _bInitialized = false;
        }
        SW_LOG_INFO( "Shutdown cleanly." );
    }

    void TaskManager::clear()
    {
        for ( const unique_ptr<WorkerSlot>& slot : _listWorkerSlot )
        {
            uintptr_t item{ 0 };
            while ( slot->_queue.steal( item ) )
            {
                drainQueueItem( item );
            }
        }
        {
            TaskNode* pTemp{ nullptr };
            while ( _queueMainThread.dequeue( pTemp ) )
            {
                if ( pTemp != nullptr )
                    pTemp->release();
            }
        }
        {
            uintptr_t item{ 0 };
            while ( _globalHighQueue.dequeue( item ) )
            {
                drainQueueItem( item );
            }
            while ( _globalWorkerQueue.dequeue( item ) )
            {
                drainQueueItem( item );
            }
            while ( _globalLowQueue.dequeue( item ) )
            {
                drainQueueItem( item );
            }
        }
        _activeTaskCount.store( 0, std::memory_order_release );
        _clearEpoch.fetch_add( 1, std::memory_order_acq_rel );
        // 큐를 모두 비웠으니 실행 중인 태스크가 없다. 살아 있는 스테이지를 모두 돌려준다.
        _nodePool->resetAllStages();
        notifyBroadcast();
    }

    void TaskManager::drainQueueItem( uintptr_t item )
    {
        if ( item == 0 )
            return;
        if ( TaskQueueItem::isTicket( item ) )
            closeGroupTicket( TaskQueueItem::toGroup( item ), false ); // 실행하지 않고 닫는다. 부모는 준비되지 않은 채로 핸들이 놓일 때 풀로 돌아간다
        else
            TaskQueueItem::toNode( item )->release();
    }

    // ------------------------------------------------------------------------------
    // 2) 스레드 구분 — 메인 · 워커 · 스크래치/대기자 슬롯
    // ------------------------------------------------------------------------------
    bool TaskManager::isMainThread() const
    {
        return _bInitialized && std::this_thread::get_id() == _mainThreadID;
    }

    void TaskManager::ensureMainThread() const
    {
        SW_ASSERT( isMainThread() );
    }

    bool TaskManager::isWorkerThread() const
    {
        return t_currentWorkerIndex >= 0;
    }

    void TaskManager::ensureWorkerThread() const
    {
        SW_ASSERT( isWorkerThread() );
    }

    uint32 TaskManager::getCurrentThreadScratchSlot()
    {
        if ( t_currentWorkerIndex >= 0 )
            return static_cast<uint32>( t_currentWorkerIndex );
        if ( t_helperScratchSlot < 0 || t_helperSlotOwnerID != _instanceID )
        {
            t_helperSlotOwnerID = _instanceID;
            // 돌려받은 칸부터 다시 쓴다(가장 낮은 비트).
            uint32 freeMask = _helperSlotFreeMask.load( std::memory_order_acquire );
            while ( freeMask != 0 )
            {
                const uint32 lowestBit = freeMask & ( ~freeMask + 1u );
                if ( _helperSlotFreeMask.compare_exchange_weak( freeMask, freeMask & ~lowestBit, std::memory_order_acq_rel ) )
                {
                    uint32 slot = 0;
                    while ( ( lowestBit >> slot ) != 1u )
                    {
                        ++slot;
                    }
                    t_helperScratchSlot = static_cast<int32>( slot );
                    return getWorkerCount() + static_cast<uint32>( t_helperScratchSlot );
                }
            }

            const uint32 assigned = _helperSlotCount.fetch_add( 1, std::memory_order_relaxed );
            if ( assigned >= kMaxHelperThreadCount )
            {
                // 넘치면 마지막 칸을 나눠 쓴다. 스크래치가 겹칠 수 있으니 알린다. 끝나는 스레드가 칸을 돌려주므로 동시에 살아 있는 비워커 스레드가
                // 이만큼일 때만 닿는다.
                SW_LOG_ERROR( "More than %# non-worker threads execute tasks at once — scratch slots collide (raise kMaxHelperThreadCount)", kMaxHelperThreadCount );
                t_helperScratchSlot = static_cast<int32>( kMaxHelperThreadCount - 1 );
            }
            else
                t_helperScratchSlot = static_cast<int32>( assigned );
        }
        return getWorkerCount() + static_cast<uint32>( t_helperScratchSlot );
    }

    void TaskManager::releaseCurrentThreadHelperSlot()
    {
        if ( t_helperScratchSlot < 0 || t_helperSlotOwnerID != _instanceID )
            return;
        // 넘쳐서 나눠 쓰던 마지막 칸은 다른 스레드도 쓰고 있을 수 있어 돌려주지 않는다.
        const bool bShared = _helperSlotCount.load( std::memory_order_relaxed ) > kMaxHelperThreadCount &&
                             t_helperScratchSlot == static_cast<int32>( kMaxHelperThreadCount - 1 );
        if ( bShared == false )
            _helperSlotFreeMask.fetch_or( 1u << static_cast<uint32>( t_helperScratchSlot ), std::memory_order_acq_rel );
        t_helperScratchSlot = -1;
        t_helperSlotOwnerID = 0;
    }

    atomic<uint32>& TaskManager::getWaiterWord( uint32 slotIndex )
    {
        const uint32 workerCount = getWorkerCount();
        if ( slotIndex < workerCount )
            return std::as_const( _listWorkerSlot )[slotIndex]->_park._word;
        uint32 helperIndex = slotIndex - workerCount;
        if ( helperIndex >= kMaxHelperThreadCount )
            helperIndex = kMaxHelperThreadCount - 1;
        return _arrHelperWaiter[helperIndex]._word;
    }

    // ------------------------------------------------------------------------------
    // 3) 태스크 만들기 · 의존성 · 제출
    // ------------------------------------------------------------------------------
    void TaskManager::deallocateNode( TaskNode* pNode )
    {
        // 한 번도 큐에 들어가지 않고 마지막 핸들이 놓였다 — 만들기만 하고 제출하지 않은 태스크다. 취소된 태스크처럼 완료 처리한다: 활성 수를 내리고
        // 부모 · 스테이지 · 후속을 푼다. 그러지 않으면 `waitAll` · 부모 · 후속이 영영 기다린다. `clear()` 뒤라면 그 수는 이미 0 으로 놓였으니
        // 건드리지 않는다.
        const bool bNeverScheduled = pNode->_bScheduled.load( std::memory_order_acquire ) == false;
        const bool bSameEpoch      = pNode->_clearEpoch == _clearEpoch.load( std::memory_order_acquire );
        if ( bNeverScheduled && bSameEpoch )
        {
            pNode->_bCancelled.store( true, std::memory_order_release );
            // 완료 처리가 이 노드의 참조를 잠깐 잡았다 놓을 수 있다(후속이 이 노드를 가리키지는 않는다). 풀로 돌리기 전에 한 번 더 잡아 둔다.
            pNode->_refCount.store( 1, std::memory_order_relaxed );
            releaseUnlaunchedGroup( pNode );
            completeTask( pNode );
            pNode->_refCount.store( 0, std::memory_order_relaxed );
        }
        _nodePool->deallocate( pNode );
    }

    TaskNode* TaskManager::createTaskNode( string_view name, TaskThreadAffinity affinity )
    {
        if ( _bInitialized == false || _bStop.load( std::memory_order_relaxed ) )
        {
            SW_LOG_WARNING( "Cannot emplace task '%#' while TaskManager is not initialized or stopping.", string{ name }.c_str() );
            return nullptr;
        }

        TaskNode* pNode    = _nodePool->allocate();
        pNode->_pOwner     = this;
        pNode->_clearEpoch = _clearEpoch.load( std::memory_order_acquire );
        pNode->setName( name );
        pNode->_affinity = affinity;
        // 실행하는 스레드가 이 태그로 할당하게 만든 쪽의 태그를 담는다(워커에서 하는 로드가 Unknown 으로 새지 않게).
        if constexpr ( kMemoryTagScopesEnabled )
            pNode->_memoryTag = Memory::getCurrentMemoryTag();

        // 태스크 본문 안에서 만든 태스크는 그 본문의 자식이다. 부모는 자식이 모두 끝나야 완료된다.
        if ( t_pCurrentRunningTask != nullptr )
        {
            pNode->_pParent = t_pCurrentRunningTask;
            t_pCurrentRunningTask->_pendingChildren.fetch_add( 1, std::memory_order_relaxed );
            t_pCurrentRunningTask->retain();
        }

        _activeTaskCount.fetch_add( 1, std::memory_order_relaxed );
        return pNode;
    }

    TaskHandle TaskManager::emplaceTask( const TaskDelegate& delegate, TaskThreadAffinity affinity )
    {
        return emplaceTask( "GeneralTask", delegate, affinity );
    }

    TaskHandle TaskManager::emplaceTask( string_view name, const TaskDelegate& delegate, TaskThreadAffinity affinity )
    {
        TaskNode* pNode = createTaskNode( name, affinity );
        if ( pNode == nullptr )
            return TaskHandle{};
        pNode->_callable = delegate;
        return TaskHandle{ pNode };
    }

    TaskHandle TaskManager::emplaceTask( const TaskArgsDelegate& delegate, const TaskArgs& args, TaskThreadAffinity affinity )
    {
        return emplaceTask( "GeneralArgsTask", delegate, args, affinity );
    }

    TaskHandle TaskManager::emplaceTask( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args, TaskThreadAffinity affinity )
    {
        TaskNode* pNode = createTaskNode( name, affinity );
        if ( pNode == nullptr )
            return TaskHandle{};
        pNode->_callable = TaskArgsPayload{ delegate, args };
        return TaskHandle{ pNode };
    }

    TaskHandle TaskManager::whenAll( vector_reference<const TaskHandle> listTask, const TaskDelegate& continuation, TaskThreadAffinity affinity )
    {
        TaskHandle nextTask = emplaceTask( "WhenAllContinuation", continuation, affinity );
        for ( const TaskHandle& task : listTask )
        {
            if ( task.isValid() )
            {
                TaskHandle mutTask = task;
                mutTask.runBefore( nextTask );
            }
        }
        return nextTask;
    }

    TaskHandle TaskManager::whenAny( vector_reference<const TaskHandle> listTask, const TaskDelegate& continuation, TaskThreadAffinity affinity )
    {
        // 유효한 핸들이 하나도 없으면 빈 목록과 같다. 주의: "선행 트리거 몫" 1 을 더해 두고 트리거를 하나도 만들지 않으면 후속이 영영 돌지
        // 않고 활성 수가 남아 `waitAll` · 종료가 멈춘다.
        bool bHasValidTask{ false };
        for ( const TaskHandle& task : listTask )
        {
            if ( task.isValid() )
            {
                bHasValidTask = true;
                break;
            }
        }
        if ( bHasValidTask == false )
            return emplaceTask( "WhenAnyContinuation", continuation, affinity );

        shared_ptr<atomic<bool>> firedFlag = sw::make_shared<atomic<bool>>( false );
        TaskHandle               nextTask  = emplaceTask( "WhenAnyContinuation", continuation, affinity );

        // 사용자의 submit() 몫 1, 선행 태스크 완료 트리거 몫 1
        nextTask.getNode()->_unresolvedDependencies.store( 2, std::memory_order_relaxed );

        for ( const TaskHandle& task : listTask )
        {
            if ( task.isValid() )
            {
                TaskDelegate triggerDelegate = SW_DELEGATE_LAMBDA( TaskDelegate,
                                                                   [nextTask, firedFlag, this]()
                {
                    if ( firedFlag->exchange( true, std::memory_order_acq_rel ) == false )
                        resolveDependency( nextTask.getNode(), true );
                } );

                TaskHandle triggerTask = emplaceTask( "whenAny_Trigger", triggerDelegate );
                TaskHandle mutTask     = task;
                mutTask.runBefore( triggerTask );
                triggerTask.submit();
            }
        }

        return nextTask;
    }

    void TaskManager::resolveDependency( TaskNode* pNode, bool bWakeWorker )
    {
        if ( pNode == nullptr )
            return;
        if ( pNode->_unresolvedDependencies.fetch_sub( 1, std::memory_order_acq_rel ) != 1 )
            return;

        // 병렬 그룹의 부모가 시작 조건(제출 + 선행)을 모두 채웠다 — 지금 티켓을 올린다. 0 을 보는 스레드는 하나뿐이므로 여기서 꺼내도 안전하다.
        // 부모는 그룹이 끝나 의존 하나가 더 풀릴 때 큐로 가고, 거기서 제 실행 스레드(메인 지정이면 메인)에서 완료된다.
        if ( std::holds_alternative<ParallelGroupLaunch>( pNode->_callable ) )
        {
            const ParallelGroupLaunch launch = std::get<ParallelGroupLaunch>( pNode->_callable );
            pNode->_callable                 = std::monostate{};
            if ( pNode->_bCancelled.load( std::memory_order_acquire ) == false && launch._pGroup != nullptr )
            {
                launchParallelGroup( pNode, launch );
                return;
            }
            // 취소됐다 — 본문(청크)은 돌지 않는다. 그룹을 돌려주고 부모는 빈 본문으로 끝낸다.
            if ( launch._pGroup != nullptr )
                _nodePool->deallocateGroup( launch._pGroup );
        }
        scheduleReadyTask( pNode, bWakeWorker );
    }

    void TaskManager::submit( const TaskHandle& handle )
    {
        resolveDependency( handle.getNode(), true );
    }

    void TaskManager::submitWithoutWake( const TaskHandle& handle )
    {
        resolveDependency( handle.getNode(), false );
    }

    // ------------------------------------------------------------------------------
    // 4) 병렬 그룹 — 티켓 · 청크 · 조인
    // ------------------------------------------------------------------------------
    TaskHandle TaskManager::emplaceParallel( uint32 count, const ParallelTaskDelegate& delegate, TaskThreadAffinity affinity )
    {
        return emplaceParallel( "ParallelTask", count, delegate, affinity );
    }

    TaskHandle TaskManager::emplaceParallel( string_view name, uint32 count, const ParallelTaskDelegate& delegate, TaskThreadAffinity affinity )
    {
        return emplaceParallelGroup( name, 0, count, nullptr, &delegate, affinity );
    }

    TaskHandle TaskManager::emplaceParallelBlock( uint32 start, uint32 end, const ParallelBlockDelegate& delegate, TaskThreadAffinity affinity )
    {
        return emplaceParallelGroup( "ParallelBlockTask", start, end, &delegate, nullptr, affinity );
    }

    TaskHandle TaskManager::emplaceParallelGroup( string_view name, uint32 start, uint32 end, const ParallelBlockDelegate* pBlockBody, const ParallelTaskDelegate* pIndexBody, TaskThreadAffinity affinity )
    {
        if ( end <= start )
            return emplaceTask( name, TaskDelegate{}, affinity );

        // 사용자에게 보이는 태스크는 부모 노드다. 핸들 · 스테이지 · 후속 태스크 · 활성 수가 모두 이 노드 하나에 연결된다.
        TaskHandle parentTask = emplaceTask( name, TaskDelegate{}, affinity );
        if ( parentTask.isValid() == false )
            return parentTask;

        const ParallelSplit split = ParallelSplit::compute( end - start, MathUtil::max( getWorkerCount(), 1u ), false );

        ParallelGroup* pGroup = _nodePool->allocateGroup();
        pGroup->_blockBody    = pBlockBody != nullptr ? *pBlockBody : ParallelBlockDelegate{};
        pGroup->_indexBody    = pIndexBody != nullptr ? *pIndexBody : ParallelTaskDelegate{};
        // 블록 본문은 자기 사본을 가리킨다. 묶이지 않은 본문이면 null 로 두어, 청크 실행이 묶였는지를 청크마다 묻지 않게 한다.
        pGroup->_pBlockBody = pGroup->_blockBody.isBound() ? &pGroup->_blockBody : nullptr;
        pGroup->_rangeEnd   = end;
        pGroup->_chunkSize  = split._chunkSize;
        if constexpr ( kMemoryTagScopesEnabled )
            pGroup->_memoryTag = Memory::getCurrentMemoryTag();
        pGroup->_nextChunkStart.store( start, std::memory_order_relaxed );
        pGroup->_join.reset();

        // 티켓은 **부모가 실행될 때** 올린다(`launchParallelGroup`). 그래야 부모에 건 `runAfter` · 취소 · 실행 스레드 지정을 본문도 따른다.
        // 부모는 그룹이 끝나야 완료된다(그룹 = 부모의 자식 몫 하나).
        TaskNode*    pParent     = parentTask.getNode();
        const uint32 ticketCount = MathUtil::max( split._ticketCount, 1u );
        pParent->_callable       = ParallelGroupLaunch{ pGroup, ticketCount };

        return parentTask;
    }

    void TaskManager::runParallel( uint32 count, uint32 serialThreshold, const ParallelBlockDelegate& body )
    {
        if ( count == 0 || body.isBound() == false )
            return;
        // 문턱값 아래이거나 워커가 없으면 이 스레드가 한 번에 실행한다. 나누는 비용이 일보다 크다.
        // 그래도 병렬 본문이다 — 본문에서 만든 태스크가 최상위인 것이 개수 · 워커 수에 따라 달라지지 않게 한다.
        const uint32 workerCount = getWorkerCount();
        if ( _bInitialized == false || workerCount == 0 || count < serialThreshold )
        {
            const ParallelTaskScope bodyScope{};
            body( 0, count );
            return;
        }

        const ParallelSplit split = ParallelSplit::compute( count, workerCount, true );
        if ( split._ticketCount == 0 )
        {
            const ParallelTaskScope bodyScope{};
            body( 0, count );
            return;
        }

        // 그룹은 이 스택에 있다. 마지막 티켓이 조인 카운터를 0 으로 만든 뒤에는 아무도 이 메모리를 건드리지 않는다.
        ParallelGroup group{};
        group._pBlockBody = &body;
        group._rangeEnd   = count;
        group._chunkSize  = split._chunkSize;
        if constexpr ( kMemoryTagScopesEnabled )
            group._memoryTag = Memory::getCurrentMemoryTag();
        group._nextChunkStart.store( 0, std::memory_order_relaxed );
        group._join.addPending( split._ticketCount );

        // `waitAll` 에게는 그룹 하나가 태스크 하나다. 티켓마다 활성 수를 올리고 내리지 않는다.
        _activeTaskCount.fetch_add( 1, std::memory_order_relaxed );
        pushGroupTickets( &group, split._ticketCount );

        // 워커가 깨어나기를 기다리지 않는다. 이 스레드가 첫 청크부터 가져간다. 남은 티켓은 이 스레드가 닿을 수 있는 큐(자기
        // 데크 · 전역)에만 있으므로, 조인을 기다리며 다른 일을 돕다 보면 스스로 가져가게 된다.
        runGroupChunks( &group );
        // Low 줄(백그라운드 I/O)은 돕지 않는다 — 합류는 Normal 의 자기 티켓과 남이 도는 청크만 기다린다. Low 하나를 집으면 그 태스크만큼 합류가 늦어진다.
        waitForJoin( group._join, false );

        if ( _activeTaskCount.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
            notifyBroadcast();
    }

    void TaskManager::pushGroupTickets( ParallelGroup* pGroup, uint32 ticketCount )
    {
        const uintptr_t item = TaskQueueItem::fromTicket( pGroup );
        for ( uint32 index = 0; index < ticketCount; ++index )
        {
            pushToNormalQueue( item );
        }
        // 티켓을 모두 넣은 뒤 **한 번만** 세대를 올리고, 티켓 수만큼만 깨운다.
        wakeSleepingWorkers( ticketCount );
    }

    void TaskManager::drainHighQueueBetweenChunks()
    {
        // 빈 큐의 size 는 위치 두 개를 읽는 것이라 청크마다 확인해도 비용이 거의 없다.
        if ( _globalHighQueue.empty() )
            return;
        uintptr_t item{ 0 };
        while ( _globalHighQueue.dequeue( item ) )
        {
            executeItem( item );
        }
    }

    void TaskManager::runGroupChunks( ParallelGroup* pGroup )
    {
        const ParallelTaskScope bodyScope{};
        const ScopedMemoryTag   groupMemoryTag{ pGroup->_memoryTag };
        const uint32            end       = pGroup->_rangeEnd;
        const uint32            chunkSize = pGroup->_chunkSize;
        for ( ;; )
        {
            // **High 큐는 청크 사이에 확인한다.** 티켓을 가진 워커가 그룹을 다 실행할 때까지 렌더 스레드의 패스 기록을 세워 두면
            // 그 줄이 그대로 프레임 지연이 된다. 청크 하나(참여자당 4개)가 작으므로 기다리는 시간의 상한도 짧다.
            drainHighQueueBetweenChunks();
            const uint64 reservedStart = pGroup->_nextChunkStart.fetch_add( chunkSize, std::memory_order_relaxed );
            if ( reservedStart >= end )
                break;
            const uint32 chunkStart = static_cast<uint32>( reservedStart );
            const uint32 chunkEnd   = MathUtil::min( chunkStart + chunkSize, end );
            if ( pGroup->_pBlockBody != nullptr )
            {
                ( *pGroup->_pBlockBody )( chunkStart, chunkEnd );
            }
            else if ( pGroup->_indexBody.isBound() )
            {
                for ( uint32 elementIndex = chunkStart; elementIndex < chunkEnd; ++elementIndex )
                {
                    pGroup->_indexBody( elementIndex );
                }
            }
        }
    }

    void TaskManager::launchParallelGroup( TaskNode* pParent, const ParallelGroupLaunch& launch )
    {
        ParallelGroup* pGroup = launch._pGroup;
        if ( pGroup == nullptr )
            return;
        // 그룹이 부모를 잡고(참조 하나), 부모는 그룹이 끝나야 준비된다(의존 하나 — 마지막 티켓이 푼다, `closeGroupTicket`).
        pParent->retain();
        pParent->_unresolvedDependencies.fetch_add( 1, std::memory_order_acq_rel );
        pGroup->_pParent = pParent;
        pGroup->_join.addPending( launch._ticketCount );
        pushGroupTickets( pGroup, launch._ticketCount );
    }

    void TaskManager::releaseUnlaunchedGroup( TaskNode* pNode )
    {
        if ( std::holds_alternative<ParallelGroupLaunch>( pNode->_callable ) == false )
            return;
        ParallelGroup* pGroup = std::get<ParallelGroupLaunch>( pNode->_callable )._pGroup;
        pNode->_callable      = std::monostate{};
        if ( pGroup != nullptr )
            _nodePool->deallocateGroup( pGroup );
    }

    void TaskManager::runGroupTicket( ParallelGroup* pGroup )
    {
        runGroupChunks( pGroup );
        closeGroupTicket( pGroup, true );
    }

    void TaskManager::closeGroupTicket( ParallelGroup* pGroup, bool bResolveParent )
    {
        // 스택 그룹은 조인 카운터가 0 이 되는 순간 사라질 수 있다. 그 뒤에 필요한 값은 모두 **미리** 읽어 둔다.
        const bool   bPooledGroup = pGroup->isPooled();
        const uint64 joinBefore   = pGroup->_join.finishOne();
        if ( JoinCounter::isLastFinish( joinBefore ) == false )
            return;

        if ( bPooledGroup )
        {
            // 풀 그룹에서는 이 스레드가 마지막으로 만지는 쪽이다. 부모의 그룹 의존성을 풀고(준비되면 큐로 간다) 그룹을 돌려준다.
            TaskNode* pParent = pGroup->_pParent;
            pGroup->_pParent  = nullptr;
            _nodePool->deallocateGroup( pGroup );
            if ( bResolveParent )
                resolveDependency( pParent, true );
            pParent->release();
        }

        const uint32 waiterPlusOne = JoinCounter::waiterSlotPlusOne( joinBefore );
        if ( waiterPlusOne != 0 )
            unparkSlot( waiterPlusOne - 1 );
    }

    // ------------------------------------------------------------------------------
    // 5) 스테이지
    // ------------------------------------------------------------------------------
#if !defined( SW_SHIPPING )
    void TaskManager::setProfileHook( const TaskProfileHook* pHook )
    {
        s_pProfileHook.store( pHook, std::memory_order_release );
    }
#endif

    TaskStageHandle TaskManager::createStage()
    {
        return createStage( string_view{} );
    }

    TaskStageHandle TaskManager::createStage( [[maybe_unused]] string_view debugName )
    {
        // 풀에서 가져온다. 프레임마다 레벨 수만큼 만드는 곳이라 힙을 쓰면 그 수만큼 할당과 해제가 반복된다(churn).
        StageNode* pStage = _nodePool->allocateStage();
#if !defined( SW_SHIPPING )
        copyTaskDebugName( pStage->_arrName, debugName ); // 풀에서 다시 쓰는 노드라 늘 덮어쓴다(빈 이름 포함)
#endif
        return TaskStageHandle{ pStage };
    }

    void TaskManager::waitStage( const TaskStageHandle& stage )
    {
        if ( stage._pNode == nullptr )
            return;
#if !defined( SW_SHIPPING )
        const TaskProfileHook* pHook = s_pProfileHook.load( std::memory_order_acquire );
        const TaskProfileZone  zone  = ( pHook != nullptr && stage._pNode->_arrName[0] != '\0' && stage._pNode->_join.getPending() > 0 )
                                         ? pHook->_pBeginZone( stage._pNode->_arrName, TaskProfileZoneKind::WaitStage )
                                         : TaskProfileZone{};
#endif
        waitForJoin( stage._pNode->_join, true );
#if !defined( SW_SHIPPING )
        if ( zone._pContext != nullptr )
            pHook->_pEndZone( zone );
#endif
    }

    bool TaskManager::isStageComplete( const TaskStageHandle& stage )
    {
        return stage._pNode == nullptr || stage._pNode->_join.getPending() == 0;
    }

    // ------------------------------------------------------------------------------
    // 6) 기다리기 — 다른 일을 돕다가 자기 워드에서 잠들기, 브로드캐스트, 메인 스레드 깨우기
    // ------------------------------------------------------------------------------
    bool TaskManager::helpOrSpin( uint32& inoutSpinCount, const bool bHelpLowQueue )
    {
        if ( isMainThread() )
            dispatchMainThreadTasks();

        if ( tryHelpAndExecute( bHelpLowQueue ) )
        {
            inoutSpinCount = 0;
            return true;
        }

        if ( inoutSpinCount < kIdleSpinCount )
        {
            sw::cpuPause();
            ++inoutSpinCount;
            return true;
        }

        inoutSpinCount = 0;
        return false;
    }

    void TaskManager::waitForJoin( JoinCounter& join, const bool bHelpLowQueue )
    {
        uint32 spinCount = 0;
        while ( join.getPending() > 0 )
        {
            if ( helpOrSpin( spinCount, bHelpLowQueue ) == false )
                parkOnJoin( join );
        }
    }

    bool TaskManager::waitAll( uint32 timeoutMs )
    {
        const Stopwatch stopwatch;
        uint32          spinCount = 0;
        while ( _activeTaskCount.load( std::memory_order_acquire ) > 0 )
        {
            if ( helpOrSpin( spinCount, true ) )
                continue;

            uint32 waitMilli = 0;
            if ( timeoutMs > 0 )
            {
                const int64 elapsed = stopwatch.getElapsedMilliseconds();
                if ( elapsed >= static_cast<int64>( timeoutMs ) )
                    return _activeTaskCount.load( std::memory_order_acquire ) == 0;
                waitMilli = static_cast<uint32>( static_cast<int64>( timeoutMs ) - elapsed );
            }

            // 대상이 전체라 대기자 하나를 적어 둘 곳이 없다. 브로드캐스트를 기다리며 잠든다(활성 수 0 · 메인 일감 · clear 가 알린다).
            const uint32 epoch = prepareBroadcastWait();
            if ( _activeTaskCount.load( std::memory_order_seq_cst ) > 0 )
                commitBroadcastWait( epoch, waitMilli );
            else
                cancelBroadcastWait();
        }
        return true;
    }

    void TaskManager::parkOnJoin( JoinCounter& join )
    {
        const uint32    slotIndex = getCurrentThreadScratchSlot();
        atomic<uint32>& word      = getWaiterWord( slotIndex );
        // **등록하기 전에** 읽는다. 등록한 뒤에 온 깨우기는 워드를 바꾸므로 아래 wait 가 바로 돌아온다.
        const uint32 observed = word.load( std::memory_order_acquire );

        switch ( join.tryRegisterWaiter( slotIndex + 1 ) )
        {
            case JoinCounter::RegisterResult::AlreadyDone:
            {
                return;
            }
            case JoinCounter::RegisterResult::Busy:
            {
                // 같은 것을 두 번째로 기다린다. 드문 경우다. 브로드캐스트를 기다리며 잠들되 짧게 끊어 폴링한다(완료되면 브로드캐스트도 보낸다).
                const uint32 epoch = prepareBroadcastWait();
                if ( join.getPending() > 0 )
                    commitBroadcastWait( epoch, kSecondWaiterPollMilli );
                else
                    cancelBroadcastWait();
                return;
            }
            case JoinCounter::RegisterResult::Registered:
            {
                break;
            }
        }

        const bool bMainThread = isMainThread();
        if ( bMainThread )
        {
            // 메인 일감을 넣는 쪽이 이 칸을 보고 깨운다. 적은 뒤에 큐를 다시 확인해야 그 사이에 들어온 일감을 놓치지 않는다.
            _mainThreadParkedSlot.store( static_cast<int32>( slotIndex ), std::memory_order_seq_cst );
            std::atomic_thread_fence( std::memory_order_seq_cst );
            if ( _queueMainThread.empty() == false )
            {
                _mainThreadParkedSlot.store( -1, std::memory_order_seq_cst );
                return;
            }
        }

        Futex::wait( word, observed );

        if ( bMainThread )
            _mainThreadParkedSlot.store( -1, std::memory_order_seq_cst );
    }

    uint32 TaskManager::prepareBroadcastWait()
    {
        const uint32 epoch = _completionEpoch.load( std::memory_order_acquire );
        _broadcastWaiterCount.fetch_add( 1, std::memory_order_seq_cst );
        std::atomic_thread_fence( std::memory_order_seq_cst );
        return epoch;
    }

    void TaskManager::commitBroadcastWait( uint32 observedEpoch, uint32 timeoutMilli )
    {
        if ( timeoutMilli == 0 )
            Futex::wait( _completionEpoch, observedEpoch );
        else
            Futex::waitFor( _completionEpoch, observedEpoch, timeoutMilli );
        _broadcastWaiterCount.fetch_sub( 1, std::memory_order_seq_cst );
    }

    void TaskManager::cancelBroadcastWait()
    {
        _broadcastWaiterCount.fetch_sub( 1, std::memory_order_seq_cst );
    }

    void TaskManager::notifyBroadcast()
    {
        // 완료를 만든 원자 연산 뒤에 메모리 펜스를 치고 대기자 수를 본다. 대기자 쪽의 "수 올리기 → 펜스 → 조건 확인" 과 짝을 이룬다.
        std::atomic_thread_fence( std::memory_order_seq_cst );
        if ( _broadcastWaiterCount.load( std::memory_order_seq_cst ) == 0 )
            return;
        _completionEpoch.fetch_add( 1, std::memory_order_release );
        Futex::wakeAll( _completionEpoch );
    }

    void TaskManager::wakeParkedMainThread()
    {
        std::atomic_thread_fence( std::memory_order_seq_cst );
        const int32 parkedSlot = _mainThreadParkedSlot.load( std::memory_order_seq_cst );
        if ( parkedSlot >= 0 )
            unparkSlot( static_cast<uint32>( parkedSlot ) );
        // 메인이 브로드캐스트(`waitAll`)를 기다리며 잠들어 있을 수도 있다.
        notifyBroadcast();
    }

    void TaskManager::unparkSlot( uint32 slotIndex )
    {
        atomic<uint32>& word = getWaiterWord( slotIndex );
        word.fetch_add( 1, std::memory_order_release );
        Futex::wakeOne( word );
    }

    // ------------------------------------------------------------------------------
    // 7) 실행 — 메인 큐 · 항목 · 태스크 · 완료
    // ------------------------------------------------------------------------------
    void TaskManager::dispatchMainThreadTasks()
    {
        ensureMainThread();

        TaskNode* pNode{ nullptr };
        while ( _queueMainThread.dequeue( pNode ) )
        {
            if ( pNode != nullptr )
                executeTask( pNode );
        }
    }

    void TaskManager::executeItem( uintptr_t item )
    {
        if ( item == 0 )
            return;
        if ( TaskQueueItem::isTicket( item ) )
            runGroupTicket( TaskQueueItem::toGroup( item ) );
        else
            executeTask( TaskQueueItem::toNode( item ) );
    }

    void TaskManager::executeTask( TaskNode* pNode )
    {
        if ( pNode->_bCancelled.load( std::memory_order_acquire ) == false )
        {
            // 끝나면 "지금 실행 중인 태스크" 를 되돌린다 — 남겨 두면 끝난 태스크가 이 스레드의 현재 태스크로 남아 뒤에 만드는 태스크가 그
            // 자식이 된다(영영 끝나지 않는다).
            TaskNode* pPrevRunningTask = t_pCurrentRunningTask;
            t_pCurrentRunningTask      = pNode;
            const ScopedMemoryTag taskMemoryTag{ pNode->_memoryTag };
#if !defined( SW_SHIPPING )
            const TaskProfileHook* pHook = s_pProfileHook.load( std::memory_order_acquire );
            const TaskProfileZone  zone  = ( pHook != nullptr && pNode->_arrName[0] != '\0' ) ? pHook->_pBeginZone( pNode->_arrName, TaskProfileZoneKind::Execute )
                                                                                              : TaskProfileZone{};
#endif

            BLOCK( "Execute Task Delegate" )
            {
                TaskExecutionVisitor visitor{};
                std::visit( visitor, pNode->_callable );
            }

#if !defined( SW_SHIPPING )
            if ( zone._pContext != nullptr )
                pHook->_pEndZone( zone );
#endif
            t_pCurrentRunningTask = pPrevRunningTask;
        }

        // 본문 몫(1)을 내린다. 0 이 되면 자식도 모두 끝난 것이고, 아니면 마지막 자식이 완료를 처리한다.
        if ( pNode->_pendingChildren.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
            completeTask( pNode );

        pNode->release(); // 큐가 잡고 있던 참조를 놓는다
    }

    void TaskManager::completeTask( TaskNode* pNode )
    {
        // **활성 수는 스테이지 · 부모보다 먼저 내린다.** 맨 끝(후속 트리거 뒤)에서 내리면 `waitStage` 가 스테이지 완료 통지를 받고
        // 돌아온 순간에도 이 태스크는 아직 활성으로 세어져 있다. 그 직후 `clear()` 가 수를 0 으로 놓으면 뒤늦은 fetch_sub 가
        // 0xFFFFFFFF 로 돌아가 이후의 `waitAll` 이 영원히 기다린다(스케줄링에 따라 CTest 아래에서만 드러난다). 후속 태스크는 만들 때
        // 이미 세어져 있으므로 여기서 내려도 `waitAll` 이 일찍 돌아오지 않는다.
        if ( _activeTaskCount.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
            notifyBroadcast();

        BLOCK( "Update Parent Task" )
        {
            TaskNode* pParent = pNode->_pParent;
            pNode->_pParent   = nullptr;
            if ( pParent != nullptr )
            {
                if ( pParent->_pendingChildren.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
                    completeTask( pParent );
                pParent->release();
            }
        }

        BLOCK( "Update Stage" )
        {
            StageNode* pStage   = pNode->_parentStage;
            pNode->_parentStage = nullptr;
            if ( pStage != nullptr )
            {
                const uint64 joinBefore = pStage->_join.finishOne();
                if ( JoinCounter::isLastFinish( joinBefore ) )
                {
                    // 마지막 태스크가 끝났다. 적혀 있던 대기자만 깨우고(두 번째 대기자는 브로드캐스트로), addTask 에서 잡은 자기 참조를 놓는다.
                    const uint32 waiterPlusOne = JoinCounter::waiterSlotPlusOne( joinBefore );
                    if ( waiterPlusOne != 0 )
                        unparkSlot( waiterPlusOne - 1 );
                    notifyBroadcast();
                    pStage->release();
                }
            }
        }

        BLOCK( "Trigger Successors and Cleanup" )
        {
            // 목록을 **닫고** 푼다. 닫은 뒤에 `runBefore` 로 붙으려는 후속은 "이미 끝났다" 를 듣고 스스로 의존을 되돌린다(`tryPushBack`).
            // 닫지 않고 훑으면 그 사이에 붙은 후속이 영영 풀리지 않거나 너무 일찍 돈다.
            pNode->_successors.closeAndForEach( [this]( TaskNode* pSuccessor )
            {
                resolveDependency( pSuccessor, true );
            } );

            pNode->_callable = std::monostate{};
        }
    }

    // ------------------------------------------------------------------------------
    // 8) 큐와 워커 — 우선순위 큐 · 훔치기 · 깨우기 · 워커 루프
    // ------------------------------------------------------------------------------
    void TaskManager::pushToNormalQueue( uintptr_t item )
    {
        // 워커는 자기 데크(LIFO 라 방금 만든 것이 캐시에 남아 있다)에, 바깥 스레드(게임 · 렌더)는 전역 큐에 넣는다.
        // 데크가 가득 차면 전역 큐로 넘긴다.
        const int32 workerID = t_currentWorkerIndex;
        if ( workerID >= 0 )
        {
            WorkerSlot& localSlot = *std::as_const( _listWorkerSlot )[static_cast<uint32>( workerID )];
            while ( localSlot._queue.push( item ) == false )
            {
                if ( _globalWorkerQueue.enqueue( item ) )
                    return;
                std::this_thread::yield();
            }
            return;
        }
        while ( _globalWorkerQueue.enqueue( item ) == false )
        {
            std::this_thread::yield();
        }
    }

    bool TaskManager::tryTakeItem( int32 workerID, const bool bTakeLowQueue, uintptr_t& outItem )
    {
        outItem                 = 0;
        const uint32 numWorkers = getWorkerCount();
        if ( numWorkers == 0 )
            return false;

        // 큐 순서: High -> 내 데크 -> Normal 전역 -> 훔치기 -> Low. 빈 큐의 dequeue 는 시퀀스 하나를 읽는 것이라
        // High 를 매번 먼저 확인해도 비용이 거의 없다.
        if ( _globalHighQueue.dequeue( outItem ) && outItem != 0 )
            return true;

        if ( workerID >= 0 )
        {
            WorkerSlot& localSlot = *std::as_const( _listWorkerSlot )[static_cast<uint32>( workerID )];
            if ( localSlot._queue.pop( outItem ) && outItem != 0 )
                return true;
        }

        if ( _globalWorkerQueue.dequeue( outItem ) && outItem != 0 )
            return true;

        // 워커는 자기 다음 워커부터 한 바퀴를 돈다(자기 데크는 방금 비웠다). 바깥 스레드는 모두 돈다.
        const uint32 firstTarget = workerID >= 0 ? static_cast<uint32>( workerID ) + 1 : 0;
        const uint32 sweepCount  = workerID >= 0 ? numWorkers - 1 : numWorkers;
        for ( uint32 step = 0; step < sweepCount; ++step )
        {
            const uint32 targetID   = ( firstTarget + step ) % numWorkers;
            WorkerSlot&  targetSlot = *std::as_const( _listWorkerSlot )[targetID];
            if ( targetSlot._queue.steal( outItem ) && outItem != 0 )
                return true;
        }

        if ( bTakeLowQueue && _globalLowQueue.dequeue( outItem ) && outItem != 0 )
            return true;

        outItem = 0;
        return false;
    }

    bool TaskManager::tryHelpAndExecute( const bool bHelpLowQueue )
    {
        uintptr_t item{ 0 };
        if ( tryTakeItem( t_currentWorkerIndex, bHelpLowQueue, item ) == false )
            return false;
        executeItem( item );
        return true;
    }

    void TaskManager::scheduleReadyTask( TaskNode* pNode, bool bWakeWorker )
    {
        if ( pNode == nullptr )
            return;

        if ( pNode->_bScheduled.exchange( true, std::memory_order_acq_rel ) )
            return;

        pNode->retain(); // 큐가 참조를 하나 잡는다

        if ( pNode->_affinity == TaskThreadAffinity::MainThread )
        {
            while ( _queueMainThread.enqueue( pNode ) == false )
            {
                if ( isMainThread() )
                    dispatchMainThreadTasks();
                else
                    std::this_thread::yield();
            }
            // 이 일감을 실행할 수 있는 것은 메인 스레드뿐이다(`dispatchMainThreadTasks`). 메인이 어느 대기에서든 잠들어 있으면 깨워야
            // 한다(아무나 하나 깨우면 엉뚱한 스레드가 깨어 메인과 태스크가 서로를 기다린다).
            wakeParkedMainThread();
            return;
        }

        if ( getWorkerCount() == 0 )
            return;

        // 큐는 우선순위로 정한다. High · Low 는 워커가 넣어도 자기 데크가 아니라 전역 큐로 간다. 데크는 LIFO 라
        // "먼저" 도 "나중" 도 보장하지 못한다.
        const uintptr_t item = TaskQueueItem::fromNode( pNode );
        if ( pNode->_priority == TaskPriority::High )
        {
            while ( _globalHighQueue.enqueue( item ) == false )
            {
                std::this_thread::yield();
            }
        }
        else if ( pNode->_priority == TaskPriority::Low )
        {
            while ( _globalLowQueue.enqueue( item ) == false )
            {
                std::this_thread::yield();
            }
        }
        else
        {
            pushToNormalQueue( item );
        }

        if ( bWakeWorker )
            wakeSleepingWorkers( 1 );
    }

    void TaskManager::wakeSleepingWorkers()
    {
        wakeSleepingWorkers( 0xFFFFFFFFu );
    }

    void TaskManager::wakeSleepingWorkers( uint32 requestedCount )
    {
        // 스핀 중인 워커는 세대로 알아챈다. seq_cst 인 이유는 워커 쪽의 "유휴 비트 올리기 → 큐 확인" 과 Dekker 식 짝을 이루기 때문이다.
        _workEpoch.fetch_add( 1, std::memory_order_seq_cst );
        if ( requestedCount == 0 )
            return;

        uint64 mask = _idleWorkerMask.load( std::memory_order_seq_cst );
        if ( mask == 0 )
            return;

        // **필요한 수만큼, 비트를 내린 쪽이** 깨운다. 두 제출자가 같은 워커를 두 번 깨우지 않고, 깨우기 하나는 주소 하나에 대한
        // 것이다(조건 변수의 `notify_one` × n 이면 깨어난 n 개가 뮤텍스를 다시 잡느라 줄을 선다).
        uint32 wokenCount = 0;
        while ( mask != 0 && wokenCount < requestedCount )
        {
            const uint32 workerID = MathUtil::countTrailingZeros( mask );
            const uint64 bit      = static_cast<uint64>( 1 ) << workerID;
            const uint64 previous = _idleWorkerMask.fetch_and( ~bit, std::memory_order_seq_cst );
            if ( ( previous & bit ) != 0 )
            {
                unparkSlot( workerID ); // 워커의 대기자 슬롯 번호는 곧 워커 번호다
                ++wokenCount;
            }
            // 처음 읽은 마스크 안에서만 고른다(남이 먼저 내린 비트는 뺀다). 부른 뒤에 잠든 워커는 잠들기 전에 큐를 한 번 더 보므로
            // 깨울 필요가 없다. 주의: `previous` 로 마스크를 새로 채워 그 워커까지 쫓으면, 할 일 없이 깨어난 워커가 스핀(2 us) 뒤 곧바로
            // 다시 잠들어서 깨우기 시스템 호출이 그보다 느린 곳(WSL)에서는 모두 깨우기 한 번이 1~90 초를 돈다.
            mask &= previous & ~bit;
        }
        if ( wokenCount > 0 )
            _wakeSignalCount.fetch_add( 1, std::memory_order_relaxed );
    }

    void TaskManager::workerLoop( uint32 workerID )
    {
        // 이 스레드에서 스택이 넘쳐도 크래시 리포트가 남게 한다(ThreadCrashStack::initializeCurrentThread 설명).
        ThreadCrashStack::initializeCurrentThread();
        t_currentWorkerIndex = static_cast<int32>( workerID );
        {
            // 디버거 · 프로파일러(Tracy)가 같은 이름을 본다. 번호는 워커 슬롯 번호다.
            fixed_string<constant::kMaxBuffer32> threadName;
            formatstring( threadName.data(), threadName.capacity(), "Worker %#", workerID );
            ThreadName::setCurrentThreadName( threadName.c_str() );
        }

        WorkerSlot&  slot    = *std::as_const( _listWorkerSlot )[workerID];
        const uint64 idleBit = static_cast<uint64>( 1 ) << workerID;

        while ( _bStop.load( std::memory_order_relaxed ) == false )
        {
            uintptr_t item{ 0 };
            if ( tryTakeItem( static_cast<int32>( workerID ), true, item ) )
            {
                executeItem( item );
                continue;
            }

            // **세대를 먼저 읽고 큐를 본다.** 그래야 그 사이에 들어온 일감이 세대를 올려 스핀이 알아챈다.
            // (읽은 뒤에 들어온 일감은 세대를 바꾸고, 읽기 전에 들어온 일감은 바로 아래 tryTakeItem 이 본다.)
            bool            bFoundInSpin  = false;
            uint32          observedEpoch = _workEpoch.load( std::memory_order_acquire );
            const Stopwatch spinStopwatch;
            for ( ;; )
            {
                if ( _workEpoch.load( std::memory_order_acquire ) != observedEpoch )
                {
                    observedEpoch = _workEpoch.load( std::memory_order_acquire );
                    if ( tryTakeItem( static_cast<int32>( workerID ), true, item ) )
                    {
                        bFoundInSpin = true;
                        break;
                    }
                }
                if ( _bStop.load( std::memory_order_relaxed ) )
                    break;
                // 시계는 32번에 한 번만 확인한다. 매번 보면 그것 자체가 스핀 비용이다.
                for ( uint32 spin = 0; spin < 32; ++spin )
                {
                    sw::cpuPause();
                }
                const int64 spentMicro = spinStopwatch.getElapsedMicroseconds();
                if ( spentMicro >= kWorkerIdleSpinMicro )
                    break;
            }

            if ( bFoundInSpin && item != 0 )
            {
                executeItem( item );
                continue;
            }

            // 잠드는 순서: 워드를 읽고 → 유휴 비트를 올리고 → 큐를 한 번 더 본다. 제출하는 쪽은 큐에 넣고 → 세대를 올리고 → 유휴
            // 비트를 본다. 양쪽 모두 seq_cst 라 둘 중 하나는 반드시 상대를 본다. 그래서 넣은 일감이 잠든 워커 뒤에 남지 않는다.
            const uint32 observedParkWord = slot._park._word.load( std::memory_order_acquire );
            _idleWorkerMask.fetch_or( idleBit, std::memory_order_seq_cst );
            std::atomic_thread_fence( std::memory_order_seq_cst );
            if ( tryTakeItem( static_cast<int32>( workerID ), true, item ) )
            {
                _idleWorkerMask.fetch_and( ~idleBit, std::memory_order_seq_cst );
                executeItem( item );
                continue;
            }
            if ( _bStop.load( std::memory_order_seq_cst ) )
            {
                _idleWorkerMask.fetch_and( ~idleBit, std::memory_order_seq_cst );
                break;
            }
            Futex::wait( slot._park._word, observedParkWord );
            // 깨운 쪽이 비트를 내렸다. 이유 없이 깨어났다면 비트가 아직 켜져 있으니 여기서 내린다. 다시 잠들지는 다음 회차가 정한다.
            _idleWorkerMask.fetch_and( ~idleBit, std::memory_order_seq_cst );
        }

        t_currentWorkerIndex = -1;
    }
} // namespace sw
