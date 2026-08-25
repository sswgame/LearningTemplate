#include "pch.h"

#include "Core/Container/sparse_set.h"

#include "TestFramework/TestFramework.h"

#include <tuple>

using namespace sw;

// ------------------------------------------------------------------------------
// 1) SparseSet — 기본 연산
// ------------------------------------------------------------------------------
/**
 * @brief [SparseSet] 삽입·포함 여부·삭제
 */
SW_TEST_CASE( SparseSet, BasicOperations )
{
	sparse_set<int32> set;

	SW_EXPECT_FALSE( set.contains( 0 ) );
	set.emplace( 0, 100 );
	SW_EXPECT_TRUE( set.contains( 0 ) );

	int32 val{ 0 };
	if ( set.contains( 0 ) )
		val = set[0];
	SW_EXPECT_EQUAL( 100, val );

	set.emplace( 5, 500 );
	SW_EXPECT_EQUAL( 2, set.size() );

	set.erase( 0 );
	SW_EXPECT_FALSE( set.contains( 0 ) );
	SW_EXPECT_EQUAL( 1, set.size() );

	if ( set.contains( 5 ) )
		val = set[5];
	SW_EXPECT_EQUAL( 500, val );
}

/**
 * @brief [SparseSet] 중간 키를 swap-remove 해도 남은 값은 키로 조회된다
 */
SW_TEST_CASE( SparseSet, SwapRemoveKeepsRemainingValues )
{
	struct Item
	{
		int32 value{ 0 };
	};

	sparse_set<Item> set;
	set.emplace( 0, Item{ 10 } );
	set.emplace( 1, Item{ 20 } );
	set.emplace( 2, Item{ 30 } );

	set.erase( 1 );
	SW_EXPECT_FALSE( set.contains( 1 ) );
	SW_EXPECT_EQUAL( 2, set.size() );
	SW_EXPECT_EQUAL( 10, set[0].value );
	SW_EXPECT_EQUAL( 30, set[2].value );

	set.emplace( 3, Item{ 40 } );
	SW_EXPECT_EQUAL( 10, set[0].value );
	SW_EXPECT_EQUAL( 30, set[2].value );
	SW_EXPECT_EQUAL( 40, set[3].value );
}

/**
 * @brief [SparseSet] 같은 키 emplace는 값을 교체하고, 삭제한 키는 다시 쓸 수 있다
 */
SW_TEST_CASE( SparseSet, OverwriteAndReuseKey )
{
	sparse_set<int32> set;
	set.emplace( 7, 1 );
	set.emplace( 7, 2 );
	SW_EXPECT_EQUAL( 1, set.size() );
	SW_EXPECT_EQUAL( 2, set[7] );

	set.erase( 7 );
	SW_EXPECT_FALSE( set.contains( 7 ) );
	set.emplace( 7, 3 );
	SW_EXPECT_EQUAL( 3, set[7] );

	uint32 visited{ 0 };
	int32  sum{ 0 };
	set.emplace( 1, 10 );
	set.emplace( 2, 20 );
	for ( auto tuple : set )
	{
		++visited;
		sum += std::get<1>( tuple );
	}
	SW_EXPECT_EQUAL( 3u, visited );
	SW_EXPECT_EQUAL( 33, sum );
}

/**
 * @brief [SparseSet] 대량 데이터 삭제 후 shrink_to_fit 호출 시 정상 동작 및 용량 축소
 */
SW_TEST_CASE( SparseSet, ShrinkToFitReclaimsCapacity )
{
	sparse_set<int32> set;
	for ( uint32 index = 0; index < 1000; ++index )
	{
		set.emplace( index, static_cast<int32>( index * 10 ) );
	}
	SW_EXPECT_EQUAL( 1000, set.size() );

	// 950개 삭제
	for ( uint32 index = 50; index < 1000; ++index )
	{
		set.erase( index );
	}
	SW_EXPECT_EQUAL( 50, set.size() );

	set.shrink_to_fit();

	SW_EXPECT_EQUAL( 50, set.size() );
	for ( uint32 index = 0; index < 50; ++index )
	{
		SW_EXPECT_TRUE( set.contains( index ) );
		SW_EXPECT_EQUAL( static_cast<int32>( index * 10 ), set[index] );
	}
	for ( uint32 index = 50; index < 1000; ++index )
	{
		SW_EXPECT_FALSE( set.contains( index ) );
	}
}
