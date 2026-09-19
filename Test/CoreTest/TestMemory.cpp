#include "pch.h"

#include "Core/Concurrency/LockFreeObjectPool.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/Memory/LinearAllocator.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/PoolAllocator.h"

#include "TestFramework/TestFramework.h"

#include <mutex>
#include <thread>

// ------------------------------------------------------------------------------
// 1) Core_Memory — FrameArena·LockFree 풀
// ------------------------------------------------------------------------------
/**
 * @brief [MemoryTest] FrameArenaAllocator 동작
 */

SW_TEST_CASE( MemoryTest, FrameArenaAllocatorOperations )
{
    sw::FrameArenaAllocator arena( 1024 );

    int32* p1 = static_cast<int32*>( arena.allocate( sizeof( int32 ), alignof( int32 ) ) );
    SW_EXPECT_TRUE( p1 != nullptr );
    *p1 = 42;
    SW_EXPECT_EQUAL( 42, *p1 );

    struct DummyStruct
    {
        float32 x, y, z;
        uint32  _id;
    };

    DummyStruct* dummy = arena.construct<DummyStruct>( 1.0f, 2.0f, 3.0f, 100u );
    SW_EXPECT_TRUE( dummy != nullptr );
    SW_EXPECT_EQUAL( 100u, dummy->_id );

    SW_EXPECT_TRUE( arena.getUsedBytes() > 0 );
    arena.reset();
    SW_EXPECT_EQUAL( size_t( 0 ), arena.getUsedBytes() );
}

/**
 * @brief [MemoryTest] 정렬과 재할당
 */
SW_TEST_CASE( MemoryTest, AlignmentAndReallocation )
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
 * @brief [MemoryTest] FrameArena 마커와 롤백
 */
SW_TEST_CASE( MemoryTest, FrameArenaMarkerAndRollback )
{
    sw::FrameArenaAllocator arena( 256 );

    int32* p1 = arena.construct<int32>( 100 );
    SW_EXPECT_TRUE( p1 != nullptr );
    SW_EXPECT_EQUAL( 100, *p1 );

    sw::FrameArenaAllocator::Marker marker       = arena.createMarker();
    size_t                          usedAtMarker = arena.getUsedBytes();

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
 * @brief [MemoryTest] Frame 더블버퍼 안전성
 */
SW_TEST_CASE( MemoryTest, FrameDoubleBufferSafety )
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
 * @brief [MemoryTest] LockFreeObjectPool 동작
 */
SW_TEST_CASE( MemoryTest, LockFreeObjectPoolOperations )
{
    struct PooledItem
    {
        int32   _id{ 0 };
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
 * @brief [MemoryTest] Memory 기본 할당/해제, SIMD 정렬 할당(allocateAligned) 및 메모리 유틸 검증
 */
SW_TEST_CASE( MemoryTest, LowLevelMemoryAllocAndAlignment )
{
    // 1) 기본 할당 / 해제
    void* rawPtr = sw::Memory::allocate( 512 );
    SW_ASSERT_NOT_NULL( rawPtr );

    sw::Memory::set( rawPtr, 0, 512 );
    const uint8* bytePtr = static_cast<const uint8*>( rawPtr );
    for ( size_t slotIndex = 0; slotIndex < 512; ++slotIndex )
    {
        SW_EXPECT_EQUAL( 0u, static_cast<uint32>( bytePtr[slotIndex] ) );
    }

    sw::Memory::free( rawPtr );

    // 2) SIMD 정렬 할당 (64바이트 캐시라인 정렬)
    void* aligned64 = sw::Memory::allocateAligned( 256, 64 );
    SW_ASSERT_NOT_NULL( aligned64 );
    SW_EXPECT_EQUAL( 0u, reinterpret_cast<uintptr_t>( aligned64 ) % 64 );

    // 메모리 복사 및 비교 검증
    const utf8* kSamplePattern = "CoreMemoryAlignmentVerification";
    sw::Memory::copy( aligned64, kSamplePattern, 32 );
    SW_EXPECT_EQUAL( 0, sw::Memory::compare( aligned64, kSamplePattern, 32 ) );

    sw::Memory::freeAligned( aligned64 );
}

/**
 * @brief [MemoryTest] PoolAllocator 및 TypedPoolAllocator 할당, 해제, 재활용 및 클리어 검증
 */
SW_TEST_CASE( MemoryTest, PoolAllocatorAndTypedPool )
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
                *_pDestructFlag = true;
        }
    };

    bool                                   bDestroyed = false;
    sw::TypedPoolAllocator<TestPoolObject> typedPool( 8, false );

    TestPoolObject* pObj = typedPool.create( 999, &bDestroyed );
    SW_ASSERT_NOT_NULL( pObj );
    SW_EXPECT_EQUAL( 999, pObj->_val );
    SW_EXPECT_FALSE( bDestroyed );

    typedPool.destroy( pObj );
    SW_EXPECT_TRUE( bDestroyed );

    typedPool.clear();
}

