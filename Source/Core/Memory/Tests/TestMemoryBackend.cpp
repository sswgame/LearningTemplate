#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Memory/MemoryBackend.h"
#include "Core/Testing/TestFramework.h"

#include <thread>

using namespace sw;

// ------------------------------------------------------------------------------
// 1) MemoryBackendTest — 힙 백엔드 계약(`MemoryBackend.h`)
//
// 어느 백엔드(`SW_MEMORY_BACKEND` — crt · mimalloc)를 링크해도 같은 테스트가 돈다. 백엔드를 바꾸는 통합은 이 묶음이 두 구성에서 다 통과하는지를 본다.
// ------------------------------------------------------------------------------

namespace
{
    /** @brief 블록 @p pBlock 의 앞 @p size 바이트를 위치마다 다른 값으로 채웁니다. */
    void fillPattern( void* pBlock, size_t size )
    {
        uint8* pByte = static_cast<uint8*>( pBlock );
        for ( size_t index = 0; index < size; ++index )
        {
            pByte[index] = static_cast<uint8>( index * 31 + 7 );
        }
    }

    /** @brief `fillPattern` 이 쓴 값이 앞 @p size 바이트에 그대로 있는지입니다. */
    bool hasPattern( const void* pBlock, size_t size )
    {
        const uint8* pByte = static_cast<const uint8*>( pBlock );
        for ( size_t index = 0; index < size; ++index )
        {
            if ( pByte[index] != static_cast<uint8>( index * 31 + 7 ) )
                return false;
        }
        return true;
    }

    bool isAligned( const void* pBlock, size_t alignment )
    {
        return reinterpret_cast<uintptr_t>( pBlock ) % alignment == 0;
    }

    /** @brief 스레드 사이 해제 테스트에서 @p blockIndex 번째 블록을 정렬 할당으로 잡는지입니다(잡는 쪽과 푸는 쪽이 같은 규칙을 본다). */
    bool usesAlignedBlock( size_t blockIndex )
    {
        return blockIndex % 3 == 0;
    }

    /** @brief 스레드 사이 해제 테스트의 @p blockIndex 번째 블록 크기입니다. */
    size_t getProducedBlockSize( size_t blockIndex )
    {
        return 16 + ( blockIndex * 37 ) % 2000;
    }

    void freeProducedBlock( void* pBlock, size_t blockIndex )
    {
        if ( usesAlignedBlock( blockIndex ) )
            MemoryBackend::freeAligned( pBlock );
        else
            MemoryBackend::free( pBlock );
    }
} // namespace

/**
 * @brief [MemoryBackendTest] 보통 블록은 기본 정렬, 정렬 블록은 요청한 정렬에 놓인다
 * @details `Memory` 의 개발 빌드 헤더(48 바이트)와 `kUsesAlignedAllocation` 이 기본 정렬(`__STDCPP_DEFAULT_NEW_ALIGNMENT__`)에 기댄다.
 */
SW_TEST_CASE( MemoryBackendTest, BlocksHonorDefaultAndRequestedAlignment )
{
    constexpr size_t kArrSize[] = { 16, 24, 64, 256, 4096, 1 << 20 };
    for ( size_t size : kArrSize )
    {
        void* pBlock = MemoryBackend::allocate( size );
        SW_REQUIRE_NOT_NULL( pBlock );
        SW_EXPECT_TRUE_MSG( isAligned( pBlock, __STDCPP_DEFAULT_NEW_ALIGNMENT__ ), "보통 블록이 기본 정렬에 놓이지 않았다" );
        fillPattern( pBlock, size );
        SW_EXPECT_TRUE( hasPattern( pBlock, size ) );
        MemoryBackend::free( pBlock );
    }

    constexpr size_t kArrAlignment[] = { sizeof( void* ), 16, 32, 64, 256, 4096 };
    for ( size_t alignment : kArrAlignment )
    {
        for ( size_t size : { size_t( 1 ), size_t( 100 ), size_t( 8192 ) } )
        {
            void* pBlock = MemoryBackend::allocateAligned( size, alignment );
            SW_REQUIRE_NOT_NULL( pBlock );
            SW_EXPECT_TRUE_MSG( isAligned( pBlock, alignment ), "정렬 블록이 요청한 정렬에 놓이지 않았다" );
            fillPattern( pBlock, size );
            SW_EXPECT_TRUE( hasPattern( pBlock, size ) );
            MemoryBackend::freeAligned( pBlock );
        }
    }
}

/**
 * @brief [MemoryBackendTest] 쓸 수 있는 크기는 요청한 크기 이상이고, 그만큼 써도 된다
 */
SW_TEST_CASE( MemoryBackendTest, UsableSizeCoversTheRequest )
{
    SW_EXPECT_EQUAL( size_t( 0 ), MemoryBackend::getUsableSize( nullptr ) );

    constexpr size_t kArrSize[] = { 1, 7, 16, 100, 1000, 65536 };
    for ( size_t size : kArrSize )
    {
        void* pBlock = MemoryBackend::allocate( size );
        SW_REQUIRE_NOT_NULL( pBlock );
        const size_t usableSize = MemoryBackend::getUsableSize( pBlock );
        SW_EXPECT_TRUE_MSG( usableSize >= size, "쓸 수 있는 크기가 요청보다 작다" );
        // 올림된 꼬리까지 쓴다 — 크기 조회가 거짓이면 힙 검사(ASan · CRT 디버그 힙)가 잡는다.
        fillPattern( pBlock, usableSize );
        SW_EXPECT_TRUE( hasPattern( pBlock, usableSize ) );
        MemoryBackend::free( pBlock );
    }
}

