#include "pch.h"

#include "Core/Testing/TestFramework.h"

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
        sw::Subscription issue() { return issueSubscription( _nextID.fetch_add( 1 ) + 1 ); }
        uint32           getReleasedCount() const { return _releasedCount.load(); }

    private:
        void releaseSubscription( uint64 id ) override
        {
            (void)id;
            _releasedCount.fetch_add( 1 );
        }

        std::atomic<uint64> _nextID{ 0 };
        std::atomic<uint32> _releasedCount{ 0 };
    };
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
