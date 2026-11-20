/**
 * @file TestRHIFence.cpp
 * @brief 렌더러 쪽 펜스(`RHIFence`)가 디바이스의 GPU 완료 알림으로만 완료 값을 올리는가 — 프레임 수를 세어 짐작하지 않는다.
 * @details 가짜 디바이스(`test::FakeRHIDevice`)는 `enqueueGPURelease` 를 부르지 않고 쌓는다. 테스트가 쌓인 콜백을 불러 "GPU 가 그 프레임을 끝냈다" 를 흉내 낸다.
 */
#include "pch.h"

#include "Core/Container/SpanAllocator.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHIFence.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

/**
 * @brief [RHIFenceTest] 낸 값은 1 부터 오르고, 완료 값은 디바이스가 그 프레임의 해제를 부른 뒤에만 오른다(몇 번을 더 내도 저절로 오르지 않는다)
 * @details 변이: `signal` 이 완료 값을 바로 올리거나 "낸 값 - 상수" 로 짐작하면 첫 단언 묶음이 진다.
 */
SW_TEST_CASE( RHIFenceTest, CompletesOnlyWhenTheDeviceReportsTheFrameDone )
{
    test::FakeRHIDevice device;
    RHIFence            fence;
    SW_EXPECT_EQUAL( static_cast<uint64>( 1 ), fence.signal( device ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 2 ), fence.signal( device ) );
    for ( uint32 index = 0; index < 8; ++index )
    {
        (void)fence.signal( device );
    }
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), fence.getCompletedValue() );
    SW_REQUIRE_EQUAL( device._listGPURelease.size(), static_cast<size_t>( 10 ) );

    device._listGPURelease[0]();
    SW_EXPECT_EQUAL( static_cast<uint64>( 1 ), fence.getCompletedValue() );
    device._listGPURelease[2]();
    device._listGPURelease[1](); // 늦게 온 앞 값의 알림이 완료 값을 되돌리지 않는다
    SW_EXPECT_EQUAL( static_cast<uint64>( 3 ), fence.getCompletedValue() );
}

/**
 * @brief [RHIFenceTest] 펜스 값을 키로 미룬 구간은 그 값이 끝나기 전에는 다시 나가지 않고, 끝나면 다시 나간다
 * @details 변이: 대기 키를 펜스 값 대신 "부른 수" 로 두고 매번 `retireDeferred( 부른 수 )` 하면 둘째 할당이 반납한 자리를 받아 진다.
 */
SW_TEST_CASE( RHIFenceTest, DeferredSpanIsReusedOnlyAfterItsFenceCompletes )
{
    test::FakeRHIDevice device;
    RHIFence            fence;
    SpanAllocator       allocator{ 8 };
    const uint32        first = allocator.allocate( 4 );
    SW_REQUIRE_EQUAL( 0u, first );

    const uint64 retireStamp = fence.signal( device );
    allocator.freeDeferred( first, 4, retireStamp );
    for ( uint32 frame = 0; frame < 6; ++frame ) // 프레임이 여럿 지나도 GPU 가 알리지 않으면 그대로다
    {
        (void)fence.signal( device );
        allocator.retireDeferred( fence.getCompletedValue() );
    }
    SW_EXPECT_EQUAL( 4u, allocator.allocate( 4 ) );                            // 남은 빈 자리
    SW_EXPECT_EQUAL( SpanAllocator::kInvalidOffset, allocator.allocate( 4 ) ); // 반납한 자리는 아직 대기 중이다

    device._listGPURelease[0]();
    allocator.retireDeferred( fence.getCompletedValue() );
    SW_EXPECT_EQUAL( 0u, allocator.allocate( 4 ) );
}
