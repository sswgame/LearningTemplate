/**
 * @file TestShardedCounter.cpp
 * @brief ShardedCounter — 여러 스레드의 더하기가 합에 모두 들어가는지, 칸 번호가 스레드마다 고정인지, "같은 칸에서 올리고 내리기" 규칙에서 합이 살아 있는 항목을 놓치지 않는지.
 */
#include "pch.h"

#include "Core/Concurrency/ShardedCounter.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

namespace
{
    struct ShardedCounterTestInternal
    {
        /** @brief 더하는 스레드 수입니다. 칸 수보다 많게 잡아 칸을 나눠 쓰는 경우도 지난다. */
        static constexpr uint32 kThreadCount = sw::ShardedCounterUtil::kShardCount + 4;
        /** @brief 스레드마다 더하는 횟수입니다. */
        static constexpr uint32 kAddCount = 20000;
        /** @brief 생겼다 사라지는 항목을 만드는 스레드 수입니다. */
        static constexpr uint32 kChurnThreadCount = 4;
        /** @brief 생겼다 사라지는 항목을 만드는 시간(밀리초)입니다. */
        static constexpr int64 kChurnMilli = 50;
    };
} // namespace

/**
 * @brief [ShardedCounterTest] 여러 스레드가 지금 칸에 더한 값이 합에 빠짐없이 들어간다(칸 수보다 스레드가 많아도)
 */
SW_TEST_CASE( ShardedCounterTest, SumIncludesEveryAddFromEveryThread )
{
    sw::ShardedCounter<int64> counter;
    sw::vector<std::thread>   listThread;
    listThread.reserve( ShardedCounterTestInternal::kThreadCount );
    for ( uint32 threadIndex = 0; threadIndex < ShardedCounterTestInternal::kThreadCount; ++threadIndex )
    {
        listThread.emplace_back( [&counter]()
        {
            for ( uint32 index = 0; index < ShardedCounterTestInternal::kAddCount; ++index )
            {
                counter.add( 1 );
            }
        } );
    }
    for ( std::thread& thread : listThread )
    {
        thread.join();
    }
    SW_EXPECT_EQUAL( static_cast<int64>( ShardedCounterTestInternal::kThreadCount ) * ShardedCounterTestInternal::kAddCount, counter.sum() );

    counter.reset();
    SW_EXPECT_EQUAL( int64{ 0 }, counter.sum() );
}

/**
 * @brief [ShardedCounterTest] 칸 번호는 칸 수 안이고, 한 스레드가 물을 때마다 같다
 */
SW_TEST_CASE( ShardedCounterTest, CurrentThreadShardIsStableAndInRange )
{
    const uint32 firstShard = sw::ShardedCounterUtil::getCurrentThreadShard();
    SW_EXPECT_TRUE( firstShard < sw::ShardedCounterUtil::kShardCount );
    SW_EXPECT_EQUAL( firstShard, sw::ShardedCounterUtil::getCurrentThreadShard() );
}

/**
 * @brief [ShardedCounterTest] 다른 칸에서 내려 칸 하나가 음수가 되어도 합은 맞다
 */
SW_TEST_CASE( ShardedCounterTest, CrossShardDecrementKeepsTheSum )
{
    sw::ShardedCounter<int64> counter;
    counter.addToShard( 0, 5 );
    counter.addToShard( 3, -2 );
    SW_EXPECT_EQUAL( int64{ 3 }, counter.sum() );

    sw::ShardedCounter<uint64> unsignedCounter;
    unsignedCounter.addToShard( 1, 7 );
    unsignedCounter.addToShard( 2, static_cast<uint64>( -int64{ 4 } ) ); // 부호 없는 칸은 감기지만 합은 정수 덧셈이라 맞다
    SW_EXPECT_EQUAL( uint64{ 3 }, unsignedCounter.sum() );
}

/**
 * @brief [ShardedCounterTest] 올린 칸에서 내리면, 다른 스레드가 항목을 쉼 없이 만들고 지우는 동안에도 합이 오래 사는 항목 하나를 놓치지 않는다
 * @details 올린 칸과 내린 칸이 다르면 합을 읽는 사이 생겨서 끝난 항목이 내린 몫만 보여 합이 실제보다 작아진다(살아 있는 항목이 있는데 0).
 */
SW_TEST_CASE( ShardedCounterTest, SameShardPairNeverHidesALiveItem )
{
    sw::ShardedCounter<int64> counter;
    const uint32              longLivedShard = sw::ShardedCounterUtil::getCurrentThreadShard();
    counter.addToShard( longLivedShard, 1 ); // 테스트 내내 살아 있는 항목

    sw::atomic<bool>        bStop{ false };
    sw::vector<std::thread> listThread;
    listThread.reserve( ShardedCounterTestInternal::kChurnThreadCount );
    for ( uint32 threadIndex = 0; threadIndex < ShardedCounterTestInternal::kChurnThreadCount; ++threadIndex )
    {
        listThread.emplace_back( [&counter, &bStop]()
        {
            while ( bStop.load( std::memory_order_relaxed ) == false )
            {
                const uint32 shard = sw::ShardedCounterUtil::getCurrentThreadShard();
                counter.addToShard( shard, 1, std::memory_order_seq_cst );
                counter.addToShard( shard, -1, std::memory_order_seq_cst );
            }
        } );
    }

    uint32              hiddenCount = 0;
    const sw::Stopwatch stopwatch;
    while ( stopwatch.getElapsedMilliseconds() < ShardedCounterTestInternal::kChurnMilli )
    {
        if ( counter.sum( std::memory_order_seq_cst ) < 1 )
            ++hiddenCount;
    }
    bStop.store( true, std::memory_order_relaxed );
    for ( std::thread& thread : listThread )
    {
        thread.join();
    }

    SW_EXPECT_EQUAL( 0u, hiddenCount );
    counter.addToShard( longLivedShard, -1 );
    SW_EXPECT_EQUAL( int64{ 0 }, counter.sum() );
}
