#include "pch.h"

#include "Core/Concurrency/Futex.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/AtomicSlot.h"
#include "Core/Delegate/LockedMulticastDelegate.h"
#include "Core/Delegate/WeakRef.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestWait.h"
#include "Core/Time/MonotonicClock.h"

static int32 s_TestValue{ 0 };
namespace
{
    /** @brief 자유 함수 델리게이트가 누적할 값을 더합니다. */
    void freeFunctionTest( int32 val )
    {
        s_TestValue += val;
    }

    /** @brief 살아 있는 개수를 세는 캡처입니다. 이동 생성자가 없어 "옮기기" 가 복사다(참조 카운트 핸들이 흔히 그렇다). */
    struct DelegateLiveCapture
    {
        static inline int32 s_liveCount = 0;

        DelegateLiveCapture() noexcept { ++s_liveCount; }
        DelegateLiveCapture( const DelegateLiveCapture& ) noexcept { ++s_liveCount; }
        DelegateLiveCapture& operator=( const DelegateLiveCapture& ) noexcept = default;
        ~DelegateLiveCapture() { --s_liveCount; }
    };

    /** @brief 구독을 내고 풀린 수를 세는 출처입니다(여러 스레드가 동시에 만들고 푸는 목록 잠금 확인용). */
    class CountingSubscriptionSource final : public sw::SubscriptionSource
    {
    public:
        CountingSubscriptionSource() = default;
        ~CountingSubscriptionSource() override { closeSubscriptions(); }
        CountingSubscriptionSource( const CountingSubscriptionSource& )            = delete;
        CountingSubscriptionSource& operator=( const CountingSubscriptionSource& ) = delete;

        sw::Subscription issue() { return issueSubscription( _nextID.fetch_add( 1 ) + 1 ); }
        void             detach( uint64 id ) { detachSubscription( id ); }
        uint32           getReleasedCount() const { return _releasedCount.load(); }

    private:
        void releaseSubscription( uint64 id ) override
        {
            (void)id;
            _releasedCount.fetch_add( 1 );
        }

    private:
        sw::atomic<uint64> _nextID{ 0 };
        sw::atomic<uint32> _releasedCount{ 0 };
    };

    class ProbeSubscriptionSource;

    /** @brief 출처 밖에서 해제를 지켜보는 상태입니다 — 출처가 사라진 뒤에도 테스트가 읽는다. */
    struct ReleaseProbe
    {
        sw::atomic<uint32>                       _releasedCount{ 0 };
        sw::atomic<bool>                         _bEntered{ false };           ///< 해제가 `releaseSubscription` 에 들어왔다
        sw::atomic<bool>                         _bReleaseHeld{ false };       ///< 서 있으면 해제가 안에서 기다린다(진행 중인 해제를 붙잡아 둔다)
        sw::unique_ptr<ProbeSubscriptionSource>* _pSourceToDestroy{ nullptr }; ///< 주면 해제 안에서 이 출처를 지운다(자기 해제 안에서 닫기)
    };

    /** @brief 해제가 파생 멤버를 만지고, 붙잡기(`ReleaseProbe::_bReleaseHeld`)가 서 있으면 해제 안에서 기다리는 출처입니다. 파생 소멸자 첫 줄에서 닫는다. */
    class ProbeSubscriptionSource final : public sw::SubscriptionSource
    {
    public:
        explicit ProbeSubscriptionSource( ReleaseProbe& probe )
            : _probe{ probe }
        {
        }
        ~ProbeSubscriptionSource() override { closeSubscriptions(); }
        ProbeSubscriptionSource( const ProbeSubscriptionSource& )            = delete;
        ProbeSubscriptionSource& operator=( const ProbeSubscriptionSource& ) = delete;

        sw::Subscription issue() { return issueSubscription( ++_nextID ); }

    private:
        void releaseSubscription( uint64 id ) override
        {
            (void)id;
            ReleaseProbe& probe = _probe;
            _releasedHere.fetch_add( 1 ); // 파생 멤버 — 출처가 소멸한 뒤라면 ASan 이 해제 뒤 사용으로 잡는다
            probe._bEntered.store( true );
            while ( probe._bReleaseHeld.load() )
            {
                sw::ThreadUtil::yield();
            }
            probe._releasedCount.fetch_add( 1 );
            if ( probe._pSourceToDestroy != nullptr )
                probe._pSourceToDestroy->reset(); // 이 객체를 지운다 — 아래로 멤버를 만지지 않는다
        }

    private:
        ReleaseProbe&      _probe;
        uint64             _nextID{ 0 };
        sw::atomic<uint32> _releasedHere{ 0 };
    };

    /** @brief 닫기를 빠뜨린 파생입니다(자식 역할 `OpenSourceChild` 만 쓴다 — 기반 소멸자가 멈춰야 한다). */
    class OpenSubscriptionSource final : public sw::SubscriptionSource
    {
    public:
        sw::Subscription issue() { return issueSubscription( 1 ); }

    private:
        void releaseSubscription( uint64 id ) override { (void)id; }
    };

    /** @brief 닫기를 빠뜨린 출처의 자식 시한(초) — Debug 의 자식 App 기동(몇 초)을 덮는다. */
    constexpr float32 kOpenSourceChildTimeoutSeconds = 120.0f;
} // namespace

