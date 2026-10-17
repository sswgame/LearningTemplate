#include "pch.h"

#include "Core/Container/SpanAllocator.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [SpanAllocatorTest] 구간을 앞에서부터 내주고, 돌려받으면 이웃과 합쳐 다시 큰 구간이 된다
 * @details 합치지 않으면 반납이 쌓일수록 빈 구간이 잘게 쪼개져, 빈 용량이 넉넉한데도 큰 메시가 들어갈 자리가 없어진다.
 */
SW_TEST_CASE( SpanAllocatorTest, FreeCoalescesNeighbours )
{
    sw::SpanAllocator allocator( 100u );
    const uint32      offsetA = allocator.allocate( 10u );
    const uint32      offsetB = allocator.allocate( 20u );
    const uint32      offsetC = allocator.allocate( 30u );
    SW_EXPECT_EQUAL( 0u, offsetA );
    SW_EXPECT_EQUAL( 10u, offsetB );
    SW_EXPECT_EQUAL( 30u, offsetC );
    SW_EXPECT_EQUAL( 60u, allocator.getAllocatedCount() );
    SW_EXPECT_EQUAL( 60u, allocator.getUsedEnd() );

    // 가운데를 돌려받으면 빈 구간이 둘(가운데, 꼬리)이다.
    allocator.free( offsetB, 20u );
    SW_EXPECT_EQUAL( 2u, allocator.getFreeSpanCount() );
    // 앞을 돌려받으면 가운데와 합쳐진다.
    allocator.free( offsetA, 10u );
    SW_EXPECT_EQUAL( 2u, allocator.getFreeSpanCount() );
    SW_EXPECT_EQUAL( 40u, allocator.getLargestFreeCount() );
    // 마지막을 돌려받으면 앞 · 꼬리와 모두 합쳐 처음처럼 하나가 된다.
    allocator.free( offsetC, 30u );
    SW_EXPECT_EQUAL( 1u, allocator.getFreeSpanCount() );
    SW_EXPECT_EQUAL( 100u, allocator.getLargestFreeCount() );
    SW_EXPECT_EQUAL( 0u, allocator.getAllocatedCount() );
    SW_EXPECT_EQUAL( 0u, allocator.getUsedEnd() );
}

/**
 * @brief [SpanAllocatorTest] 할당은 가장 잘 맞는 빈 구간을 고른다 — 큰 구간을 작은 요청에 쪼개 쓰지 않는다
 */
SW_TEST_CASE( SpanAllocatorTest, AllocatePicksBestFit )
{
    sw::SpanAllocator allocator( 100u );
    const uint32      offsetA = allocator.allocate( 30u ); // [0, 30)
    const uint32      offsetB = allocator.allocate( 10u ); // [30, 40)
    const uint32      offsetC = allocator.allocate( 5u );  // [40, 45)
    SW_EXPECT_EQUAL( 0u, offsetA );
    SW_EXPECT_EQUAL( 45u, allocator.getUsedEnd() );
    allocator.free( offsetA, 30u ); // 빈 구간: [0, 30), [45, 100)
    allocator.free( offsetC, 5u );  // 빈 구간: [0, 30), [40, 100)
    (void)offsetB;

    // 25 짜리는 60 짜리 꼬리가 아니라 30 짜리 앞 구간에 들어간다.
    SW_EXPECT_EQUAL( 0u, allocator.allocate( 25u ) );
    // 크기가 꼭 맞는 구간이 있으면 그것을 쓴다 — 앞 구간에 남은 [25, 30).
    SW_EXPECT_EQUAL( 25u, allocator.allocate( 5u ) );
    // 남은 것보다 큰 요청은 실패한다.
    SW_EXPECT_EQUAL( sw::SpanAllocator::kInvalidOffset, allocator.allocate( 61u ) );
    SW_EXPECT_EQUAL( sw::SpanAllocator::kInvalidOffset, allocator.allocate( 0u ) );
}

/**
 * @brief [SpanAllocatorTest] 용량을 늘리면 꼬리 빈 구간이 늘어나고, 꼬리가 차 있으면 새 빈 구간이 생긴다
 */
