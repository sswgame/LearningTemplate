/**
 * @file TaskNodePool.h
 * @brief TaskManager 의 노드 풀입니다(태스크 슬랩 · 스테이지 · 병렬 그룹). 프레임이 안정된 상태에서는 힙을 건드리지 않게 합니다.
 * @details **공개 API 가 아닙니다.** `Core/Task` 안의 구현 파일만 include 합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/ConcurrentQueue.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskNode.h"

namespace sw
{
    /**
     * @class TaskNodePool
     * @brief 태스크 노드는 64개 슬랩으로, 스테이지는 소유 목록 + 자유 목록으로, 병렬 그룹은 lock-free 풀로 재사용합니다.
     * @details 매니저가 소유하지만, 매니저가 사라질 때 핸들이 쥔 노드 · 스테이지가 남아 있으면 그 핸들들의 몫이 됩니다(`detachOwner`).
     *          그 뒤 놓이는 노드 · 스테이지는 남은 수를 내리고, 마지막 것이 풀을 소멸시킵니다. 참조 수를 세는 비용은 매니저가 사라질 때 한 번뿐입니다.
     */
    class TaskNodePool
    {
    public:
        /** @brief @p pOwner 가 소유하는 풀입니다. 노드는 매니저를 이 풀로 찾습니다(`getOwner`). */
        explicit TaskNodePool( TaskManager* pOwner );
        /**
         * @brief 태스크 슬랩을 돌려줍니다. 스테이지는 `_listStageAll` 이 소유하므로 여기서 할 일이 없습니다.
         * @details 슬랩만 직접 해제하는 이유는 `Memory::allocate` 로 원소 64개를 한 번에 잡고 placement new 로 만들기 때문입니다.
         *          그렇게 만든 것은 짝이 되는 해제도 직접 해야 합니다.
         */
        ~TaskNodePool();

        TaskNodePool( const TaskNodePool& )            = delete;
        TaskNodePool& operator=( const TaskNodePool& ) = delete;

        /** @brief 태스크 노드를 꺼내 기본 상태로 되돌려 줍니다(소유자 · 이름 · 호출 대상은 부르는 쪽이 채웁니다). */
        TaskNode* allocate();
        /** @brief 태스크 노드를 비우고 돌려줍니다(후속 태스크의 참조를 놓고 호출 대상을 파괴합니다). */
        void deallocate( TaskNode* pNode );

        /** @brief 스테이지 노드를 꺼냅니다. 풀에 남은 것이 없을 때만 새로 만듭니다(늘어난 용량은 그 뒤로도 남습니다). */
        StageNode* allocateStage();
        /** @brief 마지막 참조가 놓인 스테이지를 돌려줍니다. */
        void deallocateStage( StageNode* pStage );
        /** @brief 강제 정리(`clear`)용입니다. 남은 태스크가 있는 스테이지의 수를 비우고 그 몫의 참조를 놓습니다(핸들의 참조는 둔다). 아무것도 돌고 있지 않을 때만 부르십시오. */
        void resetAllStages();

        /** @brief 병렬 그룹을 꺼냅니다. 풀이 비어 있으면 힙에서 만듭니다. */
        ParallelGroup* allocateGroup();
        /** @brief 병렬 그룹을 돌려줍니다. 풀의 주소면 풀로, 아니면 힙으로 돌려줍니다. */
        void deallocateGroup( ParallelGroup* pGroup );

        /** @brief 노드를 낸 매니저입니다. 매니저가 사라졌으면(`detachOwner` 뒤) nullptr 입니다. */
        TaskManager* getOwner() const { return _pOwner; }
        /**
         * @brief 매니저가 사라질 때 부릅니다(`shutdown` 뒤, 아무것도 돌지 않을 때). 매니저를 잊고, 아직 참조가 남은 노드 · 스테이지를 셉니다.
         * @return 남은 것이 없으면 true — 소유자가 지금 지웁니다. false 면 소유자는 풀을 놓고(지우지 않고), 마지막 참조가 풀을 소멸시킵니다.
         */
        [[nodiscard]] bool detachOwner();

    private:
        /** @brief 매니저가 사라진 뒤 노드 · 스테이지 하나가 돌아왔습니다. 마지막이면 풀을 소멸시킵니다. */
        void returnDetachedReference();

    private:
        static constexpr uint32 kSlabSize = 64;

        TaskManager*                     _pOwner; ///< 노드를 낸 매니저. `detachOwner` 뒤 nullptr
        ConcurrentQueue<TaskNode*, 4096> _freeQueue;
        vector<TaskNode*>                _listOverflowFree;
        vector<TaskNode*>                _listSlab;
        mutex                            _slabMutex;
        vector<StageNode*>               _listStageFree; ///< 돌아온 스테이지. 다음 createStage 가 먼저 꺼내 쓴다. 빌려 쓰는 것이라 소유하지 않는다
        /**
         * @brief 지금까지 만든 스테이지 전부입니다. **소유합니다.**
         * @details 소유를 타입으로 적어 풀 소멸자에서 지우기를 빠뜨릴 수 없게 합니다(raw 포인터 목록이면 그 누락은 LeakSanitizer 만
         *          잡습니다). 꺼내 쓰는 쪽(`_listStageFree`)은 raw 포인터라 할당 경로에 간접 참조가 늘지 않습니다.
         */
        vector<unique_ptr<StageNode>> _listStageAll;
        mutex                         _stageMutex;
        ParallelGroupPool             _groupPool;
        atomic<uint32>                _detachedReferenceCount; ///< `detachOwner` 뒤에도 참조가 남은 노드 · 스테이지 수
        atomic<bool>                  _bDetached;              ///< `detachOwner` 가 남은 참조를 보고 풀을 그것들에 넘겼다
    };
} // namespace sw