struct DummyListener
{
    int32 _value{ 0 };
    /** @brief 멤버 함수 델리게이트가 누적할 값을 더합니다. */
    void memberFunc( int32 val )
    {
        _value += val;
    }
};

// ------------------------------------------------------------------------------
// 1) Core_Delegate — 단일·멀티캐스트
// ------------------------------------------------------------------------------
/**
 * @brief [DelegateTest] 단일 델리게이트 자유 함수
 */
SW_TEST_CASE( DelegateTest, SingleDelegateFreeFunction )
{
    s_TestValue                     = 0;
    sw::Delegate<void( int32 )> del = SW_DELEGATE_FUNCTION( sw::Delegate<void( int32 )>, freeFunctionTest );
    SW_EXPECT_TRUE( del.isBound() );

    del( 10 );
    SW_EXPECT_EQUAL( 10, s_TestValue );
}

/**
 * @brief [DelegateTest] 단일 델리게이트 멤버 함수
 */
SW_TEST_CASE( DelegateTest, SingleDelegateMemberFunction )
{
    DummyListener               listener;
    sw::Delegate<void( int32 )> del = SW_DELEGATE_METHOD( sw::Delegate<void( int32 )>, &DummyListener::memberFunc, &listener );
    SW_EXPECT_TRUE( del.isBound() );

    del( 25 );
    SW_EXPECT_EQUAL( 25, listener._value );
}

// ------------------------------------------------------------------------------
// 2) 멀티캐스트 — 브로드캐스트·제거·핸들
// ------------------------------------------------------------------------------
/**
 * @brief [DelegateTest] 멀티캐스트 델리게이트 브로드캐스트
 */
SW_TEST_CASE( DelegateTest, MulticastDelegateBroadcast )
{
    s_TestValue = 0;
    DummyListener listener;

    sw::MulticastDelegate<void( int32 )> multiDel;
    SW_EXPECT_FALSE( multiDel.isBound() );

    const sw::Subscription freeSubscription   = multiDel.add( SW_DELEGATE_FUNCTION( sw::Delegate<void( int32 )>, freeFunctionTest ) );
    const sw::Subscription memberSubscription = multiDel.add( SW_DELEGATE_METHOD( sw::Delegate<void( int32 )>, &DummyListener::memberFunc, &listener ) );
    SW_EXPECT_TRUE( multiDel.isBound() );

    multiDel.broadcast( 5 );
    SW_EXPECT_EQUAL( 5, s_TestValue );
    SW_EXPECT_EQUAL( 5, listener._value );
}

/**
 * @brief [DelegateTest] 구독을 풀면(`reset`) 그 항목만 빠진다
 */
SW_TEST_CASE( DelegateTest, MulticastDelegateRemoveSingle )
{
    s_TestValue = 0;

    sw::MulticastDelegate<void( int32 )> multiDel;
    sw::Subscription                     subscription = multiDel.add( SW_DELEGATE_FUNCTION( sw::Delegate<void( int32 )>, freeFunctionTest ) );
    SW_EXPECT_TRUE( multiDel.isBound() );
    SW_EXPECT_TRUE( subscription.isActive() );

    subscription.reset();
    SW_EXPECT_FALSE( multiDel.isBound() );
    SW_EXPECT_FALSE( subscription.isActive() );

    multiDel.broadcast( 99 );
    SW_EXPECT_EQUAL( 0, s_TestValue );
}

/**
 * @brief [DelegateTest] 멀티캐스트 델리게이트 전체 제거
 */
SW_TEST_CASE( DelegateTest, MulticastDelegateRemoveAll )
{
    s_TestValue = 0;
    DummyListener listener;

    sw::MulticastDelegate<void( int32 )> multiDel;
    const sw::Subscription               freeSubscription   = multiDel.add( SW_DELEGATE_FUNCTION( sw::Delegate<void( int32 )>, freeFunctionTest ) );
    const sw::Subscription               memberSubscription = multiDel.add( SW_DELEGATE_METHOD( sw::Delegate<void( int32 )>, &DummyListener::memberFunc, &listener ) );
    SW_EXPECT_TRUE( multiDel.isBound() );

    multiDel.removeAll();
    SW_EXPECT_FALSE( multiDel.isBound() );
    SW_EXPECT_FALSE_MSG( freeSubscription.isActive(), "removeAll 이 비운 항목의 구독이 아직 살아 있다고 답한다" );

    multiDel.broadcast( 10 );
    SW_EXPECT_EQUAL( 0, s_TestValue );
    SW_EXPECT_EQUAL( 0, listener._value );
}

/**
 * @brief [DelegateTest] 멀티캐스트 델리게이트 람다
 */
SW_TEST_CASE( DelegateTest, MulticastDelegateLambda )
{
    int32 capturedA{ 0 };
    int32 capturedB{ 0 };

    sw::MulticastDelegate<void( int32 )> multiDel;
    const sw::Subscription               subscriptionA = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void( int32 )>, [&]( int32 val )
                  { capturedA += val; } ) );
    const sw::Subscription               subscriptionB = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void( int32 )>, [&]( int32 val )
                  { capturedB += val * 2; } ) );
    SW_EXPECT_TRUE( multiDel.isBound() );

    multiDel.broadcast( 3 );
    SW_EXPECT_EQUAL( 3, capturedA );
    SW_EXPECT_EQUAL( 6, capturedB );
}

