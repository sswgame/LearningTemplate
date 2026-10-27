#include "pch.h"

#include "Core/Task/TaskTypes.h"

#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Task/TaskManager.h"
#include "Core/Task/TaskNode.h"
#include "Core/Task/TaskNodePool.h"

namespace sw
{
    SW_LOG_CALLER( "TaskHandle" );

    // ------------------------------------------------------------------------------
    // 1) TaskHandle — 침입형 참조 계수 · DAG 연결 · 제출
    // ------------------------------------------------------------------------------
    TaskHandle::TaskHandle( TaskNode* pNode )
        : _pNode{ pNode }
    {
    }

    TaskHandle::TaskHandle( const TaskHandle& other )
        : _pNode{ other._pNode }
    {
        if ( _pNode != nullptr )
            _pNode->retain();
    }

    TaskHandle::TaskHandle( TaskHandle&& other ) noexcept
        : _pNode{ other._pNode }
    {
        other._pNode = nullptr;
    }

    TaskHandle::~TaskHandle()
    {
        if ( _pNode != nullptr )
        {
            _pNode->release();
            _pNode = nullptr;
        }
    }

    TaskHandle& TaskHandle::operator=( const TaskHandle& other )
    {
        if ( this != &other )
        {
            if ( other._pNode != nullptr )
                other._pNode->retain();
            if ( _pNode != nullptr )
                _pNode->release();
            _pNode = other._pNode;
        }
        return *this;
    }

    TaskHandle& TaskHandle::operator=( TaskHandle&& other ) noexcept
    {
        if ( this != &other )
        {
            if ( _pNode != nullptr )
                _pNode->release();
            _pNode       = other._pNode;
            other._pNode = nullptr;
        }
        return *this;
    }

    TaskHandle& TaskHandle::setPriority( TaskPriority priority )
    {
        if ( _pNode != nullptr )
            _pNode->_priority = priority;
        return *this;
    }

    TaskPriority TaskHandle::getPriority() const
    {
        return _pNode != nullptr ? _pNode->_priority : TaskPriority::Normal;
    }

    namespace
    {
        /** @brief 노드가 본문과 자식까지 모두 끝났는지입니다(`TaskHandle::isCompleted` 와 같은 기준). 끝난 노드는 더 기다리게 만들 수 없습니다. */
        bool isNodeCompleted( const TaskNode* pNode )
        {
            return pNode->_pendingChildren.load( std::memory_order_acquire ) == 0;
        }

        /** @brief 로그에 쓸 노드 이름입니다. Shipping 에는 이름이 없습니다. */
        const utf8* describeNode( [[maybe_unused]] const TaskNode* pNode )
        {
#if !defined( SW_SHIPPING )
            if ( pNode->_arrName[0] != '\0' )
                return pNode->_arrName;
#endif
            return "(unnamed)";
        }

        /**
         * @brief `pFrom` 에서 후속 목록을 따라가면 `pTarget` 에 닿는지 깊이 우선으로 찾습니다.
         * @details 방문 집합이 있어 다이아몬드가 겹쳐도 노드마다 한 번만 봅니다(O(노드 + 간선)). 이미 끝난 후속은 따라가지 않습니다 —
         *          후속이 끝났다면 그 선행(`pFrom` 쪽)도 끝나 있어, 그 길로는 `pTarget` 을 기다리게 만드는 일이 없습니다.
         *          순회하는 노드는 모두 살아 있습니다: `pFrom` 은 부르는 쪽의 핸들이 쥐고, 그 후속은 선행의 목록이 참조를 쥐며(`runBefore` 의
         *          `retain`), 목록은 노드가 풀로 돌아갈 때만 비워집니다. 이 그래프는 사이클이 없다는 불변식이 지켜지므로(간선을 걸 때마다 이 검사를
         *          거친다) `pFrom` 으로 되돌아오는 길은 없고, 그래서 `pFrom` 을 방문 집합에 넣지 않는다.
         *          흔한 경우(`pFrom` 이 방금 만든 후속 — 후속이 없다)는 힙 할당 없이 끝난다.
         */
        bool canReachThroughSuccessors( TaskNode* pFrom, const TaskNode* pTarget )
        {
            vector<TaskNode*>        listPending;
            vector<TaskNode*>        listSuccessor;
            unordered_set<TaskNode*> uniqueSetVisited;

            TaskNode* pNode = pFrom;
            for ( ;; )
            {
                listSuccessor.clear();
                pNode->_successors.appendTo( listSuccessor );
                for ( TaskNode* pSuccessor : listSuccessor )
                {
                    if ( pSuccessor == pTarget )
                        return true;
                    if ( isNodeCompleted( pSuccessor ) )
                        continue;
                    if ( uniqueSetVisited.insert( pSuccessor ).second )
                        listPending.push_back( pSuccessor );
                }
                if ( listPending.empty() )
                    return false;
                pNode = listPending.back();
                listPending.pop_back();
            }
        }

