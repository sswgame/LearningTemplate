#include "pch.h"

#include "Core/Concurrency/ConcurrentQueue.h"
#include "Core/Concurrency/LockFreeObjectPool.h"
#include "Core/Concurrency/LockFreeQueue.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/DynamicBitset.h"
#include "Core/Container/string.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_DataStructure — 비트셋·락프리/동시 큐
// ------------------------------------------------------------------------------
/**
 * @brief [Core_DataStructure] DynamicBitset 기본
 */

SW_TEST_CASE( Core_DataStructure, DynamicBitsetBasic )
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

/**
 * @brief [Core_DataStructure] DynamicBitset 연산
 */
SW_TEST_CASE( Core_DataStructure, DynamicBitsetOperations )
{
	sw::DynamicBitset bs1( "1010" );
	sw::DynamicBitset bs2( "0110" );

	sw::DynamicBitset andResult = bs1 & bs2;
	SW_EXPECT_EQUAL( sw::string( "0010" ), andResult.to_string() );

	sw::DynamicBitset orResult = bs1 | bs2;
	SW_EXPECT_EQUAL( sw::string( "1110" ), orResult.to_string() );

	sw::DynamicBitset xorResult = bs1 ^ bs2;
	SW_EXPECT_EQUAL( sw::string( "1100" ), xorResult.to_string() );

	sw::DynamicBitset notResult = ~bs1;
	SW_EXPECT_EQUAL( sw::string( "0101" ), notResult.to_string() );
}

/**
 * @brief [Core_DataStructure] DynamicBitset 리사이즈와 변환
 */
SW_TEST_CASE( Core_DataStructure, DynamicBitsetResizeAndConversions )
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

/**
 * @brief [Core_DataStructure] DynamicBitset 64비트 이상 멀티 블록 시프트(<<=, >>=) 검증
 */
SW_TEST_CASE( Core_DataStructure, DynamicBitsetMultiBlockShift )
{
	// 192비트 (3개 64비트 블록) 비트셋 생성
	sw::DynamicBitset bitset( 192 );
	bitset.set( 10 );
	bitset.set( 70 );
	bitset.set( 150 );

	SW_EXPECT_TRUE( bitset.test( 10 ) );
	SW_EXPECT_TRUE( bitset.test( 70 ) );
	SW_EXPECT_TRUE( bitset.test( 150 ) );
	SW_EXPECT_EQUAL( 3u, bitset.count() );

	// 1) 64비트 (정확히 1블록) 우측 시프트
	sw::DynamicBitset shifted64 = bitset >> 64;
	SW_EXPECT_FALSE( shifted64.test( 10 ) );
	SW_EXPECT_TRUE( shifted64.test( 6 ) );	// 70 - 64 = 6
	SW_EXPECT_TRUE( shifted64.test( 86 ) ); // 150 - 64 = 86
	SW_EXPECT_EQUAL( 2u, shifted64.count() );

	// 2) 128비트 (2블록) 우측 시프트
	sw::DynamicBitset shifted128 = bitset >> 128;
	SW_EXPECT_TRUE( shifted128.test( 22 ) ); // 150 - 128 = 22
	SW_EXPECT_EQUAL( 1u, shifted128.count() );

	// 3) 65비트 (1블록 + 1비트 복합) 우측 시프트
	sw::DynamicBitset shifted65 = bitset >> 65;
	SW_EXPECT_TRUE( shifted65.test( 5 ) );	// 70 - 65 = 5
	SW_EXPECT_TRUE( shifted65.test( 85 ) ); // 150 - 65 = 85
	SW_EXPECT_EQUAL( 2u, shifted65.count() );

	// 4) 64비트 좌측 시프트
	sw::DynamicBitset leftShifted64 = bitset << 64;
	SW_EXPECT_TRUE( leftShifted64.test( 74 ) );	 // 10 + 64 = 74
	SW_EXPECT_TRUE( leftShifted64.test( 134 ) ); // 70 + 64 = 134
	SW_EXPECT_FALSE( leftShifted64.test( 10 ) );
	SW_EXPECT_EQUAL( 2u, leftShifted64.count() ); // 150 + 64 = 214 > 192 (truncated by size)
}

/**
 * @brief [Core_DataStructure] DynamicBitset 전체 커버리지
 */
SW_TEST_CASE( Core_DataStructure, DynamicBitsetFullCoverage )
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

	SW_EXPECT_EQUAL( sw::string( "1100" ), bsEqual1.to_string() );
}

