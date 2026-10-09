#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/Memory/LinearAllocator.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/PoolAllocator.h"
#include "Core/Task/LockFreeObjectPool.h"

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
 * @details 블록 크기도, 청크 헤더도, 기반 할당도 전부 16 이다. 이 값은 SSE 타입을 담아도 되는가를
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
    {
        pool.free( pBlock );
    }

    for ( uint32 index = 0; index < 32; ++index )
    {
        void* pBlock = pool.allocate();
        SW_ASSERT_NOT_NULL( pBlock );
        SW_EXPECT_TRUE( ( reinterpret_cast<uintptr_t>( pBlock ) % 16u ) == 0 );
    }
}

/**
 * @brief [MemoryTest] 스레드 안전 풀은 여러 스레드가 동시에 할당·해제해도 블록을 겹쳐 주지 않는다
 * @details 잠금은 `unique_lock{ mutex, defer_lock }` 이다 — 이른 반환마다 unlock 을 손으로 적으면 하나만 빠져도
 *          데드락이다. 그 잠금이 상호 배제를 지키는지 본다.
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
            {
                listLocal.push_back( pool.allocate() );
            }

            std::scoped_lock<std::mutex> lock{ collectMutex };
            for ( void* pBlock : listLocal )
            {
                listAll.push_back( pBlock );
            }
        } );
    }
    for ( std::thread& worker : listWorker )
    {
        worker.join();
    }

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
 * @details 할당기 셋이 모두 `size + 무언가` 로 크기를 정한다 — `size` 가 크면 그 합이 **뒤집혀
 *          작아진다.** 그러면 (1) 요청보다 작은 블록이 잡히고, (2) `오프셋 + size <= 용량` 검사가
 *          통과해 **블록 밖을 가리키는 주소**가 정상 할당인 척 돌아간다. 쓰는 순간 남의 메모리다.
 *          이런 크기는 어차피 할당될 수 없으므로 nullptr 로 끝내는 것이 맞다.
 */
