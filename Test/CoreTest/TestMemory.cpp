#include "pch.h"

#include "Core/Concurrency/LockFreeObjectPool.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/Memory/PoolAllocator.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Memory — FrameArena·LockFree 풀
// ------------------------------------------------------------------------------
/**
 * @brief [Core_Memory] FrameArenaAllocator 동작
 */

SW_TEST_CASE( Core_Memory, FrameArenaAllocatorOperations )
{
	sw::FrameArenaAllocator arena( 1024 );

	int32* p1 = static_cast<int32*>( arena.allocate( sizeof( int32 ), alignof( int32 ) ) );
	SW_EXPECT_TRUE( p1 != nullptr );
	*p1 = 42;
	SW_EXPECT_EQUAL( 42, *p1 );

	struct DummyStruct
	{
		float32 x, y, z;
		uint32	_id;
	};

	DummyStruct* dummy = arena.construct<DummyStruct>( 1.0f, 2.0f, 3.0f, 100u );
	SW_EXPECT_TRUE( dummy != nullptr );
	SW_EXPECT_EQUAL( 100u, dummy->_id );

	SW_EXPECT_TRUE( arena.getUsedBytes() > 0 );
	arena.reset();
	SW_EXPECT_EQUAL( size_t( 0 ), arena.getUsedBytes() );
}

/**
 * @brief [Core_Memory] 정렬과 재할당
 */
SW_TEST_CASE( Core_Memory, AlignmentAndReallocation )
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

/**
 * @brief [Core_Memory] FrameArena 마커와 롤백
 */
SW_TEST_CASE( Core_Memory, FrameArenaMarkerAndRollback )
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

/**
 * @brief [Core_Memory] Frame 더블버퍼 안전성
 */
SW_TEST_CASE( Core_Memory, FrameDoubleBufferSafety )
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

// ------------------------------------------------------------------------------
// 2) LockFreeObjectPool — 획득·반납
// ------------------------------------------------------------------------------
/**
 * @brief [Core_Memory] LockFreeObjectPool 동작
 */
SW_TEST_CASE( Core_Memory, LockFreeObjectPoolOperations )
{
	struct PooledItem
	{
		int32	_id{ 0 };
		float32 _value{ 0.0f };
		PooledItem( int32 id, float32 val )
			: _id{ id }
			, _value{ val }
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

/**
 * @brief [Core_Memory] Memory 기본 할당/해제, SIMD 정렬 할당(alignedAlloc) 및 메모리 유틸 검증
 */
SW_TEST_CASE( Core_Memory, LowLevelMemoryAllocAndAlignment )
{
	// 1) 기본 할당 / 해제
	void* rawPtr = sw::Memory::allocMemory( 512 );
	SW_ASSERT_NOT_NULL( rawPtr );

	sw::Memory::set( rawPtr, 0, 512 );
	const uint8* bytePtr = static_cast<const uint8*>( rawPtr );
	for ( size_t slotIndex = 0; slotIndex < 512; ++slotIndex )
	{
		SW_EXPECT_EQUAL( 0u, static_cast<uint32>( bytePtr[slotIndex] ) );
	}

	sw::Memory::freeMemory( rawPtr );

	// 2) SIMD 정렬 할당 (64바이트 캐시라인 정렬)
	void* aligned64 = sw::Memory::alignedAlloc( 256, 64 );
	SW_ASSERT_NOT_NULL( aligned64 );
	SW_EXPECT_EQUAL( 0u, reinterpret_cast<uintptr_t>( aligned64 ) % 64 );

	// 메모리 복사 및 비교 검증
	const utf8* kSamplePattern = "CoreMemoryAlignmentVerification";
	sw::Memory::copy( aligned64, kSamplePattern, 32 );
	SW_EXPECT_EQUAL( 0, sw::Memory::compare( aligned64, kSamplePattern, 32 ) );

	sw::Memory::alignedFree( aligned64 );
}

/**
 * @brief [Core_Memory] PoolAllocator 및 TypedPoolAllocator 할당, 해제, 재활용 및 클리어 검증
 */
SW_TEST_CASE( Core_Memory, PoolAllocatorAndTypedPool )
{
	// 1) PoolAllocator 기본 할당 & 해제 & 프리리스트 재활용
	sw::PoolAllocator pool( 64, 4, true );

	void* pBlock1 = pool.allocate();
	void* pBlock2 = pool.allocate();
	void* pBlock3 = pool.allocate();
	SW_ASSERT_NOT_NULL( pBlock1 );
	SW_ASSERT_NOT_NULL( pBlock2 );
	SW_ASSERT_NOT_NULL( pBlock3 );

	pool.free( pBlock2 );
	void* pBlock2Reused = pool.allocate();
	SW_EXPECT_EQUAL( pBlock2, pBlock2Reused );

	pool.free( pBlock1 );
	pool.free( pBlock2Reused );
	pool.free( pBlock3 );

	pool.clear();

	// 2) TypedPoolAllocator 수명주기 및 생성/소멸 검증
	struct TestPoolObject
	{
		int32 _val{ 0 };
		bool* _pDestructFlag{ nullptr };
		TestPoolObject( int32 val, bool* pFlag )
			: _val{ val }
			, _pDestructFlag{ pFlag }
		{
		}
		~TestPoolObject()
		{
			if ( _pDestructFlag != nullptr )
			{
				*_pDestructFlag = true;
			}
		}
	};

	bool								   bDestroyed = false;
	sw::TypedPoolAllocator<TestPoolObject> typedPool( 8, false );

	TestPoolObject* pObj = typedPool.create( 999, &bDestroyed );
	SW_ASSERT_NOT_NULL( pObj );
	SW_EXPECT_EQUAL( 999, pObj->_val );
	SW_EXPECT_FALSE( bDestroyed );

	typedPool.destroy( pObj );
	SW_EXPECT_TRUE( bDestroyed );

	typedPool.clear();
}