/**
 * @brief [DelegateTest] 멀티캐스트 델리게이트 다중 브로드캐스트
 */
SW_TEST_CASE( DelegateTest, MulticastDelegateMultipleBroadcasts )
{
    s_TestValue = 0;

    sw::MulticastDelegate<void( int32 )> multiDel;
    const sw::Subscription               subscription = multiDel.add( SW_DELEGATE_FUNCTION( sw::Delegate<void( int32 )>, freeFunctionTest ) );

    multiDel.broadcast( 10 );
    multiDel.broadcast( 20 );
    multiDel.broadcast( 30 );

    SW_EXPECT_EQUAL( 60, s_TestValue );
}

/**
 * @brief [DelegateTest] 구독자가 먼저 사라지면 항목이 빠진다(구독의 소멸이 해제다)
 */
SW_TEST_CASE( DelegateTest, SubscriberDestroyedFirstRemovesTheEntry )
{
    int32                                val{ 0 };
    sw::MulticastDelegate<void( int32 )> multiDel;

    {
        const sw::Subscription subscription = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void( int32 )>, [&]( int32 delta )
        { val += delta; } ) );
        SW_EXPECT_TRUE( subscription.isActive() );

        multiDel.broadcast( 100 );
        SW_EXPECT_EQUAL( 100, val );
    }
    SW_EXPECT_FALSE( multiDel.isBound() );
    multiDel.broadcast( 50 );
    SW_EXPECT_EQUAL( 100, val );
}

/**
 * @brief [DelegateTest] 출처가 먼저 사라지면 구독이 무효가 되고, 그 뒤 구독의 소멸 · `reset` 은 아무것도 하지 않는다
 * @details 출처의 침입형 목록이 소멸 때 살아 있는 구독의 출처 칸을 비운다. 비우지 않으면 구독의 소멸이 사라진 출처를 부른다(UAF).
 */
SW_TEST_CASE( DelegateTest, SourceDestroyedFirstInvalidatesTheSubscription )
{
    sw::Subscription first;
    sw::Subscription second;
    {
        sw::MulticastDelegate<void()> multiDel;
        first  = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, []() {} ) );
        second = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, []() {} ) );
        SW_EXPECT_TRUE( first.isActive() );
        SW_EXPECT_TRUE( second.isActive() );
    }
    SW_EXPECT_FALSE( first.isActive() );
    SW_EXPECT_FALSE( second.isActive() );
    first.reset(); // 사라진 출처를 부르지 않아야 한다(ASan · Debug 힙 검사가 잡는다)
}

/**
 * @brief [DelegateTest] 구독을 옮기면 등록이 따라가고, 옮긴 원본의 소멸은 아무것도 풀지 않는다
 * @details 이동은 출처 목록의 자리를 새 객체로 고친다. 고치지 않으면 출처가 사라질 때 옮긴 원본(이미 사라진 지역)의 칸을 쓴다.
 *          `vector<Subscription>` 이 재할당하며 옮기는 경우와 출처 자체를 옮기는 경우도 같이 본다.
 */
SW_TEST_CASE( DelegateTest, MovingSubscriptionKeepsTheRegistration )
{
    int32                         callCount = 0;
    sw::MulticastDelegate<void()> multiDel;

    sw::vector<sw::Subscription> listSubscription;
    for ( int32 index = 0; index < 8; ++index )
    {
        // 재할당이 여러 번 일어나도록 하나씩 넣는다
        listSubscription.push_back( multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&callCount]()
        { ++callCount; } ) ) );
    }
    multiDel.broadcast();
    SW_EXPECT_EQUAL( 8, callCount );

    sw::Subscription moved = std::move( listSubscription[3] );
    SW_EXPECT_FALSE( listSubscription[3].isActive() );
    SW_EXPECT_TRUE( moved.isActive() );
    listSubscription.erase( listSubscription.begin() + 3 ); // 옮긴 원본의 소멸 — 아무것도 풀지 않는다
    callCount = 0;
    multiDel.broadcast();
    SW_EXPECT_EQUAL( 8, callCount );

    // 출처를 옮기면 구독이 새 출처를 가리킨다 — 새 출처에서 풀린다
    sw::MulticastDelegate<void()> movedSource{ std::move( multiDel ) };
    moved.reset();
    callCount = 0;
    movedSource.broadcast();
    SW_EXPECT_EQUAL( 7, callCount );

    listSubscription.clear();
    SW_EXPECT_FALSE( movedSource.isBound() );
}

/**
 * @brief [DelegateTest] 여러 스레드가 한 출처의 구독을 동시에 만들고 옮기고 풀어도 목록이 깨지지 않는다(출처의 목록 잠금)
 * @details 로그 리스너처럼 등록 · 해제가 아무 스레드에서나 오는 출처가 쓴다. 목록이 깨지면 풀린 수가 맞지 않거나 출처 소멸이 사라진 구독을 쓴다.
 */