/**
 * @brief [MemoryTest] PoolAllocator 다중 청크 확장, 징검다리 해제, 재할당 및 클리어 스트레스 검증
 */
SW_TEST_CASE( MemoryTest, PoolAllocatorMultiChunkStress )
{
    constexpr size_t kBlockSize      = 32;
    constexpr uint32 kBlocksPerChunk = 8;
    constexpr size_t kAllocCount     = 64; // 8개 청크 생성

    sw::PoolAllocator pool( kBlockSize, kBlocksPerChunk, true );
    sw::vector<void*> listBlocks;
    listBlocks.reserve( kAllocCount );

    // 1) 64개 블록 할당
    for ( size_t index = 0; index < kAllocCount; ++index )
    {
        void* pBlock = pool.allocate();
        SW_ASSERT_NOT_NULL( pBlock );
        sw::Memory::set( pBlock, static_cast<uint8>( index ), kBlockSize );
        listBlocks.push_back( pBlock );
    }

    // 2) 짝수 인덱스 32개 블록 해제
    for ( size_t index = 0; index < kAllocCount; index += 2 )
    {
        pool.free( listBlocks[index] );
    }

    // 3) 32개 재할당
    sw::vector<void*> listReused;
    listReused.reserve( kAllocCount / 2 );
    for ( size_t index = 0; index < kAllocCount / 2; ++index )
    {
        void* pReused = pool.allocate();
        SW_ASSERT_NOT_NULL( pReused );
        listReused.push_back( pReused );
    }

    // 4) 전체 해제 및 클리어
    for ( size_t index = 1; index < kAllocCount; index += 2 )
    {
        pool.free( listBlocks[index] );
    }
    for ( void* pBlock : listReused )
    {
        pool.free( pBlock );
    }

    pool.clear();
}

/**
 * @brief [MemoryTest] FrameArenaAllocator 3단계 중첩 Marker 및 순차/역순 롤백 검증
 */
SW_TEST_CASE( MemoryTest, FrameArenaNestedMarkers )
{
    sw::FrameArenaAllocator arena( 1024 );

    int32* pLevel0 = arena.construct<int32>( 10 );
    SW_ASSERT_NOT_NULL( pLevel0 );

    auto   marker1 = arena.createMarker();
    int32* pLevel1 = arena.construct<int32>( 20 );
    SW_ASSERT_NOT_NULL( pLevel1 );

    auto   marker2 = arena.createMarker();
    int32* pLevel2 = arena.construct<int32>( 30 );
    SW_ASSERT_NOT_NULL( pLevel2 );

    // level 2 롤백
    arena.rollbackToMarker( marker2 );
    int32* pReallocated2 = arena.construct<int32>( 300 );
    SW_EXPECT_EQUAL( pLevel2, pReallocated2 );
    SW_EXPECT_EQUAL( 300, *pReallocated2 );

    // level 1 롤백
    arena.rollbackToMarker( marker1 );
    int32* pReallocated1 = arena.construct<int32>( 200 );
    SW_EXPECT_EQUAL( pLevel1, pReallocated1 );
    SW_EXPECT_EQUAL( 200, *pReallocated1 );
    SW_EXPECT_EQUAL( 10, *pLevel0 );
}

/**
 * @brief [MemoryTest] 풀이 내주는 블록은 **16바이트 정렬**이다 (문서가 말하는 그 값)
 * @details 헤더가 오래 "포인터 크기 단위로 정렬" 이라고 적혀 있었지만 구현은 처음부터 16 이었다 —
 *          블록 크기도, 청크 헤더도, 기반 할당도 전부 16 이다. 그 차이는 SSE 타입을 담아도 되는가를
 *          가르므로(읽는 사람이 직접 패딩을 붙이거나 아예 못 쓴다고 판단한다) 계약으로 못박는다.
 */
SW_TEST_CASE( MemoryTest, PoolAllocatorHandsOutSixteenByteAlignedBlocks )
{
    // 일부러 16 의 배수가 아닌 크기를 준다 — 올림이 도는지 같이 본다.
    sw::PoolAllocator pool{ 20, 8, false };

    sw::vector<void*> listBlock;
    for ( uint32 index = 0; index < 32; ++index )
    {
        void* pBlock = pool.allocate();
        SW_ASSERT_NOT_NULL( pBlock );
        SW_EXPECT_TRUE_MSG( ( reinterpret_cast<uintptr_t>( pBlock ) % 16u ) == 0,
                            "풀 블록이 16바이트 정렬이 아니다 — SSE 타입을 담을 수 없다" );
        listBlock.push_back( pBlock );
    }

    // 돌려주고 다시 받아도 정렬은 그대로다(프리 리스트 경로).
    for ( void* pBlock : listBlock )
        pool.free( pBlock );

    for ( uint32 index = 0; index < 32; ++index )
    {
        void* pBlock = pool.allocate();
        SW_ASSERT_NOT_NULL( pBlock );
        SW_EXPECT_TRUE( ( reinterpret_cast<uintptr_t>( pBlock ) % 16u ) == 0 );
    }
}

