/**
 * @file TaskNodePool.h
 * @brief TaskManager 의 노드 풀입니다(태스크 슬랩 · 스테이지 · 병렬 그룹). 프레임이 안정된 상태에서는 힙을 건드리지 않게 합니다.
 * @details **공개 API 가 아닙니다.** `Core/Task` 안의 구현 파일만 include 합니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/ConcurrentQueue.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryBlock.h"
#include "Core/Task/TaskNode.h"

namespace sw
{
    /**
     * @class TaskNodePool
     * @brief 태스크 노드는 64개 슬랩으로, 스테이지는 소유 목록 + 자유 목록으로, 병렬 그룹은 lock-free 풀로 재사용합니다.
     * @details 매니저가 소유하지만, 매니저가 사라질 때 핸들이 쥔 노드 · 스테이지가 남아 있으면 그 핸들들의 책임이 됩니다(`detachOwner`).
     *          그 뒤 놓이는 노드 · 스테이지는 남은 수를 내리고, 마지막 것이 풀을 소멸시킵니다. 참조 수를 세는 비용은 매니저가 사라질 때 한 번뿐입니다.
     */
    class TaskNodePool
    {
    public:
        /** @brief @p pOwner 가 소유하는 풀입니다. 노드는 매니저를 이 풀로 찾습니다(`getOwner`). */
        explicit TaskNodePool( TaskManager* pOwner );
        /**
         * @brief 태스크 슬랩을 돌려줍니다. 스테이지는 `_listStageAll` 이 소유하므로 여기서 할 일이 없습니다.
         * @details 슬랩은 `MemoryBlock` 하나에 원소 64개를 한 번에 잡고 placement new 로 만듭니다. 블록은 형식이 풀지만 원소의 소멸자는
         *          블록이 모르므로 여기서 먼저 부릅니다.
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
        /** @brief 강제 정리(`clear`)용입니다. 남은 태스크가 있는 스테이지의 수를 비우고 그 참조를 놓습니다(핸들의 참조는 둔다). 아무것도 돌고 있지 않을 때만 부르십시오. */
        void resetAllStages();

        /** @brief 병렬 그룹을 꺼냅니다. 풀이 비어 있으면 힙에서 만듭니다. */
        ParallelGroup* allocateGroup();
        /** @brief 병렬 그룹을 돌려줍니다. 풀의 주소면 풀로, 아니면 힙으로 돌려줍니다. */
        void deallocateGroup( ParallelGroup* pGroup );

        /**
         * @brief 아직 끝나지 않은 태스크 하나의 이름입니다(없으면 빈 문자열, Shipping 은 이름이 없다). 대기 시간 초과 때 원인을 로그에 적는 진단용입니다.
         * @details 슬랩을 훑어 **제출됐고**(`_bScheduled`) 본문이 안 끝난(`_pendingChildren` > 0) 이름 있는 노드를 찾는다. 만들기만 하고 제출하지 않은 노드는 잡히지 않는다.
         *          이름은 노드의 참조를 하나 잡은 뒤(`TaskNode::tryRetain`)에 읽는다 — 참조가 있는 동안 노드는 풀로 돌아가 이름이 지워지거나 새 태스크로 쓰이지 않고,
         *          제출 뒤에는 이름이 바뀌지 않으므로 이름 읽기에 데이터 레이스가 없다. 오류 경로에서만 부른다.
         */
        [[nodiscard]] string describeFirstUnfinished();

        /** @brief 노드를 낸 매니저입니다. 매니저가 사라졌으면(`detachOwner` 뒤) nullptr 입니다. */
        TaskManager* getOwner() const { return _pOwner; }
        /**
         * @brief 매니저가 사라질 때 부릅니다(`shutdown` 뒤, 아무것도 돌지 않을 때). 매니저를 잊고, 아직 참조가 남은 노드 · 스테이지를 셉니다.
         * @return 남은 것이 없으면 true — 소유자가 지금 지웁니다. false 면 소유자는 풀을 놓고(지우지 않고), 마지막 참조가 풀을 소멸시킵니다.
         */
        [[nodiscard]] bool detachOwner();

        /**
         * @brief 의존 간선을 거는 일(사이클 검사 + 후속 목록에 붙이기)을 한 번에 하나씩만 하게 하는 잠금입니다.
         * @details 두 스레드가 `A→B` 와 `B→A` 를 동시에 걸면 각자 검사할 때 상대 간선이 아직 없어 둘 다 통과합니다. 검사와 붙이기를
         *          같은 잠금 안에 두어야 사이클 검사가 정확합니다. 간선을 거는 일은 드물어(만드는 단계) 경합 비용은 무시할 만합니다.
         *          풀마다 하나라서 전역 상태가 아니고, 매니저가 사라진 뒤에도(핸들이 노드를 쥔 동안) 풀과 함께 삽니다.
         *          잠금 안에서는 후속 목록의 스핀락을 한 번에 하나만 잡습니다(그 반대로 이 잠금을 잡는 곳은 없다 — 순서 역전 없음).
         */
        mutex& getDependencyMutex() { return _dependencyMutex; }

    private:
        /** @brief 매니저가 사라진 뒤 노드 · 스테이지 하나가 돌아왔습니다. 마지막이면 풀을 소멸시킵니다. */
        void returnDetachedReference();

    private:
        static constexpr uint32 kSlabSize = 64;

        TaskManager*                        _pOwner; ///< 노드를 낸 매니저. `detachOwner` 뒤 nullptr
        ConcurrentQueue<TaskNode*, 4096>    _freeQueue;
        vector<TaskNode*> _listOverflowFree SW_GUARDED_BY( _slabMutex );
        vector<MemoryBlock> _listSlab       SW_GUARDED_BY( _slabMutex ); ///< 64개 슬랩의 바이트 블록. **소유한다**(노드의 소멸은 `~TaskNodePool`)
        mutex                               _slabMutex;
        vector<StageNode*> _listStageFree   SW_GUARDED_BY( _stageMutex ); ///< 돌아온 스테이지. 다음 createStage 가 먼저 꺼내 쓴다. 빌려 쓰는 것이라 소유하지 않는다
        /**
         * @brief 지금까지 만든 스테이지 전부입니다. **소유합니다.**
         * @details 소유를 타입으로 적어 풀 소멸자에서 지우기를 빠뜨릴 수 없게 합니다(raw 포인터 목록이면 그 누락은 LeakSanitizer 만
         *          잡습니다). 꺼내 쓰는 쪽(`_listStageFree`)은 raw 포인터라 할당 경로에 간접 참조가 늘지 않습니다.
         */
        vector<unique_ptr<StageNode>> _listStageAll SW_GUARDED_BY( _stageMutex );
        mutex                                       _stageMutex;
        ParallelGroupPool                           _groupPool;
        atomic<uint32>                              _detachedReferenceCount; ///< `detachOwner` 뒤에도 참조가 남은 노드 · 스테이지 수
        atomic<bool>                                _bDetached;              ///< `detachOwner` 가 남은 참조를 보고 풀을 그것들에 넘겼다
        mutex                                       _dependencyMutex;        ///< 의존 간선 검사 + 붙이기를 직렬화한다(`getDependencyMutex`)
    };
} // namespace sw