SW_TEST_CASE( DelegateTest, SubscriptionsFromManyThreadsKeepTheListConsistent )
{
    constexpr int32            kThreadCount     = 4;
    constexpr int32            kRoundsPerThread = 500;
    CountingSubscriptionSource source;
    sw::vector<std::thread>    listWorker;
    for ( int32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listWorker.emplace_back( [&source]()
        {
            for ( int32 round = 0; round < kRoundsPerThread; ++round )
            {
                sw::Subscription first  = source.issue();
                sw::Subscription second = source.issue();
                sw::Subscription moved  = std::move( first ); // 옮기기도 이웃을 고친다
                second.reset();
            }
        } );
    }
    for ( std::thread& worker : listWorker )
    {
        worker.join();
    }
    SW_EXPECT_EQUAL( static_cast<uint32>( kThreadCount * kRoundsPerThread * 2 ), source.getReleasedCount() );

    // 스레드가 끝난 뒤에도 출처는 새 구독을 낸다(목록이 빈 채로 남았다)
    const sw::Subscription leftover = source.issue();
    SW_EXPECT_TRUE( leftover.isActive() );
}

/**
 * @brief [DelegateTest] 출처가 구독 하나만 무효화하면(한 번 쓰고 끝난 등록) 그 구독만 꺼지고, 그 뒤 풀어도 해제를 부르지 않으며 이웃 구독은 그대로다
 */
SW_TEST_CASE( DelegateTest, DetachingOneSubscriptionLeavesTheOthers )
{
    CountingSubscriptionSource source;
    sw::Subscription           first  = source.issue(); // id 1
    sw::Subscription           second = source.issue(); // id 2
    source.detach( 1 );
    SW_EXPECT_FALSE( first.isActive() );
    SW_EXPECT_TRUE( second.isActive() );
    first.reset();
    SW_EXPECT_EQUAL( 0u, source.getReleasedCount() );
    second.reset();
    SW_EXPECT_EQUAL( 1u, source.getReleasedCount() );
}

struct ConstListener
{
    int32 calculate( int32 a, int32 b ) const
    {
        return a + b;
    }
};

/**
 * @brief [DelegateTest] 델리게이트 전체 커버리지
 */
SW_TEST_CASE( DelegateTest, DelegateFullCoverage )
{
    sw::Delegate<int32( int32, int32 )> nullDel;
    SW_EXPECT_FALSE( nullDel.isBound() );
    SW_EXPECT_TRUE( nullDel == nullptr );

    ConstListener                       listener;
    sw::Delegate<int32( int32, int32 )> del = SW_DELEGATE_METHOD( sw::Delegate<int32( int32, int32 )>, &ConstListener::calculate, &listener );

    SW_EXPECT_TRUE( del.isBound() );
    SW_EXPECT_TRUE( del != nullptr );
    SW_EXPECT_EQUAL( 15, del( 7, 8 ) );

    sw::Delegate<int32( int32, int32 )> delSame = SW_DELEGATE_METHOD( sw::Delegate<int32( int32, int32 )>, &ConstListener::calculate, &listener );
    SW_EXPECT_TRUE( del == delSame );

    sw::MulticastDelegate<void( int32 )> multiDel;
    int32                                val{ 0 };
    sw::Delegate<void( int32 )>          delLambda = SW_DELEGATE_LAMBDA( sw::Delegate<void( int32 )>, [&]( int32 delta )
             { val += delta; } );

    sw::Subscription subscription = multiDel.add( delLambda );
    multiDel.broadcast( 20 );
    SW_EXPECT_EQUAL( 20, val );

    subscription.reset();
    multiDel.broadcast( 30 );
    SW_EXPECT_EQUAL( 20, val );
}

/**
 * @brief [DelegateTest] MulticastDelegate 브로드캐스트 도중 자기 자신 또는 후속 리스너 remove 시 안전성 검증
 */
SW_TEST_CASE( DelegateTest, MulticastDelegateDeferredRemoveDuringBroadcast )
{
    sw::MulticastDelegate<void()> multiDel;
    int32                         firstCallCount  = 0;
    int32                         secondCallCount = 0;

    sw::Subscription subscriptionA;
    sw::Subscription subscriptionB;

    subscriptionA = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&]()
    {
        firstCallCount++;
        // A 실행 중 B의 구독을 푼다 -> B는 unbind되어 이번 브로드캐스트 및 다음 브로드캐스트에서 실행되지 않아야 함
        subscriptionB.reset();
    } ) );

    subscriptionB = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&]()
    {
        secondCallCount++;
    } ) );

    multiDel.broadcast();
    SW_EXPECT_EQUAL( 1, firstCallCount );
    SW_EXPECT_EQUAL( 0, secondCallCount );

    multiDel.broadcast();
    SW_EXPECT_EQUAL( 2, firstCallCount );
    SW_EXPECT_EQUAL( 0, secondCallCount );
}

/**
 * @brief [DelegateTest] `broadcastUntil` 은 결과가 멈춤을 고른 곳에서 서고, 그 사이 자기 구독을 푼 항목은 순회가 끝난 뒤 빠진다
 */
