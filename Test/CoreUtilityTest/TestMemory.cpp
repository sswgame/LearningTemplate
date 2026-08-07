/**
 * @file TestMemory.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "Core/Utility/Memory/FrameArenaAllocator.h"

SW_TEST_CASE( Utility_Memory, FrameArenaAllocatorOperations )
{
	sw::FrameArenaAllocator arena( 1024 );

	int32* p1 = static_cast<int32*>( arena.allocate( sizeof( int32 ), alignof( int32 ) ) );
	SW_EXPECT_TRUE( p1 != nullptr );
	*p1 = 42;
	SW_EXPECT_EQUAL( 42, *p1 );

	struct DummyStruct
	{
		float  x, y, z;
		uint32 id;
	};

	DummyStruct* dummy = arena.construct<DummyStruct>( 1.0f, 2.0f, 3.0f, 100u );
	SW_EXPECT_TRUE( dummy != nullptr );
	SW_EXPECT_EQUAL( 100u, dummy->id );

	SW_EXPECT_TRUE( arena.getUsedBytes() > 0 );
	arena.reset();
	SW_EXPECT_EQUAL( size_t( 0 ), arena.getUsedBytes() );
}

SW_TEST_CASE( Utility_Memory, AlignmentAndReallocation )
{
	sw::FrameArenaAllocator arena( 128 );

	void* ptr16 = arena.allocate( 64, 16 );
	SW_EXPECT_TRUE( ptr16 != nullptr );
	SW_EXPECT_EQUAL( size_t( 0 ), reinterpret_cast<uintptr_t>( ptr16 ) % 16 );

	void* ptr32 = arena.allocate( 128, 32 );
	SW_EXPECT_TRUE( ptr32 != nullptr );
	SW_EXPECT_EQUAL( size_t( 0 ), reinterpret_cast<uintptr_t>( ptr32 ) % 32 );

	SW_EXPECT_TRUE( arena.getTotalAllocatedBytes() >= 128 );

	arena.reset();
	SW_EXPECT_EQUAL( size_t( 0 ), arena.getUsedBytes() );
}

SW_TEST_CASE( Utility_Memory, FrameArenaMarkerAndRollback )
{
	sw::FrameArenaAllocator arena( 256 );

	int32* p1 = arena.construct<int32>( 100 );
	SW_EXPECT_TRUE( p1 != nullptr );
	SW_EXPECT_EQUAL( 100, *p1 );

	sw::FrameArenaAllocator::Marker marker		 = arena.createMarker();
	size_t							usedAtMarker = arena.getUsedBytes();

	int32* p2 = arena.construct<int32>( 200 );
	int32* p3 = arena.construct<int32>( 300 );
	SW_EXPECT_TRUE( p2 != nullptr && p3 != nullptr );
	SW_EXPECT_TRUE( arena.getUsedBytes() > usedAtMarker );

	arena.rollbackToMarker( marker );
	SW_EXPECT_EQUAL( usedAtMarker, arena.getUsedBytes() );

	int32* p4 = arena.construct<int32>( 400 );
	SW_EXPECT_EQUAL( p2, p4 );
	SW_EXPECT_EQUAL( 400, *p4 );
}

#include "Core/Utility/Memory/FrameDoubleBuffer.h"

SW_TEST_CASE( Utility_Memory, FrameDoubleBufferSafety )
{
	sw::FrameDoubleBuffer doubleBuffer( 1024 );

	SW_EXPECT_EQUAL( 0u, doubleBuffer.getCurrentIndex() );
	int32* frame0Ptr = static_cast<int32*>( doubleBuffer.allocate( sizeof( int32 ) ) );
	SW_EXPECT_TRUE( frame0Ptr != nullptr );
	*frame0Ptr = 111;

	doubleBuffer.swapAndResetPrevious();
	SW_EXPECT_EQUAL( 1u, doubleBuffer.getCurrentIndex() );
	int32* frame1Ptr = static_cast<int32*>( doubleBuffer.allocate( sizeof( int32 ) ) );
	SW_EXPECT_TRUE( frame1Ptr != nullptr );
	*frame1Ptr = 222;

	SW_EXPECT_EQUAL( 111, *frame0Ptr );

	doubleBuffer.swapAndResetPrevious();
	SW_EXPECT_EQUAL( 0u, doubleBuffer.getCurrentIndex() );
	SW_EXPECT_EQUAL( 0u, doubleBuffer.getCurrentUsedBytes() );
}

#include "Core/Utility/Memory/LockFreeObjectPool.h"

SW_TEST_CASE( Utility_Memory, LockFreeObjectPoolOperations )
{
	struct PooledItem
	{
		int32 _id	 = 0;
		float _value = 0.0f;
		PooledItem( int32 id, float val )
			: _id( id )
			, _value( val )
		{
		}
	};

	sw::LockFreeObjectPool<PooledItem, 16> pool;
	SW_EXPECT_EQUAL( 0u, pool.getActiveCount() );
	SW_EXPECT_EQUAL( 16u, pool.getAvailableCount() );

	PooledItem* item1 = pool.acquire( 10, 3.14f );
	SW_EXPECT_TRUE( item1 != nullptr );
	if ( item1 != nullptr )
	{
		SW_EXPECT_EQUAL( 10, item1->_id );
		SW_EXPECT_NEAR_EQUAL( 3.14f, item1->_value, 1e-4f );
	}
	SW_EXPECT_EQUAL( 1u, pool.getActiveCount() );

	pool.release( item1 );
	SW_EXPECT_EQUAL( 0u, pool.getActiveCount() );
	SW_EXPECT_EQUAL( 16u, pool.getAvailableCount() );
}
