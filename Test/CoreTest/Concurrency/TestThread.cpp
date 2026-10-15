/**
 * @file TestThread.cpp
 * @brief sw::thread — 시작 · 끝 · 합류 대기가 잠금 관찰자에 닿는지, 교착 검출기가 "락을 쥔 채 join" 순환을 잡는지, 로더 락 안의 join 을 막는지.
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
    RecordingLockObserver observer;
    if ( sw::mutex::getLockObserver() != nullptr )
        SW_TEST_SKIP( "another lock observer is registered" );
    sw::mutex::registerLockObserver( &observer );

    std::thread::id workerID;
    sw::thread      worker{ [&workerID]()
    { workerID = std::this_thread::get_id(); } };
    worker.join();
    sw::mutex::unregisterLockObserver( &observer );

    const sw::vector<RecordingLockObserver::Event> listEvent = observer.takeEvents();
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
    if ( sw::mutex::getLockObserver() != nullptr )
        SW_TEST_SKIP( "another lock observer is registered" );
    sw::DeadlockDetector detector;
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
