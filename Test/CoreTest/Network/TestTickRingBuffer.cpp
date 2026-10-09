#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "TestFramework/TestFramework.h"

// 틱 고리 — 키 전체로 찾기(덮인 자리는 없다), 들어 있는 수 · 가장 새 틱 · 들 수 있는 가장 오래된 틱, 지우기, 비우기는 값(버퍼)을 남긴다, 빈 표시는 찾지 못한다.

using namespace sw;

/**
 * @brief [TickRingBufferTest] 한 바퀴 뒤의 틱이 덮은 자리는 없다고 하고, 들어 있는 수 · 가장 새 틱 · 가장 오래된 틱이 따라온다
 */
SW_TEST_CASE( TickRingBufferTest, OverwrittenTicksAreForgotten )
{
    TickRingBuffer<int32> buffer;
    SW_EXPECT_NULL( buffer.find( 0 ) ); // 자리를 잡기 전
    SW_EXPECT_EQUAL( 0, buffer.getCapacity() );
    buffer.initialize( 4 );
    for ( uint32 tick = 0; tick < 6; ++tick )
    {
        buffer.acquire( tick ) = static_cast<int32>( tick ) * 10;
    }
    SW_EXPECT_NULL( buffer.find( 0 ) ); // 4 가 덮었다
    SW_EXPECT_NULL( buffer.find( 1 ) ); // 5 가 덮었다
    SW_ASSERT_NOT_NULL( buffer.find( 5 ) );
    SW_EXPECT_EQUAL( 50, *buffer.find( 5 ) );
    SW_EXPECT_FALSE( buffer.exists( 9 ) ); // 5 와 같은 자리지만 다른 틱
    SW_EXPECT_EQUAL( 4, buffer.getCount() );
    SW_EXPECT_EQUAL( 5u, buffer.getNewestTick() );
    SW_EXPECT_EQUAL( 2u, buffer.computeOldestTick() );

    buffer.acquire( 3 ) = 31; // 들어 있는 틱을 다시 — 수 · 가장 새 틱은 그대로
    SW_EXPECT_EQUAL( 31, *buffer.find( 3 ) );
    SW_EXPECT_EQUAL( 4, buffer.getCount() );
    SW_EXPECT_EQUAL( 5u, buffer.getNewestTick() );
    buffer.acquire( 1 ) = 11; // 옛 틱이 5 의 자리를 덮는다 — 가장 새 틱은 줄지 않는다
    SW_EXPECT_NULL( buffer.find( 5 ) );
    SW_EXPECT_EQUAL( 5u, buffer.getNewestTick() );
    SW_EXPECT_EQUAL( 4, buffer.getCount() );
}

/**
 * @brief [TickRingBufferTest] 지우기는 그 틱만, 비우기는 틱만 잊고 값은 자리에 남긴다 — acquire 는 옛 값을 비우지 않는다(버퍼 다시 쓰기)
 */
SW_TEST_CASE( TickRingBufferTest, RemoveAndResetForgetTicksButKeepSlotValues )
{
    TickRingBuffer<vector<int32>> buffer;
    buffer.initialize( 2 );
    buffer.acquire( 7 ).push_back( 1 );
    buffer.remove( 9 ); // 같은 자리, 다른 틱 — 그대로
    SW_EXPECT_TRUE( buffer.exists( 7 ) );
    buffer.remove( 7 );
    SW_EXPECT_FALSE( buffer.exists( 7 ) );
    SW_EXPECT_EQUAL( 0, buffer.getCount() );
    SW_EXPECT_EQUAL( 7u, buffer.getNewestTick() ); // 지워도 가장 새 틱은 그대로

    buffer.acquire( 8 ).push_back( 2 );
    buffer.reset();
    SW_EXPECT_FALSE( buffer.hasNewest() );
    SW_EXPECT_EQUAL( 0, buffer.getCount() );
    SW_EXPECT_NULL( buffer.find( 8 ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( buffer.acquire( 10 ).size() ) ); // 8 의 자리 — 옛 값이 남아 있다
    SW_EXPECT_EQUAL( 1, static_cast<int32>( buffer.acquire( 11 ).size() ) ); // 7 의 자리
}

/**
 * @brief [TickRingBufferTest] 빈 자리 표시(kEmpty)는 넣어도 찾지 못하고 수에 들지 않는다
 */
SW_TEST_CASE( TickRingBufferTest, EmptyMarkerIsNeverFound )
{
    TickRingBuffer<int32> buffer;
    buffer.initialize( 4 );
    SW_EXPECT_NULL( buffer.find( TickRingBuffer<int32>::kEmpty ) );
    buffer.acquire( TickRingBuffer<int32>::kEmpty ) = 1;
    SW_EXPECT_NULL( buffer.find( TickRingBuffer<int32>::kEmpty ) );
    SW_EXPECT_EQUAL( 0, buffer.getCount() );
    buffer.acquire( 3 ) = 2; // kEmpty 와 같은 자리(0xFFFFFFFF % 4 = 3)
    SW_EXPECT_EQUAL( 1, buffer.getCount() );
}