SW_TEST_CASE( MemoryTest, AbsurdSizesReturnNullInsteadOfAWrappedBlock )
{
    constexpr size_t kNearMax = ~size_t( 0 ) - 8;

    // 1) 아레나 — `size + alignment` 가 뒤집히면 작은 청크를 끝없이 늘린다.
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

    // 2) 선형 할당기 — 같은 함정이 블록 쪽에 있다.
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

/**
 * @brief [MemoryTest] 원소 개수 × 크기가 뒤집히면 **던진다** — 작은 블록을 내주지 않는다
 * @details `sw::Allocator<T>::allocate( n )` 은 `n * sizeof( T )` 로 바이트 수를 구한다. 그 곱이
 *          뒤집히면 **요청보다 훨씬 작은 블록**이 잡히고, 호출부는 원소 n 개를 쓸 수 있다고 믿고
 *          그 밖으로 나간다. 한계는 `vector::max_size()` 와 같은 `SIZE_MAX / sizeof(T)` 다.
 *          표준 할당기가 같은 자리에서 던지는 이유가 이것이다.
 */
SW_TEST_CASE( MemoryTest, AllocatorRejectsElementCountThatOverflows )
{
    sw::Allocator<int64> allocator;

    // sizeof(int64) == 8 이므로 max_size 는 SIZE_MAX/8 이다. 그 위는 곱이 뒤집힌다.
    const size_t overflowingCount = ( ~size_t( 0 ) / 8 ) + 1;

    bool bThrew = false;
    try
    {
        int64* pMemory = allocator.allocate( overflowingCount );
        allocator.deallocate( pMemory, overflowingCount );
    }
    catch ( const std::bad_alloc& )
    {
        bThrew = true;
    }
    SW_EXPECT_TRUE_MSG( bThrew, "곱이 뒤집히는 개수에 블록을 내줬습니다 — 호출부가 그 밖으로 나갑니다" );

#if !defined( SW_ENABLE_STL_CONTAINER ) // 아래는 커스텀 vector 의 한계다 — std::vector 는 `length_error` 를 던진다
    // vector 가 말하는 한계와 실제로 같은 자리인지 못박는다.
    sw::vector<int64> listValue;
    SW_EXPECT_EQUAL( ~size_t( 0 ) / sizeof( int64 ), listValue.max_size() );

    bool bReserveThrew = false;
    try
    {
        listValue.reserve( overflowingCount );
    }
    catch ( const std::bad_alloc& )
    {
        bReserveThrew = true;
    }
    SW_EXPECT_TRUE_MSG( bReserveThrew, "vector 가 max_size 를 넘는 reserve 를 받아들였습니다" );
    SW_EXPECT_TRUE( listValue.empty() );

    // 평범한 크기는 그대로 된다 — "다 막는다" 로 굳지 않는다.
    listValue.reserve( 16 );
    listValue.push_back( 42 );
    SW_EXPECT_EQUAL( size_t( 1 ), listValue.size() );
    SW_EXPECT_EQUAL( int64( 42 ), listValue[0] );
#endif
}

/**
 * @brief [MemoryTest] 풀의 자유 목록이 고리가 되지 않는지 검증(회귀 가드)
 * @details 같은 블록을 두 번 반납하면 `_pNext` 가 자기 자신을 가리켜 목록이 **자기 고리**가 된다 —
 *          그 뒤 모든 할당이 같은 블록을 돌려주고, 서로 다른 두 객체가 같은 주소에 앉는다(파괴 경로의
 *          check-then-set 경쟁이 이렇게 드러난다).
 *
 *          Debug 에서는 `PoolAllocator::free` 가 **두 번째 반납 그 자리에서** 단언으로
 *          멈춘다(표식 하나로 O(1) 에 본다 — 목록을 훑으면 해제가 O(n) 이 된다). 그 단언은
 *          프로세스를 세우므로 테스트로 부를 수 없고, 여기서는 **정상 순환이 그대로인지**를
 *          지킨다: 한 바퀴 돌린 뒤 새로 받은 블록들이 전부 다른 주소여야 한다.
 */
SW_TEST_CASE( MemoryTest, PoolFreeListDoesNotLoopAfterChurn )
{
    constexpr uint32  kBlockCount = 16;
    sw::PoolAllocator pool{ 64, kBlockCount, false };

    sw::vector<void*> listBlock;
    listBlock.reserve( kBlockCount );
    for ( uint32 index = 0; index < kBlockCount; ++index )
    {
        void* pBlock = pool.allocate();
        SW_ASSERT_NOT_NULL( pBlock );
        listBlock.push_back( pBlock );
    }

    for ( void* pBlock : listBlock )
    {
        pool.free( pBlock );
    }

    sw::unordered_set<void*> setFresh;
    for ( uint32 index = 0; index < kBlockCount; ++index )
    {
        void* pBlock = pool.allocate();
        SW_ASSERT_NOT_NULL( pBlock );
        setFresh.insert( pBlock );
    }

    // 고리가 생겼으면 여기가 1 이 된다.
    SW_EXPECT_EQUAL( size_t( kBlockCount ), setFresh.size() );
}

namespace
{
    struct alignas( 64 ) CacheLineAlignedInternal
    {
        uint8 _value{ 7 };
    };

    struct alignas( 16 ) SixteenAlignedInternal
    {
        float32 _arrValue[4]{ 1.f, 2.f, 3.f, 4.f };
    };

    /**
     * @brief 할당 관찰자 시험의 기록입니다. 다른 스레드(로거 등)도 같은 관찰자를 지나므로 **시험이 고른 크기**의 블록만 셉니다.
     */
    struct AllocationObserverRecordInternal
    {
        static constexpr size_t kProbeSize = 777773; ///< 다른 코드가 우연히 잡지 않을 크기

        static sw::atomic<const void*> s_pLastAllocated;
        static sw::atomic<uint32>      s_allocateCount;
        static sw::atomic<uint32>      s_freeCount;
        static sw::atomic<const void*> s_pWatchedFree; ///< 이 주소의 해제만 센다

        static void onAllocate( const void* pPtr, size_t size, sw::MemoryTag )
        {
            if ( size != kProbeSize )
                return;
            s_pLastAllocated.store( pPtr );
            s_allocateCount.fetch_add( 1 );
        }

        static void onFree( const void* pPtr, sw::MemoryTag )
        {
            if ( pPtr != nullptr && pPtr == s_pWatchedFree.load() )
                s_freeCount.fetch_add( 1 );
        }
    };

    sw::atomic<const void*> AllocationObserverRecordInternal::s_pLastAllocated{ nullptr };
    sw::atomic<uint32>      AllocationObserverRecordInternal::s_allocateCount{ 0 };
    sw::atomic<uint32>      AllocationObserverRecordInternal::s_freeCount{ 0 };
    sw::atomic<const void*> AllocationObserverRecordInternal::s_pWatchedFree{ nullptr };
} // namespace

/**
 * @brief [MemoryTest] `sw_new` 로 만든 과정렬 타입은 제 정렬로 잡히고, `sw_delete` 는 같은 힙 계열로 푼다.
 * @details `sw_new` 에 정렬 오버로드가 없으면 64 바이트 정렬 타입(`ParallelGroup` 등)이 16 바이트 정렬로 잡힌다. 잡기와 풀기가 다른 힙 계열이면
 *          (16 바이트 정렬 타입은 MSVC 의 `max_align_t` 가 8 이라 해제만 정렬 해제 `_aligned_free` 가 되는 식) 힙이 깨진다.
 */
SW_TEST_CASE( MemoryTest, SwNewHonoursOverAlignmentAndMatchesSwDelete )
{
    for ( uint32 repeat = 0; repeat < 32; ++repeat )
    {
        CacheLineAlignedInternal* pLine = sw_new CacheLineAlignedInternal();
        SW_ASSERT_NOT_NULL( pLine );
        SW_EXPECT_EQUAL( 0u, static_cast<uint32>( reinterpret_cast<uintptr_t>( pLine ) % 64 ) );
        SW_EXPECT_EQUAL( 7u, static_cast<uint32>( pLine->_value ) );
        sw_delete( pLine );

        SixteenAlignedInternal* pSixteen = sw_new SixteenAlignedInternal();
        SW_ASSERT_NOT_NULL( pSixteen );
        SW_EXPECT_EQUAL( 0u, static_cast<uint32>( reinterpret_cast<uintptr_t>( pSixteen ) % 16 ) );
        sw_delete( pSixteen );

        sw::unique_ptr<CacheLineAlignedInternal> pOwned = sw::make_unique<CacheLineAlignedInternal>();
        SW_EXPECT_EQUAL( 0u, static_cast<uint32>( reinterpret_cast<uintptr_t>( pOwned.get() ) % 64 ) );
    }

    // 컨테이너(`sw::Allocator`)도 원소 정렬을 지킨다.
    sw::vector<CacheLineAlignedInternal> listLine;
    listLine.resize( 5 );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( reinterpret_cast<uintptr_t>( listLine.data() ) % 64 ) );
}

