/**
 * @file TestThread.cpp
 * @brief sw::thread — 시작 · 끝 · 합류 대기가 잠금 관찰자에 닿는지, 교착 검출기가 "락을 쥔 채 join" 순환을 잡는지, 로더 락 안의 join 을 막는지,
 *        소멸 · 이동 대입이 정지를 요청하고 합류하는지.
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Diagnostics/DeadlockDetector.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/TestFramework.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
#endif

namespace
{
    /** @brief 테스트 호스트가 걸어 둔 관찰자(교착 검출기, `DiagnosticsSubsystem`)를 이 범위 동안 뗍니다 — 관찰자는 하나뿐이라 그대로면 케이스가 자기 것을 걸지 못한다. */
    struct ScopedHostLockObserverRemoval
    {
        ScopedHostLockObserverRemoval()
            : _pPrevious{ sw::mutex::getLockObserver() }
        {
            if ( _pPrevious != nullptr )
                sw::mutex::unregisterLockObserver( _pPrevious );
        }
        ~ScopedHostLockObserverRemoval()
        {
            if ( _pPrevious != nullptr )
                sw::mutex::registerLockObserver( _pPrevious );
        }
        ScopedHostLockObserverRemoval( const ScopedHostLockObserverRemoval& )            = delete;
        ScopedHostLockObserverRemoval& operator=( const ScopedHostLockObserverRemoval& ) = delete;

        sw::ILockObserver* _pPrevious;
    };

    /** @brief 받은 사건을 차례로 적는 관찰자입니다(스레드 · 종류 · 키). */
    class RecordingLockObserver final : public sw::ILockObserver
    {
    public:
        struct Event
        {
            std::thread::id _threadID;
            void*           _pKey{ nullptr };
            utf8            _kind{ 0 }; ///< 'T' 시도 · 'A' 획득 · 'R' 해제
        };

        void recordLockAttempt( void* pLock ) override { record( 'T', pLock ); }
        void recordLockAcquired( void* pLock ) override { record( 'A', pLock ); }
        void recordLockReleased( void* pLock ) override { record( 'R', pLock ); }

        sw::vector<Event> takeEvents()
        {
            std::scoped_lock<std::mutex> lock{ _mutex };
            return std::move( _listEvent );
        }

    private:
        void record( utf8 kind, void* pKey )
        {
            std::scoped_lock<std::mutex> lock{ _mutex };
            _listEvent.push_back( Event{ std::this_thread::get_id(), pKey, kind } );
        }

        std::mutex        _mutex;
        sw::vector<Event> _listEvent;
    };

    /** @brief @p bFlag 가 서거나 @p timeoutMilliseconds 가 지날 때까지 기다립니다. 섰으면 true 입니다. */
    bool waitForFlag( const sw::atomic<bool>& bFlag, int64 timeoutMilliseconds )
    {
        const sw::Deadline deadline = sw::Deadline::afterMilliseconds( timeoutMilliseconds );
        while ( bFlag.load() == false )
        {
            if ( deadline.isExpired() )
                return false;
            std::this_thread::yield();
        }
        return true;
    }
} // namespace

/**
 * @brief [ThreadTest] 스레드는 자기 합류 키를 잡고 시작해 놓고 끝나며, join 은 같은 키를 기다리는 잠금 시도로 보인다
 */
