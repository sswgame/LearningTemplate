/**
 * @file TestRHIStagingSpan.cpp
 * @brief 스테이징 구간(`RHIStagingSpan`)이 이동 전용 소유 형식으로 한 번만 돌아가는가 — 돌려주지 않으면 소멸자가 `free` 하고, 옛 세대 구간은 새 링을 건드리지 않는다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Transfer/RHIStagingAllocator.h"

#include <utility>

using namespace sw;

/**
 * @brief [RHIStagingSpanTest] 돌려주지 않은 구간은 소멸자가 돌려주고, 옮긴 구간은 한 번만 돌아간다
 * @details 변이: 소멸자의 `free` 를 빼면(옛 평범한 구조체) 산 구간 수가 1 로 남아 첫 단언이 지고, 이동 뒤 원본을 비우지 않으면 두 번 돌려줘 둘째 묶음이 진다.
 */
SW_TEST_CASE( RHIStagingSpanTest, DroppedSpanIsFreedAndMovedSpanFreesOnce )
{
    RHIStagingAllocator allocator;
    allocator.initialize( 4096, 256, 0 );
    {
        RHIStagingSpan span;
        SW_REQUIRE_TRUE( allocator.allocate( 512, 0, span ) == RHIStagingAllocateResult::Allocated );
        SW_EXPECT_TRUE( span.isValid() );
        SW_EXPECT_EQUAL( 1u, allocator.getLiveCount() );
    }
    SW_EXPECT_EQUAL( 0u, allocator.getLiveCount() );

    RHIStagingSpan first;
    SW_REQUIRE_TRUE( allocator.allocate( 512, 0, first ) == RHIStagingAllocateResult::Allocated );
    RHIStagingSpan moved = std::move( first );
    SW_EXPECT_FALSE( first.isValid() ); // NOLINT(bugprone-use-after-move): 이동 뒤 비었는지가 테스트 대상이다
    SW_EXPECT_TRUE( moved.isValid() );
    allocator.free( std::move( moved ) );
    SW_EXPECT_FALSE( moved.isValid() ); // NOLINT(bugprone-use-after-move): 돌려준 뒤 비었는지가 테스트 대상이다
    SW_EXPECT_EQUAL( 0u, allocator.getLiveCount() );
}

/**
 * @brief [RHIStagingSpanTest] 시리얼을 붙인 구간은 워터마크가 지나야 돌아오고, 구간 값은 그 뒤 비어 있다
 * @details 변이: `markSubmitted` 가 구간을 비우지 않으면 범위를 벗어날 때 소멸자가 한 번 더 돌려줘 산 구간 수가 어긋난다.
 */
SW_TEST_CASE( RHIStagingSpanTest, SubmittedSpanReturnsAfterTheWatermark )
{
    RHIStagingAllocator allocator;
    allocator.initialize( 4096, 256, 0 );
    {
        RHIStagingSpan span;
        SW_REQUIRE_TRUE( allocator.allocate( 256, 0, span ) == RHIStagingAllocateResult::Allocated );
        allocator.markSubmitted( std::move( span ), 7 );
        SW_EXPECT_FALSE( span.isValid() ); // NOLINT(bugprone-use-after-move): 넘긴 뒤 비었는지가 테스트 대상이다
    }
    SW_EXPECT_EQUAL( 1u, allocator.getLiveCount() );
    allocator.freeCompleted( 6 );
    SW_EXPECT_EQUAL( 1u, allocator.getLiveCount() );
    allocator.freeCompleted( 7 );
    SW_EXPECT_EQUAL( 0u, allocator.getLiveCount() );
}

/**
 * @brief [RHIStagingSpanTest] 할당기를 다시 세운 뒤 사라지는 옛 구간은 새 링의 산 구간을 건드리지 않는다
 * @details 변이: 세대 대조를 빼면 옛 구간의 소멸자가 새 링의 같은 자리를 돌려줘 산 구간 수가 0 이 된다.
 */
SW_TEST_CASE( RHIStagingSpanTest, SpanFromAnEarlierGenerationIsDropped )
{
    RHIStagingAllocator allocator;
    allocator.initialize( 4096, 256, 0 );
    RHIStagingSpan stale;
    SW_REQUIRE_TRUE( allocator.allocate( 256, 0, stale ) == RHIStagingAllocateResult::Allocated );
    allocator.shutdown();
    allocator.initialize( 4096, 256, 0 );
    RHIStagingSpan fresh;
    SW_REQUIRE_TRUE( allocator.allocate( 256, 0, fresh ) == RHIStagingAllocateResult::Allocated );
    SW_EXPECT_EQUAL( stale.getOffsetBytes(), fresh.getOffsetBytes() );
    stale = RHIStagingSpan{};
    SW_EXPECT_EQUAL( 1u, allocator.getLiveCount() );
    allocator.free( std::move( fresh ) );
    SW_EXPECT_EQUAL( 0u, allocator.getLiveCount() );
}
