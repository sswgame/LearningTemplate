#include "pch.h"

#include "Core/Container/KeyedPendingList.h"
#include "Core/Container/vector.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [KeyedPendingListTest] 키가 기준값 이하인 항목만 넣은 순서대로 꺼내고, 남는 항목의 순서도 지킨다
 */
SW_TEST_CASE( KeyedPendingListTest, ExtractReadyKeepsInsertionOrder )
{
    KeyedPendingList<int32> list;
    list.push( 1, 5 );
    list.push( 2, 3 );
    list.push( 3, 7 );
    list.push( 4, 3 );
    SW_EXPECT_EQUAL( size_t( 2 ), list.countReady( 3 ) );

    vector<int32> listReady;
    list.extractReady( 3, listReady );
    SW_ASSERT_EQUAL( size_t( 2 ), listReady.size() );
    SW_EXPECT_EQUAL( 2, listReady[0] );
    SW_EXPECT_EQUAL( 4, listReady[1] );
    SW_EXPECT_EQUAL( size_t( 2 ), list.size() );

    // 기준값이 닿지 않으면 아무것도 꺼내지 않는다.
    list.extractReady( 4, listReady );
    SW_EXPECT_EQUAL( size_t( 2 ), listReady.size() );

    // 경계(키 == 기준값)는 꺼낸다. 꺼낸 것은 기존 그릇 뒤에 붙는다.
    list.extractReady( 7, listReady );
    SW_ASSERT_EQUAL( size_t( 4 ), listReady.size() );
    SW_EXPECT_EQUAL( 1, listReady[2] );
    SW_EXPECT_EQUAL( 3, listReady[3] );
    SW_EXPECT_TRUE( list.isEmpty() );
}

/**
 * @brief [KeyedPendingListTest] swap 으로 통째로 넘겨받은 뒤 extractAll 은 키와 상관없이 넣은 순서대로 모두 꺼낸다
 */
SW_TEST_CASE( KeyedPendingListTest, SwapThenExtractAllTakesEverything )
{
    KeyedPendingList<int32> list;
    list.push( 10, 100 );
    list.push( 20, 1 );

    KeyedPendingList<int32> taken;
    taken.swap( list );
    SW_EXPECT_TRUE( list.isEmpty() );
    SW_EXPECT_EQUAL( size_t( 2 ), taken.size() );

    vector<int32> listAll;
    taken.extractAll( listAll );
    SW_ASSERT_EQUAL( size_t( 2 ), listAll.size() );
    SW_EXPECT_EQUAL( 10, listAll[0] );
    SW_EXPECT_EQUAL( 20, listAll[1] );
    SW_EXPECT_TRUE( taken.isEmpty() );
}
