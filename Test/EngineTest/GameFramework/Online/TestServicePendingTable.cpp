// 응답 대기 표 — 꼬리표는 요청마다 다르고, 한 번만 꺼낸다.
#include "pch.h"

#include "GameFramework/Base/Online/Service/ServicePendingTable.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( ServicePendingTableTest, TagsAreDistinctAndTakenOnce )
{
    ServicePendingTable table;
    NetRequestToken     first;
    first._handle    = StreamConnectionHandle::make( 1, 1 );
    first._requestId = 10;
    NetRequestToken second;
    second._handle         = StreamConnectionHandle::make( 2, 1 );
    second._requestId      = 11;
    const uint64 firstTag  = table.add( first );
    const uint64 secondTag = table.add( second );
    SW_EXPECT_TRUE( firstTag != 0 && secondTag != 0 );
    SW_EXPECT_NOT_EQUAL( firstTag, secondTag );
    SW_EXPECT_EQUAL( table.getCount(), 2 );

    NetRequestToken taken;
    SW_ASSERT_TRUE( table.take( firstTag, taken ) );
    SW_EXPECT_EQUAL( taken._requestId, uint64( 10 ) );
    SW_EXPECT_TRUE( taken._handle == first._handle );
    SW_EXPECT_FALSE( table.take( firstTag, taken ) );

    table.clear();
    SW_EXPECT_FALSE( table.take( secondTag, taken ) );
    SW_EXPECT_EQUAL( table.getCount(), 0 );
}
