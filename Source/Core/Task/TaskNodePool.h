/**
 * @file TaskNodePool.h
 * @brief TaskManager 의 노드 풀 — 태스크 슬랩 · 스테이지 · 병렬 그룹. 프레임 정상 상태에서 힙을 만지지 않게 한다.
 * @details **공개 API 가 아니다.** `Core/Task` 안의 구현 파일만 포함한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/ConcurrentQueue.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskNode.h"

namespace sw
{
    /**
     * @class TaskNodePool
     * @brief 태스크 노드는 64 개 슬랩으로, 스테이지는 소유 목록 + 자유 목록으로, 병렬 그룹은 락프리 풀로 돌려씁니다.
     */
    class TaskNodePool
    {
    public:
        TaskNodePool() = default;
        /**
         * @brief 태스크 슬랩을 되돌립니다. 스테이지는 `_listStageAll` 이 소유하므로 여기서 할 일이 없다.
         * @details 슬랩만 손으로 푸는 것은 `Memory::allocate` 로 원소 64 개를 한 번에 잡고 배치 new 로
         *          짓기 때문이다 — 그쪽은 짝이 되는 해제도 손으로 해야 한다.
         */
        ~TaskNodePool();

        TaskNodePool( const TaskNodePool& )            = delete;
        TaskNodePool& operator=( const TaskNodePool& ) = delete;

        /** @brief 태스크 노드를 꺼내 기본 상태로 되돌려 줍니다 (소유자 · 이름 · 콜러블은 부르는 쪽이 채운다). */
        TaskNode* allocate();
        /** @brief 태스크 노드를 비우고 되돌립니다 (후속 참조 해제 · 콜러블 파괴). */
        void deallocate( TaskNode* pNode );

        /** @brief 스테이지 노드를 꺼냅니다 — 풀에 남은 것이 없을 때만 새로 만든다(용량은 그 뒤로 남는다). */
        StageNode* allocateStage();
        /** @brief 마지막 참조가 놓인 스테이지를 되돌립니다. 기다리지 않고 버린 스테이지의 태스크 참조도 여기서 놓는다. */
        void deallocateStage( StageNode* pStage );
        /** @brief 강제 정리(`clear`) — 살아 있는 스테이지를 전부 되돌립니다. 아무것도 돌고 있지 않을 때만. */
        void resetAllStages();

        /** @brief 병렬 그룹을 꺼냅니다. 풀이 비면 힙에서 — 그 경우 `_bHeap` 이 켜진다. */
        ParallelGroup* allocateGroup();
        /** @brief 병렬 그룹을 되돌립니다 — 풀로, 힙에서 왔으면 힙으로. */
        void deallocateGroup( ParallelGroup* pGroup );

    private:
        static constexpr uint32 kSlabSize = 64;

        ConcurrentQueue<TaskNode*, 4096> _freeQueue;
        vector<TaskNode*>                _listOverflowFree;
        vector<TaskNode*>                _listSlab;
        mutex                            _slabMutex;
        vector<StageNode*>               _listStageFree; ///< 돌아온 스테이지 — 다음 createAnonymousStage 가 먼저 집는다. 빌려 쓰는 것이라 소유하지 않는다
        /**
         * @brief 만든 스테이지 전부 — **소유한다.**
         * @details 처음에는 raw 포인터 목록이었고 풀 소멸자가 지우는 자리를 빠뜨려 스테이지가 프로세스
         *          끝까지 남았다(리눅스 CI 의 LeakSanitizer 가 잡았다 — 윈도우 테스트는 전부 초록이었다).
         *          소유를 타입으로 적으면 그 자리를 빠뜨릴 수 없다. 꺼내 쓰는 쪽(`_listStageFree`)은
         *          여전히 raw 라 할당 경로에 간접이 늘지 않는다.
         */
        vector<unique_ptr<StageNode>> _listStageAll;
        mutex                         _stageMutex;
        ParallelGroupPool             _groupPool;
    };
} // namespace sw
