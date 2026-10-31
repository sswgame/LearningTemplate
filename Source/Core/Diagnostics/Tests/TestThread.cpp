// sw::thread — 시작 · 끝 · 합류 대기가 잠금 관찰자에 닿는지, 교착 검출기가 "락을 쥔 채 join" 순환을 잡는지, 로더 락 안의 join 을 막는지,
// 소멸 · 이동 대입이 정지를 요청하고 합류하는지, 정지 토큰을 첫 · 마지막 인자로 넘기는지.
#include "pch.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/StopToken.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/DeadlockDetector.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Foundation/PlatformOSHeaders.h"
#endif

using namespace sw;

namespace
{
    /**
     * @brief 엔진이 걸어 둔 관찰자(Debug 의 교착 검출기, `DiagnosticsSubsystem`)를 이 범위 동안 뗍니다 — 관찰자는 하나뿐이라 그대로면 케이스가 자기 것을 걸지 못한다.
     * @details 떼어 둔 동안의 합류 사건은 엔진 검출기가 못 본다. `mutex` 는 `SW_ENABLE_DEADLOCK_DETECTION` 빌드에서만 알리므로 보통은 드문 스레드 시작 · 끝뿐이다.
     */
    struct ScopedHostLockObserverRemoval
    {
        ScopedHostLockObserverRemoval()
            : _pPrevious{ mutex::getLockObserver() }
        {
            if ( _pPrevious != nullptr )
                mutex::unregisterLockObserver( _pPrevious );
        }
        ~ScopedHostLockObserverRemoval()
        {
            if ( _pPrevious != nullptr )
                mutex::registerLockObserver( _pPrevious );
        }
        ScopedHostLockObserverRemoval( const ScopedHostLockObserverRemoval& )            = delete;
        ScopedHostLockObserverRemoval& operator=( const ScopedHostLockObserverRemoval& ) = delete;

        ILockObserver* _pPrevious;
    };

    /** @brief 받은 사건을 차례로 적는 관찰자입니다(스레드 · 종류 · 키). */
    class RecordingLockObserver final : public ILockObserver
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

        vector<Event> takeEvents()
        {
            ScopedLock<mutex> lock{ _mutex };
            return std::move( _listEvent );
        }

    private:
        void record( utf8 kind, void* pKey )
        {
            ScopedLock<mutex> lock{ _mutex };
            _listEvent.push_back( Event{ std::this_thread::get_id(), pKey, kind } );
        }

        mutex                    _mutex;
        vector<Event> _listEvent SW_GUARDED_BY( _mutex );
    };

    /** @brief @p bFlag 가 서거나 @p timeoutMilliseconds 가 지날 때까지 기다립니다. 섰으면 true 입니다. */
    bool waitForThreadTestFlag( const atomic<bool>& bFlag, int64 timeoutMilliseconds )
    {
        const Deadline deadline = Deadline::afterMilliseconds( timeoutMilliseconds );
        while ( bFlag.load() == false )
        {
            if ( deadline.isExpired() )
                return false;
            ThreadUtil::yield();
        }
        return true;
    }

    /** @brief 멤버 함수가 정지 토큰을 마지막 인자로 받는 소유자입니다(`thread( &X::run, this, … )`). */
    struct StopTokenLastArgumentOwner
    {
        atomic<int32> _loopCount{ 0 };
        atomic<int32> _seenValue{ 0 };

        void run( int32 value, StopToken stopToken )
        {
            _seenValue.store( value );
            while ( stopToken.isStopRequested() == false )
            {
                _loopCount.fetch_add( 1 );
                ThreadUtil::yield();
            }
        }
    };
} // namespace

/**
 * @brief 스레드는 자기 합류 키를 잡고 시작해 놓고 끝나며, join 은 같은 키를 기다리는 잠금 시도로 보인다.
 */
