/**
 * @file DedicatedWorker.h
 * @brief 이름 있는 전용 스레드 하나가 받은 순서대로 일을 처리하는 작업 큐입니다. 디스크 · 소켓처럼 기다리는 일을 TaskManager 워커 밖에서 돌립니다.
 * @details 일은 막히는 꺼내기(조건 변수)로 받아 `start` 에 넘긴 처리 함수를 그 스레드에서 부릅니다. 넣는 쪽은 막히지 않습니다(뮤텍스 한 번).
 *          정지 계약: `stop` 은 새 일을 받지 않게 하고, 이미 받은 일은 **버리지 않고** 모두 처리 함수에 넘긴 뒤(`bStopping` 이 참) 스레드를 합류합니다.
 *          받은 일을 정지 때 버릴지(읽기)와 끝까지 할지(쓰기)는 처리 함수가 `bStopping` 을 보고 정합니다 — 큐가 일을 말없이 잃지 않습니다.
 *          소유자는 자기 `shutdown` · 소멸자에서 `stop` 을 부릅니다(처리 함수가 소유자를 부르므로, 소유자가 사라진 뒤에 돌면 안 된다 — 소멸자가 단언한다).
 *          언리얼 `FRunnable` + `TQueue` 의 전용 스레드, 유니티 `Thread` + `BlockingCollection` 에 해당합니다.
 */
#pragma once
#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/deque.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Process/ThreadCrashStack.h"

#include <condition_variable>

namespace sw
{
    /**
     * @class DedicatedWorker
     * @tparam TJob 일 하나(이동 가능)
     */
    template <typename TJob>
    class DedicatedWorker
    {
    public:
        /** @brief 일 하나를 처리합니다. @p bStopping 이면 `stop` 이 불린 뒤 꺼낸 일입니다(버릴지 할지는 처리 쪽이 정한다). */
        using ProcessDelegate = Delegate<void( TJob& job, bool bStopping )>;

        DedicatedWorker()
            : _listQueuedJob{}
            , _process{}
            , _thread{}
            , _mutex{}
            , _jobReady{}
            , _pThreadName{ nullptr }
            , _bAccepting{ SW_FALSE }
        {
        }

        /** @brief 소유자가 `stop` 을 부르지 않았으면 단언합니다(처리 함수가 이미 사라진 소유자를 부를 수 있다). */
        ~DedicatedWorker() { SW_ASSERT( _thread.joinable() == false ); }

        DedicatedWorker( const DedicatedWorker& )            = delete;
        DedicatedWorker& operator=( const DedicatedWorker& ) = delete;

        /**
         * @brief @p pThreadName 이라는 스레드를 띄워 @p process 로 일을 처리합니다. 한 번만 부릅니다.
         * @param pThreadName 디버거 · 프로파일러에 보일 이름. 문자열 리터럴처럼 이 객체보다 오래 사는 글이어야 합니다.
         */
        void start( const utf8* pThreadName, const ProcessDelegate& process )
        {
            SW_ASSERT( _thread.joinable() == false && process.isBound() );
            _process     = process;
            _pThreadName = pThreadName;
            {
                ScopedLock<mutex> lock{ _mutex };
                _bAccepting = SW_TRUE;
            }
            _thread = sw::thread( &DedicatedWorker::run, this );
        }

        /**
         * @brief 일을 넣습니다. 시작 전이거나 `stop` 이 불린 뒤면 false 이고 @p job 은 옮겨지지 않습니다(부르는 쪽이 실패로 끝낸다).
         */
        [[nodiscard]] bool submit( TJob&& job )
        {
            {
                ScopedLock<mutex> lock{ _mutex };
                if ( _bAccepting == SW_FALSE )
                    return false;
                _listQueuedJob.push_back( std::move( job ) );
            }
            _jobReady.notify_one();
            return true;
        }

        /** @brief 새 일을 막고, 남은 일을 모두 `bStopping` 으로 처리한 뒤 스레드를 합류합니다. 두 번 불러도 됩니다. */
        void stop()
        {
            {
                ScopedLock<mutex> lock{ _mutex };
                _bAccepting = SW_FALSE;
            }
            _jobReady.notify_all();
            if ( _thread.joinable() )
                _thread.join();
        }

        /** @brief `start` 뒤 `stop` 전이면 true 입니다. 처음 일이 올 때 스레드를 띄우는 쪽이 봅니다(`start` · `stop` 과 같은 순서로 부른다). */
        bool isRunning() const { return _thread.joinable(); }

        /** @brief 큐에서 기다리는 일의 수입니다(처리 중인 일은 세지 않는다). */
        uint32 getQueuedCount() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return static_cast<uint32>( _listQueuedJob.size() );
        }

    private:
        void run()
        {
            ThreadCrashStack::initializeCurrentThread();
            ThreadName::setCurrentThreadName( _pThreadName );
            for ( ;; )
            {
                TJob job;
                bool bStopping = false;
                {
                    UniqueLock<mutex> lock{ _mutex };
                    while ( _listQueuedJob.empty() && _bAccepting == SW_TRUE )
                    {
                        _jobReady.wait( lock );
                    }
                    if ( _listQueuedJob.empty() )
                        return; // 멈추는 중이고 남은 일이 없다
                    job = std::move( _listQueuedJob.front() );
                    _listQueuedJob.pop_front();
                    bStopping = _bAccepting == SW_FALSE;
                }
                _process( job, bStopping );
            }
        }

        deque<TJob>                 _listQueuedJob;
        ProcessDelegate             _process;
        sw::thread                  _thread;
        mutable mutex               _mutex;
        std::condition_variable_any _jobReady;
        const utf8*                 _pThreadName;
        uint8                       _bAccepting;
    };
} // namespace sw
