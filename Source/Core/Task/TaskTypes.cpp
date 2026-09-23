#include "pch.h"

#include "Core/Task/TaskTypes.h"

#include "Core/Task/TaskManager.h"
#include "Core/Task/TaskNode.h"

namespace sw
{
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

    TaskHandle& TaskHandle::precede( const TaskHandle& targetTask )
    {
        TaskNode* pTargetNode = targetTask.getNode();
        if ( _pNode != nullptr && pTargetNode != nullptr && _pNode != pTargetNode )
        {
            pTargetNode->retain();
            _pNode->_successors.push_back( pTargetNode );
            pTargetNode->_unresolvedDependencies.fetch_add( 1, std::memory_order_relaxed );
        }
        return *this;
    }

    TaskHandle& TaskHandle::succeed( TaskHandle dependencyTask )
    {
        dependencyTask.precede( *this );
        return *this;
    }

    TaskHandle TaskHandle::then( const TaskDelegate& nextTaskDelegate, TaskThreadAffinity affinity )
    {
        if ( _pNode == nullptr || _pNode->_pOwner == nullptr )
            return TaskHandle{};

        TaskHandle nextTask = _pNode->_pOwner->emplaceTask( "ChainedTask", nextTaskDelegate, affinity );
        precede( nextTask );
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

    void TaskHandle::submit()
    {
        if ( _pNode == nullptr || _pNode->_pOwner == nullptr )
            return;
        _pNode->_pOwner->submit( *this );
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
            std::scoped_lock<mutex> lock{ _pNode->_listMutex };
            pTaskNode->retain();
            _pNode->_listTask.push_back( pTaskNode );
            pTaskNode->_parentStage = _pNode;
            // 남은 태스크가 생기는 순간 스테이지가 스스로를 쥔다 — 핸들이 먼저 사라져도 완료 통지가 갈 곳이 남는다.
            if ( _pNode->_join.addPending( 1 ) == 0 )
                _pNode->retain();
        }
        return *this;
    }
} // namespace sw