        /**
         * @brief `pNode` 가 `pAncestor` 의 (손자 포함) 자식이면 true 입니다 — 본문 안에서 만든 태스크 사슬을 `_pParent` 로 거슬러 오릅니다.
         * @details 부모는 자식이 모두 끝나야 완료되므로 "자식 → 부모" 가 후속 목록에 없는 숨은 간선입니다. `부모.runBefore( 자식 )` 은 자식이 부모의 완료를 기다리는데
         *          부모는 그 자식의 완료를 기다리는 사이클이라 걸면 `waitAll` 이 멈춥니다. 후속 목록으로는 보이지 않아 이 사슬을 따로 봅니다.
         *          사슬 노드는 자식이 참조를 쥐고 있어(`createTaskNode` 의 `retain`) 자식이 끝나지 않은 동안 살아 있다. 부르는 쪽이 `pNode` 가 아직 끝나지 않았음을 확인한 뒤에 부른다.
         *          (완료 경로가 `_pParent` 를 비우는 순간과 겹치면 사슬이 거기서 끊기고 "아니다" 로 답한다 — 끝난 노드는 새 간선이 기다림을 만들지 못하므로 맞는 답이다.)
         */
        bool isDescendantOf( const TaskNode* pNode, const TaskNode* pAncestor )
        {
            for ( const TaskNode* pParent = pNode->_pParent.load( std::memory_order_acquire ); pParent != nullptr; pParent = pParent->_pParent.load( std::memory_order_acquire ) )
            {
                if ( pParent == pAncestor )
                    return true;
            }
            return false;
        }
    } // namespace

    bool TaskHandle::runBefore( const TaskHandle& targetTask )
    {
        TaskNode* const pTargetNode = targetTask.getNode();
        if ( _pNode == nullptr || pTargetNode == nullptr )
            return false;

        // 간선은 한 풀 안에서만 건다. 사이클 검사와 걸기를 지키는 잠금이 풀마다 하나라서, 서로 다른 풀의 태스크끼리 반대로 거는 두 스레드는 서로의 간선을 못 보고
        // 둘 다 통과한다. 그 동시 정확성을 보증할 수 없으므로 거부한다(한 매니저는 풀이 하나라 다른 매니저의 태스크끼리 거는 경우뿐이다).
        if ( _pNode->_pPool != pTargetNode->_pPool )
        {
            SW_LOG_ERROR_ID( "Task.DependencyCrossPool", "Task '%#' cannot run before '%#': they belong to different task managers, so the dependency was not added.", describeNode( _pNode ),
                             describeNode( pTargetNode ) );
            return false;
        }

        bool bCycle{ false };
        bool bAttached{ false };
        {
            // 검사와 붙이기를 한 잠금 안에서 한다 — 동시에 걸린 `A→B` 와 `B→A` 가 서로를 못 보고 둘 다 통과하지 않게.
            ScopedLock<mutex> lock{ _pNode->_pPool->getDependencyMutex() };

            // 사이클: 자기 자신이거나, 후속에서 후속 목록을 따라 이 태스크에 닿는다. 둘 중 하나가 이미 끝났다면 새 간선이 기다림을
            // 만들지 못하므로(끝난 선행은 목록이 닫혀 간선이 붙지 않고, 끝난 후속은 다시 돌지 않는다) 사이클이 아니다.
            const bool bSelf              = _pNode == pTargetNode;
            const bool bBothPending       = isNodeCompleted( _pNode ) == false && isNodeCompleted( pTargetNode ) == false;
            const bool bTargetReachesThis = bSelf == false && bBothPending && canReachThroughSuccessors( pTargetNode, _pNode );
            // 대상이 이 태스크의 본문 안에서 만든 자식이면, 이 태스크는 대상의 완료를 기다리는데 대상은 이 태스크의 완료를 기다리게 된다.
            const bool bTargetIsDescendant = bSelf == false && bBothPending && isDescendantOf( pTargetNode, _pNode );
            bCycle                         = bSelf || bTargetReachesThis || bTargetIsDescendant;
            if ( bCycle == false )
            {
                // 의존 수를 **먼저** 올린다. 목록에 붙인 뒤에 올리면, 그 사이에 이 태스크가 끝나 수를 내려 대상이 너무 일찍 돌 수 있다.
                pTargetNode->retain();
                pTargetNode->_unresolvedDependencies.fetch_add( 1, std::memory_order_acq_rel );
                bAttached = _pNode->_successors.tryPushBack( pTargetNode );
            }
        }

        if ( bCycle )
        {
            // 간선을 걸지 않는다 — 걸면 대상의 의존 수가 영영 0 이 되지 않아 `waitAll` 이 멈춘다. 호출하는 쪽이 반환값으로도 안다.
            SW_LOG_ERROR_ID( "Task.DependencyCycle", "Task '%#' cannot run before '%#': the dependency would form a cycle, so it was not added.", describeNode( _pNode ), describeNode( pTargetNode ) );
            return false;
        }

        if ( bAttached == false )
        {
            // 이 태스크는 이미 끝났다 — 의존은 이미 채워졌다. 올린 수를 되돌린다(대상이 이것만 기다렸다면 여기서 준비된다).
            pTargetNode->resolveOneDependency();
            pTargetNode->release();
        }
        return true;
    }

