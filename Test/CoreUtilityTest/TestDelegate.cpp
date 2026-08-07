/**
 * @file TestDelegate.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"

#include "Core/Utility/Delegate/Delegate.h"

static int	s_TestValue = 0;
static void freeFunctionTest( int val )
{
	s_TestValue += val;
}

struct DummyListener
{
	int	 value = 0;
	void memberFunc( int val )
	{
		value += val;
	}
};

SW_TEST_CASE( Utility_Delegate, SingleDelegateFreeFunction )
{
	s_TestValue					  = 0;
	sw::Delegate<void( int )> del = SW_DELEGATE_FUNCTION( sw::Delegate<void( int )>, freeFunctionTest );
	SW_EXPECT_TRUE( del.isBound() );

	del( 10 );
	SW_EXPECT_EQUAL( 10, s_TestValue );
}

SW_TEST_CASE( Utility_Delegate, SingleDelegateMemberFunction )
{
	DummyListener			  listener;
	sw::Delegate<void( int )> del = SW_DELEGATE_METHOD( sw::Delegate<void( int )>, &DummyListener::memberFunc, &listener );
	SW_EXPECT_TRUE( del.isBound() );

	del( 25 );
	SW_EXPECT_EQUAL( 25, listener.value );
}

SW_TEST_CASE( Utility_Delegate, MulticastDelegateBroadcast )
{
	s_TestValue = 0;
	DummyListener listener;

	sw::MulticastDelegate<void( int )> multiDel;
	SW_EXPECT_FALSE( multiDel.isBound() );

	multiDel.add( SW_DELEGATE_FUNCTION( sw::Delegate<void( int )>, freeFunctionTest ) );
	multiDel.add( SW_DELEGATE_METHOD( sw::Delegate<void( int )>, &DummyListener::memberFunc, &listener ) );
	SW_EXPECT_TRUE( multiDel.isBound() );

	multiDel.broadcast( 5 );
	SW_EXPECT_EQUAL( 5, s_TestValue );
	SW_EXPECT_EQUAL( 5, listener.value );
}

SW_TEST_CASE( Utility_Delegate, MulticastDelegateRemoveSingle )
{
	s_TestValue = 0;

	sw::MulticastDelegate<void( int )> multiDel;
	sw::Delegate<void( int )>		   delFree = SW_DELEGATE_FUNCTION( sw::Delegate<void( int )>, freeFunctionTest );

	multiDel.add( delFree );
	SW_EXPECT_TRUE( multiDel.isBound() );

	multiDel.remove( delFree );
	SW_EXPECT_FALSE( multiDel.isBound() );

	multiDel.broadcast( 99 );
	SW_EXPECT_EQUAL( 0, s_TestValue );
}

SW_TEST_CASE( Utility_Delegate, MulticastDelegateRemoveAll )
{
	s_TestValue = 0;
	DummyListener listener;

	sw::MulticastDelegate<void( int )> multiDel;
	multiDel.add( SW_DELEGATE_FUNCTION( sw::Delegate<void( int )>, freeFunctionTest ) );
	multiDel.add( SW_DELEGATE_METHOD( sw::Delegate<void( int )>, &DummyListener::memberFunc, &listener ) );
	SW_EXPECT_TRUE( multiDel.isBound() );

	multiDel.removeAll();
	SW_EXPECT_FALSE( multiDel.isBound() );

	multiDel.broadcast( 10 );
	SW_EXPECT_EQUAL( 0, s_TestValue );
	SW_EXPECT_EQUAL( 0, listener.value );
}

SW_TEST_CASE( Utility_Delegate, MulticastDelegateLambda )
{
	int capturedA = 0;
	int capturedB = 0;

	auto lambdaA = [&]( int val )
	{ capturedA += val; };
	auto lambdaB = [&]( int val )
	{ capturedB += val * 2; };

	sw::MulticastDelegate<void( int )> multiDel;
	multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void( int )>, lambdaA ) );
	multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void( int )>, lambdaB ) );
	SW_EXPECT_TRUE( multiDel.isBound() );

	multiDel.broadcast( 3 );
	SW_EXPECT_EQUAL( 3, capturedA );
	SW_EXPECT_EQUAL( 6, capturedB );
}

SW_TEST_CASE( Utility_Delegate, MulticastDelegateMultipleBroadcasts )
{
	s_TestValue = 0;

	sw::MulticastDelegate<void( int )> multiDel;
	multiDel.add( SW_DELEGATE_FUNCTION( sw::Delegate<void( int )>, freeFunctionTest ) );

	multiDel.broadcast( 10 );
	multiDel.broadcast( 20 );
	multiDel.broadcast( 30 );

	SW_EXPECT_EQUAL( 60, s_TestValue );
}

SW_TEST_CASE( Utility_Delegate, MulticastDelegateHandle )
{
	int32							   val = 0;
	sw::MulticastDelegate<void( int )> multiDel;

	auto lambda = [&]( int delta )
	{ val += delta; };
	sw::DelegateHandle h1 = multiDel.add( SW_DELEGATE_LAMBDA( sw::Delegate<void( int )>, lambda ) );
	SW_EXPECT_TRUE( h1.isValid() );

	multiDel.broadcast( 100 );
	SW_EXPECT_EQUAL( 100, val );

	multiDel.remove( h1 );
	multiDel.broadcast( 50 );
	SW_EXPECT_EQUAL( 100, val );
}

struct ConstListener
{
	int calculate( int a, int b ) const
	{
		return a + b;
	}
};

SW_TEST_CASE( Utility_Delegate, DelegateFullCoverage )
{
	sw::Delegate<int( int, int )> nullDel;
	SW_EXPECT_FALSE( nullDel.isBound() );
	SW_EXPECT_TRUE( nullDel == nullptr );

	ConstListener				  listener;
	sw::Delegate<int( int, int )> del = SW_DELEGATE_METHOD( sw::Delegate<int( int, int )>, &ConstListener::calculate, &listener );

	SW_EXPECT_TRUE( del.isBound() );
	SW_EXPECT_TRUE( del != nullptr );
	SW_EXPECT_EQUAL( 15, del( 7, 8 ) );

	sw::Delegate<int( int, int )> delSame = SW_DELEGATE_METHOD( sw::Delegate<int( int, int )>, &ConstListener::calculate, &listener );
	SW_EXPECT_TRUE( del == delSame );

	sw::MulticastDelegate<void( int )> multiDel;
	int								   val	  = 0;
	auto							   lambda = [&]( int delta )
	{ val += delta; };
	sw::Delegate<void( int )> handle = SW_DELEGATE_LAMBDA( sw::Delegate<void( int )>, lambda );

	multiDel.add( handle );
	multiDel.broadcast( 20 );
	SW_EXPECT_EQUAL( 20, val );

	multiDel.remove( handle );
	multiDel.broadcast( 30 );
	SW_EXPECT_EQUAL( 20, val );
}