SW_TEST_CASE( ThreadTest, JoinIsAWaitOnTheThreadsJoinKey )
{
    const ScopedHostLockObserverRemoval hostObserverRemoval;
    RecordingLockObserver               observer;
    sw::mutex::registerLockObserver( &observer );

    std::thread::id workerID;
    sw::thread      worker{ [&workerID]()
    { workerID = std::this_thread::get_id(); } };
    worker.join();
    sw::mutex::unregisterLockObserver( &observer );

    // 그동안 다른 스레드(태스크 워커)가 잡은 mutex 사건도 들어올 수 있다 — 합류 키(홀수)만 본다.
    sw::vector<RecordingLockObserver::Event> listEvent;
    for ( const RecordingLockObserver::Event& event : observer.takeEvents() )
    {
        if ( ( reinterpret_cast<uintptr_t>( event._pKey ) & 1 ) == 1 )
            listEvent.push_back( event );
    }
    SW_ASSERT_EQUAL( size_t( 5 ), listEvent.size() );
    void* const pKey = listEvent[0]._pKey;
    SW_EXPECT_TRUE( ( reinterpret_cast<uintptr_t>( pKey ) & 1 ) == 1 ); // 정렬된 mutex 주소와 겹치지 않는다
    sw::string kinds;
    for ( const RecordingLockObserver::Event& event : listEvent )
    {
        SW_EXPECT_TRUE( event._pKey == pKey );
        kinds += event._kind;
    }
    // 워커의 획득 · 해제, 그리고 합류하는 쪽의 시도 · 획득 · 해제. 워커의 두 사건과 합류 시도의 순서는 스케줄에 달렸다.
    SW_EXPECT_TRUE( std::count( kinds.begin(), kinds.end(), 'T' ) == 1 );
    SW_EXPECT_TRUE( kinds.substr( kinds.size() - 2 ) == "AR" );
    SW_EXPECT_TRUE( listEvent.back()._threadID == std::this_thread::get_id() );
    uint32 workerEventCount = 0;
    for ( const RecordingLockObserver::Event& event : listEvent )
    {
        workerEventCount += event._threadID == workerID ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 2u, workerEventCount );
}

/**
 * @brief [ThreadTest] 락을 쥔 채 그 락을 기다리는 스레드를 join 하면 교착 검출기가 순환으로 잡는다
 * @details 예전에는 대기 그래프가 `mutex` 끼리만이라 이 순환(에디터 종료 교착과 같은 모양)을 못 봤다. 락은 검출기에 직접 적은 가짜 키라 실제로는 막히지 않는다.
 */
SW_TEST_CASE( ThreadTest, JoinWhileHoldingALockTheThreadWaitsForIsADeadlock )
{
    SW_TEST_REQUIRES_FEATURE( DebugAsserts );
    const ScopedHostLockObserverRemoval hostObserverRemoval;
    sw::DeadlockDetector                detector;
    detector.initialize();
    test::ScopedAssertCapture capture;

    void* const      pHeldLock = reinterpret_cast<void*>( 0x4000 );
    sw::atomic<bool> bWorkerWaiting{ false };
    detector.recordLockAcquired( pHeldLock );
    sw::thread worker{ [&detector, &bWorkerWaiting, &capture, pHeldLock]()
    {
        detector.recordLockAttempt( pHeldLock ); // 합류하는 쪽이 쥔 락을 기다린다
        bWorkerWaiting.store( true );
        const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 5000 );
        while ( capture.getCount() == 0 && deadline.isExpired() == false )
        {
            std::this_thread::yield();
        }
    } };
    SW_ASSERT_TRUE( waitForFlag( bWorkerWaiting, 5000 ) );
    worker.join();
    detector.recordLockReleased( pHeldLock );
    detector.shutdown();
    SW_EXPECT_TRUE( capture.getCount() >= 1 );
}

/**
 * @brief [ThreadTest] 소멸자는 정지를 요청하고 합류한다 — 정지 토큰을 받는 스레드는 요청을 보고 끝난다
 * @details `join` 을 빠뜨린 소유자가 `std::terminate` 로 끝나지 않고, 정지 토큰을 보는 루프는 소멸 하나로 멈춘다(`std::jthread` 와 같은 규칙).
 */
SW_TEST_CASE( ThreadTest, DestructorRequestsStopAndJoins )
{
    sw::atomic<bool>  bStarted{ false };
    sw::atomic<bool>  bFinished{ false };
    sw::atomic<int32> loopCount{ 0 };
    {
        sw::thread worker{ [&bStarted, &bFinished, &loopCount]( sw::ThreadStopToken stopToken )
        {
            bStarted.store( true );
            while ( stopToken.stop_requested() == false )
            {
                loopCount.store( loopCount.load() + 1 );
                std::this_thread::yield();
            }
            bFinished.store( true );
        } };
        SW_ASSERT_TRUE( waitForFlag( bStarted, 5000 ) );
        SW_EXPECT_TRUE( worker.get_stop_token().stop_requested() == false );
    }
    // 소멸자가 돌아왔으면 스레드는 끝났다(합류했다)
    SW_EXPECT_TRUE( bFinished.load() );
    SW_EXPECT_TRUE( loopCount.load() >= 1 );
}

