#include "pch.h"

#include "Core/Concurrency/Futex.h"
#include "Core/Concurrency/OnceInit.h"
#include "Core/Concurrency/Queue/LockedSwapQueue.h"
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Concurrency/StopToken.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/ThreadTestAccess.h"

using namespace sw;

namespace
{
    /** @brief 잠금 분석이 따라가지 못하는 시도(쥔 채 다시 시도, 결과를 값으로 보기)를 분석 밖에서 합니다. */
    struct SpinLockTestUtil
    {
        static bool tryLock( SpinLock& lock ) SW_NO_THREAD_SAFETY_ANALYSIS { return lock.try_lock(); }
        static void unlock( SpinLock& lock ) SW_NO_THREAD_SAFETY_ANALYSIS { lock.unlock(); }
    };
} // namespace

// ---------------------------------------------------------------------- 정지 신호

SW_TEST_CASE( StopTokenTest, DefaultTokenIsNeverStopped )
{
    const StopToken token;
    SW_EXPECT_FALSE( token.isStopRequested() );
}

SW_TEST_CASE( StopTokenTest, RequestReachesTokensAndOutlivesTheSource )
{
    StopToken token;
    {
        StopSource source;
        token = source.getToken();
        SW_EXPECT_FALSE( token.isStopRequested() );
        SW_EXPECT_FALSE( source.isStopRequested() );
        source.requestStop();
        SW_EXPECT_TRUE( source.isStopRequested() );
    }
    SW_EXPECT_TRUE( token.isStopRequested() ); // 원천이 사라져도 읽을 수 있다
}

SW_TEST_CASE( StopTokenTest, MovedFromSourceDoesNothing )
{
    StopSource source;
    StopSource moved = std::move( source );
    source.requestStop(); // NOLINT: 옮겨진 원천이 멈추지 않는지가 이 테스트다
    SW_EXPECT_FALSE( source.isStopRequested() );
    SW_EXPECT_FALSE( source.getToken().isStopRequested() );
    SW_EXPECT_FALSE( moved.isStopRequested() );
}

// ---------------------------------------------------------------------- 한 번 정하는 칸

SW_TEST_CASE( OnceInitTest, HoldsAValueUntilReset )
{
    OnceInit<int32> slot;
    SW_EXPECT_FALSE( slot.isSet() );
    SW_EXPECT_EQUAL( 0, slot.get() );
    slot.set( 7 );
    SW_EXPECT_TRUE( slot.isSet() );
    SW_EXPECT_EQUAL( 7, slot.get() );
    slot.reset();
    SW_EXPECT_FALSE( slot.isSet() );
    SW_EXPECT_EQUAL( 0, slot.get() );
    slot.set( 9 ); // 비운 뒤에는 다시 정할 수 있다
    SW_EXPECT_EQUAL( 9, slot.get() );
}

SW_TEST_CASE( OnceInitTest, ArrowReachesTheObjectThroughAnyPointerKind )
{
    struct Config
    {
        int32 _value{ 3 };
    };
    OnceInit<Config> byValue;
    byValue.set( Config{ 4 } );
    SW_EXPECT_EQUAL( 4, byValue->_value );

    Config            target{ 5 };
    OnceInit<Config*> byPointer;
    byPointer.set( &target );
    SW_EXPECT_EQUAL( 5, byPointer->_value );

    OnceInit<std::unique_ptr<Config>> byUnique;
    byUnique.set( std::make_unique<Config>( Config{ 6 } ) );
    SW_EXPECT_EQUAL( 6, byUnique->_value );

    OnceInit<std::shared_ptr<Config>> byShared;
    byShared.set( std::make_shared<Config>( Config{ 8 } ) );
    SW_EXPECT_EQUAL( 8, byShared->_value );
}

// ---------------------------------------------------------------------- 쌓고 통째로 가져가는 큐

SW_TEST_CASE( LockedSwapQueueTest, TakeAllReturnsInOrderAndEmptiesTheQueue )
{
    LockedSwapQueue<int32> queue;
    vector<int32>          listTaken;
    SW_EXPECT_TRUE( queue.isEmpty() );
    SW_EXPECT_FALSE( queue.takeAll( listTaken ) );

    queue.push( 1 );
    const int32 two = 2;
    queue.push( two );
    queue.pushRange( { 3, 4 } );
    SW_EXPECT_FALSE( queue.isEmpty() );
    SW_REQUIRE_TRUE( queue.takeAll( listTaken ) );
    SW_REQUIRE_EQUAL( size_t{ 4 }, listTaken.size() );
    SW_EXPECT_EQUAL( 1, listTaken[0] );
    SW_EXPECT_EQUAL( 4, listTaken[3] );
    SW_EXPECT_TRUE( queue.isEmpty() );
}

