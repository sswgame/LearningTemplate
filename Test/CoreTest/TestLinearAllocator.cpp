#include "pch.h"

#include "Core/Memory/LinearAllocator.h"

#include "TestFramework/TestFramework.h"

#include <algorithm>
#include <mutex>
#include <thread>

using namespace sw;

// ------------------------------------------------------------------------------
// 1) LinearAllocatorTest — 정렬 계약 · reset 재사용 · clear · 동시 할당
//
// `EventDispatcher` 가 프레임마다 이 할당기 둘을 reset 한다(`_arrFrameAllocator[2]`). 그래서
// "reset 이 무엇을 되돌려 주는가" 가 곧 프레임당 메모리 증가량이다 — 여기가 비어 있었다.
// ------------------------------------------------------------------------------

/**
 * @brief [LinearAllocatorTest] 크기 0 은 거절하고, 2의 거듭제곱이 아닌 정렬은 기본 정렬로 되돌린다
 */
SW_TEST_CASE( LinearAllocatorTest, AllocateHonorsAlignmentAndRejectsZeroSize )
{
    LinearAllocator allocator{ 4096 };

    SW_EXPECT_NULL( allocator.allocate( 0 ) );

    // 정렬은 2의 거듭제곱만 뜻이 있다 — 아닌 값은 **거절이 아니라** 기본 정렬로 되돌린다.
    void* pDefaultAligned = allocator.allocate( 8, 3 );
    SW_ASSERT_NOT_NULL( pDefaultAligned );
    SW_EXPECT_EQUAL( uintptr_t( 0 ), reinterpret_cast<uintptr_t>( pDefaultAligned ) % alignof( std::max_align_t ) );

    constexpr size_t kArrAlignment[] = { 16, 32, 64, 128 };
    for ( size_t alignment : kArrAlignment )
    {
        // 앞에 1바이트를 끼워 넣어 다음 할당이 반드시 정렬 패딩을 밟게 한다.
        SW_EXPECT_NOT_NULL( allocator.allocate( 1, 1 ) );

        void* pAligned = allocator.allocate( 24, alignment );
        SW_ASSERT_NOT_NULL( pAligned );
        SW_EXPECT_TRUE_MSG( ( reinterpret_cast<uintptr_t>( pAligned ) % alignment ) == 0,
                            "요청한 정렬로 잘라 주지 않았다" );
    }
}

/**
 * @brief [LinearAllocatorTest] reset 은 이미 확보한 블록을 **전부** 다시 내준다
 * @details 헤더가 "오프셋만 되돌리고 메모리는 유지한다" 고 말하는 자리다. 첫 블록만 다시 쓰고
 *          나머지를 새로 잡으면, 비어 있는 블록을 끌어안은 채 reset 할 때마다 표가 늘어난다
 *          (블록 용량은 배로 커지므로 늘어나는 양도 배가 된다). 주소가 그 증거다 — 같은 순서로
 *          같은 크기를 요청하면 같은 주소가 나와야 한다.
 */
SW_TEST_CASE( LinearAllocatorTest, ResetReusesTheBlocksItAlreadyHolds )
{
    // 요청 하나가 블록 하나를 거의 채우도록 작게 잡는다 — 블록 셋을 만드는 것이 목적이다.
    LinearAllocator allocator{ 256 };

    void* pFirst  = allocator.allocate( 200 );
    void* pSecond = allocator.allocate( 500 );
    void* pThird  = allocator.allocate( 500 );
    SW_ASSERT_NOT_NULL( pFirst );
    SW_ASSERT_NOT_NULL( pSecond );
    SW_ASSERT_NOT_NULL( pThird );
    SW_EXPECT_TRUE( pFirst != pSecond );
    SW_EXPECT_TRUE( pSecond != pThird );

    allocator.reset();

    SW_EXPECT_EQUAL( pFirst, allocator.allocate( 200 ) );
    SW_EXPECT_TRUE_MSG( allocator.allocate( 500 ) == pSecond,
                        "reset 뒤에 둘째 블록을 다시 쓰지 않았다 — 빈 블록을 두고 새로 잡는다" );
    SW_EXPECT_TRUE_MSG( allocator.allocate( 500 ) == pThird,
                        "reset 뒤에 셋째 블록을 다시 쓰지 않았다 — 빈 블록을 두고 새로 잡는다" );
}

/**
 * @brief [LinearAllocatorTest] 기본 블록보다 큰 요청도 자기 블록을 받아 끝까지 쓸 수 있다
 */
SW_TEST_CASE( LinearAllocatorTest, RequestLargerThanTheDefaultBlockGetsItsOwnBlock )
{
    LinearAllocator allocator{ 128 };

    constexpr size_t kLargeSize = 8192;
    uint8*           pLarge     = static_cast<uint8*>( allocator.allocate( kLargeSize, 64 ) );
    SW_ASSERT_NOT_NULL( pLarge );
    SW_EXPECT_EQUAL( uintptr_t( 0 ), reinterpret_cast<uintptr_t>( pLarge ) % 64u );

    // 용량을 잘라 잡았다면 마지막 바이트에서 남의 메모리를 밟는다(ASan 이 여기서 운다).
    for ( size_t index = 0; index < kLargeSize; ++index )
        pLarge[index] = static_cast<uint8>( index & 0xFFu );

    SW_EXPECT_EQUAL( uint32( 0 ), static_cast<uint32>( pLarge[0] ) );
    SW_EXPECT_EQUAL( uint32( ( kLargeSize - 1 ) & 0xFFu ), static_cast<uint32>( pLarge[kLargeSize - 1] ) );
}

