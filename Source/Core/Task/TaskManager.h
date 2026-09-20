/**
 * @file TaskManager.h
 * @brief 멀티스레드 기반의 작업(Task) 큐를 관리하고 스케줄링하는 비동기 프레임워크입니다.
 * @details 방향성 비순환 그래프(DAG) 형태의 의존성 작업 실행을 지원합니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/ConcurrentQueue.h"
#include "Core/Concurrency/WorkStealingDeque.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Task/TaskTypes.h"

namespace sw
{
    struct StageNode;
    struct TaskNode;

    class TaskNodePool;

    /**
     * @class TaskManager
     * @brief 스레드 풀(Worker Threads)을 생성 및 관리하고, 대기열(Queue)에 등록된 태스크를 분배하여 비동기 실행을 처리합니다.
     * @details
     * - **MPSC / Work-Stealing 큐**: 각 워커마다 독립된 고정 크기 락-프리 큐를 보유하며, 유휴 워커는 다른 워커의 작업을 스틸(Steal)합니다.
     * - **DAG 의존성 기반 자동 스케줄링**: 선행 태스크가 완료되면 카운트다운을 거쳐 후속 태스크가 자동으로 큐에 인큐됩니다.
     * - **Work Helping**: 부모 태스크가 자식/병렬 태스크의 완료를 기다릴 때 단순 슬립(Sleep)하지 않고 큐의 다른 작업을 직접 수행하여 데드락을 방지하고 스레드 활용률을 극대화합니다.
     * - **Event Wait**: 유휴 워커와 waitAll/waitStage는 짧은 스핀 후 조건 변수로 대기하고, 제출/완료 시점에 즉시 깨어납니다.
     */
    class SW_API TaskManager
    {
        friend struct TaskNode;

    public:
        TaskManager();
        ~TaskManager();

        /**
         * @brief 태스크 매니저와 워커 스레드 풀을 초기화합니다.
         * @param threadCount 생성할 워커 스레드 수 (0 전달 시 시스템 논리 CPU 코어 수에 맞게 자동 설정)
         * @return 초기화 성공 여부
         */
        bool initialize( uint32 threadCount = 0 );

        /** @brief 활성화된 모든 워커 스레드의 실행을 중지하고 풀을 종료합니다. */
        void shutdown();

        /**
         * @brief 매개변수가 없는 기본 태스크를 생성합니다.
         * @param delegate 실행할 함수/람다 델리게이트
         * @param affinity 실행할 스레드 선호도 (Any 또는 MainThread)
         * @return 생성된 태스크 핸들
         */
        TaskHandle emplaceTask( const TaskDelegate& delegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 디버깅 및 프로파일링용 이름을 가진 기본 태스크를 생성합니다.
         * @param name 태스크 식별 이름
         * @param delegate 실행할 함수/람다 델리게이트
         * @param affinity 실행할 스레드 선호도
         * @return 생성된 태스크 핸들
         */
        TaskHandle emplaceTask( string_view name, const TaskDelegate& delegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 임의의 타입 소거 인자(TaskArgs)를 전달받는 태스크를 생성합니다.
         * @param delegate 실행할 함수/람다 델리게이트
         * @param args 태스크에 전달할 인자 가방
         * @param affinity 실행할 스레드 선호도
         * @return 생성된 태스크 핸들
         */
        TaskHandle emplaceTask( const TaskArgsDelegate& delegate, const TaskArgs& args, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 디버깅용 이름을 가지고 임의의 인자를 받는 태스크를 생성합니다.
         * @param name 태스크 식별 이름
         * @param delegate 실행할 함수/람다 델리게이트
         * @param args 태스크에 전달할 인자 가방
         * @param affinity 실행할 스레드 선호도
         * @return 생성된 태스크 핸들
         */
        TaskHandle emplaceTask( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 주어진 횟수(count)만큼 분할되어 여러 워커 스레드에서 병렬 처리되는 태스크 그룹을 생성합니다.
         * @param count 실행할 반복 횟수 (각 하위 태스크에 0 ~ count-1 인덱스 전달)
         * @param delegate 각 인덱스마다 실행될 델리게이트
         * @param affinity 실행할 스레드 선호도
         * @return 생성된 부모 병렬 태스크 핸들
         */
        TaskHandle emplaceParallel( uint32 count, const ParallelTaskDelegate& delegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 디버깅용 이름을 가진 병렬 작업 그룹을 생성합니다.
         * @param name 태스크 식별 이름
         * @param count 실행할 반복 횟수
         * @param delegate 각 인덱스마다 실행될 델리게이트
         * @param affinity 실행할 스레드 선호도
         * @return 생성된 부모 병렬 태스크 핸들
         */
        TaskHandle emplaceParallel( string_view name, uint32 count, const ParallelTaskDelegate& delegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 범위를 여러 청크 블록([start, end))으로 나누어 병렬 처리하는 블록 태스크를 생성합니다.
         * @param start 시작 인덱스 (inclusive)
         * @param end 끝 인덱스 (exclusive)
         * @param delegate 각 블록 범위마다 실행될 델리게이트
         * @param affinity 실행할 스레드 선호도
         * @return 생성된 부모 병렬 태스크 핸들
         */
        TaskHandle emplaceParallelBlock( uint32 start, uint32 end, const ParallelBlockDelegate& delegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /** @brief 전역 맵에 등록되지 않는 익명 스테이지를 생성합니다. (임시 동기화 및 메모리 누수 방지용) */
        TaskStageHandle createAnonymousStage( string_view stageName );

        /** @brief 이름으로 기존 스테이지를 검색합니다. (없으면 유효하지 않은 핸들 반환) */
        TaskStageHandle getStage( string_view stageName );

        /** @brief 이름으로 스테이지를 검색하고, 없으면 새로 생성하여 반환합니다. */
        TaskStageHandle getOrCreateStage( string_view stageName );

        /** @brief 특정 스테이지에 소속된 모든 태스크가 완료될 때까지 호출 스레드를 블로킹 대기합니다. (Work Helping 수행) */
        void waitStage( const TaskStageHandle& stage );

        /** @brief 특정 스테이지의 모든 작업이 완료되었는지 여부를 확인합니다. */
        bool isStageComplete( const TaskStageHandle& stage );

        /**
         * @brief 생성(Builder) 시 부여되었던 임시 잠금 의존성을 해제하고 스케줄러에 태스크를 제출합니다.
         * @details precede/succeed 등 의존성 설정이 끝난 후 호출하여 작업이 준비되었음을 알립니다.
         */
        void submit( const TaskHandle& handle );
        /**
         * @brief `submit` 과 같되 잠든 워커를 깨우지 않습니다. 여러 태스크를 한 번에 넣을 때 쓴다.
         * @details 태스크마다 깨우면 깨우기(뮤텍스 + 조건 변수 시그널)가 태스크 수만큼 반복된다 — 청크 32 개에
         *          디스패치가 125 us 였고 그 안의 일은 몇 us 였다. 다 넣은 뒤 `wakeSleepingWorkers` 한 번이다.
         */
        void submitWithoutWake( const TaskHandle& handle );
        /** @brief 잠든 워커를 전부 깨웁니다. 태스크 수를 아는 쪽은 아래 오버로드로 **필요한 수만** 깨운다. */
        void wakeSleepingWorkers();
        /**
         * @brief 잠든 워커 중 @p wantedCount 명만 깨웁니다. `submitWithoutWake` 로 다 넣은 뒤 부른다.
         * @details 전부 깨우면 워커 열넷이 일감 여섯을 다투는 천둥 무리가 된다 — 렌더 그래프 병렬 기록이
         *          두 배 느려졌다(150~192 -> 329~380 us). 넣은 태스크 수만큼만 깨우는 것이 가장 빨랐다.
         */
        void wakeSleepingWorkers( uint32 wantedCount );
        /** @brief 지금까지 워커를 깨운 시그널 수. 테스트가 "그룹 하나에 한 번" 을 확인하는 데 쓴다. */
        uint32 getWakeSignalCount() const { return _wakeSignalCount.load( std::memory_order_relaxed ); }
        /** @brief 만들어졌고 아직 끝나지 않은 태스크 수. `waitStage`/`waitAll` 이 돌아온 직후엔 그 몫이 빠져 있어야 한다(테스트용). */
        uint32 getActiveTaskCount() const { return _activeTaskCount.load( std::memory_order_acquire ); }

        /**
         * @brief 현재 시스템에 등록된 모든 비동기 태스크가 완료될 때까지 대기합니다.
         * @param timeoutMs 최대 대기 시간(밀리초). 0이면 완료될 때까지 무제한 대기.
         * @return 모든 태스크가 완료되었으면 true, 타임아웃 발생 시 false.
         */
        bool waitAll( uint32 timeoutMs = 0 );

        /** @brief 내부 대기열 및 스테이지 상태를 강제로 정리합니다. */
        void clear();

        /**
         * @brief 전달된 모든 tasks가 완료되었을 때 실행될 통합 후속(Continuation) 태스크를 생성합니다.
         * @param listTask 선행 완료되어야 하는 태스크 핸들 목록
         * @param continuation 모두 완료 시 실행될 델리게이트
         * @param affinity 실행할 스레드 선호도
         * @return 생성된 후속 태스크 핸들
         */
        TaskHandle whenAll( const vector<TaskHandle>& listTask, const TaskDelegate& continuation, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 전달된 tasks 중 '어느 하나라도' 먼저 완료되면 즉시 실행되는 후속 태스크를 생성합니다.
         * @param listTask 감시 대상 태스크 핸들 목록
         * @param continuation 최초 완료 시 실행될 델리게이트
         * @param affinity 실행할 스레드 선호도
         * @return 생성된 후속 태스크 핸들
         */
        TaskHandle whenAny( const vector<TaskHandle>& listTask, const TaskDelegate& continuation, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 메인 스레드 친화도(`MainThread`)로 등록된 태스크들을 일괄 실행합니다.
         * @note 메인 렌더/게임 루프의 매 틱마다 주기적으로 호출되어야 합니다.
         */
        void dispatchMainThreadTasks();

        /** @brief 생성된 워커 스레드의 총 개수를 반환합니다. */
        uint32 getWorkerCount() const { return static_cast<uint32>( _listWorkerQueue.size() ); }

        /** @brief 현재 호출 스레드가 TaskManager를 초기화한 메인 스레드인지 확인합니다. */
        bool isMainThread() const;
        /** @brief Debug: 메인 스레드가 아니면 assert를 발생시킵니다. */
        void ensureMainThread() const;

        /** @brief 현재 워커 스레드의 인덱스를 반환합니다. (워커가 아니면 -1) */
        int32 getCurrentWorkerIndex() const;

        bool isWorkerThread() const;
        /** @brief Debug: 워커 스레드가 아니면 assert를 발생시킵니다. */
        void ensureWorkerThread() const;

        /** @brief 현재 스레드가 병렬(Parallel) 태스크의 본문 실행 중인지 여부를 반환합니다. */
        bool isInsideParallelTask() const;
        /** @brief Debug: Parallel 태스크 안이 아니면 assert를 발생시킵니다. */
        void ensureInsideParallelTask() const;

    private:
        /** @brief 워커 스레드의 메인 루프 (인큐된 태스크 소비, 스틸링, 어댑티브 스핀 및 슬립 처리) */
        void workerLoop( uint32 workerId );
        /** @brief 의존성이 충족된 태스크 노드를 적절한 워커 큐 또는 메인 스레드 큐로 라우팅합니다. */
        void scheduleReadyTask( TaskNode* pNode );
        /** @brief 준비된 태스크를 큐에 넣되, @p bWakeWorker 가 false 면 잠든 워커를 깨우지 않습니다 (`submitWithoutWake`). */
        void scheduleReadyTask( TaskNode* pNode, bool bWakeWorker );
        /** @brief 단일 태스크 노드의 본문을 실행하고 후속 의존성을 트리거합니다. */
        void executeTask( TaskNode* pNode );
        /** @brief 태스크 완료 시 후속 태스크들의 카운트다운을 감소시키고 완료 조건을 전파합니다. */
        void onTaskFinished( TaskNode* pNode );
        /** @brief 워커 로컬/글로벌 큐와 스틸로 실행할 태스크를 가져옵니다. */
        bool tryTakeTask( uint32 workerId, TaskNode*& pNode );
        /** @brief 현재 스레드의 로컬 큐를 비운 뒤, 다른 워커 작업을 도와 실행합니다. */
        bool tryHelpAndExecute();
        /** @brief 다른 워커의 큐에서 작업을 훔쳐와(Work Stealing) 즉시 실행합니다. */
        bool tryStealAndExecute( uint32 excludedWorkerId = invalid_index::kUint32 );
        /** @brief 내부 노드 할당 및 해제 (TaskNode 내부용) */
        TaskNode* allocateNode();
        void      deallocateNode( TaskNode* pNode );

    private:
        /** @brief 각 워커 스레드 전용 고정 크기 락-프리 큐 래퍼 */
        struct WorkerQueue
        {
            WorkStealingDeque<TaskNode*> _queue{ 4096 };
        };

        bool                            _bInitialized;      ///< 매니저 초기화 여부
        std::thread::id                 _mainThreadId;      ///< 메인 스레드 고유 ID
        atomic<bool>                    _bStop;             ///< 워커 스레드 종료 플래그
        vector<std::thread>             _listWorker;        ///< 워커 스레드 핸들 목록
        vector<unique_ptr<WorkerQueue>> _listWorkerQueue;   ///< 각 워커별 독립 대기열
        alignas( 64 ) atomic<uint32> _nextWorkerQueueIndex; ///< 라운드 로빈 작업 분배용 인덱스
        alignas( 64 ) atomic<int32> _sleepingWorkerCount;   ///< 현재 조건 변수 대기(Sleep) 중인 워커 수
        /**
         * @brief 일감이 들어올 때마다 오르는 세대. 스핀 중인 워커는 **이것만 읽는다.**
         * @details 예전 스핀은 매 회 `tryTakeTask` 를 불렀다 — 전역 MPMC 큐의 CAS 와 열네 개 덱의 steal 을
         *          워커 열다섯이 동시에 두드려, 스핀을 늘리자 게임·렌더 스레드의 코어까지 빼앗았다.
         *          읽기 전용 한 줄만 보면 경합이 없고, 세대가 바뀌었을 때만 큐를 만진다.
         */
        alignas( 64 ) atomic<uint32> _workEpoch;
        atomic<uint32> _wakeSignalCount; ///< 워커를 깨운 시그널 수 (통계 · 테스트용)

        /**
         * @brief `TaskPriority::High` 전용 전역 큐. **모든 워커가 자기 덱보다 먼저 본다.**
         * @details 게임 스레드의 대량 잡(트랜스폼 플러시·씬 수집)과 렌더 스레드의 병렬 패스 기록이 같은 풀을
         *          쓴다. 레인이 없으면 렌더 스레드가 방금 넣은 기록 태스크가 게임 스레드의 청크 수십 개 뒤에
         *          줄을 서고, 렌더 스레드는 그 스테이지를 곧바로 기다리므로 그 줄이 그대로 프레임 지연이다
         *          (큐브 8000 에서 GT 잡을 켜자 RT 그래프 기록 170 -> 261 us). 상용 엔진이 렌더·오디오 잡을
         *          별도 레인에 두는 이유다. `_priority` 는 그동안 저장만 되고 스케줄러가 보지 않았다.
         */
        ConcurrentQueue<TaskNode*, 1024> _globalHighQueue;
        ConcurrentQueue<TaskNode*, 4096> _globalWorkerQueue; ///< `Normal` 전역 큐 — 워커 밖(게임·렌더 스레드)에서 넣는 자리
        /** @brief `TaskPriority::Low` 전용 전역 큐. 훔칠 것도 없을 때만 본다 — 백그라운드 I/O·통계의 자리. */
        ConcurrentQueue<TaskNode*, 1024> _globalLowQueue;
        ConcurrentQueue<TaskNode*, 1024> _queueMainThread; ///< 메인 스레드 전용 태스크 큐 (락-프리)
        mutex                            _workerMutex;     ///< 워커 조건 변수 보호용 뮤텍스
        std::condition_variable_any      _cvWorker;        ///< 유휴 워커 깨우기용 조건 변수
        mutable mutex                    _waitAllMutex;    ///< waitAll 대기용 뮤텍스
        std::condition_variable_any      _cvWaitAll;       ///< 모든 작업 완료 알림용 조건 변수

        vector<weak_ptr<StageNode>> _listAllStage;     ///< 등록된 전체 스테이지 목록 (약한 참조)
        mutable mutex               _stageMutex;       ///< 스테이지 목록 동기화 뮤텍스
        alignas( 64 ) atomic<uint32> _activeTaskCount; ///< 현재 시스템에서 실행/대기 중인 활성 태스크 총 개수
        unique_ptr<TaskNodePool> _nodePool;            ///< 태스크 노드 슬랩 풀 매니저
    };
} // namespace sw