SW_TEST_CASE( ThreadTest, JoinIsAWaitOnTheThreadsJoinKey )
{
    const ScopedHostLockObserverRemoval hostObserverRemoval;
    RecordingLockObserver               observer;
    mutex::registerLockObserver( &observer );
    SW_REQUIRE_TRUE( mutex::getLockObserver() == &observer );

    std::thread::id workerID;
    thread          worker{ [&workerID]()
    { workerID = std::this_thread::get_id(); } };
    worker.join();
    mutex::unregisterLockObserver( &observer );

    // 그동안 다른 스레드(태스크 워커)의 사건도 들어올 수 있다 — 워커가 스스로 알린 키(합류 키)만 본다.
    const vector<RecordingLockObserver::Event> listAll = observer.takeEvents();
    void*                                      pKey    = nullptr;
    for ( const RecordingLockObserver::Event& event : listAll )
    {
        if ( event._threadID == workerID )
        {
            pKey = event._pKey;
            break;
        }
    }
    SW_REQUIRE_NOT_NULL( pKey );
    SW_EXPECT_TRUE( ( reinterpret_cast<uintptr_t>( pKey ) & 1 ) == 1 ); // 홀수 — 정렬된 mutex 주소와 겹치지 않는다

    string          kinds;
    uint32          workerEventCount = 0;
    std::thread::id lastThreadID;
    for ( const RecordingLockObserver::Event& event : listAll )
    {
        if ( event._pKey != pKey )
            continue;
        kinds += event._kind;
        workerEventCount += event._threadID == workerID ? 1 : 0;
        lastThreadID = event._threadID;
    }
    // 워커의 획득 · 해제, 그리고 합류하는 쪽의 시도 · 획득 · 해제. 워커의 두 사건과 합류 시도의 순서는 스케줄에 달렸다.
    SW_REQUIRE_EQUAL( size_t( 5 ), kinds.size() );
    uint32 attemptCount = 0;
    for ( const utf8 kind : kinds )
        attemptCount += kind == 'T' ? 1 : 0;
    SW_EXPECT_EQUAL( 1u, attemptCount );
    SW_EXPECT_TRUE( kinds.compare( kinds.size() - 2, 2, "AR" ) == 0 );
    SW_EXPECT_TRUE( lastThreadID == std::this_thread::get_id() );
    SW_EXPECT_EQUAL( 2u, workerEventCount );
}

#if defined( SW_DEBUG )
/**
 * @brief 락을 쥔 채 그 락을 기다리는 스레드를 join 하면 교착 검출기가 순환으로 잡는다(오류 로그 + 단언 — 단언은 가로채기로 센다).
 * @details 락은 검출기에 직접 적은 가짜 키라 실제로는 막히지 않는다. 워커는 단언이 세어질 때까지 기다려, 합류하는 쪽의 시도가 순환을 닫는 순간 워커가 아직 기다리는 중이다.
 *          Debug 만 — 단언이 없는 구성에서는 워커가 기다릴 신호가 없다.
 */
SW_TEST_CASE( ThreadTest, JoinWhileHoldingALockTheThreadWaitsForIsADeadlock )
{
    SW_EXPECT_LOG( "Deadlock.Detected" );
    const ScopedHostLockObserverRemoval hostObserverRemoval;
    DeadlockDetector                    detector;
    detector.initialize();
    SW_REQUIRE_TRUE( mutex::getLockObserver() == &detector );
    test::ScopedAssertCapture capture;

    void* const  pHeldLock = reinterpret_cast<void*>( 0x4000 );
    atomic<bool> bWorkerWaiting{ false };
    detector.recordLockAcquired( pHeldLock );
    thread     worker{ [&detector, &bWorkerWaiting, &capture, pHeldLock]()
    {
        detector.recordLockAttempt( pHeldLock ); // 합류하는 쪽이 쥔 락을 기다린다
        bWorkerWaiting.store( true );
        const Deadline deadline = Deadline::afterMilliseconds( 5000 );
        while ( capture.getCount() == 0 && deadline.isExpired() == false )
            ThreadUtil::yield();
    } };
    const bool bWaiting = waitForThreadTestFlag( bWorkerWaiting, 5000 );
    SW_EXPECT_TRUE( bWaiting );
    worker.join();
    detector.recordLockReleased( pHeldLock );
    detector.shutdown();
    SW_EXPECT_TRUE( capture.getCount() >= 1 );
}
#endif

/**
 * @brief 소멸자는 정지를 요청하고 합류한다 — 정지 토큰을 받는 스레드는 요청을 보고 끝난다(`std::jthread` 와 같은 규칙).
 */
SW_TEST_CASE( ThreadTest, DestructorRequestsStopAndJoins )
{
    atomic<bool>  bStarted{ false };
    atomic<bool>  bFinished{ false };
    atomic<int32> loopCount{ 0 };
    {
        thread worker{ [&bStarted, &bFinished, &loopCount]( StopToken stopToken )
        {
            bStarted.store( true );
            while ( stopToken.isStopRequested() == false )
            {
                loopCount.fetch_add( 1 );
                ThreadUtil::yield();
            }
            bFinished.store( true );
        } };
        SW_EXPECT_TRUE( waitForThreadTestFlag( bStarted, 5000 ) );
        SW_EXPECT_FALSE( worker.get_stop_token().isStopRequested() );
    }
    // 소멸자가 돌아왔으면 스레드는 끝났다(합류했다)
    SW_EXPECT_TRUE( bFinished.load() );
    SW_EXPECT_TRUE( loopCount.load() >= 1 );
}