SW_TEST_CASE( DelegateTest, MulticastDelegateBroadcastUntilStopsAndDefersRemove )
{
    sw::MulticastDelegate<int32()> multiDel;
    int32                          thirdCallCount = 0;
    sw::Subscription               subscriptionA;
    subscriptionA                  = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<int32()>, [&]() -> int32
                     {
        subscriptionA.reset(); // 순회 중 자기 구독을 푼다
        return -1;
    } ) );
    sw::Subscription subscriptionB = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<int32()>, []() -> int32
    { return 7; } ) );
    sw::Subscription subscriptionC = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<int32()>, [&]() -> int32
    {
        ++thirdCallCount;
        return 9;
    } ) );

    int32      firstCode = -1;
    const bool bStopped  = multiDel.broadcastUntil( [&]( int32 code )
    {
        if ( code == -1 )
            return false;
        firstCode = code;
        return true;
    } );
    SW_EXPECT_TRUE( bStopped );
    SW_EXPECT_EQUAL( 7, firstCode );
    SW_EXPECT_EQUAL( 0, thirdCallCount );
    SW_EXPECT_FALSE( subscriptionA.isActive() );

    int32 resultCount = 0;
    SW_EXPECT_FALSE( multiDel.broadcastUntil( [&]( int32 )
    {
        ++resultCount;
        return false;
    } ) );
    SW_EXPECT_EQUAL( 2, resultCount ); // A 는 빠졌다
    SW_EXPECT_EQUAL( 1, thirdCallCount );
}

/**
 * @brief [DelegateTest] MulticastDelegate 는 **진짜로 이동한다** (복사로 떨어지지 않는다)
 * @details 복사 생성자를 `= default` 로 *선언* 하면 암시적 이동 생성자·이동 대입이 생기지 않는다.
 *          그러면 `std::move` 를 써도 구독자 벡터가 통째로 깊은 복사되고,
 *          `is_nothrow_move_constructible` 이 false 다. 옮긴 뒤 원본이 비는지로 확인한다 —
 *          복사로 떨어지면 원본이 그대로 남는다.
 */
SW_TEST_CASE( DelegateTest, MulticastDelegateMovesInsteadOfCopying )
{
    using Multi = sw::MulticastDelegate<void()>;

    static_assert( std::is_nothrow_move_constructible_v<Multi>,
                   "이동 생성자가 없다 — 복사 생성자를 직접 선언하면 암시적 이동이 사라진다" );
    static_assert( std::is_nothrow_move_assignable_v<Multi>, "이동 대입이 없다" );

    int32 callCount = 0;

    Multi                  source;
    const sw::Subscription subscription = source.add( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&callCount]()
    { ++callCount; } ) );
    SW_REQUIRE_TRUE( source.isBound() );

    Multi moved{ std::move( source ) };
    SW_EXPECT_TRUE_MSG( source.isBound() == false, "이동한 원본이 그대로 남아 있다 — 복사로 떨어졌다" );
    SW_EXPECT_TRUE( moved.isBound() );

    moved.broadcast();
    SW_EXPECT_EQUAL( 1, callCount );

    Multi movedAssign;
    movedAssign = std::move( moved );
    SW_EXPECT_TRUE_MSG( moved.isBound() == false, "이동 대입한 원본이 그대로 남아 있다" );
    movedAssign.broadcast();
    SW_EXPECT_EQUAL( 2, callCount );
}

/**
 * @brief [DelegateTest] 방송 중에 복사해도 **사본은 지연 제거에 갇히지 않는다**
 * @details `_broadcastDepth` 와 지연 제거 큐는 값이 아니라 *그 인스턴스의 호출 스택 상태*다.
 *          복사 생성자가 `= default` 면 그 둘까지 같이 복사된다 — broadcast 중에 복사하면
 *          사본이 깊이 0 이 아닌 채로 태어나, 그 사본에서 `remove` 한 것이 **영영 반영되지 않는다**
 *          (자기 broadcast 는 1→2→1 로만 오가므로 0 이 안 된다).
 *
 * @note **복사 생성자를 타야 한다.** 복사 대입은 받는 쪽의 깊이를 건드리지 않으므로(그 깊이는 지금
 *       그 객체를 방송 중인 호출 스택의 것이다) 이 결함을 드러내지 못한다 — 대입으로 쓰면 변이 검사에서도 통과한다.
 */
SW_TEST_CASE( DelegateTest, CopyMadeDuringBroadcastStartsWithCleanBroadcastState )
{
    using Multi = sw::MulticastDelegate<void()>;

    int32 sourceCount = 0;

    Multi                 source;
    sw::unique_ptr<Multi> pCopy;

    // 방송 도중에 **복사 생성자**로 사본을 만든다 — 그 순간 원본의 _broadcastDepth 는 1 이다.
    const sw::Subscription subscription = source.add( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&]()
    {
        ++sourceCount;
        pCopy = sw::make_unique<Multi>( source );
    } ) );

    source.broadcast();
    SW_REQUIRE_TRUE( pCopy != nullptr );
    SW_EXPECT_EQUAL( 1, sourceCount );
    SW_EXPECT_TRUE( pCopy->isBound() );

    // 사본에서 전부 제거한다. 사본의 방송 깊이가 0 이라면 **즉시** 반영돼야 한다.
    pCopy->removeAll();
    SW_EXPECT_TRUE_MSG( pCopy->isBound() == false,
                        "방송 중에 뜬 사본이 방송 깊이를 물려받아 제거가 지연되고 있다" );

    const int32 beforeBroadcast = sourceCount;
    pCopy->broadcast();
    SW_EXPECT_EQUAL( beforeBroadcast, sourceCount );
}

