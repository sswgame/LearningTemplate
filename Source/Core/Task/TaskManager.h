/**
 * @file TaskManager.h
 * @brief 멀티스레드 작업(Task) 큐를 관리하고 스케줄링하는 비동기 프레임워크입니다.
 * @details 방향성 비순환 그래프(DAG) 형태의 의존성을 가진 작업 실행을 지원합니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/ConcurrentQueue.h"
#include "Core/Concurrency/WorkStealingDeque.h"
#include "Core/Task/TaskTypes.h"

namespace sw
{
    struct JoinCounter;
    struct ParallelGroup;
    struct TaskNode;

    class TaskNodePool;

    /**
     * @class TaskManager
     * @brief 워커 스레드 풀을 만들어 관리하고, 큐에 등록된 태스크를 나눠 비동기로 실행합니다.
     * @details
     * - **작업 훔치기 데크**: 워커마다 고정 크기 lock-free 데크를 따로 가지며, 할 일이 없는 워커는 다른 워커의 작업을 훔쳐
     *   옵니다(work stealing).
     * - **DAG 의존성 기반 자동 스케줄링**: 선행 태스크가 끝나면 카운트다운을 거쳐 후속 태스크가 자동으로 큐에 들어갑니다.
     * - **대기 중 돕기(work helping)**: 부모 태스크가 자식 · 병렬 태스크의 완료를 기다릴 때 그냥 잠들지 않고 큐의 다른 작업을
     *   직접 실행해 데드락을 막고 스레드 활용률을 높입니다.
     * - **주소 대기**: 할 일이 없는 워커와 대기자는 잠깐 스핀한 뒤 **자기 워드 하나**에서 잠들고(`Futex`), 제출 · 완료하는 쪽은
     *   그 주소만 깨웁니다. 뮤텍스도 조건 변수도 없습니다. 워커 n 개를 깨우는 일이 서로 독립적인 n 번의 깨우기이고, 깨어난 쪽이
     *   다시 잡아야 할 잠금도 없습니다.
     * - **병렬 그룹은 티켓 방식**: `runParallel` · `emplaceParallel*` 은 청크마다 노드를 만들지 않습니다. 그룹 하나에 티켓 몇
     *   장을 큐에 넣고, 티켓을 가져간 스레드는 원자 카운터로 남은 청크를 계속 가져와 실행합니다. 호출 스레드도 바로 함께
     *   실행합니다.
     */
    class SW_API TaskManager
    {
        friend struct TaskNode;

    public:
        TaskManager();
        ~TaskManager();

        /**
         * @brief 태스크 매니저와 워커 스레드 풀을 초기화합니다.
         * @param threadCount 만들 워커 스레드 수(0 이면 시스템의 논리 CPU 코어 수에 맞춰 자동으로 정합니다)
         * @return 초기화에 성공하면 true
         */
        bool initialize( uint32 threadCount = 0 );

        /** @brief 모든 워커 스레드를 멈추고 풀을 종료합니다. */
        void shutdown();

        /**
         * @brief 매개변수가 없는 기본 태스크를 만듭니다.
         * @param delegate 실행할 함수 · 람다 델리게이트
         * @param affinity 실행할 스레드(Any 또는 MainThread)
         * @return 만든 태스크의 핸들
         */
        TaskHandle emplaceTask( const TaskDelegate& delegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 디버깅 · 프로파일링용 이름을 가진 기본 태스크를 만듭니다.
         * @param name 태스크 이름
         * @param delegate 실행할 함수 · 람다 델리게이트
         * @param affinity 실행할 스레드
         * @return 만든 태스크의 핸들
         */
        TaskHandle emplaceTask( string_view name, const TaskDelegate& delegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 임의 타입의 인자 묶음(TaskArgs)을 받는 태스크를 만듭니다.
         * @param delegate 실행할 함수 · 람다 델리게이트
         * @param args 태스크에 넘길 인자 묶음
         * @param affinity 실행할 스레드
         * @return 만든 태스크의 핸들
         */
        TaskHandle emplaceTask( const TaskArgsDelegate& delegate, const TaskArgs& args, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 디버깅용 이름을 가지고 임의의 인자를 받는 태스크를 만듭니다.
         * @param name 태스크 이름
         * @param delegate 실행할 함수 · 람다 델리게이트
         * @param args 태스크에 넘길 인자 묶음
         * @param affinity 실행할 스레드
         * @return 만든 태스크의 핸들
         */
        TaskHandle emplaceTask( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief count 번으로 나눠 여러 워커 스레드에서 병렬로 처리하는 태스크 그룹을 만듭니다.
         * @param count 반복 횟수(각 실행에 0 ~ count-1 인덱스를 넘깁니다)
         * @param delegate 인덱스마다 실행할 델리게이트
         * @param affinity 실행할 스레드
         * @return 만든 부모 병렬 태스크의 핸들
         */
        TaskHandle emplaceParallel( uint32 count, const ParallelTaskDelegate& delegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 디버깅용 이름을 가진 병렬 태스크 그룹을 만듭니다.
         * @param name 태스크 이름
         * @param count 반복 횟수
         * @param delegate 인덱스마다 실행할 델리게이트
         * @param affinity 실행할 스레드
         * @return 만든 부모 병렬 태스크의 핸들
         */
        TaskHandle emplaceParallel( string_view name, uint32 count, const ParallelTaskDelegate& delegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 범위를 여러 청크 블록([start, end))으로 나눠 병렬 처리하는 블록 태스크를 만듭니다.
         * @param start 시작 인덱스(포함)
         * @param end 끝 인덱스(제외)
         * @param delegate 블록 범위마다 실행할 델리게이트
         * @param affinity 실행할 스레드
         * @return 만든 부모 병렬 태스크의 핸들
         */
        TaskHandle emplaceParallelBlock( uint32 start, uint32 end, const ParallelBlockDelegate& delegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 포크-조인 병렬 for 입니다. [0, count) 를 청크로 나눠 실행하고 끝날 때까지 기다립니다(상용 엔진의 ParallelFor).
         * @details `count` 가 `serialThreshold` 보다 작거나 워커가 없으면 현재 스레드가 한 번에 실행합니다. 그 이상이면 그룹을
         *          **호출 스레드의 스택에** 두고 티켓 몇 장만 큐에 넣습니다. 노드도 스테이지도 힙도 쓰지 않습니다. 호출 스레드는
         *          워커가 깨어나기를 기다리지 않고 바로 첫 청크를 가져갑니다. 엔진 코드는 서비스 바인딩까지 감싼
         *          `engine::runParallel` 을 쓰십시오.
         */
        void runParallel( uint32 count, uint32 serialThreshold, const ParallelBlockDelegate& body );

        /**
         * @brief 스테이지를 만듭니다. 풀에서 꺼내므로 프레임이 안정된 상태에서는 힙을 건드리지 않습니다.
         * @details 예전에는 이름으로 찾는 스테이지(`getOrCreateStage` · `getStage`)가 따로 있었습니다. 이름으로 다시 찾는 곳이
         *          하나도 없어서(부르는 곳마다 한 번 만들고 한 번 기다렸다) 목록 · 뮤텍스 · 선형 탐색 · 이름 칸을 들고 있을 이유가
         *          없었습니다. 스테이지를 나눠 쓰려면 핸들을 복사해 건네십시오.
         */
        TaskStageHandle createStage();

        /** @brief 스테이지에 속한 모든 태스크가 끝날 때까지 호출 스레드를 막고 기다립니다(기다리는 동안 다른 일을 돕습니다). */
        void waitStage( const TaskStageHandle& stage );

        /** @brief 스테이지의 모든 작업이 끝났는지 확인합니다. */
        bool isStageComplete( const TaskStageHandle& stage );

        /**
         * @brief 만들 때(빌더) 걸어 둔 임시 잠금 의존성을 풀고 태스크를 스케줄러에 제출합니다.
         * @details precede · succeed 같은 의존성 설정을 마친 뒤 불러, 작업이 준비됐음을 알립니다.
         */
        void submit( const TaskHandle& handle );
        /**
         * @brief `submit` 과 같지만 잠든 워커를 깨우지 않고, 스핀 중인 워커에게도 알리지 않습니다. 여러 태스크를 한 번에 넣을 때 씁니다.
         * @details 태스크마다 깨우면 깨우기가 태스크 수만큼 반복됩니다. 청크 32개를 넣는 디스패치가 125 us 였는데 그 안의 실제 일은
         *          몇 us 였습니다. 모두 넣은 뒤 `wakeSleepingWorkers` 를 한 번만 부르십시오. 그것이 세대를 올리고 잠든 워커를 깨웁니다.
         */
        void submitWithoutWake( const TaskHandle& handle );
        /**
         * @brief 부른 순간 잠들어 있던 워커를 모두 깨웁니다. 태스크 수를 아는 쪽은 아래 오버로드로 **필요한 수만큼만** 깨우십시오.
         * @details 부른 뒤에 잠드는 워커는 깨우지 않습니다. 그 워커는 잠들기 전에 큐를 한 번 더 보므로 넣은 일감을 놓치지 않습니다.
         */
        void wakeSleepingWorkers();
        /**
         * @brief 잠든 워커 중 @p wantedCount 개만 깨웁니다. `submitWithoutWake` 로 모두 넣은 뒤 부릅니다.
         * @details 모두 깨우면 워커 열넷이 일감 여섯을 두고 다투는 thundering herd 가 됩니다. 렌더 그래프 병렬 기록이 두 배
         *          느려졌습니다(150~192 -> 329~380 us). 넣은 태스크 수만큼만 깨우는 것이 가장 빨랐습니다.
         */
        void wakeSleepingWorkers( uint32 wantedCount );
        /** @brief 지금까지 워커를 깨운 시그널 수입니다. 테스트가 "그룹 하나에 한 번" 인지 확인하는 데 씁니다. */
        uint32 getWakeSignalCount() const { return _wakeSignalCount.load( std::memory_order_relaxed ); }
        /** @brief 만들어졌지만 아직 끝나지 않은 태스크 수입니다. `waitStage` · `waitAll` 이 돌아온 직후에는 그 몫이 빠져 있어야 합니다(테스트용). */
        uint32 getActiveTaskCount() const { return _activeTaskCount.load( std::memory_order_acquire ); }

        /**
         * @brief 등록된 모든 비동기 태스크가 끝날 때까지 기다립니다.
         * @param timeoutMs 최대 대기 시간(밀리초). 0 이면 끝날 때까지 무제한으로 기다립니다.
         * @return 모든 태스크가 끝났으면 true, 시간이 다 되면 false
         */
        bool waitAll( uint32 timeoutMs = 0 );

        /** @brief 내부 큐와 스테이지 상태를 강제로 정리합니다. 아무것도 돌고 있지 않을 때만 부르십시오. */
        void clear();

        /**
         * @brief 넘긴 태스크가 모두 끝나면 실행될 후속 태스크(continuation)를 만듭니다.
         * @param listTask 먼저 끝나야 하는 태스크 핸들 목록
         * @param continuation 모두 끝나면 실행할 델리게이트
         * @param affinity 실행할 스레드
         * @return 만든 후속 태스크의 핸들
         */
        TaskHandle whenAll( const vector<TaskHandle>& listTask, const TaskDelegate& continuation, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 넘긴 태스크 중 **하나라도** 먼저 끝나면 바로 실행되는 후속 태스크를 만듭니다.
         * @param listTask 지켜볼 태스크 핸들 목록
         * @param continuation 처음으로 끝난 태스크가 나오면 실행할 델리게이트
         * @param affinity 실행할 스레드
         * @return 만든 후속 태스크의 핸들
         */
        TaskHandle whenAny( const vector<TaskHandle>& listTask, const TaskDelegate& continuation, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 메인 스레드 전용(`MainThread`)으로 등록된 태스크를 한꺼번에 실행합니다.
         * @note 메인 렌더 · 게임 루프에서 틱마다 불러야 합니다.
         */
        void dispatchMainThreadTasks();

        /** @brief 만든 워커 스레드 수를 반환합니다. */
        uint32 getWorkerCount() const { return static_cast<uint32>( _listWorkerSlot.size() ); }

        /** @brief 현재 스레드가 TaskManager 를 초기화한 메인 스레드인지 확인합니다. */
        bool isMainThread() const;
        /** @brief Debug: 메인 스레드가 아니면 assert 합니다(UE 의 `check( IsInGameThread() )` 자리). */
        void ensureMainThread() const;
        /** @brief 현재 스레드가 이 풀의 워커인지 확인합니다. */
        bool isWorkerThread() const;
        /** @brief Debug: 워커 스레드가 아니면 assert 합니다. */
        void ensureWorkerThread() const;
        /**
         * @brief 현재 스레드가 병렬 그룹(`emplaceParallel*` · `runParallel`)의 본문을 실행 중인지 반환합니다.
         * @details 병렬 본문에서 부르면 안 되는 함수(부모 바꾸기 · 컴포넌트 추가 같은 구조 변경)가
         *          `SW_ASSERT( isInsideParallelTask() == false )` 로 스스로를 지키는 데 씁니다. UE 가 `FTaskTagScope` 로 병렬
         *          작업 구간을 표시하고 `IsInParallelRenderingThread()` 같은 검사를 두는 것과 같은 자리입니다. `runParallel` 이
         *          문턱 아래라 호출 스레드가 한 번에 돌 때도 병렬 본문으로 봅니다(검사가 개수 · 워커 수에 따라 달라지지 않게).
         *          청크 사이에 High 레인 태스크를 돌리는 동안은 병렬 본문 밖으로 봅니다(그 태스크는 병렬 본문이 아닙니다).
         */
        bool isInsideParallelTask() const;
        /** @brief Debug: 병렬 태스크 본문 안이 아니면 assert 합니다. */
        void ensureInsideParallelTask() const;

        /**
         * @brief 태스크 본문을 실행할 수 있는 스레드의 최대 수입니다. 워커 수 + 기다리는 동안 다른 일을 돕는 스레드 몫(`kMaxHelperThreadCount`).
         * @details 스레드마다 하나씩 쓰는 스크래치(플러시 DFS 스택 · 더티 루트 · 트랜스폼 쓰기 큐) 배열의 크기입니다. **워커가 아닌
         *          스레드도 태스크를 실행합니다.** `waitStage` · `waitAll` · `runParallel` 이 기다리는 동안 `helpOrSpin` 으로 준비된
         *          잡을 아무거나 돕는데, 메인 스레드뿐 아니라 렌더 스레드(패스 기록 대기) · 로더 스레드(스트리밍 대기)도 그 경로를
         *          지납니다. 예전에는 그 스레드들이 모두 "마지막 칸" 하나를 나눠 써서, 메인과 렌더가 같은 프레임에 같은 슬롯 벡터에
         *          push 해 벡터가 깨졌습니다(틱 중 트랜스폼 쓰기 큐에서 세그폴트로 드러났습니다).
         */
        uint32 getScratchSlotCount() const { return getWorkerCount() + kMaxHelperThreadCount; }
        /**
         * @brief 현재 스레드의 스크래치 슬롯입니다. 워커면 그 번호이고, 아니면 처음 물을 때 받는 고유한 도우미 번호(워커 수 + n)입니다.
         * @details 도우미 번호는 스레드마다 한 번 배정되어 그 스레드가 사는 동안 바뀌지 않습니다. 상한(`kMaxHelperThreadCount`)을
         *          넘는 스레드는 마지막 칸을 나눠 쓰고, 그 경우를 로그로 알립니다(엔진에서 태스크를 기다리는 스레드는 메인 · 렌더 ·
         *          로더 · 업로드 정도라 넘지 않습니다). 대기자 슬롯(잠드는 워드)도 같은 번호를 씁니다.
         */
        uint32 getCurrentThreadScratchSlot();
        /** @brief 워커가 아니면서 태스크를 실행할 수 있는 스레드의 상한입니다(메인 · 렌더 · 로더 · 업로드 · 에디터 등). */
        static constexpr uint32 kMaxHelperThreadCount = 8;

    private:
        // --- 수명 · 정리 ---
        /** @brief 워커 스레드의 메인 루프입니다. 큐 소비 · 훔치기 · 세대 스핀 · 자기 워드에서 잠들기를 합니다. */
        void workerLoop( uint32 workerId );
        /** @brief `clear` 가 큐에서 꺼낸 항목 하나를 실행하지 않고 놓습니다(노드는 참조를 해제하고, 티켓은 그룹을 닫습니다). */
        void drainQueueItem( uintptr_t item );

        // --- 태스크 만들기 · 의존성 ---
        /** @brief 노드를 꺼내 이름 · 실행 스레드를 적고, 태스크 안이면 그 태스크의 자식으로 연결하고, 활성 수를 올립니다. 만들지 못하면 nullptr 입니다. */
        TaskNode* createTaskNode( string_view name, TaskThreadAffinity affinity );
        /** @brief 의존성 하나를 풉니다. 마지막이었으면 큐에 넣습니다(`submit` · 선행 태스크 완료 · 병렬 그룹 완료가 모두 이 함수를 거칩니다). */
        void resolveDependency( TaskNode* pNode, bool bWakeWorker );
        /**
         * @brief 의존성이 모두 풀린 태스크 노드를 레인(High · 로컬 데크 · Normal 전역 · Low · 메인)에 넣습니다.
         * @param bWakeWorker false 면 잠든 워커를 깨우지 않습니다(`submitWithoutWake`).
         */
        void scheduleReadyTask( TaskNode* pNode, bool bWakeWorker );
        /** @brief 마지막 참조가 놓인 노드를 풀로 돌려줍니다(`TaskNode::release` 가 부릅니다). */
        void deallocateNode( TaskNode* pNode );

        // --- 병렬 그룹 ---
        /** @brief `emplaceParallel` · `emplaceParallelBlock` 의 공통 본체입니다. 부모 노드 하나 + 풀 그룹 하나 + 티켓으로 이루어집니다. */
        TaskHandle emplaceParallelGroup( string_view name, uint32 start, uint32 end, const ParallelBlockDelegate* pBlockBody, const ParallelTaskDelegate* pIndexBody, TaskThreadAffinity affinity );
        /** @brief 티켓 @p ticketCount 장을 이 스레드의 레인에 넣고, 그만큼 워커를 깨웁니다. */
        void pushGroupTickets( ParallelGroup* pGroup, uint32 ticketCount );
        /** @brief 그룹의 청크가 남지 않을 때까지 가져와 실행합니다(티켓을 받은 워커와 호출 스레드가 함께 씁니다). */
        void runGroupChunks( ParallelGroup* pGroup );
        /** @brief 청크 사이에 High 레인을 비웁니다. 그룹을 실행하는 동안에도 렌더 패스 기록이 줄을 서지 않게 하기 위해서입니다. */
        void runHighLaneBetweenChunks();
        /** @brief 병렬 그룹의 티켓 하나를 처리합니다. 청크가 남아 있는 동안 가져와 실행하고 티켓을 닫습니다. */
        void runGroupTicket( ParallelGroup* pGroup );
        /**
         * @brief 티켓 하나를 닫습니다. 마지막이면 대기자를 깨우고, 풀 그룹이면 그룹을 돌려주고 (@p bResolveParent 면) 부모의 의존성을 풉니다.
         * @details 스택 그룹(`runParallel`)은 조인 카운터가 0 이 되는 순간 사라질 수 있습니다. 그 뒤에는 @p pGroup 을 역참조하지 않습니다.
         */
        void closeGroupTicket( ParallelGroup* pGroup, bool bResolveParent );

        // --- 기다리기 ---
        /**
         * @brief 기다리는 동안의 한 걸음입니다. 메인이면 메인 일감을 돌리고, 준비된 일을 하나 돕거나, 잠깐 스핀합니다.
         * @return 계속 돌아야 하면 true, 스핀 예산을 다 써서 이제 잠들 차례면 false(예산은 다시 채워 둡니다)
         * @details `waitForJoin` 과 `waitAll` 이 같은 앞부분을 각자 들고 있었습니다. 둘이 다른 것은 잠드는 방법뿐입니다.
         */
        bool helpOrSpin( uint32& inoutSpinCount );
        /** @brief @p join 이 0 이 될 때까지 기다립니다. 메인 일감을 실행하고, 다른 일을 돕고, 잠깐 스핀하다가 자기 슬롯에서 잠듭니다. */
        void waitForJoin( JoinCounter& join );
        /** @brief 이 스레드를 @p join 의 대기자로 등록하고 잠듭니다. 깨어나면 부르는 쪽이 조건을 다시 확인합니다. */
        void parkOnJoin( JoinCounter& join );
        /**
         * @brief 완료 브로드캐스트(스테이지 완료 · 활성 수 0 · 메인 일감 · clear)를 기다리며 잠들 준비를 합니다. 대기자 수를 올리고 세대를 반환합니다.
         * @details 부르는 쪽은 그 뒤에 조건을 다시 확인하고, `commitBroadcastWait` 로 잠들거나 `cancelBroadcastWait` 로 취소합니다. 수를
         *          먼저 올리고 조건을 보는 쪽과, 조건을 바꾸고 수를 보는 완료 쪽이 Dekker 식 짝을 이룹니다.
         */
        uint32 prepareBroadcastWait();
        /** @brief 브로드캐스트 세대가 @p seenEpoch 인 동안 잠듭니다. @p timeoutMilli 가 0 이면 무제한입니다. 대기자 수는 여기서 내립니다. */
        void commitBroadcastWait( uint32 seenEpoch, uint32 timeoutMilli );
        /** @brief 잠들지 않기로 했을 때 대기자 수만 내립니다. */
        void cancelBroadcastWait();
        /** @brief 브로드캐스트 대기자가 있으면 브로드캐스트 세대를 올려 모두 깨웁니다. */
        void notifyBroadcast();
        /** @brief 메인 스레드가(어느 대기에서든) 잠들어 있으면 깨웁니다. 메인 전용 일감이 들어왔을 때 부릅니다. */
        void wakeParkedMainThread();
        /** @brief 대기자 슬롯 @p slotIndex 의 스레드를 깨웁니다. 할 일 없는 워커(슬롯 = 워커 번호)와 기다리는 스레드 모두 이 함수 하나로 깨웁니다. */
        void unparkSlot( uint32 slotIndex );
        /** @brief 대기자 슬롯 @p slotIndex 의 워드입니다. 번호는 `getCurrentThreadScratchSlot` 과 같습니다. */
        atomic<uint32>& getWaiterWord( uint32 slotIndex );

        // --- 실행 ---
        /** @brief 큐 항목 하나를 실행합니다. 태스크 노드이거나 병렬 그룹의 티켓입니다. */
        void executeItem( uintptr_t item );
        /** @brief 태스크 노드 하나의 본문을 실행하고, 자식들의 조인 뒤 완료를 처리합니다. */
        void executeTask( TaskNode* pNode );
        /** @brief 본문과 자식이 모두 끝난 태스크의 완료를 처리합니다(활성 수 · 스테이지 · 부모 · 후속 태스크). */
        void completeTask( TaskNode* pNode );

        // --- 큐 ---
        /** @brief Normal 레인에 넣습니다. 워커면 자기 데크(가득 차면 전역 큐), 아니면 전역 큐에 넣습니다. */
        void pushToNormalLane( uintptr_t item );
        /** @brief 레인 순서(High → 내 데크 → Normal 전역 → 훔치기 → Low)대로 항목 하나를 가져옵니다. @p workerId 가 음수면 워커가 아닙니다. */
        bool tryTakeItem( int32 workerId, uintptr_t& outItem );
        /** @brief 이 스레드가 가져올 수 있는 항목 하나를 가져와 실행합니다. 기다리는 동안 다른 일을 돕는 곳입니다. */
        bool tryHelpAndExecute();

    private:
        /**
         * @brief 대기자 하나가 잠드는 워드입니다. 스레드마다 하나이고, 캐시 라인 하나를 혼자 씁니다.
         * @details `waitStage` · `runParallel` · 워커의 유휴 잠들기가 모두 이 방식입니다. 잠드는 쪽은 자기 워드에서 잠들고, 깨우는
         *          쪽은 그 주소로 깨웁니다. 예전의 조건 변수 하나(`_cvWaitAll`)에는 게임 · 렌더 · 로더 스레드가 함께 잠들어 있어서, 어떤
         *          완료든 모두를 깨웠고 깨어난 쪽은 뮤텍스를 다시 잡아야 돌아왔습니다.
         */
        struct alignas( 64 ) WaiterSlot
        {
            atomic<uint32> _word; ///< 0 에서 시작한다(래퍼의 기본 생성). 값 자체에는 의미가 없고 바뀌었는지만 본다
        };

        /** @brief 워커 하나의 자리입니다. 고정 크기 lock-free 데크 · 잠드는 워드 · 스레드 핸들을 가집니다. */
        struct WorkerSlot
        {
            WorkStealingDeque<uintptr_t> _queue{ 4096 }; ///< 항목은 태스크 노드(짝수)이거나 병렬 그룹의 티켓(홀수)이다
            WaiterSlot                   _park;          ///< 유휴 워커가 잠드는 워드이자, 이 워커가 태스크 안에서 기다릴 때 쓰는 대기 워드
            std::thread                  _thread;        ///< 이 자리를 도는 워커 스레드. `shutdown` 이 합류한다
        };

        /** @brief 유휴 비트마스크가 담을 수 있는 워커 수입니다. `initialize` 가 이 수로 제한합니다. */
        static constexpr uint32 kMaxWorkerCount = 64;

        bool                           _bInitialized;    ///< 매니저를 초기화했는지 여부
        std::thread::id                _mainThreadId;    ///< 메인 스레드의 ID
        atomic<bool>                   _bStop;           ///< 워커 스레드 종료 플래그
        vector<unique_ptr<WorkerSlot>> _listWorkerSlot;  ///< 워커마다 데크 + 잠드는 워드 + 스레드. 워커가 도는 동안 크기가 바뀌지 않는다
        atomic<uint32>                 _helperSlotCount; ///< 지금까지 도우미 슬롯을 받은 비워커 스레드 수
        /**
         * @brief 잠든 워커의 비트마스크입니다. 깨우는 쪽은 비트를 **원자적으로 내리고** 그 워커만 깨웁니다.
         * @details 예전에는 `_sleepingWorkerCount` 와 조건 변수 하나였습니다. `notify_one` 은 누구를 깨울지 고를 수 없고, 깨어난 워커가
         *          모두 `_workerMutex` 를 다시 잡아야 했습니다. 이제는 비트를 내린 쪽만 깨우므로 두 제출자가 같은 워커를 두 번 깨우지
         *          않고, n 번의 깨우기는 서로 독립적인 n 번의 주소 깨우기입니다.
         */
        alignas( 64 ) atomic<uint64> _idleWorkerMask;
        /**
         * @brief 일감이 들어올 때마다 오르는 세대입니다. 스핀 중인 워커는 **이 값만 읽습니다.**
         * @details 예전 스핀은 매번 큐를 뒤졌습니다(지금의 `tryTakeItem`). 전역 MPMC 큐의 CAS 와 열네 개 데크의 steal 을 워커 열다섯이
         *          동시에 반복해서 건드려서, 스핀을 늘리자 게임 · 렌더 스레드의 코어까지 빼앗았습니다. 읽기 전용 캐시 라인 하나만 보면
         *          경합이 없고, 세대가 바뀌었을 때만 큐를 건드립니다. `submitWithoutWake` 는 세대를 올리지 않고, 묶음 끝의
         *          `wakeSleepingWorkers` 가 한 번 올립니다.
         */
        alignas( 64 ) atomic<uint32> _workEpoch;
        atomic<uint32> _wakeSignalCount; ///< 워커를 깨운 시그널 수(통계 · 테스트용)
        /**
         * @brief 완료 브로드캐스트 세대입니다. `waitAll` 과 "두 번째 대기자" 가 잠드는 워드입니다.
         * @details 스테이지 · 병렬 그룹은 대기자 **하나**를 자기 조인 카운터에 적어 두고, 마지막 태스크가 그 스레드만 깨웁니다. 같은
         *          스테이지를 둘이 기다리거나 `waitAll` 처럼 대상이 전체인 대기만 이 브로드캐스트를 기다리며 잠듭니다.
         *          `_broadcastWaiterCount` 가 0 이면 브로드캐스트는 원자 변수 하나를 읽고 끝납니다. 완료 경로에 시스템 콜이 없습니다.
         */
        alignas( 64 ) atomic<uint32> _completionEpoch;
        atomic<uint32> _broadcastWaiterCount; ///< 브로드캐스트를 기다리며 잠든(또는 잠들려는) 스레드 수
        /**
         * @brief 메인 스레드가 잠들어 있으면 그 대기자 슬롯이고, 아니면 -1 입니다.
         * @details 메인 전용 일감(`MainThread` 지정)은 메인 스레드만 실행할 수 있습니다. 메인이 스테이지를 기다리다 잠든 사이에 그
         *          스테이지의 태스크가 메인 일감을 만들면, 메인을 깨우지 않는 한 둘 다 영원히 기다립니다(예전에 `notify_one` 이 엉뚱한
         *          스레드를 깨워 실제로 멈췄습니다). 메인은 어느 대기에서든 잠들기 전에 여기에 자기 슬롯을 적고, 메인 일감을 넣는 쪽은
         *          여기를 봅니다.
         */
        atomic<int32> _mainThreadParkedSlot;

        /**
         * @brief `TaskPriority::High` 전용 전역 큐입니다. **모든 워커가 자기 데크보다 먼저 봅니다.**
         * @details 게임 스레드의 대량 잡(트랜스폼 플러시 · 씬 수집)과 렌더 스레드의 병렬 패스 기록이 같은 풀을 씁니다. 레인이 없으면
         *          렌더 스레드가 방금 넣은 기록 태스크가 게임 스레드의 청크 수십 개 뒤에 줄을 서고, 렌더 스레드는 그 스테이지를 바로
         *          기다리므로 그 줄이 그대로 프레임 지연이 됩니다(큐브 8000개에서 게임 스레드 잡을 켜자 렌더 스레드의 그래프 기록이
         *          170 -> 261 us). 상용 엔진이 렌더 · 오디오 잡을 별도 레인에 두는 이유입니다. `_priority` 는 그동안 저장만 되고
         *          스케줄러가 보지 않았습니다.
         */
        ConcurrentQueue<uintptr_t, 1024> _globalHighQueue;
        ConcurrentQueue<uintptr_t, 4096> _globalWorkerQueue; ///< `Normal` 전역 큐. 워커가 아닌 스레드(게임 · 렌더 스레드)가 넣는 곳
        /** @brief `TaskPriority::Low` 전용 전역 큐입니다. 훔쳐 올 것도 없을 때만 봅니다. 백그라운드 I/O · 통계용입니다. */
        ConcurrentQueue<uintptr_t, 1024> _globalLowQueue;
        ConcurrentQueue<TaskNode*, 1024> _queueMainThread; ///< 메인 스레드 전용 태스크 큐(lock-free)
        /** @brief 워커가 아닌 스레드(메인 · 렌더 · 로더 …)의 대기자 슬롯입니다. 번호는 `getCurrentThreadScratchSlot` 의 도우미 칸과 같습니다. */
        WaiterSlot _arrHelperWaiter[kMaxHelperThreadCount];

        alignas( 64 ) atomic<uint32> _activeTaskCount; ///< 지금 실행 중이거나 대기 중인 활성 태스크의 총 수
        unique_ptr<TaskNodePool> _nodePool;            ///< 태스크 노드 · 스테이지 · 병렬 그룹 풀
    };
} // namespace sw