// 가져간 저장소를 비워 다시 넘기면 번갈아 돌아 다시 할당하지 않는다.
SW_TEST_CASE( LockedSwapQueueTest, DrainedStorageIsReused )
{
    LockedSwapQueue<int32> queue;
    queue.reserve( 16 );
    vector<int32> listTaken;
    queue.push( 1 );
    SW_REQUIRE_TRUE( queue.takeAll( listTaken ) );
    const size_t capacity = listTaken.capacity();
    SW_EXPECT_TRUE( capacity >= 16u );
    listTaken.clear();
    queue.push( 2 );
    SW_REQUIRE_TRUE( queue.takeAll( listTaken ) );
    listTaken.clear();
    queue.push( 3 );
    SW_REQUIRE_TRUE( queue.takeAll( listTaken ) );
    SW_EXPECT_EQUAL( capacity, listTaken.capacity() );
}

SW_TEST_CASE( LockedSwapQueueTest, ClearDropsWhatIsPending )
{
    LockedSwapQueue<int32> queue;
    queue.push( 1 );
    queue.clear();
    SW_EXPECT_TRUE( queue.isEmpty() );
}

// 도장은 잠금 안에서 찍혀 스레드가 여럿이어도 큐 순서와 도장 순서가 같다.
SW_TEST_CASE( LockedSwapQueueTest, StampsMatchQueueOrderAcrossThreads )
{
    constexpr int32        kThreadCount = 4;
    constexpr int32        kPushCount   = 2000;
    LockedSwapQueue<int32> queue;
    int32                  nextStamp = 0; // 잠금 안에서만 쓴다
    {
        vector<thread> listThread;
        for ( int32 index = 0; index < kThreadCount; ++index )
        {
            listThread.emplace_back( ThreadTestAccess::getKey(), [&queue, &nextStamp]
            {
                for ( int32 push = 0; push < kPushCount; ++push )
                {
                    int32 item = 0;
                    (void)queue.pushStamped( std::move( item ), [&nextStamp]( int32& inoutItem )
                    {
                        inoutItem = nextStamp++;
                        return inoutItem;
                    } );
                }
            } );
        }
    }
    vector<int32> listTaken;
    SW_REQUIRE_TRUE( queue.takeAll( listTaken ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( kThreadCount * kPushCount ), listTaken.size() );
    bool bInOrder = true;
    for ( size_t index = 0; index < listTaken.size(); ++index )
    {
        bInOrder = bInOrder && listTaken[index] == static_cast<int32>( index );
    }
    SW_EXPECT_TRUE( bInOrder );
}

// ---------------------------------------------------------------------- 잠금

SW_TEST_CASE( SpinLockTest, TryLockFailsWhileHeld )
{
    SpinLock lock;
    SW_EXPECT_FALSE( lock.isLocked() );
    SW_REQUIRE_TRUE( SpinLockTestUtil::tryLock( lock ) );
    SW_EXPECT_TRUE( lock.isLocked() );
    SW_EXPECT_FALSE( SpinLockTestUtil::tryLock( lock ) );
    SpinLockTestUtil::unlock( lock );
    SW_EXPECT_FALSE( lock.isLocked() );
}

SW_TEST_CASE( SpinLockTest, ProtectsACounterUnderContention )
{
    constexpr int32 kThreadCount = 4;
    constexpr int32 kAddCount    = 20000;
    SpinLock        lock;
    int32           counter = 0;
    {
        vector<thread> listThread;
        for ( int32 index = 0; index < kThreadCount; ++index )
        {
            listThread.emplace_back( ThreadTestAccess::getKey(), [&lock, &counter]
            {
                for ( int32 add = 0; add < kAddCount; ++add )
                {
                    ScopedLock<SpinLock> guard{ lock };
                    ++counter;
                }
            } );
        }
    }
    SW_EXPECT_EQUAL( kThreadCount * kAddCount, counter );
}

// 쥔 채 오래 머물면 기다리는 쪽은 스핀 예산(`SpinLock::kSpinLimit`)을 넘겨 잠든다. 풀 때 잠든 대기자가 차례로 모두 깨어나야 한다 —
// 깨어난 쪽이 "잠든 대기자 있음" 상태를 이어받지 않으면 나머지가 영영 잠들어 이 테스트가 끝나지 않는다.
SW_TEST_CASE( SpinLockTest, SleepingWaitersAllWakeAfterALongHold )
{
    constexpr int32 kWaiterCount = 8;
    SpinLock        lock;
    atomic<int32>   acquiredCount{ 0 };
    SW_REQUIRE_TRUE( SpinLockTestUtil::tryLock( lock ) );
    {
        vector<thread> listThread;
        for ( int32 index = 0; index < kWaiterCount; ++index )
        {
            listThread.emplace_back( ThreadTestAccess::getKey(), [&lock, &acquiredCount]
            {
                ScopedLock<SpinLock> guard{ lock };
                acquiredCount.fetch_add( 1, std::memory_order_relaxed );
            } );
        }
        ThreadUtil::sleepForMilliseconds( 30 ); // 스핀 예산(약 2 us)을 한참 넘겨 대기자가 잠들게 한다
        SW_EXPECT_EQUAL( 0, acquiredCount.load( std::memory_order_relaxed ) );
        SW_EXPECT_TRUE( lock.isLocked() );
        SpinLockTestUtil::unlock( lock );
    }
    SW_EXPECT_EQUAL( kWaiterCount, acquiredCount.load( std::memory_order_relaxed ) );
    SW_EXPECT_FALSE( lock.isLocked() );
}

// 감사 C-5 의 모양: 코어보다 스레드가 많고 구간 안에서 힙 할당(벡터 성장)과 양보가 돈다. 상호 배제가 깨지지 않고 넣은 수가 모두 남아야 한다.
SW_TEST_CASE( SpinLockTest, OversubscribedSectionsWithHeapAllocationStayExclusive )
{
    const uint32    processorCount = thread::getUsableProcessorCount();
    const int32     threadCount    = static_cast<int32>( processorCount * 4 < 8 ? 8 : processorCount * 4 );
    constexpr int32 kAddCount      = 2000;
    constexpr int32 kYieldEvery    = 64;
    SpinLock        lock;
    vector<int32>   listValue;
    atomic<int32>   insideCount{ 0 };
    atomic<int32>   overlapCount{ 0 };
    {
        vector<thread> listThread;
        for ( int32 index = 0; index < threadCount; ++index )
        {
            listThread.emplace_back( ThreadTestAccess::getKey(), [&lock, &listValue, &insideCount, &overlapCount, index]
            {
                for ( int32 add = 0; add < kAddCount; ++add )
                {
                    ScopedLock<SpinLock> guard{ lock };
                    if ( insideCount.fetch_add( 1, std::memory_order_relaxed ) != 0 )
                        overlapCount.fetch_add( 1, std::memory_order_relaxed );
                    listValue.push_back( index );
                    if ( add % kYieldEvery == 0 )
                        ThreadUtil::yield(); // 쥔 채 양보해 다른 스레드가 스핀 예산을 넘기고 잠드는 길을 탄다
                    insideCount.fetch_sub( 1, std::memory_order_relaxed );
                }
            } );
        }
    }
    SW_EXPECT_EQUAL( 0, overlapCount.load( std::memory_order_relaxed ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( threadCount ) * kAddCount, listValue.size() );
    SW_EXPECT_FALSE( lock.isLocked() );
}

SW_TEST_CASE( ScopedLockTest, UniqueLockFollowsLockAndUnlock )
{
    SpinLock lock;
    {
        UniqueLock<SpinLock> guard{ lock };
        SW_EXPECT_TRUE( guard.owns_lock() );
        SW_EXPECT_TRUE( lock.isLocked() );
        guard.unlock();
        SW_EXPECT_FALSE( guard.owns_lock() );
        SW_EXPECT_FALSE( lock.isLocked() );
        guard.lock();
        SW_EXPECT_TRUE( lock.isLocked() );
    }
    SW_EXPECT_FALSE( lock.isLocked() );
}

SW_TEST_CASE( ScopedLockTest, DeferredUniqueLockReleasesOnlyWhatItHolds )
{
    SpinLock lock;
    {
        UniqueLock<SpinLock> guard{ lock, std::defer_lock };
        SW_EXPECT_FALSE( guard.owns_lock() );
        SW_EXPECT_FALSE( lock.isLocked() );
        SW_EXPECT_TRUE( guard.try_lock() );
    }
    SW_EXPECT_FALSE( lock.isLocked() );

    SW_REQUIRE_TRUE( SpinLockTestUtil::tryLock( lock ) ); // 남이 쥔 잠금
    {
        UniqueLock<SpinLock> guard{ lock, std::defer_lock };
        SW_EXPECT_FALSE( guard.try_lock() );
    }
    SW_EXPECT_TRUE( lock.isLocked() ); // 쥐지 못한 가드는 풀지 않는다
    SpinLockTestUtil::unlock( lock );
}

SW_TEST_CASE( ScopedLockTest, PairHoldsBothOptionalMayBeEmpty )
{
    SpinLock first;
    SpinLock second;
    {
        ScopedLockPair<SpinLock> guard{ first, second };
        SW_EXPECT_TRUE( first.isLocked() );
        SW_EXPECT_TRUE( second.isLocked() );
    }
    SW_EXPECT_FALSE( first.isLocked() );
    SW_EXPECT_FALSE( second.isLocked() );

    {
        ScopedOptionalLock<SpinLock> empty{ nullptr };
        ScopedOptionalLock<SpinLock> guard{ &first };
        SW_EXPECT_TRUE( first.isLocked() );
    }
    SW_EXPECT_FALSE( first.isLocked() );
}

// 쥔 잠금 수는 Debug 만 센다. 로거가 0 을 단언하는 잎 잠금 규칙의 근거다.
SW_TEST_CASE( MutexTest, HeldLockDepthFollowsEveryKindOfLock )
{
    mutex        plain;
    shared_mutex shared;
#if defined( SW_DEBUG )
    constexpr uint32 kOne = 1;
#else
    constexpr uint32 kOne = 0;
#endif
    const uint32 base = mutex::getHeldLockDepth();
    {
        ScopedLock<mutex> guard{ plain };
        SW_EXPECT_EQUAL( base + kOne, mutex::getHeldLockDepth() );
        {
            ScopedSharedLock<shared_mutex> sharedGuard{ shared };
            SW_EXPECT_EQUAL( base + kOne * 2, mutex::getHeldLockDepth() );
        }
        {
            const ScopedLeafLockExemption exemption;
            SW_EXPECT_EQUAL( 0u, mutex::getHeldLockDepth() );
        }
        SW_EXPECT_EQUAL( base + kOne, mutex::getHeldLockDepth() );
    }
    {
        ScopedLock<shared_mutex> exclusive{ shared };
        SW_EXPECT_EQUAL( base + kOne, mutex::getHeldLockDepth() );
    }
    SW_EXPECT_EQUAL( base, mutex::getHeldLockDepth() );
}

// 공유끼리는 함께 쥐고, 독점은 혼자 쥔다. 시도는 다른 스레드에서 한다(같은 스레드의 재잠금은 정의되지 않았다).
SW_TEST_CASE( MutexTest, SharedLocksCoexistExclusiveDoesNot )
{
    shared_mutex lock;
    atomic<bool> bSharedWhileShared{ false };
    atomic<bool> bExclusiveWhileShared{ true };
    atomic<bool> bSharedWhileExclusive{ true };
    {
        ScopedSharedLock<shared_mutex> guard{ lock };
        thread                         other{ ThreadTestAccess::getKey(), [&]
        {
            const bool bShared = lock.try_lock_shared();
            bSharedWhileShared.store( bShared );
            if ( bShared )
                lock.unlock_shared();
            const bool bExclusive = lock.try_lock();
            bExclusiveWhileShared.store( bExclusive );
            if ( bExclusive )
                lock.unlock();
        } };
        other.join();
    }
    {
        ScopedLock<shared_mutex> guard{ lock };
        thread                   other{ ThreadTestAccess::getKey(), [&]
        {
            const bool bShared = lock.try_lock_shared();
            bSharedWhileExclusive.store( bShared );
            if ( bShared )
                lock.unlock_shared();
        } };
        other.join();
    }
    SW_EXPECT_TRUE( bSharedWhileShared.load() );
    SW_EXPECT_FALSE( bExclusiveWhileShared.load() );
    SW_EXPECT_FALSE( bSharedWhileExclusive.load() );
}

// ---------------------------------------------------------------------- 퓨텍스

SW_TEST_CASE( FutexTest, ReturnsAtOnceWhenTheValueDiffers )
{
    atomic<uint32> word{ 1 };
    Futex::wait( word, 0 ); // 멈추면 테스트가 끝나지 않는다
    SW_EXPECT_TRUE( Futex::waitFor( word, 0, 10000 ) );
}

SW_TEST_CASE( FutexTest, WaitForTimesOutWhenNothingChanges )
{
    atomic<uint32> word{ 0 };
    bool           bTimedOut = false;
    // 이유 없이 깨어날 수 있으므로 몇 번 시도한다.
    for ( int32 attempt = 0; attempt < 5 && bTimedOut == false; ++attempt )
    {
        bTimedOut = Futex::waitFor( word, 0, 5 ) == false;
    }
    SW_EXPECT_TRUE( bTimedOut );
    Futex::wakeOne( word ); // 잠든 스레드가 없어도 괜찮다
    Futex::wakeAll( word );
}

SW_TEST_CASE( FutexTest, WakeAllReleasesEveryWaiter )
{
    constexpr uint32 kWaiterCount = 4;
    atomic<uint32>   word{ 0 };
    atomic<uint32>   arrived{ 0 };
    atomic<uint32>   released{ 0 };
    {
        vector<thread> listWaiter;
        for ( uint32 index = 0; index < kWaiterCount; ++index )
        {
            listWaiter.emplace_back( ThreadTestAccess::getKey(), [&]
            {
                arrived.fetch_add( 1 );
                while ( word.load() == 0 ) // 약속대로 조건을 다시 확인한다
                {
                    Futex::wait( word, 0 );
                }
                released.fetch_add( 1 );
            } );
        }
        while ( arrived.load() < kWaiterCount )
        {
            ThreadUtil::yield();
        }
        word.store( 1 ); // 깨우는 쪽은 값을 먼저 바꾼다
        Futex::wakeAll( word );
    }
    SW_EXPECT_EQUAL( kWaiterCount, released.load() );
}

// ---------------------------------------------------------------------- 스레드 이름 · 프로세서 수

SW_TEST_CASE( ThreadNameTest, NameIsReadBackOnTheSameThread )
{
    atomic<bool> bBefore{ true };
    atomic<bool> bAfter{ false };
    atomic<bool> bMatches{ false };
    atomic<bool> bEmptyKept{ false };
    {
        thread other{ ThreadTestAccess::getKey(), [&]
        {
            utf8 arrName[64]{};
            bBefore.store( ThreadName::tryGetCurrentThreadName( arrName, sizeof( arrName ) ) );
            ThreadName::setCurrentThreadName( "SwTestThread" );
            bAfter.store( ThreadName::tryGetCurrentThreadName( arrName, sizeof( arrName ) ) );
            bMatches.store( string_view( arrName ) == "SwTestThread" );
            ThreadName::setCurrentThreadName( nullptr );
            ThreadName::setCurrentThreadName( "" );
            (void)ThreadName::tryGetCurrentThreadName( arrName, sizeof( arrName ) );
            bEmptyKept.store( string_view( arrName ) == "SwTestThread" );
        } };
    }
    SW_EXPECT_TRUE( bAfter.load() );
    SW_EXPECT_TRUE( bMatches.load() );
    SW_EXPECT_TRUE( bEmptyKept.load() );
}

// 받는 칸이 작으면 잘라서 끝낸다(넘치지 않는다).
SW_TEST_CASE( ThreadNameTest, SmallBufferIsTerminatedNotOverrun )
{
    atomic<bool> bTerminated{ false };
    {
        thread other{ ThreadTestAccess::getKey(), [&]
        {
            ThreadName::setCurrentThreadName( "SwTestThread" );
            utf8 arrName[6];
            for ( utf8& letter : arrName )
            {
                letter = 'x';
            }
            (void)ThreadName::tryGetCurrentThreadName( arrName, 4 );
            bTerminated.store( arrName[4] == 'x' && arrName[5] == 'x' );
        } };
    }
    SW_EXPECT_TRUE( bTerminated.load() );
    utf8 arrUnused[1];
    SW_EXPECT_FALSE( ThreadName::tryGetCurrentThreadName( nullptr, 16 ) );
    SW_EXPECT_FALSE( ThreadName::tryGetCurrentThreadName( arrUnused, 0 ) );
}

SW_TEST_CASE( ThreadTest, ThereIsAtLeastOneUsableProcessor )
{
    SW_EXPECT_TRUE( thread::getUsableProcessorCount() >= 1u );
}