/**
 * @brief [DelegateTest] 인라인 버퍼(SBO)에 든 람다를 옮겨도 캡처가 새지 않는다 — 옮긴 원본도 파괴된다
 * @details 이동이 새 자리에 이동 생성만 하고 원본을 파괴하지 않으면, 원본의 관리자를 지우므로 그 뒤로 아무도 파괴하지 않는다 — 이동이 사실상
 *          복사인 캡처는 하나씩 샌다(참조 카운트가 내려가지 않는다).
 */
SW_TEST_CASE( DelegateTest, MovingAnInlineLambdaDestroysTheSource )
{
    DelegateLiveCapture::s_liveCount = 0;
    {
        DelegateLiveCapture capture;
        int32               callCount = 0;
        {
            sw::Delegate<void()> first  = SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [capture, &callCount]()
             {
                (void)capture;
                ++callCount;
            } );
            sw::Delegate<void()> second = std::move( first ); // 이동 생성
            sw::Delegate<void()> third;
            third = std::move( second ); // 이동 대입
            third();
            SW_EXPECT_EQUAL( 1, callCount );
            SW_EXPECT_EQUAL( 2, DelegateLiveCapture::s_liveCount ); // 지역 하나 + 델리게이트 안의 하나
        }
        SW_EXPECT_EQUAL( 1, DelegateLiveCapture::s_liveCount );
    }
    SW_EXPECT_EQUAL( 0, DelegateLiveCapture::s_liveCount );
}

/**
 * @brief [DelegateTest] LockedMulticastDelegate 는 등록 순서대로 부르고, 방송 안에서 자기 구독을 풀어도 멈추지 않으며(자기를 기다리지 않는다) 다음 방송에서 빠진다
 */
SW_TEST_CASE( DelegateTest, LockedMulticastReleaseInsideBroadcastDoesNotWaitForItself )
{
    sw::LockedMulticastDelegate<void( int32 )> multiDel;
    sw::vector<int32>                          listCalled;
    sw::Subscription                           subscriptionA;
    subscriptionA                  = multiDel.add( sw::Delegate<void( int32 )>::create( [&]( int32 value )
    {
        listCalled.push_back( value );
        subscriptionA.reset();
    } ) );
    sw::Subscription subscriptionB = multiDel.add( sw::Delegate<void( int32 )>::create( [&]( int32 value )
    { listCalled.push_back( value * 10 ); } ) );
    SW_EXPECT_TRUE( multiDel.isBound() );

    multiDel.broadcast( 1 );
    multiDel.broadcast( 2 );
    SW_EXPECT_EQUAL( size_t{ 3 }, listCalled.size() );
    SW_EXPECT_EQUAL( 1, listCalled[0] );
    SW_EXPECT_EQUAL( 10, listCalled[1] );
    SW_EXPECT_EQUAL( 20, listCalled[2] );

    subscriptionB.reset();
    SW_EXPECT_FALSE( multiDel.isBound() );
}

/**
 * @brief [DelegateTest] AtomicSlot 은 다시 걸면 갈아 끼우고, 옛 구독이 늦게 풀려도 새 값을 지우지 않는다
 */
SW_TEST_CASE( DelegateTest, AtomicSlotLateReleaseKeepsTheNewerValue )
{
    static int32           s_first  = 1;
    static int32           s_second = 2;
    sw::AtomicSlot<int32*> slot;
    sw::Subscription       oldBinding = slot.bind( &s_first );
    sw::Subscription       newBinding = slot.bind( &s_second );
    oldBinding.reset();
    SW_EXPECT_TRUE( slot.get() == &s_second );
    newBinding.reset();
    SW_EXPECT_TRUE( slot.get() == nullptr );
}

// ------------------------------------------------------------------------------
// 2) 구독의 제어 블록 — 출처보다 오래 사는 잠금과 해제 중 대기(감사 C-20)
// ------------------------------------------------------------------------------
/**
 * @brief [DelegateTest] 한 스레드가 구독을 풀고 다른 스레드가 같은 출처를 지우기를 1 만 번 겹쳐도 해제된 메모리를 만지지 않는다
 * @details 구독이 잡는 잠금이 출처 안에 있으면 그 사이 출처가 사라져 해제된 메모리의 잠금을 잡고, 잠금 밖 `releaseSubscription` 이 소멸된 파생을 부른다.
 *          판정은 ASan(`linux-verify` 의 sanitize) — 보통 구성에서는 깨진 힙이 늦게 드러날 수 있다.
 */
