#include "pch.h"

#include "Core/Container/PagedArray.h"
#include "Core/Container/vector.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [PagedArrayTest] 청크는 처음 닿을 때 생기고, 그 전에는 `find` 가 nullptr 을 돌려줍니다.
 */
SW_TEST_CASE( PagedArrayTest, FindIsNullUntilEnsured )
{
    PagedArray<uint64, 16, 4> pages;
    SW_EXPECT_EQUAL( 64ull, ( PagedArray<uint64, 16, 4>::kCapacity ) );
    SW_EXPECT_TRUE( pages.find( 0 ) == nullptr );
    SW_EXPECT_TRUE( pages.find( 63 ) == nullptr );

    uint64* pValue = pages.ensure( 20 );
    SW_ASSERT_TRUE( pValue != nullptr );
    SW_EXPECT_EQUAL( 0ull, *pValue ); // 새 청크는 값 초기화된다
    *pValue = 7;

    // 같은 청크(16..31)의 다른 칸은 이제 보이고, 다른 청크는 여전히 없다.
    SW_EXPECT_TRUE( pages.find( 16 ) != nullptr );
    SW_EXPECT_TRUE( pages.find( 31 ) != nullptr );
    SW_EXPECT_TRUE( pages.find( 15 ) == nullptr );
    SW_EXPECT_TRUE( pages.find( 32 ) == nullptr );
    SW_EXPECT_EQUAL( 7ull, *pages.find( 20 ) );
}

/**
 * @brief [PagedArrayTest] 범위 밖 인덱스는 `find` · `ensure` 모두 nullptr 입니다.
 */
SW_TEST_CASE( PagedArrayTest, OutOfRangeIsRejected )
{
    PagedArray<uint32, 8, 2> pages;
    SW_EXPECT_TRUE( ( PagedArray<uint32, 8, 2>::isInRange( 15 ) ) );
    SW_EXPECT_FALSE( ( PagedArray<uint32, 8, 2>::isInRange( 16 ) ) );
    SW_EXPECT_TRUE( pages.ensure( 16 ) == nullptr );
    SW_EXPECT_TRUE( pages.find( 16 ) == nullptr );
    SW_EXPECT_TRUE( pages.ensure( 15 ) != nullptr );
}

/**
 * @brief [PagedArrayTest] 청크를 더 붙여도 이미 돌려준 원소의 주소는 그대로입니다(이 컨테이너가 있는 이유).
 */
SW_TEST_CASE( PagedArrayTest, AddressesSurviveGrowth )
{
    PagedArray<uint32, 4, 64> pages;
    vector<uint32*>           listAddress;
    for ( uint32 index = 0; index < 256; ++index )
    {
        uint32* pValue = pages.ensure( index );
        SW_ASSERT_TRUE( pValue != nullptr );
        *pValue = index * 3;
        listAddress.push_back( pValue );
    }
    for ( uint32 index = 0; index < 256; ++index )
    {
        SW_EXPECT_TRUE( pages.find( index ) == listAddress[index] );
        SW_EXPECT_EQUAL( index * 3, *listAddress[index] );
    }
}

/**
 * @brief [PagedArrayTest] `forEachElement` 는 만들어진 청크만 돌고, `releaseChunks` 뒤에는 아무것도 없습니다.
 */
SW_TEST_CASE( PagedArrayTest, ForEachVisitsAllocatedChunksOnly )
{
    PagedArray<uint32, 8, 8> pages;
    *pages.ensure( 3 )  = 1;
    *pages.ensure( 42 ) = 1;

    uint32 visitCount = 0;
    uint32 sum        = 0;
    pages.forEachElement( [&visitCount, &sum]( uint32& value )
    {
        ++visitCount;
        sum += value;
    } );
    SW_EXPECT_EQUAL( 16u, visitCount ); // 청크 둘(0..7, 40..47) × 8 칸
    SW_EXPECT_EQUAL( 2u, sum );

    pages.releaseChunks();
    SW_EXPECT_TRUE( pages.find( 3 ) == nullptr );
    SW_EXPECT_TRUE( pages.find( 42 ) == nullptr );
    visitCount = 0;
    pages.forEachElement( [&visitCount]( uint32& )
    { ++visitCount; } );
    SW_EXPECT_EQUAL( 0u, visitCount );
}