/**
 * @brief [MemoryTest] 스레드 안전 풀은 여러 스레드가 동시에 할당·해제해도 블록을 겹쳐 주지 않는다
 * @details 잠금을 `unique_lock{ mutex, defer_lock }` 으로 바꿨다 — 예전에는 이른 반환마다 unlock 을
 *          손으로 적었고(7곳) 하나만 빠져도 데드락이다. 그 교체가 상호 배제를 망가뜨리지 않았는지 본다.
 */
SW_TEST_CASE( MemoryTest, ThreadSafePoolNeverHandsOutTheSameBlockTwice )
{
    sw::PoolAllocator pool{ 32, 64, true };

    constexpr uint32        kThreadCount     = 4;
    constexpr uint32        kBlocksPerThread = 200;
    sw::vector<std::thread> listWorker;
    std::mutex              collectMutex;
    sw::vector<void*>       listAll;

    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listWorker.emplace_back( [&]()
        {
            sw::vector<void*> listLocal;
            for ( uint32 index = 0; index < kBlocksPerThread; ++index )
                listLocal.push_back( pool.allocate() );

            std::scoped_lock<std::mutex> lock{ collectMutex };
            for ( void* pBlock : listLocal )
                listAll.push_back( pBlock );
        } );
    }
    for ( std::thread& worker : listWorker )
        worker.join();

    SW_EXPECT_EQUAL( static_cast<size_t>( kThreadCount * kBlocksPerThread ), listAll.size() );

    // 같은 주소를 두 번 내줬으면 두 소유자가 같은 메모리를 쓴다.
    std::sort( listAll.begin(), listAll.end() );
    uint32 duplicateCount = 0;
    for ( size_t index = 1; index < listAll.size(); ++index )
    {
        if ( listAll[index] == listAll[index - 1] )
            ++duplicateCount;
    }
    SW_EXPECT_TRUE_MSG( duplicateCount == 0, "풀이 같은 블록을 두 번 내줬다" );
}

/**
 * @brief [MemoryTest] 담을 수 없는 크기는 **주소를 만들어 주지 않는다**
 * @details 할당기 셋이 모두 `size + 무언가` 로 크기를 정했다 — `size` 가 클수록 그 합이 **뒤집혀
 *          작아진다.** 그러면 (1) 요청보다 작은 블록이 잡히고, (2) `오프셋 + size <= 용량` 검사가
 *          통과해 **블록 밖을 가리키는 주소**가 정상 할당인 척 돌아간다. 쓰는 순간 남의 메모리다.
 *          이런 크기는 어차피 할당될 수 없으므로 nullptr 로 끝내는 것이 맞다.
 */
SW_TEST_CASE( MemoryTest, AbsurdSizesReturnNullInsteadOfAWrappedBlock )
{
    constexpr size_t kNearMax = ~size_t( 0 ) - 8;

    // 1) 아레나 — 예전에는 `size + alignment` 가 뒤집혀 작은 청크를 끝없이 늘렸다.
    {
        sw::FrameArenaAllocator arena{ 4096 };
        SW_EXPECT_TRUE_MSG( arena.allocate( kNearMax, 64 ) == nullptr,
                            "담을 수 없는 크기에 주소를 돌려줬습니다" );
        SW_EXPECT_TRUE( arena.allocate( ~size_t( 0 ), 16 ) == nullptr );

        // 평소 할당은 그대로 된다 — "다 막는다" 로 굳지 않는다.
        void* pSmall = arena.allocate( 128, 16 );
        SW_EXPECT_TRUE( pSmall != nullptr );
        SW_EXPECT_TRUE( reinterpret_cast<uintptr_t>( pSmall ) % 16 == 0 );
    }

    // 2) 선형 할당기 — 같은 함정이 블록 쪽에 있었다.
    {
        sw::LinearAllocator linear{ 4096 };
        SW_EXPECT_TRUE_MSG( linear.allocate( kNearMax, 64 ) == nullptr,
                            "담을 수 없는 크기에 주소를 돌려줬습니다" );
        SW_EXPECT_TRUE( linear.allocate( ~size_t( 0 ), 16 ) == nullptr );

        void* pSmall = linear.allocate( 128, 16 );
        SW_EXPECT_TRUE( pSmall != nullptr );
        SW_EXPECT_TRUE( reinterpret_cast<uintptr_t>( pSmall ) % 16 == 0 );
    }

    // 3) 바닥의 Memory — 헤더 크기를 더하다 뒤집히면 헤더 쓰기가 곧바로 범위를 넘는다.
    {
        SW_EXPECT_TRUE( sw::Memory::allocate( ~size_t( 0 ) ) == nullptr );
        SW_EXPECT_TRUE( sw::Memory::allocateAligned( ~size_t( 0 ), 64 ) == nullptr );

        void* pSmall = sw::Memory::allocate( 64 );
        SW_ASSERT_TRUE( pSmall != nullptr );
        sw::Memory::free( pSmall );
    }
}
