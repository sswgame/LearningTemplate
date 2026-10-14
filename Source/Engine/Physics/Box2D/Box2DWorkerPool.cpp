#include "pch.h"

#include "Engine/Physics/Box2D/Box2DWorkerPool.h"

#include "Core/Concurrency/Futex.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/thread.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    namespace
    {
        struct Box2DWorkerPoolInternal
        {
            /**
             * @brief 대기열이 비었을 때 잠들기 전에 다시 볼 시간입니다.
             * @details Box2D 는 한 스텝 안에서 짧은 태스크를 여러 번 넣는다(충돌 → 솔버 → 바디 마무리). 그 사이마다 잠들고 깨면 깨우는 비용이
             *          태스크보다 크다. 스텝과 스텝 사이(프레임 길이)에는 이 시간 뒤 잠든다.
             */
            static constexpr int64 kSpinNanoseconds = 50000;
        };
    } // namespace
} // namespace sw

namespace sw
{
    Box2DWorkerPool::Box2DWorkerPool( uint32 workerCount )
        : _listThread{}
        , _listTask{}
        , _listFreeTask{}
        , _listRange{}
        , _mutex{}
        , _wakeWord{ 0 }
        , _bStopping{ false }
        , _nextRangeIndex{ 0 }
        , _workerCount{ workerCount < 2 ? 2u : workerCount }
    {
        _listThread.reserve( _workerCount - 1 );
        for ( uint32 workerIndex = 1; workerIndex < _workerCount; ++workerIndex )
        {
            _listThread.push_back( sw::thread( &Box2DWorkerPool::runWorker, this, workerIndex ) );
        }
    }

    Box2DWorkerPool::~Box2DWorkerPool()
    {
        _bStopping.store( true, std::memory_order_release );
        _wakeWord.fetch_add( 1, std::memory_order_release );
        Futex::wakeAll( _wakeWord );
        for ( sw::thread& thread : _listThread )
        {
            thread.join();
        }
    }

    void* Box2DWorkerPool::enqueueTask( b2TaskCallback* pTask, int32 itemCount, int32 minRange, void* pTaskContext, void* pUserContext )
    {
        Box2DWorkerPool& pool = *static_cast<Box2DWorkerPool*>( pUserContext );
        return pool.enqueue( pTask, itemCount, minRange, pTaskContext );
    }

    void Box2DWorkerPool::finishTask( void* pUserTask, void* pUserContext )
    {
        Box2DWorkerPool& pool = *static_cast<Box2DWorkerPool*>( pUserContext );
        pool.finish( static_cast<Task*>( pUserTask ) );
    }

    Box2DWorkerPool::Task* Box2DWorkerPool::enqueue( b2TaskCallback* pTask, int32 itemCount, int32 minRange, void* pTaskContext )
    {
        if ( itemCount <= 0 )
            return nullptr; // 할 일이 없다 — nullptr 은 Box2D 에게 "끝났다" 는 뜻이다

        Task* pRecord = nullptr;
        if ( _listFreeTask.empty() )
        {
            _listTask.push_back( make_unique<Task>() );
            pRecord = _listTask.back().get();
        }
        else
        {
            pRecord = _listFreeTask.back();
            _listFreeTask.pop_back();
        }

        // 조각 수: 일꾼 수를 넘지 않고, 한 조각이 minRange 보다 작아지지 않게 나눈다. 솔버 태스크(항목 하나)는 조각 하나다.
        const int32 safeMinRange  = minRange < 1 ? 1 : minRange;
        const int32 maxRangeCount = ( itemCount + safeMinRange - 1 ) / safeMinRange;
        const int32 rangeCount    = maxRangeCount < static_cast<int32>( _workerCount ) ? maxRangeCount : static_cast<int32>( _workerCount );
        const int32 itemsPerRange = itemCount / rangeCount;
        const int32 remainder     = itemCount - itemsPerRange * rangeCount;
        pRecord->_pCallback       = pTask;
        pRecord->_pContext        = pTaskContext;
        pRecord->_remainingRangeCount.store( static_cast<uint32>( rangeCount ), std::memory_order_relaxed );
        {
            std::scoped_lock<mutex> lock{ _mutex };
            int32                   startIndex = 0;
            for ( int32 rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex )
            {
                const int32 endIndex = startIndex + itemsPerRange + ( rangeIndex < remainder ? 1 : 0 );
                _listRange.push_back( Range{ pRecord, startIndex, endIndex } );
                startIndex = endIndex;
            }
            _wakeWord.fetch_add( 1, std::memory_order_release );
        }
        // 조각 수만큼(전용 스레드 수 이하) 깨운다. 스텝 스레드는 finishTask 에서 함께 꺼낸다.
        const uint32 wakeCount = static_cast<uint32>( rangeCount ) < _workerCount - 1 ? static_cast<uint32>( rangeCount ) : _workerCount - 1;
        for ( uint32 wakeIndex = 0; wakeIndex < wakeCount; ++wakeIndex )
        {
            Futex::wakeOne( _wakeWord );
        }
        return pRecord;
    }

    void Box2DWorkerPool::finish( Task* pTask )
    {
        if ( pTask == nullptr )
            return;
        // 스텝 스레드는 일꾼 0 이다. 기다리는 동안 대기열(다른 태스크의 조각 포함)을 함께 비운다.
        while ( pTask->_remainingRangeCount.load( std::memory_order_acquire ) != 0 )
        {
            if ( tryRunOne( 0 ) == false )
                std::this_thread::yield();
        }
        _listFreeTask.push_back( pTask );
    }

    bool Box2DWorkerPool::tryRunOne( uint32 workerIndex )
    {
        Range range;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _nextRangeIndex >= _listRange.size() )
                return false;
            range = _listRange[_nextRangeIndex];
            ++_nextRangeIndex;
            if ( _nextRangeIndex == _listRange.size() )
            {
                // 다 꺼냈으면 처음으로 되돌린다(용량은 남아 다음 스텝이 할당하지 않는다).
                _listRange.clear();
                _nextRangeIndex = 0;
            }
        }
        range._pTask->_pCallback( range._startIndex, range._endIndex, workerIndex, range._pTask->_pContext );
        range._pTask->_remainingRangeCount.fetch_sub( 1, std::memory_order_acq_rel );
        return true;
    }

    void Box2DWorkerPool::runWorker( uint32 workerIndex )
    {
        ThreadName::setCurrentThreadName( "Box2DWorker" );
        while ( _bStopping.load( std::memory_order_acquire ) == false )
        {
            const uint32 seenWake = _wakeWord.load( std::memory_order_acquire );
            if ( tryRunOne( workerIndex ) )
                continue;
            const int64 spinEnd = MonotonicClock::nowNanoseconds() + Box2DWorkerPoolInternal::kSpinNanoseconds;
            bool        bRan    = false;
            while ( MonotonicClock::nowNanoseconds() < spinEnd && _bStopping.load( std::memory_order_acquire ) == false )
            {
                if ( tryRunOne( workerIndex ) )
                {
                    bRan = true;
                    break;
                }
                std::this_thread::yield();
            }
            if ( bRan == false )
                Futex::wait( _wakeWord, seenWake ); // 그 사이 넣었으면 워드가 바뀌어 바로 돌아온다
        }
    }
} // namespace sw