SW_TEST_CASE( SpanAllocatorTest, GrowExtendsTail )
{
    sw::SpanAllocator allocator( 10u );
    SW_EXPECT_EQUAL( 0u, allocator.allocate( 10u ) );
    SW_EXPECT_EQUAL( sw::SpanAllocator::kInvalidOffset, allocator.allocate( 1u ) );
    allocator.grow( 30u );
    SW_EXPECT_EQUAL( 30u, allocator.getCapacity() );
    SW_EXPECT_EQUAL( 10u, allocator.allocate( 15u ) );
    allocator.grow( 40u ); // 꼬리 [25, 30) 가 [25, 40) 로 늘어난다
    SW_EXPECT_EQUAL( 1u, allocator.getFreeSpanCount() );
    SW_EXPECT_EQUAL( 15u, allocator.getLargestFreeCount() );
    allocator.grow( 20u ); // 줄이지 않는다
    SW_EXPECT_EQUAL( 40u, allocator.getCapacity() );
}

/**
 * @brief [SpanAllocatorTest] 미뤄 돌려받은 구간은 그 번호가 지나기 전에는 다시 나가지 않는다
 * @details 정점 풀은 업로드 힙 정점 버퍼에 CPU 가 직접 쓴다. 방금 내린 메시의 구간을 곧바로 새 메시에 주면, 아직 끝나지 않은 앞 프레임이
 *          그 자리를 읽는 동안 덮어써 한두 프레임 동안 엉뚱한 모양이 보인다.
 */
SW_TEST_CASE( SpanAllocatorTest, DeferredFreeWaitsForStamp )
{
    sw::SpanAllocator allocator( 20u );
    const uint32      offsetA = allocator.allocate( 10u );
    SW_EXPECT_EQUAL( 10u, allocator.allocate( 10u ) );
    allocator.freeDeferred( offsetA, 10u, 5u );
    SW_EXPECT_EQUAL( 1u, allocator.getDeferredSpanCount() );
    SW_EXPECT_EQUAL( 20u, allocator.getAllocatedCount() );
    SW_EXPECT_EQUAL( sw::SpanAllocator::kInvalidOffset, allocator.allocate( 10u ) );

    allocator.retireDeferred( 4u );
    SW_EXPECT_EQUAL( sw::SpanAllocator::kInvalidOffset, allocator.allocate( 10u ) );
    allocator.retireDeferred( 5u );
    SW_EXPECT_EQUAL( 0u, allocator.getDeferredSpanCount() );
    SW_EXPECT_EQUAL( 0u, allocator.allocate( 10u ) );

    // reset 은 대기열도 버린다.
    allocator.freeDeferred( 0u, 10u, 9u );
    allocator.reset( 20u );
    SW_EXPECT_EQUAL( 0u, allocator.getDeferredSpanCount() );
    SW_EXPECT_EQUAL( 0u, allocator.getAllocatedCount() );
}

/**
 * @brief [SpanAllocatorTest] 이미 빈 구간을 다시 돌려받으면 거절한다
 * @details 받으면 빈 목록에 같은 자리가 두 번 들어가 다음 두 할당이 그 자리를 서로 다른 메시에 준다. Debug 에서는 단언이 멈추므로
 *          단언 가로채기 안에서 부른다.
 */
SW_TEST_CASE( SpanAllocatorTest, DoubleFreeIsRejected )
{
    sw::SpanAllocator allocator( 20u );
    const uint32      offsetA = allocator.allocate( 10u );
    allocator.free( offsetA, 10u );
    {
        test::ScopedAssertCapture assertCapture;
        allocator.free( offsetA, 10u );
        allocator.free( 15u, 10u ); // 용량 밖으로 넘친다
        if ( test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_EQUAL( 2u, assertCapture.getCount() );
    }
    SW_EXPECT_EQUAL( 1u, allocator.getFreeSpanCount() );
    SW_EXPECT_EQUAL( 0u, allocator.getAllocatedCount() );
    SW_EXPECT_EQUAL( 20u, allocator.getLargestFreeCount() );
}
