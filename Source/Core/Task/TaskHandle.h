/**
 * @file TaskHandle.h
 * @brief 만든 태스크 · 스테이지를 가리키는 핸들과 우선순위 · 실행 스레드 선택을 정의합니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Task/TaskDelegate.h"

namespace sw
{
    /**
     * @enum TaskPriority
     * @brief 태스크의 실행 우선순위입니다.
     */
    enum class TaskPriority : uint8
    {
        High   = 0, ///< 프레임에 결정적인 렌더링 · 물리 태스크(가장 먼저 처리)
        Normal = 1, ///< 일반 게임플레이 · 계산 태스크(기본값)
        Low    = 2  ///< 백그라운드 I/O · 에셋 파싱 · 통계 태스크
    };

    /**
     * @enum TaskThreadAffinity
     * @brief 태스크를 실행할 스레드를 정합니다.
     */
    enum class TaskThreadAffinity : uint8
    {
        Any,       ///< 워커 풀의 아무 유휴 스레드에서나 실행한다
        MainThread ///< 메인 스레드(렌더 · UI · 엔진 메인 루프)에서만 실행한다(dispatchMainThreadTasks 를 부를 때)
    };

    struct StageNode;
    struct TaskNode;

    class TaskManager;

    /**
     * @struct TaskHandle
     * @brief 만든 태스크를 가리키는 핸들입니다. DAG 의존성 연결과 체이닝을 지원합니다.
     * @details
     * - 침입형 참조 계수를 써서, 복사 · 이동할 때 스마트 포인터 같은 별도 할당이 없습니다.
     * - `then()`, `runBefore()`, `runAfter()` 로 작업 사이의 선후 관계를 선언적으로 조립할 수 있습니다.
     */
    struct SW_API TaskHandle
    {
        friend class TaskManager;

        /** @brief 빈 핸들입니다(노드 없음). */
        constexpr TaskHandle() = default;

        /** @brief TaskNode 포인터로 핸들을 만들고 참조 카운트를 올립니다. */
        explicit TaskHandle( TaskNode* pNode );
        TaskHandle( const TaskHandle& other );
        TaskHandle( TaskHandle&& other ) noexcept;
        ~TaskHandle();

        TaskHandle& operator=( const TaskHandle& other );
        TaskHandle& operator=( TaskHandle&& other ) noexcept;

        /** @brief 유효한 태스크 노드를 가리키는지 반환합니다. */
        bool isValid() const { return _pNode != nullptr; }

        /** @brief 내부 TaskNode 포인터를 반환합니다. */
        TaskNode* getNode() const { return _pNode; }

        /** @brief 태스크의 우선순위를 정합니다. */
        TaskHandle& setPriority( TaskPriority priority );

        /** @brief 태스크의 우선순위를 반환합니다. */
        TaskPriority getPriority() const;

        /**
         * @brief 이 태스크가 targetTask 보다 반드시 먼저 끝나도록 DAG 선후 의존성을 겁니다.
         * @param targetTask 이 태스크가 끝난 뒤 실행될 후속 태스크
         * @return 의존이 성립했으면 true(이 태스크가 이미 끝나 바로 채워진 경우 포함). 새 간선이 사이클을 만들거나(자기 자신 포함)
         *         빈 핸들이 있으면 false 이고 아무것도 걸지 않습니다. 사이클은 `Task.DependencyCycle` Error 로그도 남깁니다 — 걸었다면
         *         후속의 의존 수가 영영 0 이 되지 않아 `waitAll` 이 멈춥니다. 검사는 Release 에서도 합니다.
         * @details 사이클 검사는 후속(`targetTask`)에서 후속 목록을 따라 이 태스크에 닿는지 봅니다. 비용은 후속 아래로 아직 안 끝난
         *          노드 수에 비례합니다(방문 집합이 있어 노드마다 한 번). 방금 만든 후속에는 후속이 없어 거의 공짜입니다. 검사와 걸기는
         *          한 잠금(풀마다 하나) 안에서 하므로 서로 반대 방향으로 동시에 거는 두 스레드도 둘 다 통과하지 못합니다.
         *          끝난 노드는 따라가지 않습니다(끝난 쪽은 새 간선이 기다림을 만들지 못합니다).
         * @note 자식 → 부모 완료 기다림(본문 안에서 만든 태스크는 부모가 그 완료를 기다린다)은 후속 목록의 간선이 아니지만 `_pParent` 사슬로 따로 봅니다:
         *       `부모.runBefore( 자식 · 손자 )` 는 사이클이라 거부합니다. 사슬 확인은 대상의 조상만 보므로 `부모.runBefore( X )`, `X.runBefore( 자식 )` 처럼 둘 이상의
         *       간선을 지나는 간접 사이클은 후속 목록을 따라가는 검사가 `자식 → 부모` 를 지나지 못해 놓칩니다.
         *       간선은 같은 풀(= 같은 `TaskManager`)의 태스크끼리만 겁니다 — 검사와 걸기를 지키는 잠금이 풀마다 하나라 다른 풀 사이의 반대 방향 간선은
         *       동시 정확성을 보증하지 못해 `Task.DependencyCrossPool` Error 로 거부합니다.
         *       `whenAny` 의 트리거 → 후속 연결은 후속 목록의 간선이 아니다(트리거가 후속의 의존 수를 직접 내린다 — 입력 중 하나만 후속을 풀어야 해서
         *       목록 간선으로 두면 나머지 입력이 내리는 수를 완료 순서에 맞춰 보정해야 한다). 대신 트리거 노드의 숨은 후속 칸(`TaskNode::_pAnyOfSuccessor`)을
         *       검사가 따라가 "입력 → 트리거 → 후속 → 입력" 도 사이클로 거부한다. 완료 경로는 그대로다.
         */
        [[nodiscard]] bool runBefore( const TaskHandle& targetTask );

        /**
         * @brief dependencyTask 가 반드시 먼저 끝난 뒤에 이 태스크가 실행되도록 DAG 선후 의존성을 겁니다.
         * @param dependencyTask 이 태스크보다 먼저 끝나야 하는 선행 태스크
         * @return `runBefore` 와 같습니다(사이클이나 빈 핸들이면 false 이고 걸지 않습니다).
         */
        [[nodiscard]] bool runAfter( TaskHandle dependencyTask );

        /**
         * @brief 이 태스크가 끝나면 자동으로 실행될 후속 태스크(continuation)를 만들어 연결합니다.
         * @param nextTaskDelegate 이어서 실행할 델리게이트
         * @param affinity 후속 태스크를 실행할 스레드
         * @return 새로 만든 후속 태스크의 핸들. 매니저가 멈추는 중이면 빈 핸들입니다. 후속은 새로 만든 노드라 사이클이 될 수 없습니다.
         * @note 후속 본문은 선행이 취소돼도 돕니다(`cancel` 참고). 선행이 취소됐는지는 후속이 `isCancelled` 로 물어야 합니다.
         */
        TaskHandle then( const TaskDelegate& nextTaskDelegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /**
         * @brief 태스크를 취소합니다. 아직 실행 중이 아니면 본문을 건너뜁니다.
         * @details 취소는 전파되지 않습니다. 취소된 태스크도 실행 차례가 오면 본문만 건너뛰고 평소처럼 끝난 것으로 처리되어
         *          후속(`runBefore` · `then` · `whenAll`)의 선행 하나가 풀립니다. 그래서 취소가 후속의 `waitAll` 을 영영 막지
         *          않습니다(만들기만 하고 버린 선행도 같은 길로 끝납니다). 후속까지 멈추려면 후속도 `cancel` 하거나, 후속 본문에서
         *          선행의 `isCancelled` 로 물어 일을 건너뜁니다(선행 핸들을 캡처해 둡니다).
         *          이미 실행 중인 본문은 끊지 않습니다. 협력 취소가 필요하면 `StopToken`(`Core/Concurrency/StopToken.h`)을 씁니다.
         *          태스크 본문에서 예외를 던지면 안 됩니다 — 잡는 곳이 없고 실패는 반환값 · 로그로 다룹니다.
         */
        bool cancel();

        /** @brief 태스크가 취소됐는지 반환합니다. 후속 본문이 선행의 취소를 알려면 선행 핸들로 이것을 부릅니다(핸들이 노드를 붙들어 둡니다). */
        bool isCancelled() const;

        /**
         * @brief 태스크의 본문과, 본문 안에서 만든 자식 태스크가 모두 끝났는지 반환합니다. 기다리지 않고 묻기만 합니다.
         * @details UE 의 `FGraphEvent::IsComplete()` · Unity 의 `JobHandle.IsCompleted` 에 해당합니다. 취소된 태스크도 실행 차례가
         *          지나면 끝난 것으로 봅니다. true 를 본 스레드에는 그 태스크(와 자식)가 쓴 것이 보입니다. 빈 핸들은 기다릴
         *          것이 없으므로 true 입니다(`isStageComplete` 와 같습니다).
         */
        bool isCompleted() const;

        /** @brief 빌더 의존성을 풀고 스케줄러에 제출합니다. 선행 조건이 모두 만족되면 실행됩니다. */
        void submit();

    private:
        TaskNode* _pNode{ nullptr };
    };

    /**
     * @struct TaskStageHandle
     * @brief 여러 태스크를 하나의 논리적 단계(stage)로 묶어 관리하고 동기화하는 스테이지 핸들입니다.
     * @details 스테이지 안의 모든 태스크가 끝날 때까지 `waitStage()` 로 블로킹 대기할 수 있습니다. 스테이지는 남은 수만
     *          세고 태스크를 붙들지 않습니다. `addTask` 는 태스크를 제출하기 전에 부르십시오.
     */
    struct SW_API TaskStageHandle
    {
        friend class TaskManager;

        constexpr TaskStageHandle() = default;
        /**
         * @brief 노드의 참조 하나를 넘겨받아 핸들을 만듭니다(매니저 전용).
         * @details `TaskHandle` 처럼 침입형 참조 계수이고 노드는 매니저의 풀에서 옵니다(렌더 그래프는 프레임마다 레벨 수만큼 스테이지를
         *          만듭니다). 그래서 프레임이 안정된 상태에서는 스테이지를 디스패치해도 힙을 건드리지 않습니다.
         */
        explicit TaskStageHandle( StageNode* pNode ) noexcept
            : _pNode{ pNode } {}
        TaskStageHandle( const TaskStageHandle& other );
        TaskStageHandle( TaskStageHandle&& other ) noexcept;
        TaskStageHandle& operator=( const TaskStageHandle& other );
        TaskStageHandle& operator=( TaskStageHandle&& other ) noexcept;
        ~TaskStageHandle();

        bool isValid() const { return _pNode != nullptr; }

        TaskStageHandle& addTask( const TaskHandle& task );

    private:
        StageNode* _pNode{ nullptr };
    };
} // namespace sw