SW_TEST_CASE( DelegateTest, SourceDestroyedOnAnotherThreadWhileSubscriptionsReset )
{
    constexpr uint32                        kRoundCount = 10000;
    ReleaseProbe                            probe;
    sw::unique_ptr<ProbeSubscriptionSource> pSource;
    sw::Subscription                        subscription;
    sw::atomic<uint32>                      startedRound{ 0 };  // 본 스레드가 출처와 구독을 준비한 회차
    sw::atomic<uint32>                      finishedCount{ 0 }; // 두 일꾼이 끝낸 일의 수(회차마다 둘)
    const auto                              runWorker = [&]( const bool bResetsSource )
    {
        for ( uint32 round = 1; round <= kRoundCount; ++round )
        {
            // 막는 대기 — 바쁜 대기(yield)는 ctest 가 GPU 일과 겹친 바쁜 기계에서 회차마다 밀려 1 만 회차가 시한(30 초)을 넘겼다.
            for ( uint32 observed = startedRound.load(); observed != round; observed = startedRound.load() )
            {
                sw::Futex::wait( startedRound, observed );
            }
            if ( bResetsSource )
                pSource.reset();
            else
                subscription.reset();
            finishedCount.fetch_add( 1 );
            sw::Futex::wakeAll( finishedCount );
        }
    };
    std::thread resetWorker{ runWorker, false };
    std::thread destroyWorker{ runWorker, true };
    for ( uint32 round = 1; round <= kRoundCount; ++round )
    {
        pSource      = sw::make_unique<ProbeSubscriptionSource>( probe );
        subscription = pSource->issue();
        startedRound.store( round );
        sw::Futex::wakeAll( startedRound );
        for ( uint32 observed = finishedCount.load(); observed != round * 2; observed = finishedCount.load() )
        {
            sw::Futex::wait( finishedCount, observed );
        }
    }
    resetWorker.join();
    destroyWorker.join();
    SW_EXPECT_TRUE( pSource == nullptr );
    SW_EXPECT_FALSE( subscription.isActive() );
    SW_EXPECT_TRUE( probe._releasedCount.load() <= kRoundCount ); // 출처가 먼저 닫힌 회차는 해제를 부르지 않는다
}

/**
 * @brief [DelegateTest] 출처를 닫는 소멸은 다른 스레드에서 진행 중인 해제가 끝날 때까지 돌아오지 않는다
 * @details 해제를 안에서 붙잡아 두고 다른 스레드가 출처를 지운다. 닫기가 기다리지 않으면 소멸이 먼저 끝나고, 해제는 소멸된 파생 안에서 이어진다.
 *          "아직 끝나지 않았다" 는 짧은 시한으로만 볼 수 있다 — 기계가 느리면 결함을 놓칠 수는 있어도 옳은 구현이 지지는 않는다.
 */
SW_TEST_CASE( DelegateTest, SourceCloseWaitsForReleaseInProgress )
{
    constexpr float64                       kCloseFinishedEarlySeconds = 0.2;
    ReleaseProbe                            probe;
    sw::unique_ptr<ProbeSubscriptionSource> pSource      = sw::make_unique<ProbeSubscriptionSource>( probe );
    sw::Subscription                        subscription = pSource->issue();
    probe._bReleaseHeld.store( true );

    std::thread resetWorker{ [&subscription]()
    { subscription.reset(); } };
    SW_REQUIRE_TRUE( sw::test::waitUntil( [&probe]()
    { return probe._bEntered.load(); } ) );

    sw::atomic<bool> bCloseStarted{ false };
    sw::atomic<bool> bCloseFinished{ false };
    std::thread      destroyWorker{ [&]()
    {
        bCloseStarted.store( true );
        pSource.reset();
        bCloseFinished.store( true );
    } };
    SW_EXPECT_TRUE( sw::test::waitUntil( [&bCloseStarted]()
    { return bCloseStarted.load(); } ) );
    const bool bClosedWhileReleasing = sw::test::waitUntil( [&bCloseFinished]()
    { return bCloseFinished.load(); }, kCloseFinishedEarlySeconds );
    SW_EXPECT_FALSE( bClosedWhileReleasing );

    probe._bReleaseHeld.store( false );
    resetWorker.join();
    destroyWorker.join();
    SW_EXPECT_TRUE( bCloseFinished.load() );
    SW_EXPECT_EQUAL( 1u, probe._releasedCount.load() );
}

/**
 * @brief [DelegateTest] 자기 해제 안에서 그 출처를 닫는 스레드는 기다리지 않는다(자기를 기다리면 영영 멈춘다)
 */
SW_TEST_CASE( DelegateTest, SourceClosedInsideItsOwnReleaseDoesNotWait )
{
    ReleaseProbe                            probe;
    sw::unique_ptr<ProbeSubscriptionSource> pSource = sw::make_unique<ProbeSubscriptionSource>( probe );
    probe._pSourceToDestroy                         = &pSource;
    sw::Subscription subscription                   = pSource->issue();
    subscription.reset();
    SW_EXPECT_TRUE( pSource == nullptr );
    SW_EXPECT_EQUAL( 1u, probe._releasedCount.load() );
    SW_EXPECT_FALSE( subscription.isActive() );
}

/**
 * @brief [DelegateTest] 자식 역할: 닫기(`closeSubscriptions`)를 빠뜨린 출처를 지운다 — 기반 소멸자가 멈춰야 한다.
 */
SW_TEST_CHILD_ROLE( DelegateTest, OpenSourceChild )
{
    sw::Subscription subscription;
    {
        OpenSubscriptionSource source;
        subscription = source.issue();
    }
    SW_EXPECT_FALSE( subscription.isActive() ); // 여기까지 오면 멈추지 않은 것이다 — 부모가 표지 글이 없어 진다
}

/**
 * @brief [DelegateTest] 파생 소멸자에서 닫지 않은 출처는 기반 소멸자가 멈춘다(빠뜨린 파생은 그 형식을 지우는 첫 테스트에서 드러난다)
 */
