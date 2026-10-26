// RingBuffer — 가득 차면 가장 오래된 값을 덮어쓰고, 순회 · 첨자는 오래된 것부터 새것 순서인지.
#include "pch.h"

#include "Core/Container/RingBuffer.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( RingBufferTest, OverwritesOldestWhenFull )
{
    RingBuffer<int32, 4> ring;
    SW_EXPECT_TRUE( ring.empty() );
    for ( int32 value = 1; value <= 6; ++value )
    {
        ring.push( value );
    }
    SW_ASSERT_EQUAL( 4u, ring.size() );
    SW_EXPECT_TRUE( ring.full() );
    SW_EXPECT_EQUAL( 3, ring.front() ); // 1, 2 는 덮였다
    SW_EXPECT_EQUAL( 6, ring.back() );
    SW_EXPECT_EQUAL( 4, ring[1] );

    int32 expected = 3;
    for ( const int32 value : ring )
    {
        SW_EXPECT_EQUAL( expected, value );
        ++expected;
    }
    SW_EXPECT_EQUAL( 7, expected );
}

SW_TEST_CASE( RingBufferTest, PopFrontAndClear )
{
    RingBuffer<int32, 2> ring;
    ring.push( 10 );
    ring.push( 20 );
    ring.popFront();
    SW_ASSERT_EQUAL( 1u, ring.size() );
    SW_EXPECT_EQUAL( 20, ring.front() );
    ring.push( 30 ); // 시작이 한 칸 밀린 뒤에도 감겨 들어간다
    SW_EXPECT_EQUAL( 20, ring[0] );
    SW_EXPECT_EQUAL( 30, ring[1] );
    ring.clear();
    SW_EXPECT_TRUE( ring.empty() );
    SW_EXPECT_TRUE( ring.begin() == ring.end() );
}