/**
 * @brief [MemoryBackendTest] 재할당은 키울 때도 줄일 때도 앞쪽 바이트를 옮기고, nullptr · 0 은 계약대로 할당 · 해제다
 */
SW_TEST_CASE( MemoryBackendTest, ReallocatePreservesContents )
{
    void* pBlock = MemoryBackend::reallocate( nullptr, 64 );
    SW_REQUIRE_NOT_NULL( pBlock );
    fillPattern( pBlock, 64 );

    // 크기 등급을 여러 번 넘겨 옮겨지게 한다(같은 자리에서 늘 수 있는 크기만 보면 복사를 시험하지 못한다).
    size_t           keptSize       = 64;
    constexpr size_t kArrGrowSize[] = { 200, 4096, 100000, 3 << 20 };
    for ( size_t newSize : kArrGrowSize )
    {
        void* pGrown = MemoryBackend::reallocate( pBlock, newSize );
        SW_REQUIRE_NOT_NULL( pGrown );
        SW_EXPECT_TRUE_MSG( hasPattern( pGrown, keptSize ), "키운 블록에 앞쪽 바이트가 옮겨지지 않았다" );
        pBlock = pGrown;
        fillPattern( pBlock, newSize );
        keptSize = newSize;
    }

    void* pShrunk = MemoryBackend::reallocate( pBlock, 32 );
    SW_REQUIRE_NOT_NULL( pShrunk );
    SW_EXPECT_TRUE_MSG( hasPattern( pShrunk, 32 ), "줄인 블록에 앞쪽 바이트가 남지 않았다" );

    SW_EXPECT_NULL( MemoryBackend::reallocate( pShrunk, 0 ) );

    // nullptr 해제는 아무것도 하지 않는다.
    MemoryBackend::free( nullptr );
    MemoryBackend::freeAligned( nullptr );
}

/**
 * @brief [MemoryBackendTest] 다른 스레드가 잡은 블록을 풀어도 된다(작업 스레드가 잡고 게임 스레드가 푸는 모양)
 * @details 스레드 캐시가 있는 백엔드(mimalloc)는 남의 스레드 블록을 원래 주인에게 돌려보내는 길이 따로 있다. 잡은 스레드가 끝난 뒤에 푸는 것도 본다.
 */
SW_TEST_CASE( MemoryBackendTest, BlocksCanBeFreedOnAnotherThread )
{
    constexpr size_t kThreadCount    = 4;
    constexpr size_t kBlockPerThread = 256;

    vector<vector<void*>> arrProducedBlock;
    arrProducedBlock.resize( kThreadCount );
    {
        vector<std::thread> arrThread;
        for ( size_t threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
        {
            // 칸 참조는 스레드를 만들기 전에 얻는다 — 바깥 vector 의 non-const operator[] 를 여러 스레드가 동시에 부르면 경쟁 감지기(RaceDetectContext)가 쓰기 둘로 본다.
            vector<void*>& arrBlock = arrProducedBlock[threadIndex];
            arrThread.emplace_back( [&arrBlock]()
            {
                for ( size_t blockIndex = 0; blockIndex < kBlockPerThread; ++blockIndex )
                {
                    const size_t size   = getProducedBlockSize( blockIndex );
                    void*        pBlock = usesAlignedBlock( blockIndex ) ? MemoryBackend::allocateAligned( size, 64 ) : MemoryBackend::allocate( size );
                    if ( pBlock != nullptr )
                        fillPattern( pBlock, size );
                    arrBlock.push_back( pBlock );
                }
            } );
        }
        // 잡은 스레드가 모두 끝난 뒤에 푼다 — 주인 스레드가 사라진 블록이다.
        for ( std::thread& thread : arrThread )
        {
            thread.join();
        }
    }

    // 절반은 새 스레드가, 나머지 절반은 이 스레드가 푼다.
    std::thread releaseThread{ [&arrProducedBlock]()
    {
        for ( size_t threadIndex = 0; threadIndex < kThreadCount / 2; ++threadIndex )
        {
            vector<void*>& arrBlock = arrProducedBlock[threadIndex];
            for ( size_t blockIndex = 0; blockIndex < arrBlock.size(); ++blockIndex )
            {
                freeProducedBlock( arrBlock[blockIndex], blockIndex );
            }
        }
    } };
    releaseThread.join();

    for ( size_t threadIndex = kThreadCount / 2; threadIndex < kThreadCount; ++threadIndex )
    {
        const vector<void*>& arrBlock = arrProducedBlock[threadIndex];
        for ( size_t blockIndex = 0; blockIndex < arrBlock.size(); ++blockIndex )
        {
            SW_REQUIRE_NOT_NULL( arrBlock[blockIndex] );
            SW_EXPECT_TRUE( hasPattern( arrBlock[blockIndex], getProducedBlockSize( blockIndex ) ) );
            freeProducedBlock( arrBlock[blockIndex], blockIndex );
        }
    }
}
