// 프레임 링(`FrameResourceRing` · `RHITimestampRing`) 범위 검사 — 링 칸 수를 넘는 칸 번호가 개발 구성에서 단언으로 잡히고, 경계 안은 잡히지 않는지(음성 대조).
#include "pch.h"

#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Backend/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Backend/RHITimestampRing.h"

using namespace sw;

/**
 * @brief [RHIRingBoundsTest] FrameResourceRing — 칸 밖 펜스 읽기 · 쓰기는 단언하고(배포는 가드가 0 · 무시) 링을 바꾸지 않는다
 */
SW_TEST_CASE( RHIRingBoundsTest, FrameResourceRingSlotIsChecked )
{
    FrameResourceRing ring;
    constexpr uint32  kLastSlot = constant::render::kMaxFrameCountInFlight - 1;
    {
        const test::ScopedAssertCapture assertCapture;
        ring.setFenceValue( kLastSlot, 5 );
        SW_EXPECT_EQUAL( uint64{ 5 }, ring.getFenceValue( kLastSlot ) );
        SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
    }
    {
        const test::ScopedAssertCapture assertCapture;
        ring.setFenceValue( kLastSlot + 1, 9 );
        SW_EXPECT_EQUAL( uint64{ 0 }, ring.getFenceValue( kLastSlot + 1 ) );
        if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_EQUAL( 2u, assertCapture.getCount() );
    }
    SW_EXPECT_EQUAL( uint64{ 5 }, ring.getFenceValue( kLastSlot ) );
}

/**
 * @brief [RHIRingBoundsTest] RHITimestampRing — 칸 밖 마스크 읽기는 단언한다(주소만 만든다)
 */
SW_TEST_CASE( RHIRingBoundsTest, TimestampRingSlotIsChecked )
{
    RHITimestampRing ring;
    {
        const test::ScopedAssertCapture assertCapture;
        (void)&ring.getSlotMask( RHITimestampRing::kSlotCount - 1 );
        SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
    }
    if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
    {
        const test::ScopedAssertCapture assertCapture;
        (void)&ring.getSlotMask( RHITimestampRing::kSlotCount );
        SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
    }
}
