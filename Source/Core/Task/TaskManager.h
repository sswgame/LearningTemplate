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
    struct JoinCounter;
    struct ParallelGroup;
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
     * - **주소 대기**: 유휴 워커와 대기자는 짧은 스핀 뒤 **자기 워드 하나**에 잠들고(`Futex`), 제출·완료는 그 주소만 두드린다.
     *   뮤텍스도 조건 변수도 없다 — 워커 n 개를 깨우는 것이 서로 독립인 n 번이고, 깨어난 쪽이 다시 잡을 잠금이 없다.
     * - **병렬 그룹은 티켓**: `runParallel` · `emplaceParallel*` 은 청크마다 노드를 만들지 않는다. 그룹 하나에 티켓
     *   몇 장을 큐에 넣고, 티켓을 집은 스레드는 원자 카운터로 청크를 남는 만큼 집어 돈다 — 호출 스레드도 곧바로 같이 돈다.
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

        /**
         * @brief 포크-조인 병렬 for — [0, count) 를 청크로 나눠 돌리고 끝날 때까지 기다립니다. 상용 엔진의 ParallelFor.
         * @details `count` 가 `serialThreshold` 미만이거나 워커가 없으면 현재 스레드가 한 번에 돈다. 그 위에서는 그룹을
         *          **호출 스레드의 스택에** 두고 티켓 몇 장만 큐에 넣는다 — 노드도 스테이지도 힙도 없다. 호출 스레드는
         *          워커가 깨기를 기다리지 않고 곧바로 첫 청크를 집는다. 엔진 코드는 서비스 바인딩까지 감싼
         *          `engine::runParallel` 을 쓴다.
         */
        void runParallel( uint32 count, uint32 serialThreshold, const ParallelBlockDelegate& body );

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
         * @brief `submit` 과 같되 잠든 워커를 깨우지 않고 스핀 중인 워커에게도 알리지 않습니다. 여러 태스크를 한 번에 넣을 때 쓴다.
         * @details 태스크마다 깨우면 깨우기가 태스크 수만큼 반복된다 — 청크 32 개에 디스패치가 125 us 였고 그 안의 일은
         *          몇 us 였다. 다 넣은 뒤 `wakeSleepingWorkers` 한 번이다 — 그것이 세대를 올리고 잠든 워커를 깨운다.
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
        uint32 getWorkerCount() const { return static_cast<uint32>( _listWorkerSlot.size() ); }

        /** @brief 현재 호출 스레드가 TaskManager를 초기화한 메인 스레드인지 확인합니다. */
        bool isMainThread() const;
        /** @brief Debug: 메인 스레드가 아니면 assert를 발생시킵니다. */
        void ensureMainThread() const;

        /** @brief 현재 워커 스레드의 인덱스를 반환합니다. (워커가 아니면 -1) */
        int32 getCurrentWorkerIndex() const;

        /**
         * @brief 태스크 본문을 실행할 수 있는 스레드가 최대 몇 개인가 — 워커 수 + 대기 중에 남의 일을 돕는 스레드 몫(`kMaxHelperThreadCount`).
         * @details 스레드마다 하나씩 쓰는 스크래치(플러시 DFS 스택 · 더티 루트 · 트랜스폼 쓰기 큐)의 배열 크기다.
         *          **워커가 아닌 스레드도 태스크를 실행한다** — `waitStage` · `waitAll` · `runParallel` 이 기다리는 동안
         *          `tryHelpAndExecute` 로 아무 준비된 잡이나 돕는데, 메인 스레드뿐 아니라 렌더 스레드(패스 기록 대기) ·
         *          로더 스레드(스트리밍 대기)도 그 자리를 지난다. 예전에는 그 전부가 "마지막 칸" 하나를 나눠 썼고, 메인과
         *          렌더가 같은 프레임에 같은 슬롯 벡터에 push 해 벡터가 깨졌다(틱 중 트랜스폼 쓰기 큐에서 세그폴트로 드러났다).
         */
        uint32 getScratchSlotCount() const { return getWorkerCount() + kMaxHelperThreadCount; }
        /**
         * @brief 지금 스레드의 스크래치 슬롯 — 워커면 그 번호, 아니면 처음 물을 때 받는 고유한 도우미 번호(워커 수 + n).
         * @details 도우미 번호는 스레드마다 한 번 배정되어 스레드가 사는 동안 그대로다. 상한(`kMaxHelperThreadCount`)을 넘는
         *          스레드는 마지막 칸을 나눠 쓴다 — 그 경우를 로그로 알린다(엔진에서 태스크를 기다리는 스레드는 메인 · 렌더 ·
         *          로더 · 업로드 정도라 넘지 않는다). 대기자 슬롯(잠드는 워드)도 같은 번호다.
         */
        uint32 getCurrentThreadScratchSlot();
        /** @brief 워커가 아니면서 태스크를 실행할 수 있는 스레드의 상한 (메인 · 렌더 · 로더 · 업로드 · 에디터 등). */
        static constexpr uint32 kMaxHelperThreadCount = 8;

        bool isWorkerThread() const;
        /** @brief Debug: 워커 스레드가 아니면 assert를 발생시킵니다. */
        void ensureWorkerThread() const;

        /** @brief 현재 스레드가 병렬(Parallel) 태스크의 본문 실행 중인지 여부를 반환합니다. */
        bool isInsideParallelTask() const;
        /** @brief Debug: Parallel 태스크 안이 아니면 assert를 발생시킵니다. */
        void ensureInsideParallelTask() const;

    private:
        /** @brief 워커 스레드의 메인 루프 — 큐 소비 · 스틸 · 세대 스핀 · 자기 워드에 잠들기. */
        void workerLoop( uint32 workerId );
        /** @brief 의존성이 충족된 태스크 노드를 레인(High · 로컬 덱 · Normal 전역 · Low · 메인)에 넣습니다. */
        void scheduleReadyTask( TaskNode* pNode );
        /** @brief 준비된 태스크를 큐에 넣되, @p bWakeWorker 가 false 면 잠든 워커를 깨우지 않습니다 (`submitWithoutWake`). */
        void scheduleReadyTask( TaskNode* pNode, bool bWakeWorker );
        /** @brief 큐 항목 하나를 실행합니다 — 태스크 노드이거나 병렬 그룹의 티켓이다. */
        void executeItem( uintptr_t item );
        /** @brief 단일 태스크 노드의 본문을 실행하고 자식 합류 뒤 완료를 처리합니다. */
        void executeTask( TaskNode* pNode );
        /** @brief 본문과 자식이 모두 끝난 태스크의 완료 — 활성 수 · 스테이지 · 부모 · 후속. */
        void completeTask( TaskNode* pNode );
        /** @brief `emplaceParallel` · `emplaceParallelBlock` 의 공통 본체 — 부모 노드 하나 + 풀 그룹 하나 + 티켓. */
        TaskHandle emplaceParallelGroup( string_view name, uint32 start, uint32 end, const ParallelBlockDelegate* pBlockBody, const ParallelTaskDelegate* pIndexBody, TaskThreadAffinity affinity );
        /** @brief 병렬 그룹의 티켓 하나 — 청크가 남아 있는 동안 집어 돌리고 마지막이면 그룹을 닫습니다. */
        void runGroupTicket( ParallelGroup* pGroup );
        /** @brief 그룹의 청크를 남은 것이 없을 때까지 집어 돌립니다 (티켓 워커와 호출 스레드가 같이 쓴다). */
        void runGroupChunks( ParallelGroup* pGroup );
        /** @brief 청크 사이에 High 레인을 비웁니다 — 그룹을 도는 동안에도 렌더 패스 기록이 줄을 서지 않게. */
        void runHighLaneBetweenChunks();
        /** @brief 티켓 @p ticketCount 장을 이 스레드의 레인에 넣고 그만큼 워커를 깨웁니다. */
        void pushGroupTickets( ParallelGroup* pGroup, uint32 ticketCount );
        /**
         * @brief 병렬 그룹의 마지막 티켓이 닫힐 때 — 대기자를 깨우고, 풀 그룹이면 부모의 의존성을 풀고 되돌립니다.
         * @param bPooledGroup 풀에서 온 그룹인가. 스택 그룹(`runParallel`)이면 @p pGroup 을 **역참조하지 않는다** — 이미 사라졌을 수 있다.
         */
        void onGroupFinished( ParallelGroup* pGroup, uint64 joinBeforeFinish, bool bPooledGroup );
        /** @brief `runParallel` 의 조인 — 남은 티켓을 돕다가 자기 슬롯에 잠듭니다. */
        void waitForGroup( ParallelGroup& group );
        /** @brief `clear` 가 큐에서 꺼낸 항목 하나를 돌리지 않고 놓습니다 (노드는 참조 해제, 티켓은 그룹 닫기). */
        void drainQueueItem( uintptr_t item );
        /** @brief 워커 로컬/글로벌 큐와 스틸로 실행할 항목을 가져옵니다. */
        bool tryTakeTask( uint32 workerId, uintptr_t& outItem );
        /** @brief 현재 스레드의 로컬 큐를 비운 뒤, 다른 워커 작업을 도와 실행합니다. */
        bool tryHelpAndExecute();
        /** @brief 다른 워커의 큐에서 작업을 훔쳐와(Work Stealing) 즉시 실행합니다. */
        bool tryStealAndExecute( uint32 excludedWorkerId = invalid_index::kUint32 );
        /** @brief 워커 @p workerId 를 자기 워드로 깨웁니다 (유휴 비트는 부르는 쪽이 이미 내렸다). */
        void unparkWorker( uint32 workerId );
        /** @brief 대기자 슬롯 @p slotIndex 의 스레드를 깨웁니다. */
        void unparkWaiter( uint32 slotIndex );
        /** @brief 이 스레드의 대기자 슬롯 번호 — 워커면 자기 번호, 아니면 도우미 칸 (`getCurrentThreadScratchSlot` 과 같다). */
        uint32 getCurrentWaiterSlotIndex();
        /** @brief 대기자 슬롯 @p slotIndex 의 워드. */
        atomic<uint32>& getWaiterWord( uint32 slotIndex );
        /** @brief 이 스레드를 @p join 의 대기자로 올리고 잠듭니다. 깨어나면 부르는 쪽이 조건을 다시 본다. */
        void parkOnJoin( JoinCounter& join );
        /**
         * @brief 완료 방송(스테이지 완료 · 활성 0 · 메인 일감 · clear)에 잠들 준비 — 대기자 수를 올리고 세대를 돌려줍니다.
         * @details 부르는 쪽이 그 뒤에 조건을 다시 보고 `commitBroadcastWait` 로 잠들거나 `cancelBroadcastWait` 로 물린다.
         *          수를 먼저 올리고 조건을 보는 것과, 완료 쪽이 조건을 바꾸고 수를 보는 것이 데커 짝이다.
         */
        uint32 prepareBroadcastWait();
        /** @brief 방송 세대가 @p seenEpoch 인 동안 잠듭니다. @p timeoutMilli 0 은 무제한. 대기자 수는 여기서 내린다. */
        void commitBroadcastWait( uint32 seenEpoch, uint32 timeoutMilli );
        /** @brief 잠들지 않기로 했다 — 대기자 수만 내립니다. */
        void cancelBroadcastWait();
        /** @brief 방송 대기자가 있으면 방송 세대를 올려 전부 깨웁니다. */
        void notifyBroadcast();
        /** @brief 메인 스레드가 잠들어 있으면(어느 대기에서든) 깨웁니다 — 메인 전용 일감이 들어왔다. */
        void wakeParkedMainThread();
        /** @brief 내부 노드 할당 및 해제 (TaskNode 내부용) */
        TaskNode* allocateNode();
        void      deallocateNode( TaskNode* pNode );

    private:
        /**
         * @brief 대기자 하나가 잠드는 워드. 스레드마다 하나이고 캐시 라인 하나를 혼자 쓴다.
         * @details `waitStage` · `runParallel` · 워커의 유휴 잠들기가 전부 이 모양이다 — 잠드는 쪽은 자기 워드에,
         *          깨우는 쪽은 그 주소로. 예전의 조건 변수 하나(`_cvWaitAll`)에는 게임·렌더·로더 스레드가 같이
         *          잠들어 있어서 어느 완료든 전부를 깨웠고, 깨어난 쪽은 뮤텍스를 다시 잡아야 돌아왔다.
         */
        struct alignas( 64 ) WaiterSlot
        {
            atomic<uint32> _word; ///< 0 에서 시작한다(래퍼 기본 생성) — 값 자체는 뜻이 없고 바뀌었는지만 본다
        };

        /** @brief 워커 하나의 자리 — 고정 크기 락프리 덱과 잠드는 워드. */
        struct WorkerSlot
        {
            WorkStealingDeque<uintptr_t> _queue{ 4096 }; ///< 항목은 태스크 노드이거나(짝수) 병렬 그룹 티켓(홀수)
            WaiterSlot                   _park;          ///< 유휴 워커가 잠드는 워드 · 이 워커가 태스크 안에서 기다릴 때의 대기 워드
        };

        /** @brief 유휴 비트마스크가 담을 수 있는 워커 수. `initialize` 가 이 수로 자른다. */
        static constexpr uint32 kMaxWorkerCount = 64;

        bool                           _bInitialized;    ///< 매니저 초기화 여부
        std::thread::id                _mainThreadId;    ///< 메인 스레드 고유 ID
        atomic<bool>                   _bStop;           ///< 워커 스레드 종료 플래그
        vector<std::thread>            _listWorker;      ///< 워커 스레드 핸들 목록
        vector<unique_ptr<WorkerSlot>> _listWorkerSlot;  ///< 워커별 덱 + 잠드는 워드
        atomic<uint32>                 _helperSlotCount; ///< 지금까지 도우미 슬롯을 받은 비-워커 스레드 수
        /**
         * @brief 잠든 워커의 비트마스크. 깨우는 쪽은 비트를 **원자로 내리고** 그 워커만 깨운다.
         * @details 예전에는 `_sleepingWorkerCount` 와 조건 변수 하나였다 — `notify_one` 이 누구를 깨울지 고를 수
         *          없고, 깨어난 워커 전부가 `_workerMutex` 를 다시 잡아야 했다. 비트를 내린 쪽만 깨우므로 두
         *          제출자가 같은 워커를 두 번 깨우지 않고, 깨움 n 번은 서로 독립인 주소 두드리기 n 번이다.
         */
        alignas( 64 ) atomic<uint64> _idleWorkerMask;
        /**
         * @brief 일감이 들어올 때마다 오르는 세대. 스핀 중인 워커는 **이것만 읽는다.**
         * @details 예전 스핀은 매 회 `tryTakeTask` 를 불렀다 — 전역 MPMC 큐의 CAS 와 열네 개 덱의 steal 을
         *          워커 열다섯이 동시에 두드려, 스핀을 늘리자 게임·렌더 스레드의 코어까지 빼앗았다.
         *          읽기 전용 한 줄만 보면 경합이 없고, 세대가 바뀌었을 때만 큐를 만진다.
         *          `submitWithoutWake` 는 올리지 않는다 — 묶음 끝의 `wakeSleepingWorkers` 가 한 번 올린다.
         */
        alignas( 64 ) atomic<uint32> _workEpoch;
        atomic<uint32> _wakeSignalCount; ///< 워커를 깨운 시그널 수 (통계 · 테스트용)
        /**
         * @brief 완료 방송 세대 — `waitAll` 과 "둘째 대기자" 가 잠드는 워드.
         * @details 스테이지 · 병렬 그룹은 대기자 **하나**를 자기 합류 카운터에 적어 두고 마지막 태스크가 그 스레드만
         *          깨운다. 같은 스테이지를 둘이 기다리거나 `waitAll` 처럼 대상이 전체인 대기만 이 방송에 잠든다.
         *          `_broadcastWaiterCount` 가 0 이면 방송은 원자 하나 읽고 끝이다 — 완료 경로에 시스템 콜이 없다.
         */
        alignas( 64 ) atomic<uint32> _completionEpoch;
        atomic<uint32> _broadcastWaiterCount; ///< 방송에 잠든(또는 잠들려는) 스레드 수
        /**
         * @brief 메인 스레드가 잠들어 있으면 그 대기자 슬롯, 아니면 -1.
         * @details 메인 전용 일감(`MainThread` 친화도)은 메인 스레드만 돌릴 수 있다. 메인이 스테이지를 기다리다
         *          잠든 사이 그 스테이지의 태스크가 메인 일감을 만들면, 메인을 깨우지 않는 한 둘 다 영원히 기다린다
         *          (예전에 `notify_one` 이 엉뚱한 스레드를 깨워 실제로 멈췄다). 메인은 어느 대기에서든 잠들기 전에
         *          여기 자기 슬롯을 적고, 메인 일감을 넣는 쪽은 여기를 본다.
         */
        atomic<int32> _mainThreadParkedSlot;

        /**
         * @brief `TaskPriority::High` 전용 전역 큐. **모든 워커가 자기 덱보다 먼저 본다.**
         * @details 게임 스레드의 대량 잡(트랜스폼 플러시·씬 수집)과 렌더 스레드의 병렬 패스 기록이 같은 풀을
         *          쓴다. 레인이 없으면 렌더 스레드가 방금 넣은 기록 태스크가 게임 스레드의 청크 수십 개 뒤에
         *          줄을 서고, 렌더 스레드는 그 스테이지를 곧바로 기다리므로 그 줄이 그대로 프레임 지연이다
         *          (큐브 8000 에서 GT 잡을 켜자 RT 그래프 기록 170 -> 261 us). 상용 엔진이 렌더·오디오 잡을
         *          별도 레인에 두는 이유다. `_priority` 는 그동안 저장만 되고 스케줄러가 보지 않았다.
         */
        ConcurrentQueue<uintptr_t, 1024> _globalHighQueue;
        ConcurrentQueue<uintptr_t, 4096> _globalWorkerQueue; ///< `Normal` 전역 큐 — 워커 밖(게임·렌더 스레드)에서 넣는 자리
        /** @brief `TaskPriority::Low` 전용 전역 큐. 훔칠 것도 없을 때만 본다 — 백그라운드 I/O·통계의 자리. */
        ConcurrentQueue<uintptr_t, 1024> _globalLowQueue;
        ConcurrentQueue<TaskNode*, 1024> _queueMainThread; ///< 메인 스레드 전용 태스크 큐 (락-프리)
        /** @brief 워커가 아닌 스레드(메인 · 렌더 · 로더 …)의 대기자 슬롯. 번호는 `getCurrentThreadScratchSlot` 의 도우미 칸과 같다. */
        WaiterSlot _arrHelperWaiter[kMaxHelperThreadCount];

        vector<StageNode*> _listAllStage;              ///< 이름 있는 스테이지 목록 — 참조를 하나씩 쥔다 (clear 가 놓는다)
        mutable mutex      _stageMutex;                ///< 스테이지 목록 동기화 뮤텍스
        alignas( 64 ) atomic<uint32> _activeTaskCount; ///< 현재 시스템에서 실행/대기 중인 활성 태스크 총 개수
        unique_ptr<TaskNodePool> _nodePool;            ///< 태스크 노드 슬랩 풀 매니저
    };
} // namespace sw