// ------------------------------------------------------------------------------
// 2) 큐 — LockFreeQueue·ConcurrentQueue
// ------------------------------------------------------------------------------
/**
 * @brief [Core_DataStructure] LockFreeQueue 기본과 동시성
 */
SW_TEST_CASE( Core_DataStructure, LockFreeQueueBasicAndConcurrent )
{
	sw::LockFreeQueue<int32, 64> queue;
	SW_EXPECT_TRUE( queue.empty() );
	SW_EXPECT_EQUAL( 0u, queue.size() );

	bool push1 = queue.push( 100 );
	bool push2 = queue.push( 200 );
	SW_EXPECT_TRUE( push1 );
	SW_EXPECT_TRUE( push2 );
	SW_EXPECT_EQUAL( 2u, queue.size() );

	int32 val1{ 0 };
	int32 val2{ 0 };
	bool  pop1 = queue.pop( val1 );
	bool  pop2 = queue.pop( val2 );
	SW_EXPECT_TRUE( pop1 );
	SW_EXPECT_TRUE( pop2 );
	SW_EXPECT_EQUAL( 100, val1 );
	SW_EXPECT_EQUAL( 200, val2 );
	SW_EXPECT_TRUE( queue.empty() );

	std::thread producer( [&queue]()
	{
		for ( int32 itemIndex = 0; itemIndex < 1000; ++itemIndex )
		{
			while ( queue.push( itemIndex ) == false )
			{
				std::this_thread::yield();
			}
		}
	} );

	std::thread consumer( [&queue]()
	{
		int32 receivedCount{ 0 };
		while ( receivedCount < 1000 )
		{
			int32 val{ 0 };
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

/**
 * @brief [Core_DataStructure] ConcurrentQueue 멀티스레드
 */
SW_TEST_CASE( Core_DataStructure, ConcurrentQueueMultiThread )
{
	sw::ConcurrentQueue<int32, 128> queue;
	SW_EXPECT_TRUE( queue.empty() );
	SW_EXPECT_EQUAL( 0u, queue.size() );

	constexpr int32	  kItemsPerThread = 500;
	constexpr int32	  kNumProducers	  = 4;
	constexpr int32	  kNumConsumers	  = 4;
	sw::atomic<int32> totalSumReceived{ 0 };

	sw::vector<std::thread> producers;
	for ( int32 producerIndex = 0; producerIndex < kNumProducers; ++producerIndex )
	{
		producers.emplace_back( [&queue, producerIndex]()
		{
			for ( int32 itemIndex = 0; itemIndex < kItemsPerThread; ++itemIndex )
			{
				while ( queue.enqueue( producerIndex * 10000 + itemIndex ) == false )
				{
					std::this_thread::yield();
				}
			}
		} );
	}

	sw::vector<std::thread> consumers;
	for ( int32 consumerIndex = 0; consumerIndex < kNumConsumers; ++consumerIndex )
	{
		consumers.emplace_back( [&queue, &totalSumReceived]()
		{
			int32 count{ 0 };
			int32 target = ( kNumProducers * kItemsPerThread ) / kNumConsumers;
			while ( count < target )
			{
				int32 item{ 0 };
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

namespace
{
	struct PooledTestStruct
	{
		int32	   _id{ 0 };
		sw::string _message{};

		PooledTestStruct( int32 id, sw::string message )
			: _id{ id }
			, _message{ std::move( message ) }
		{
		}

		~PooledTestStruct()
		{
			_id = -1;
		}
	};
} // namespace

/**
 * @brief [Core_DataStructure] LockFreeObjectPool 기본 수명주기 및 인자 전달 검증
 */
SW_TEST_CASE( Core_DataStructure, LockFreeObjectPoolLifecycle )
{
	sw::LockFreeObjectPool<PooledTestStruct, 16> pool;
	SW_EXPECT_EQUAL( 16u, pool.capacity() );
	SW_EXPECT_EQUAL( 0u, pool.getActiveCount() );
	SW_EXPECT_EQUAL( 16u, pool.getAvailableCount() );

	PooledTestStruct* obj1 = pool.acquire( 42, sw::string( "Hello Pool" ) );
	SW_ASSERT_NOT_NULL( obj1 );
	SW_EXPECT_EQUAL( 42, obj1->_id );
	SW_EXPECT_EQUAL( sw::string( "Hello Pool" ), obj1->_message );
	SW_EXPECT_EQUAL( 1u, pool.getActiveCount() );
	SW_EXPECT_EQUAL( 15u, pool.getAvailableCount() );

	PooledTestStruct* obj2 = pool.acquire( 100, sw::string( "Second Object" ) );
	SW_ASSERT_NOT_NULL( obj2 );
	SW_EXPECT_EQUAL( 2u, pool.getActiveCount() );
	SW_EXPECT_EQUAL( 14u, pool.getAvailableCount() );

	pool.release( obj1 );
	SW_EXPECT_NULL( obj1 );
	SW_EXPECT_EQUAL( 1u, pool.getActiveCount() );
	SW_EXPECT_EQUAL( 15u, pool.getAvailableCount() );

	pool.release( obj2 );
	SW_EXPECT_NULL( obj2 );
	SW_EXPECT_EQUAL( 0u, pool.getActiveCount() );
	SW_EXPECT_EQUAL( 16u, pool.getAvailableCount() );
}

/**
 * @brief [Core_DataStructure] LockFreeObjectPool 용량 초과 및 반납 후 재획득 검증
 */
SW_TEST_CASE( Core_DataStructure, LockFreeObjectPoolExhaustionAndReacquire )
{
	constexpr uint32						kPoolCap = 4;
	sw::LockFreeObjectPool<int32, kPoolCap> pool;

	int32* items[kPoolCap]{};
	for ( uint32 itemIndex = 0; itemIndex < kPoolCap; ++itemIndex )
	{
		items[itemIndex] = pool.acquire( static_cast<int32>( itemIndex * 10 ) );
		SW_ASSERT_NOT_NULL( items[itemIndex] );
		SW_EXPECT_EQUAL( static_cast<int32>( itemIndex * 10 ), *items[itemIndex] );
	}

	SW_EXPECT_EQUAL( kPoolCap, pool.getActiveCount() );
	SW_EXPECT_EQUAL( 0u, pool.getAvailableCount() );

	// 용량 초과 시 nullptr 반환 검증
	int32* overflow = pool.acquire( 999 );
	SW_EXPECT_NULL( overflow );

	// 하나 반납 후 즉시 재획득 가능 검증
	pool.release( items[1] );
	SW_EXPECT_NULL( items[1] );
	SW_EXPECT_EQUAL( kPoolCap - 1u, pool.getActiveCount() );
	SW_EXPECT_EQUAL( 1u, pool.getAvailableCount() );

	items[1] = pool.acquire( 777 );
	SW_ASSERT_NOT_NULL( items[1] );
	SW_EXPECT_EQUAL( 777, *items[1] );
	SW_EXPECT_EQUAL( kPoolCap, pool.getActiveCount() );

	// 전체 반납
	for ( uint32 itemIndex = 0; itemIndex < kPoolCap; ++itemIndex )
	{
		pool.release( items[itemIndex] );
		SW_EXPECT_NULL( items[itemIndex] );
	}

	SW_EXPECT_EQUAL( 0u, pool.getActiveCount() );
	SW_EXPECT_EQUAL( kPoolCap, pool.getAvailableCount() );
}

/**
 * @brief [Core_DataStructure] LockFreeObjectPool 멀티스레드 동시 acquire/release 검증
 */
SW_TEST_CASE( Core_DataStructure, LockFreeObjectPoolConcurrent )
{
	constexpr uint32 kCapacity	 = 256;
	constexpr int32	 kIterations = 1000;
	constexpr int32	 kNumThreads = 4;

	sw::LockFreeObjectPool<int32, kCapacity> pool;
	sw::atomic<int32>						 successfulAcquires{ 0 };

	sw::vector<std::thread> workers;
	for ( int32 threadIndex = 0; threadIndex < kNumThreads; ++threadIndex )
	{
		workers.emplace_back( [&pool, &successfulAcquires, threadIndex]()
		{
			for ( int32 iter = 0; iter < kIterations; ++iter )
			{
				int32* ptr = pool.acquire( threadIndex * 1000 + iter );
				if ( ptr != nullptr )
				{
					SW_EXPECT_EQUAL( threadIndex * 1000 + iter, *ptr );
					successfulAcquires.fetch_add( 1, std::memory_order_relaxed );
					std::this_thread::yield();
					pool.release( ptr );
					SW_EXPECT_NULL( ptr );
				}
				else
				{
					std::this_thread::yield();
				}
			}
		} );
	}

	for ( std::thread& t : workers )
	{
		t.join();
	}

	SW_EXPECT_TRUE( successfulAcquires.load() > 0 );
	SW_EXPECT_EQUAL( 0u, pool.getActiveCount() );
	SW_EXPECT_EQUAL( kCapacity, pool.getAvailableCount() );
}
