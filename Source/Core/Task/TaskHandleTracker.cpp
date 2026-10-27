#include "pch.h"

#include "Core/Task/TaskHandleTracker.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Task/TaskManager.h"
#include "Core/Task/TaskNode.h"
#include "Core/Task/TaskNodePool.h"
#include "Core/Time/MonotonicClock.h"

#include <chrono>
#include <thread>

namespace sw
{
    void TaskHandleTracker::track( const TaskHandle& handle )
    {
        ScopedLock<mutex> lock{ _mutex };
        // 올릴 때마다 훑으면 요청이 수천 개 쌓일 때 제곱으로 든다. 문턱을 넘을 때만 끝난 것을 버리고, 문턱은 남은 수의 두 배로 다시 잡는다.
        if ( _listHandle.size() >= _pruneThreshold )
        {
            VectorUtil::removeIf( _listHandle, []( const TaskHandle& tracked )
            { return tracked.isCompleted(); } );
            constexpr size_t kMinThreshold = 64;
            _pruneThreshold                = _listHandle.size() * 2 > kMinThreshold ? _listHandle.size() * 2 : kMinThreshold;
        }
        _listHandle.push_back( handle );
    }

    uint32 TaskHandleTracker::getPendingCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        uint32            count{ 0 };
        for ( const TaskHandle& tracked : _listHandle )
        {
            if ( tracked.isCompleted() == false )
                ++count;
        }
        return count;
    }

    string TaskHandleTracker::describeFirstPending() const
    {
        ScopedLock<mutex> lock{ _mutex };
        for ( const TaskHandle& tracked : _listHandle )
        {
            if ( tracked.isCompleted() )
                continue;
#if !defined( SW_SHIPPING )
            if ( const TaskNode* pNode = tracked.getNode() )
                return string( pNode->_arrName );
#endif
            return string( "(unnamed)" );
        }
        return string();
    }

    TaskManager* TaskHandleTracker::findPendingOwner() const
    {
        ScopedLock<mutex> lock{ _mutex };
        for ( const TaskHandle& tracked : _listHandle )
        {
            if ( tracked.isCompleted() )
                continue;
            const TaskNode* pNode = tracked.getNode();
            if ( pNode != nullptr && pNode->_pPool != nullptr )
                return pNode->_pPool->getOwner();
        }
        return nullptr;
    }

    bool TaskHandleTracker::waitIdle( uint32 timeoutMs, string_view owner )
    {
        constexpr uint32 kPollMilliseconds = 1; // 종료 · 취소 때 한 번 도는 대기라 깨우기 신호를 따로 두지 않고 짧게 끊어 본다
        const Stopwatch  stopwatch;
        while ( getPendingCount() > 0 )
        {
            // 기다리는 스레드가 메인이면 메인 전용 큐를 비워 준다: 올린 태스크가 `MainThread` 친화도여도 메인이 여기서 자고 있어 영영 못 도는 교착이 없다.
            // 메인이 아닌 스레드는 메인 큐를 돕지 못하므로(메인이 다른 곳에서 틱을 돌고 있어야 한다) 시간 제한이 마지막 안전망이다.
            if ( TaskManager* pManager = findPendingOwner(); pManager != nullptr && pManager->isMainThread() )
                pManager->dispatchMainThreadTasks();

            if ( stopwatch.getElapsedMilliseconds() >= static_cast<int64>( timeoutMs ) )
            {
                const uint32 pending = getPendingCount();
                if ( pending == 0 )
                    return true;
                SW_LOG_ERROR( "%# waited %# ms for its own tasks but %# did not finish (first pending: '%#') - continuing without them", owner, timeoutMs, pending, describeFirstPending() );
                return false;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( kPollMilliseconds ) );
        }
        return true;
    }
} // namespace sw