SW_TEST_CASE( DelegateTest, SourceLeftOpenAtDestructionIsFatal, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const sw::test::ChildRunResult child = sw::test::runChildRole( "DelegateTest.OpenSourceChild", {}, kOpenSourceChildTimeoutSeconds );
    SW_EXPECT_TRUE_MSG( child._exitCode.has_value() && *child._exitCode != 0, child.getOutputTail() );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "SubscriptionSource destroyed without closeSubscriptions" ), child.getOutputTail() );
}

/**
 * @brief [DelegateTest] 구독 비용(ns/짝 · 출처 만들고 지우기)을 로그로 남깁니다. 시간 문턱은 두지 않는다
 * @details 재는 법: Release `App --test --test-filter=DelegateTest.SubscribeCost` 5 번, 제어 블록 커밋과 그 앞에서 로그 `DelegateTest.SubscribeCost` 의 중앙값.
 *          판정(docs/NEXT.md 측정 "구독 제어 블록"): ns/짝 증가가 30 ns 를 넘으면 출처가 스레드 정책을 고르게 한다.
 */
SW_TEST_CASE( DelegateTest, SubscribeCost, ._tags = "bench" )
{
    constexpr uint32 kListenerCounts[] = { 1, 8, 64 };
    constexpr uint32 kPairsPerCount    = 64 * 1024;
    constexpr uint32 kSourceCount      = 10000;

    sw::MulticastDelegate<void( int32 )> multiDel;
    sw::vector<sw::Subscription>         listSubscription;
    for ( const uint32 listenerCount : kListenerCounts )
    {
        listSubscription.reserve( listenerCount );
        const uint32        roundCount = kPairsPerCount / listenerCount;
        const sw::Stopwatch stopwatch;
        for ( uint32 round = 0; round < roundCount; ++round )
        {
            for ( uint32 listener = 0; listener < listenerCount; ++listener )
            {
                listSubscription.push_back( multiDel.add( sw::Delegate<void( int32 )>::create( []( int32 ) {} ) ) );
            }
            for ( sw::Subscription& subscription : listSubscription )
            {
                subscription.reset();
            }
            listSubscription.clear();
        }
        SW_LOG_INFO_ID( "DelegateTest.SubscribeCost", "add + reset with %# listeners: %# ns per pair", listenerCount,
                        stopwatch.getElapsedNanoseconds() / static_cast<int64>( roundCount * listenerCount ) );
    }
    SW_EXPECT_FALSE( multiDel.isBound() );

    const sw::Stopwatch                                       sourceStopwatch;
    sw::vector<sw::unique_ptr<sw::MulticastDelegate<void()>>> listSource;
    listSource.reserve( kSourceCount );
    for ( uint32 sourceIndex = 0; sourceIndex < kSourceCount; ++sourceIndex )
    {
        listSource.push_back( sw::make_unique<sw::MulticastDelegate<void()>>() );
        listSubscription.push_back( listSource.back()->add( sw::Delegate<void()>::create( []() {} ) ) );
    }
    listSource.clear(); // 출처가 먼저 사라진다 — 구독은 무효가 된다
    SW_EXPECT_FALSE( listSubscription.front().isActive() );
    listSubscription.clear();
    SW_LOG_INFO_ID( "DelegateTest.SubscribeCost", "%# sources with one subscription created and destroyed: %# ns per source", kSourceCount,
                    sourceStopwatch.getElapsedNanoseconds() / static_cast<int64>( kSourceCount ) );
}

namespace
{
    /** @brief 약한 참조의 대상입니다(화면이 빌리는 모델 자리). */
    struct WeakRefTarget
    {
        int32             _value{ 7 };
        sw::WeakRefSource _weakRefSource;

        sw::WeakRefSource& getWeakRefSource() { return _weakRefSource; }
    };
} // namespace

/**
 * @brief [DelegateTest] 약한 참조는 대상이 사라지면 비고, 참조가 먼저 사라져도 대상은 그대로다
 * @details 엔진 UI 가 소유한 화면이 게임 인스턴스가 소유한 모델을 날 포인터로 들다 모델이 먼저 지워져 죽던 모양을 형식으로 막는다(언리얼 `TWeakObjectPtr`).
 */
SW_TEST_CASE( DelegateTest, WeakRefEmptiesWhenTheTargetIsDestroyed )
{
    sw::WeakRef<WeakRefTarget> reference;
    SW_EXPECT_NULL( reference.get() );
    {
        WeakRefTarget target;
        reference = sw::WeakRef<WeakRefTarget>{ target };
        SW_EXPECT_TRUE( reference.get() == &target );
        SW_EXPECT_EQUAL( 7, reference.get()->_value );

        sw::WeakRef<WeakRefTarget> moved{ std::move( reference ) };
        SW_EXPECT_TRUE( moved.get() == &target );
        SW_EXPECT_NULL( reference.get() ); // NOLINT(bugprone-use-after-move): 옮긴 참조는 빈다
        reference = std::move( moved );
        {
            const sw::WeakRef<WeakRefTarget> shortLived{ target }; // 참조가 먼저 사라진다
        }
        SW_EXPECT_TRUE( reference.get() == &target );
    }
    SW_EXPECT_NULL( reference.get() );
    reference.reset(); // 사라진 출처를 부르지 않아야 한다(ASan 이 잡는다)
}
