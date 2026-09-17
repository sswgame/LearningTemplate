#include "pch.h"

#include "TestFramework/TestFramework.h"

static int32 s_TestValue{ 0 };
namespace
{
    /** @brief 자유 함수 델리게이트가 누적할 값을 더합니다. */
    void freeFunctionTest( int32 val )
    {
        s_TestValue += val;
    }
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

    multiDel.add( SW_DELEGATE_FUNCTION( sw::Delegate<void( int32 )>, freeFunctionTest ) );
    multiDel.add( SW_DELEGATE_METHOD( sw::Delegate<void( int32 )>, &DummyListener::memberFunc, &listener ) );
    SW_EXPECT_TRUE( multiDel.isBound() );

    multiDel.broadcast( 5 );
    SW_EXPECT_EQUAL( 5, s_TestValue );
    SW_EXPECT_EQUAL( 5, listener._value );
}

/**
 * @brief [DelegateTest] 멀티캐스트 델리게이트 단일 제거
 */
SW_TEST_CASE( DelegateTest, MulticastDelegateRemoveSingle )
{
    s_TestValue = 0;

    sw::MulticastDelegate<void( int32 )> multiDel;
    sw::Delegate<void( int32 )>          delFree = SW_DELEGATE_FUNCTION( sw::Delegate<void( int32 )>, freeFunctionTest );

    multiDel.add( delFree );
    SW_EXPECT_TRUE( multiDel.isBound() );

    multiDel.remove( delFree );
    SW_EXPECT_FALSE( multiDel.isBound() );

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
    multiDel.add( SW_DELEGATE_FUNCTION( sw::Delegate<void( int32 )>, freeFunctionTest ) );
    multiDel.add( SW_DELEGATE_METHOD( sw::Delegate<void( int32 )>, &DummyListener::memberFunc, &listener ) );
    SW_EXPECT_TRUE( multiDel.isBound() );

    multiDel.removeAll();
    SW_EXPECT_FALSE( multiDel.isBound() );

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
    multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void( int32 )>, [&]( int32 val )
    { capturedA += val; } ) );
    multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void( int32 )>, [&]( int32 val )
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
    multiDel.add( SW_DELEGATE_FUNCTION( sw::Delegate<void( int32 )>, freeFunctionTest ) );

    multiDel.broadcast( 10 );
    multiDel.broadcast( 20 );
    multiDel.broadcast( 30 );

    SW_EXPECT_EQUAL( 60, s_TestValue );
}

/**
 * @brief [DelegateTest] 멀티캐스트 델리게이트 핸들
 */
SW_TEST_CASE( DelegateTest, MulticastDelegateHandle )
{
    int32                                val{ 0 };
    sw::MulticastDelegate<void( int32 )> multiDel;

    sw::DelegateHandle h1 = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void( int32 )>, [&]( int32 delta )
    { val += delta; } ) );
    SW_EXPECT_TRUE( h1.isValid() );

    multiDel.broadcast( 100 );
    SW_EXPECT_EQUAL( 100, val );

    multiDel.remove( h1 );
    multiDel.broadcast( 50 );
    SW_EXPECT_EQUAL( 100, val );
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

    auto handle = multiDel.add( delLambda );
    multiDel.broadcast( 20 );
    SW_EXPECT_EQUAL( 20, val );

    multiDel.remove( handle );
    multiDel.broadcast( 30 );
    SW_EXPECT_EQUAL( 20, val );
}

/**
 * @brief [DelegateTest] MulticastDelegate 브로드캐스트 도중 자기 자신 또는 후속 리스너 remove 시 안전성 검증
 */
SW_TEST_CASE( DelegateTest, MulticastDelegateDeferredRemoveDuringBroadcast )
{
    sw::MulticastDelegate<void()> multiDel;
    int32                         countA = 0;
    int32                         countB = 0;

    sw::DelegateHandle handleA;
    sw::DelegateHandle handleB;

    handleA = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&]()
    {
        countA++;
        // A 실행 중 B를 remove -> B는 unbind되어 이번 브로드캐스트 및 다음 브로드캐스트에서 실행되지 않아야 함
        multiDel.remove( handleB );
    } ) );

    handleB = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&]()
    {
        countB++;
    } ) );

    multiDel.broadcast();
    SW_EXPECT_EQUAL( 1, countA );
    SW_EXPECT_EQUAL( 0, countB );

    multiDel.broadcast();
    SW_EXPECT_EQUAL( 2, countA );
    SW_EXPECT_EQUAL( 0, countB );
}

/**
 * @brief [DelegateTest] MulticastDelegate 는 **진짜로 이동한다** (복사로 떨어지지 않는다)
 * @details 복사 생성자를 `= default` 로 *선언* 하면 암시적 이동 생성자·이동 대입이 생기지 않는다.
 *          그 상태였기 때문에 `std::move` 를 써도 구독자 벡터가 통째로 깊은 복사됐고,
 *          `is_nothrow_move_constructible` 이 false 였다. 옮긴 뒤 원본이 비는지로 확인한다 —
 *          복사로 떨어지면 원본이 그대로 남는다.
 */
SW_TEST_CASE( DelegateTest, MulticastDelegateMovesInsteadOfCopying )
{
    using Multi = sw::MulticastDelegate<void()>;

    static_assert( std::is_nothrow_move_constructible_v<Multi>,
                   "이동 생성자가 없다 — 복사 생성자를 직접 선언하면 암시적 이동이 사라진다" );
    static_assert( std::is_nothrow_move_assignable_v<Multi>, "이동 대입이 없다" );

    int32 callCount = 0;

    Multi source;
    source.add( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&callCount]()
    { ++callCount; } ) );
    SW_ASSERT_TRUE( source.isBound() );

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
 *          예전에는 복사 생성자가 `= default` 라 그 둘까지 같이 복사됐다 — broadcast 중에 복사하면
 *          사본이 깊이 0 이 아닌 채로 태어나, 그 사본에서 `remove` 한 것이 **영영 반영되지 않는다**
 *          (자기 broadcast 는 1→2→1 로만 오가므로 0 이 안 된다).
 *
 * @note **복사 생성자를 타야 한다.** 복사 대입은 받는 쪽의 깊이를 건드리지 않으므로(그 깊이는 지금
 *       그 객체를 방송 중인 호출 스택의 것이다) 이 결함을 드러내지 못한다 — 처음에 대입으로 썼다가
 *       변이 테스트에서 통과해 버려 다시 썼다.
 */
SW_TEST_CASE( DelegateTest, CopyMadeDuringBroadcastStartsWithCleanBroadcastState )
{
    using Multi = sw::MulticastDelegate<void()>;

    int32 sourceCount = 0;

    Multi                 source;
    sw::unique_ptr<Multi> pCopy;

    // 방송 도중에 **복사 생성자**로 사본을 만든다 — 그 순간 원본의 _broadcastDepth 는 1 이다.
    source.add( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&]()
    {
        ++sourceCount;
        pCopy = sw::make_unique<Multi>( source );
    } ) );

    source.broadcast();
    SW_ASSERT_TRUE( pCopy != nullptr );
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