/**
 * @brief [MemoryTest] `sw_new_array` 는 원소를 값 초기화하고 정렬을 지키며, `make_unique<T[]>` 도 같은 경로로 잡는다
 */
SW_TEST_CASE( MemoryTest, NewArrayValueInitializesAndKeepsAlignment )
{
    CacheLineAlignedInternal* pAligned = sw_new_array<CacheLineAlignedInternal>( 5 );
    SW_ASSERT_NOT_NULL( pAligned );
    for ( uint32 index = 0; index < 5; ++index )
    {
        SW_EXPECT_EQUAL( 0u, static_cast<uint32>( reinterpret_cast<uintptr_t>( pAligned + index ) % 64 ) );
        SW_EXPECT_EQUAL( 7u, static_cast<uint32>( pAligned[index]._value ) );
    }
    sw_delete_array( pAligned, 5 );

    sw::unique_ptr<uint64[]> arrWord = sw::make_unique<uint64[]>( 33 );
    SW_ASSERT_NOT_NULL( arrWord.get() );
    for ( uint32 index = 0; index < 33; ++index )
    {
        SW_EXPECT_EQUAL( 0ull, arrWord[index] );
    }
}

/**
 * @brief [MemoryTest] 할당 관찰자는 관찰 중에 잡힌 블록의 해제만 알린다
 * @details 외부 프로파일러(Tracy)는 짝 없는 해제를 받으면 기록을 멈춘다. 관찰자를 걸기 **전에** 잡은 블록을 나중에 풀 때 알리면 안 된다.
 *          배포본은 헤더가 없어 관찰자를 지원하지 않는다(아무것도 알리지 않는다).
 */
SW_TEST_CASE( MemoryTest, AllocationObserverSeesOnlyBlocksAllocatedWhileAttached )
{
    using Record = AllocationObserverRecordInternal;
    static constexpr sw::MemoryAllocationObserver kObserver{ &Record::onAllocate, &Record::onFree };

    void* pBefore = sw::Memory::allocate( Record::kProbeSize );
    SW_ASSERT_NOT_NULL( pBefore );

    sw::Memory::setAllocationObserver( &kObserver );
    Record::s_pWatchedFree.store( pBefore );
    sw::Memory::free( pBefore );
    [[maybe_unused]] const uint32 freeOfUnobserved = Record::s_freeCount.load();

    void* pDuring = sw::Memory::allocate( Record::kProbeSize );
    SW_ASSERT_NOT_NULL( pDuring );
    Record::s_pWatchedFree.store( pDuring );
    sw::Memory::free( pDuring );
    sw::Memory::setAllocationObserver( nullptr );

#if defined( SW_SHIPPING )
    SW_EXPECT_EQUAL( 0u, Record::s_allocateCount.load() );
    SW_EXPECT_EQUAL( 0u, Record::s_freeCount.load() );
#else
    SW_EXPECT_EQUAL( 0u, freeOfUnobserved );
    SW_EXPECT_EQUAL( 1u, Record::s_allocateCount.load() );
    SW_EXPECT_TRUE( Record::s_pLastAllocated.load() == pDuring );
    SW_EXPECT_EQUAL( 1u, Record::s_freeCount.load() );
#endif

    // 뗀 뒤에는 아무것도 오지 않는다.
    void* pAfter = sw::Memory::allocate( Record::kProbeSize );
    sw::Memory::free( pAfter );
#if !defined( SW_SHIPPING )
    SW_EXPECT_EQUAL( 1u, Record::s_allocateCount.load() );
#endif
}