/**
 * @brief 정지 토큰을 받지 않는 함수도 그대로 돈다. 이동 대입은 지금 스레드를 합류한 뒤 넘겨받는다.
 */
SW_TEST_CASE( ThreadTest, MoveAssignJoinsTheCurrentThreadFirst )
{
    atomic<int32> finishedCount{ 0 };
    thread        worker{ [&finishedCount]()
    { finishedCount.fetch_add( 1 ); } };
    thread        next{ [&finishedCount]( StopToken stopToken )
    {
        while ( stopToken.isStopRequested() == false )
            ThreadUtil::yield();
        finishedCount.fetch_add( 10 );
    } };
    worker = std::move( next ); // 첫 스레드를 합류한다(정지 요청은 토큰을 받지 않는 함수에 아무 일도 하지 않는다)
    SW_EXPECT_EQUAL( 1, finishedCount.load() );
    SW_EXPECT_TRUE( worker.joinable() );
    worker.request_stop();
    worker.join();
    SW_EXPECT_EQUAL( 11, finishedCount.load() );
}

/**
 * @brief 정지 토큰을 마지막 인자로 받는 멤버 함수도 토큰을 받는다 — 소멸이 정지를 요청하고 합류한다.
 */
SW_TEST_CASE( ThreadTest, MemberFunctionTakesTheStopTokenAsLastArgument )
{
    StopTokenLastArgumentOwner owner;
    {
        thread         worker{ &StopTokenLastArgumentOwner::run, &owner, 7 };
        const Deadline deadline = Deadline::afterMilliseconds( 5000 );
        while ( owner._loopCount.load() == 0 && deadline.isExpired() == false )
            ThreadUtil::yield();
    }
    SW_EXPECT_EQUAL( 7, owner._seenValue.load() );
    SW_EXPECT_TRUE( owner._loopCount.load() >= 1 );
}

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_DEBUG )
/**
 * @brief 로더 락을 쥔 채 join 하면 단언하고 스레드를 뗀다 — 기다리면 끝나는 스레드도 로더 락을 기다려 영원히 멈춘다.
 * @details DLL 을 내릴 때 모듈 정적 소멸자가 스레드를 join 하던 에디터 종료 교착과 같은 조건을 `LdrLockLoaderLock` 으로 만든다. 단언은 가로채기로 센다(Debug 만).
 */
SW_TEST_CASE( ThreadTest, JoinUnderTheLoaderLockAssertsAndDetaches )
{
    using LockLoaderLockFunction           = LONG( NTAPI* )( ULONG, ULONG*, void** );
    using UnlockLoaderLockFunction         = LONG( NTAPI* )( ULONG, void* );
    const HMODULE                  hNtdll  = GetModuleHandleW( L"ntdll.dll" );
    const LockLoaderLockFunction   pLock   = reinterpret_cast<LockLoaderLockFunction>( reinterpret_cast<void*>( GetProcAddress( hNtdll, "LdrLockLoaderLock" ) ) );
    const UnlockLoaderLockFunction pUnlock = reinterpret_cast<UnlockLoaderLockFunction>( reinterpret_cast<void*>( GetProcAddress( hNtdll, "LdrUnlockLoaderLock" ) ) );
    SW_REQUIRE_TRUE_MSG( pLock != nullptr && pUnlock != nullptr, "ntdll exports the loader lock functions" );

    // 뗀 스레드는 이 함수보다 오래 살 수 있다 — 플래그는 스레드와 나눠 갖는다.
    const std::shared_ptr<atomic<bool>> pStarted = std::make_shared<atomic<bool>>( false );
    const std::shared_ptr<atomic<bool>> pRelease = std::make_shared<atomic<bool>>( false );
    thread                              worker{ [pStarted, pRelease]()
    {
        pStarted->store( true );
        while ( pRelease->load() == false )
            ThreadUtil::yield();
    } };
    SW_EXPECT_TRUE( waitForThreadTestFlag( *pStarted, 5000 ) );
    {
        test::ScopedAssertCapture capture;
        void*                     pCookie = nullptr;
        const bool                bLocked = pLock( 0, nullptr, &pCookie ) == 0;
        SW_EXPECT_TRUE( bLocked );
        if ( bLocked )
        {
            worker.join();
            (void)pUnlock( 0, pCookie ); // 방금 잡은 락이다 — 놓지 못하면 다음 케이스가 멈춰 드러난다
            SW_EXPECT_EQUAL( 1u, capture.getCount() );
        }
    }
    SW_EXPECT_FALSE( worker.joinable() );
    pRelease->store( true ); // 뗀 스레드가 스스로 끝난다
}
#endif
