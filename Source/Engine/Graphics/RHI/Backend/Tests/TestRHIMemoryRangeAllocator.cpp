/**
 * @file TestRHIMemoryRangeAllocator.cpp
 * @brief 메모리 블록의 구간 장부(`RHIMemoryRangeAllocator`)입니다 — 정렬 · 겹치지 않음 · 놓은 뒤 이웃 합치기.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Backend/RHIMemoryRangeAllocator.h"

namespace sw
{
    /** @brief 안을 보는 테스트 창구입니다(`friend`). */
    struct RHIMemoryRangeAllocatorTestAccess
    {
        static uint32 getFreeRangeCount( const RHIMemoryRangeAllocator& ranges ) { return static_cast<uint32>( ranges._listFree.size() ); }
    };
} // namespace sw

using namespace sw;

/**
 * @brief [RHIMemoryRangeAllocatorTest] 정렬에 맞춘 구간을 겹치지 않게 주고, 자리가 없으면 거절한다. 정렬로 남은 앞 조각도 다시 쓴다
 * @details 변이: 앞 조각을 버리면 마지막 작은 할당이 진다.
 */
SW_TEST_CASE( RHIMemoryRangeAllocatorTest, AlignsWithoutOverlapAndReusesPadding )
{
    RHIMemoryRangeAllocator ranges;
    ranges.reset( 1024 );
    uint64 first{ 0 };
    uint64 second{ 0 };
    SW_REQUIRE_TRUE( ranges.allocate( 100, 1, first ) );
    SW_REQUIRE_TRUE( ranges.allocate( 256, 256, second ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, first );
    SW_EXPECT_EQUAL( uint64{ 256 }, second );
    uint64 tooBig{ 0 };
    SW_EXPECT_FALSE( ranges.allocate( 600, 1, tooBig ) );
    uint64 padding{ 0 };
    SW_REQUIRE_TRUE( ranges.allocate( 156, 1, padding ) ); // 100..256 의 앞 조각
    SW_EXPECT_EQUAL( uint64{ 100 }, padding );
    SW_EXPECT_EQUAL( uint64{ 512 }, ranges.getUsedBytes() );
}

/**
 * @brief [RHIMemoryRangeAllocatorTest] 어떤 차례로 놓아도 다 놓으면 빈 구간 하나로 돌아와 블록 전체를 다시 줄 수 있다
 * @details 변이: 앞이나 뒤 이웃과 합치지 않으면 빈 구간 수나 마지막 전체 할당이 진다.
 */
SW_TEST_CASE( RHIMemoryRangeAllocatorTest, ReleasingEverythingMergesBack )
{
    constexpr uint64        kPiece = 64;
    constexpr uint32        kCount = 16;
    RHIMemoryRangeAllocator ranges;
    ranges.reset( kPiece * kCount );
    vector<uint64> listOffset;
    for ( uint32 index = 0; index < kCount; ++index )
    {
        uint64 offset{ 0 };
        SW_REQUIRE_TRUE( ranges.allocate( kPiece, kPiece, offset ) );
        listOffset.push_back( offset );
    }
    uint64 none{ 0 };
    SW_EXPECT_FALSE( ranges.allocate( 1, 1, none ) );
    for ( uint32 index = 0; index < kCount; index += 2 )
    {
        ranges.release( listOffset[index], kPiece );
    }
    SW_EXPECT_EQUAL( kCount / 2, RHIMemoryRangeAllocatorTestAccess::getFreeRangeCount( ranges ) );
    for ( uint32 index = 1; index < kCount; index += 2 )
    {
        ranges.release( listOffset[index], kPiece );
    }
    SW_EXPECT_TRUE( ranges.isEmpty() );
    SW_EXPECT_EQUAL( 1u, RHIMemoryRangeAllocatorTestAccess::getFreeRangeCount( ranges ) );
    uint64 whole{ 1 };
    SW_REQUIRE_TRUE( ranges.allocate( kPiece * kCount, 1, whole ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, whole );
}
