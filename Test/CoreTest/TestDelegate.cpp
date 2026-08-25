#include "pch.h"

#include "TestFramework/TestFramework.h"

static int32 s_TestValue{ 0 };
/** @brief 자유 함수 델리게이트가 누적할 값을 더합니다. */
static void freeFunctionTest( int32 val )
{
	s_TestValue += val;
}

struct DummyListener
{
	int32 value{ 0 };
	/** @brief 멤버 함수 델리게이트가 누적할 값을 더합니다. */
	void memberFunc( int32 val )
	{
		value += val;
	}
};

// ------------------------------------------------------------------------------
// 1) Core_Delegate — 단일·멀티캐스트
// ------------------------------------------------------------------------------
/**
 * @brief [Core_Delegate] 단일 델리게이트 자유 함수
 */
SW_TEST_CASE( Core_Delegate, SingleDelegateFreeFunction )
{
	s_TestValue						= 0;
	sw::Delegate<void( int32 )> del = SW_DELEGATE_FUNCTION( sw::Delegate<void( int32 )>, freeFunctionTest );
	SW_EXPECT_TRUE( del.isBound() );

	del( 10 );
	SW_EXPECT_EQUAL( 10, s_TestValue );
}

/**
 * @brief [Core_Delegate] 단일 델리게이트 멤버 함수
 */
SW_TEST_CASE( Core_Delegate, SingleDelegateMemberFunction )
{
	DummyListener				listener;
	sw::Delegate<void( int32 )> del = SW_DELEGATE_METHOD( sw::Delegate<void( int32 )>, &DummyListener::memberFunc, &listener );
	SW_EXPECT_TRUE( del.isBound() );

	del( 25 );
	SW_EXPECT_EQUAL( 25, listener.value );
}

// ------------------------------------------------------------------------------
// 2) 멀티캐스트 — 브로드캐스트·제거·핸들
// ------------------------------------------------------------------------------
/**
 * @brief [Core_Delegate] 멀티캐스트 델리게이트 브로드캐스트
 */
SW_TEST_CASE( Core_Delegate, MulticastDelegateBroadcast )
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
	SW_EXPECT_EQUAL( 5, listener.value );
}

/**
 * @brief [Core_Delegate] 멀티캐스트 델리게이트 단일 제거
 */
SW_TEST_CASE( Core_Delegate, MulticastDelegateRemoveSingle )
{
	s_TestValue = 0;

	sw::MulticastDelegate<void( int32 )> multiDel;
	sw::Delegate<void( int32 )>			 delFree = SW_DELEGATE_FUNCTION( sw::Delegate<void( int32 )>, freeFunctionTest );

	multiDel.add( delFree );
	SW_EXPECT_TRUE( multiDel.isBound() );

	multiDel.remove( delFree );
	SW_EXPECT_FALSE( multiDel.isBound() );

	multiDel.broadcast( 99 );
	SW_EXPECT_EQUAL( 0, s_TestValue );
}

/**
 * @brief [Core_Delegate] 멀티캐스트 델리게이트 전체 제거
 */
SW_TEST_CASE( Core_Delegate, MulticastDelegateRemoveAll )
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
	SW_EXPECT_EQUAL( 0, listener.value );
}

/**
 * @brief [Core_Delegate] 멀티캐스트 델리게이트 람다
 */
SW_TEST_CASE( Core_Delegate, MulticastDelegateLambda )
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
 * @brief [Core_Delegate] 멀티캐스트 델리게이트 다중 브로드캐스트
 */
SW_TEST_CASE( Core_Delegate, MulticastDelegateMultipleBroadcasts )
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
 * @brief [Core_Delegate] 멀티캐스트 델리게이트 핸들
 */
SW_TEST_CASE( Core_Delegate, MulticastDelegateHandle )
{
	int32								 val{ 0 };
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
 * @brief [Core_Delegate] 델리게이트 전체 커버리지
 */
SW_TEST_CASE( Core_Delegate, DelegateFullCoverage )
{
	sw::Delegate<int32( int32, int32 )> nullDel;
	SW_EXPECT_FALSE( nullDel.isBound() );
	SW_EXPECT_TRUE( nullDel == nullptr );

	ConstListener						listener;
	sw::Delegate<int32( int32, int32 )> del = SW_DELEGATE_METHOD( sw::Delegate<int32( int32, int32 )>, &ConstListener::calculate, &listener );

	SW_EXPECT_TRUE( del.isBound() );
	SW_EXPECT_TRUE( del != nullptr );
	SW_EXPECT_EQUAL( 15, del( 7, 8 ) );

	sw::Delegate<int32( int32, int32 )> delSame = SW_DELEGATE_METHOD( sw::Delegate<int32( int32, int32 )>, &ConstListener::calculate, &listener );
	SW_EXPECT_TRUE( del == delSame );

	sw::MulticastDelegate<void( int32 )> multiDel;
	int32								 val{ 0 };
	sw::Delegate<void( int32 )>			 delLambda = SW_DELEGATE_LAMBDA( sw::Delegate<void( int32 )>, [&]( int32 delta )
	{ val += delta; } );

	auto handle = multiDel.add( delLambda );
	multiDel.broadcast( 20 );
	SW_EXPECT_EQUAL( 20, val );

	multiDel.remove( handle );
	multiDel.broadcast( 30 );
	SW_EXPECT_EQUAL( 20, val );
}