/**
 * @brief [ThreadTest] 정지 토큰을 받지 않는 함수도 그대로 돈다. 이동 대입은 지금 스레드를 합류한 뒤 넘겨받는다
 */
SW_TEST_CASE( ThreadTest, MoveAssignJoinsTheCurrentThreadFirst )
{
    sw::atomic<int32> finishedCount{ 0 };
    sw::thread        worker{ [&finishedCount]()
    { finishedCount.store( finishedCount.load() + 1 ); } };
    sw::thread        next{ [&finishedCount]( sw::ThreadStopToken stopToken )
    {
        while ( stopToken.stop_requested() == false )
        {
            std::this_thread::yield();
        }
        finishedCount.store( finishedCount.load() + 10 );
    } };
    worker = std::move( next ); // 첫 스레드를 합류한다(정지 요청은 토큰을 받지 않는 함수에 아무 일도 하지 않는다)
    SW_EXPECT_EQUAL( 1, finishedCount.load() );
    SW_EXPECT_TRUE( worker.joinable() );
    worker.request_stop();
    worker.join();
    SW_EXPECT_EQUAL( 11, finishedCount.load() );
}

#if defined( SW_PLATFORM_WINDOWS )
/**
 * @brief [ThreadTest] 로더 락을 쥔 채 join 하면 단언하고 스레드를 뗀다 — 기다리면 끝나는 스레드도 로더 락을 기다려 영원히 멈춘다
 * @details DLL 을 내릴 때 모듈 정적 소멸자가 스레드를 join 하던 에디터 종료 교착과 같은 조건을 `LdrLockLoaderLock` 으로 만든다.
 */
SW_TEST_CASE( ThreadTest, JoinUnderTheLoaderLockAssertsAndDetaches )
{
    SW_TEST_REQUIRES_FEATURE( DebugAsserts );
    using LockLoaderLockFunction           = LONG( NTAPI* )( ULONG, ULONG*, void** );
    using UnlockLoaderLockFunction         = LONG( NTAPI* )( ULONG, void* );
    const HMODULE                  hNtdll  = GetModuleHandleW( L"ntdll.dll" );
    const LockLoaderLockFunction   pLock   = reinterpret_cast<LockLoaderLockFunction>( reinterpret_cast<void*>( GetProcAddress( hNtdll, "LdrLockLoaderLock" ) ) );
    const UnlockLoaderLockFunction pUnlock = reinterpret_cast<UnlockLoaderLockFunction>( reinterpret_cast<void*>( GetProcAddress( hNtdll, "LdrUnlockLoaderLock" ) ) );
    if ( pLock == nullptr || pUnlock == nullptr )
        SW_TEST_SKIP( "ntdll does not export the loader lock functions" );

    // 뗀 스레드는 이 함수보다 오래 살 수 있다 — 플래그는 스레드와 나눠 갖는다.
    const std::shared_ptr<sw::atomic<bool>> pStarted = std::make_shared<sw::atomic<bool>>( false );
    const std::shared_ptr<sw::atomic<bool>> pRelease = std::make_shared<sw::atomic<bool>>( false );
    sw::thread                              worker{ [pStarted, pRelease]()
    {
        pStarted->store( true );
        while ( pRelease->load() == false )
        {
            std::this_thread::yield();
        }
    } };
    SW_ASSERT_TRUE( waitForFlag( *pStarted, 5000 ) );
    {
        test::ScopedAssertCapture capture;
        void*                     pCookie = nullptr;
        SW_ASSERT_TRUE( pLock( 0, nullptr, &pCookie ) == 0 );
        worker.join();
        (void)pUnlock( 0, pCookie ); // 방금 잡은 락이다 — 놓지 못하면 다음 테스트가 멈춰 드러난다
        SW_EXPECT_EQUAL( 1u, capture.getCount() );
    }
    SW_EXPECT_FALSE( worker.joinable() );
    pRelease->store( true ); // 뗀 스레드가 스스로 끝난다
}
#endif
