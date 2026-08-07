/**
 * @file TestDataStructure.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "Core/Utility/DataStructure/DynamicBitset.h"

SW_TEST_CASE( Utility_DataStructure, DynamicBitsetBasic )
{
	sw::DynamicBitset bitset( 100 );
	SW_EXPECT_EQUAL( 100u, bitset.size() );
	SW_EXPECT_TRUE( bitset.none() );
	SW_EXPECT_FALSE( bitset.any() );
	SW_EXPECT_FALSE( bitset.all() );

	bitset.set( 5 );
	bitset.set( 10 );
	SW_EXPECT_TRUE( bitset.test( 5 ) );
	SW_EXPECT_TRUE( bitset.test( 10 ) );
	SW_EXPECT_FALSE( bitset.test( 0 ) );
	SW_EXPECT_EQUAL( 2u, bitset.count() );
	SW_EXPECT_TRUE( bitset.any() );

	bitset.reset( 5 );
	SW_EXPECT_FALSE( bitset.test( 5 ) );
	SW_EXPECT_EQUAL( 1u, bitset.count() );

	bitset.flip( 10 );
	SW_EXPECT_FALSE( bitset.test( 10 ) );
	SW_EXPECT_TRUE( bitset.none() );
}

SW_TEST_CASE( Utility_DataStructure, DynamicBitsetOperations )
{
	sw::DynamicBitset bs1( "1010" );
	sw::DynamicBitset bs2( "0110" );

	sw::DynamicBitset andResult = bs1 & bs2;
	SW_EXPECT_EQUAL( std::string( "0010" ), andResult.to_string() );

	sw::DynamicBitset orResult = bs1 | bs2;
	SW_EXPECT_EQUAL( std::string( "1110" ), orResult.to_string() );

	sw::DynamicBitset xorResult = bs1 ^ bs2;
	SW_EXPECT_EQUAL( std::string( "1100" ), xorResult.to_string() );

	sw::DynamicBitset notResult = ~bs1;
	SW_EXPECT_EQUAL( std::string( "0101" ), notResult.to_string() );
}

SW_TEST_CASE( Utility_DataStructure, DynamicBitsetResizeAndConversions )
{
	sw::DynamicBitset bs( 8, 0b10101010u );
	SW_EXPECT_EQUAL( 8u, bs.size() );
	SW_EXPECT_EQUAL( static_cast<uint64>( 0b10101010u ), bs.to_ullong() );

	bs.resize( 16, true );
	SW_EXPECT_EQUAL( 16u, bs.size() );
	SW_EXPECT_TRUE( bs.test( 15 ) );

	bs.reserve( 128 );
	SW_EXPECT_TRUE( bs.capacity() >= 128u );

	bs.shrink_to_fit();
	SW_EXPECT_TRUE( bs.memory_usage() > 0u );

	sw::DynamicBitset bsShift = bs << 2;
	SW_EXPECT_EQUAL( bs.size(), bsShift.size() );

	sw::DynamicBitset bsRightShift = bs >> 2;
	SW_EXPECT_EQUAL( bs.size(), bsRightShift.size() );

	bs >>= 2;
	SW_EXPECT_EQUAL( 16u, bs.size() );
}

SW_TEST_CASE( Utility_DataStructure, DynamicBitsetFullCoverage )
{
	sw::DynamicBitset emptySet;
	SW_EXPECT_TRUE( emptySet.empty() );

	sw::DynamicBitset bs( 8, 0xFFu );
	SW_EXPECT_EQUAL( static_cast<uint32>( 0xFFu ), bs.to_ulong() );

	sw::DynamicBitset bsEqual1( "1100" );
	sw::DynamicBitset bsEqual2( "1100" );
	sw::DynamicBitset bsDifferent( "1010" );

	SW_EXPECT_TRUE( bsEqual1 == bsEqual2 );
	SW_EXPECT_FALSE( bsEqual1 != bsEqual2 );
	SW_EXPECT_TRUE( bsDifferent != bsEqual1 );
	SW_EXPECT_TRUE( bsDifferent < bsEqual1 );

	sw::DynamicBitset bsAll( 8 );
	bsAll.set();
	SW_EXPECT_TRUE( bsAll.all() );
	SW_EXPECT_EQUAL( 8u, bsAll.count() );

	bsAll.reset();
	SW_EXPECT_TRUE( bsAll.none() );
	SW_EXPECT_EQUAL( 0u, bsAll.count() );

	bsAll.flip();
	SW_EXPECT_TRUE( bsAll.all() );

	std::stringstream ss;
	ss << bsEqual1;
	SW_EXPECT_EQUAL( std::string( "1100" ), ss.str() );
}

#include "Core/Utility/DataStructure/LockFreeQueue.h"

SW_TEST_CASE( Utility_DataStructure, LockFreeQueueBasicAndConcurrent )
{
	sw::LockFreeQueue<int32, 64> queue;
	SW_EXPECT_TRUE( queue.empty() );
	SW_EXPECT_EQUAL( 0u, queue.size() );

	bool push1 = queue.push( 100 );
	bool push2 = queue.push( 200 );
	SW_EXPECT_TRUE( push1 );
	SW_EXPECT_TRUE( push2 );
	SW_EXPECT_EQUAL( 2u, queue.size() );

	int32 val1 = 0;
	int32 val2 = 0;
	bool  pop1 = queue.pop( val1 );
	bool  pop2 = queue.pop( val2 );
	SW_EXPECT_TRUE( pop1 );
	SW_EXPECT_TRUE( pop2 );
	SW_EXPECT_EQUAL( 100, val1 );
	SW_EXPECT_EQUAL( 200, val2 );
	SW_EXPECT_TRUE( queue.empty() );

	std::thread producer( [&queue]()
	{
		for ( int32 i = 0; i < 1000; ++i )
		{
			while ( !queue.push( i ) )
			{
				std::this_thread::yield();
			}
		}
	} );

	std::thread consumer( [&queue]()
	{
		int32 receivedCount = 0;
		while ( receivedCount < 1000 )
		{
			int32 val = 0;
			if ( queue.pop( val ) )
			{
				receivedCount++;
			}
			else
			{
				std::this_thread::yield();
			}
		}
	} );

	producer.join();
	consumer.join();

	SW_EXPECT_TRUE( queue.empty() );
}

#include "Core/Utility/DataStructure/ConcurrentQueue.h"

SW_TEST_CASE( Utility_DataStructure, ConcurrentQueueMultiThread )
{
	sw::ConcurrentQueue<int32, 128> queue;
	SW_EXPECT_TRUE( queue.empty() );
	SW_EXPECT_EQUAL( 0u, queue.size() );

	constexpr int32	   kItemsPerThread = 500;
	constexpr int32	   kNumProducers   = 4;
	constexpr int32	   kNumConsumers   = 4;
	std::atomic<int32> totalSumReceived{ 0 };

	std::vector<std::thread> producers;
	for ( int32 p = 0; p < kNumProducers; ++p )
	{
		producers.emplace_back( [&queue, p]()
		{
			for ( int32 i = 0; i < kItemsPerThread; ++i )
			{
				while ( !queue.enqueue( p * 10000 + i ) )
				{
					std::this_thread::yield();
				}
			}
		} );
	}

	std::vector<std::thread> consumers;
	for ( int32 c = 0; c < kNumConsumers; ++c )
	{
		consumers.emplace_back( [&queue, &totalSumReceived]()
		{
			int32 count	 = 0;
			int32 target = ( kNumProducers * kItemsPerThread ) / kNumConsumers;
			while ( count < target )
			{
				int32 item = 0;
				if ( queue.dequeue( item ) )
				{
					totalSumReceived.fetch_add( 1, std::memory_order_relaxed );
					count++;
				}
				else
				{
					std::this_thread::yield();
				}
			}
		} );
	}

	for ( std::thread& t : producers )
	{
		t.join();
	}
	for ( std::thread& t : consumers )
	{
		t.join();
	}

	SW_EXPECT_TRUE( queue.empty() );
	SW_EXPECT_EQUAL( kNumProducers * kItemsPerThread, totalSumReceived.load() );
}