/**
 * @brief [LinearAllocatorTest] clear 는 블록을 돌려주고, 그 뒤에도 할당기는 계속 쓸 수 있다
 * @details clear 뒤에는 현재 블록이 nullptr 이다 — allocate 가 그 자리에서 블록을 다시 잡는
 *          경로(첫 진입과 같은 길)를 밟는다.
 */
SW_TEST_CASE( LinearAllocatorTest, ClearReleasesBlocksAndTheAllocatorStaysUsable )
{
    LinearAllocator allocator{ 256 };

    SW_ASSERT_NOT_NULL( allocator.allocate( 200 ) );
    allocator.clear();

    uint8* pAfterClear = static_cast<uint8*>( allocator.allocate( 200 ) );
    SW_ASSERT_NOT_NULL( pAfterClear );
    pAfterClear[0]   = 7;
    pAfterClear[199] = 9;
    SW_EXPECT_EQUAL( uint32( 7 ), static_cast<uint32>( pAfterClear[0] ) );
    SW_EXPECT_EQUAL( uint32( 9 ), static_cast<uint32>( pAfterClear[199] ) );
}

/**
 * @brief [LinearAllocatorTest] 여러 스레드가 동시에 할당해도 같은 메모리를 두 번 내주지 않는다
 * @details 헤더가 약속하는 것이 정확히 이것이다(allocate 는 동시 호출 안전, reset/clear 는 아님).
 *          블록이 가득 차 새 블록으로 넘어가는 자리가 경합 지점이라, 블록을 여러 번 넘기도록
 *          작은 초기 용량을 준다.
 */
SW_TEST_CASE( LinearAllocatorTest, ConcurrentAllocateNeverHandsOutOverlappingMemory )
{
    struct Chunk
    {
        uint8* _pData;
        uint8  _pattern;
    };

    constexpr uint32 kThreadCount    = 4;
    constexpr uint32 kChunkPerThread = 200;
    constexpr size_t kChunkSize      = 64;

    LinearAllocator     allocator{ 1024 };
    vector<std::thread> listWorker;
    std::mutex          collectMutex;
    vector<Chunk>       listAll;

    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listWorker.emplace_back( [&allocator, &collectMutex, &listAll, threadIndex]()
        {
            const uint8   pattern = static_cast<uint8>( threadIndex + 1 );
            vector<Chunk> listLocal;
            for ( uint32 index = 0; index < kChunkPerThread; ++index )
            {
                uint8* pChunk = static_cast<uint8*>( allocator.allocate( kChunkSize, 16 ) );
                if ( pChunk == nullptr )
                    continue;

                for ( size_t byteIndex = 0; byteIndex < kChunkSize; ++byteIndex )
                    pChunk[byteIndex] = pattern;
                listLocal.push_back( Chunk{ pChunk, pattern } );
            }

            std::scoped_lock<std::mutex> lock{ collectMutex };
            for ( const Chunk& chunk : listLocal )
                listAll.push_back( chunk );
        } );
    }
    for ( std::thread& worker : listWorker )
        worker.join();

    SW_ASSERT_EQUAL( static_cast<size_t>( kThreadCount * kChunkPerThread ), listAll.size() );

    // 1) 같은 주소를 두 번 내줬으면 두 소유자가 같은 메모리를 쓴다.
    vector<uint8*> listAddress;
    listAddress.reserve( listAll.size() );
    for ( const Chunk& chunk : listAll )
        listAddress.push_back( chunk._pData );

    std::sort( listAddress.begin(), listAddress.end() );
    uint32 duplicateCount = 0;
    for ( size_t index = 1; index < listAddress.size(); ++index )
    {
        if ( listAddress[index] == listAddress[index - 1] )
            ++duplicateCount;
    }
    SW_EXPECT_TRUE_MSG( duplicateCount == 0, "할당기가 같은 주소를 두 번 내줬다" );

    // 2) 주소가 달라도 범위가 겹칠 수 있다 — 쓴 값이 그대로면 겹치지 않은 것이다.
    uint32 corruptedCount = 0;
    for ( const Chunk& chunk : listAll )
    {
        for ( size_t byteIndex = 0; byteIndex < kChunkSize; ++byteIndex )
        {
            if ( chunk._pData[byteIndex] != chunk._pattern )
            {
                ++corruptedCount;
                break;
            }
        }
    }
    SW_EXPECT_TRUE_MSG( corruptedCount == 0, "다른 스레드의 할당이 내가 쓴 바이트를 덮었다" );
}