    bool TaskHandle::runAfter( TaskHandle dependencyTask )
    {
        return dependencyTask.runBefore( *this );
    }

    TaskHandle TaskHandle::then( const TaskDelegate& nextTaskDelegate, TaskThreadAffinity affinity )
    {
        TaskManager* const pOwner = _pNode != nullptr ? _pNode->_pPool->getOwner() : nullptr;
        if ( pOwner == nullptr )
            return TaskHandle{};

        // 후속은 방금 만든 노드라 사이클이 될 수 없다. 걸리지 않는 경우는 후속이 비었을 때(매니저가 멈추는 중)뿐이다.
        TaskHandle nextTask = pOwner->emplaceTask( "ChainedTask", nextTaskDelegate, affinity );
        if ( runBefore( nextTask ) == false )
            return TaskHandle{};
        return nextTask;
    }

    bool TaskHandle::cancel()
    {
        if ( _pNode != nullptr )
        {
            _pNode->_bCancelled.store( true, std::memory_order_release );
            return true;
        }
        return false;
    }

    bool TaskHandle::isCancelled() const
    {
        return _pNode != nullptr && _pNode->_bCancelled.load( std::memory_order_acquire );
    }

    bool TaskHandle::isCompleted() const
    {
        // 본문 분(1)과 자식 수를 함께 세는 카운터가 0 이면 끝났다. 따로 상태를 적지 않는다 — 완료 경로에 저장이 늘지 않는다.
        // 핸들이 참조를 잡고 있으므로 노드가 풀로 돌아가 다시 1 이 되는 일은 없다.
        return _pNode == nullptr || _pNode->_pendingChildren.load( std::memory_order_acquire ) == 0;
    }

    void TaskHandle::submit()
    {
        TaskManager* const pOwner = _pNode != nullptr ? _pNode->_pPool->getOwner() : nullptr;
        if ( pOwner == nullptr )
            return;
        pOwner->submit( *this );
    }

    // ------------------------------------------------------------------------------
    // 2) TaskStageHandle — 침입형 참조 계수 · 태스크 묶기
    // ------------------------------------------------------------------------------
    TaskStageHandle::TaskStageHandle( const TaskStageHandle& other )
        : _pNode{ other._pNode }
    {
        if ( _pNode != nullptr )
            _pNode->retain();
    }

    TaskStageHandle::TaskStageHandle( TaskStageHandle&& other ) noexcept
        : _pNode{ other._pNode }
    {
        other._pNode = nullptr;
    }

    TaskStageHandle& TaskStageHandle::operator=( const TaskStageHandle& other )
    {
        if ( this == &other )
            return *this;
        if ( other._pNode != nullptr )
            other._pNode->retain();
        if ( _pNode != nullptr )
            _pNode->release();
        _pNode = other._pNode;
        return *this;
    }

    TaskStageHandle& TaskStageHandle::operator=( TaskStageHandle&& other ) noexcept
    {
        if ( this == &other )
            return *this;
        if ( _pNode != nullptr )
            _pNode->release();
        _pNode       = other._pNode;
        other._pNode = nullptr;
        return *this;
    }

    TaskStageHandle::~TaskStageHandle()
    {
        if ( _pNode != nullptr )
            _pNode->release();
    }

    TaskStageHandle& TaskStageHandle::addTask( const TaskHandle& task )
    {
        TaskNode* pTaskNode = task.getNode();
        if ( _pNode != nullptr && pTaskNode != nullptr )
        {
            // 스테이지는 태스크를 붙들지 않는다. 완료할 때 찾아올 곳만 적어 둔다(제출 전이라 아직 아무도 이 값을 읽지 않는다).
            pTaskNode->_parentStage = _pNode;
            // 남은 태스크가 생기는 순간 스테이지가 스스로를 잡는다. 핸들이 먼저 사라져도 완료 통지가 갈 곳이 남는다.
            if ( _pNode->_join.addPending( 1 ) == 0 )
                _pNode->retain();
        }
        return *this;
    }
} // namespace sw
