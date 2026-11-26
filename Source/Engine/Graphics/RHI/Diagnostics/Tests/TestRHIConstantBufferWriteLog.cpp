/**
 * @file TestRHIConstantBufferWriteLog.cpp
 * @brief 상수 버퍼 쓰기 기록(`RHIConstantBufferWriteLog`) 테스트입니다.
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIConstantBufferWriteLog.h"

using namespace sw;

/**
 * @brief [RHIConstantBufferWriteLogTest] 한 프레임에 같은 구간의 두 번째 쓰기는 거절로 답하고, 다른 버퍼 · 다른 구간과 다음 프레임의 쓰기는 받는다(R-12).
 * @details DX12 · Vulkan · GL 은 두 번째 쓰기가 같은 프레임 슬롯을 덮어 앞 드로우도 마지막 값을 읽는다 — 예전엔 아무도 알리지 않았다.
 */
SW_TEST_CASE( RHIConstantBufferWriteLogTest, SecondWriteInAFrameIsRejected )
{
    RHIConstantBufferWriteLog log;
    constexpr RHIBufferHandle kBufferA{ 0x100000001ull };
    constexpr RHIBufferHandle kBufferB{ 0x100000002ull };
    log.beginFrame();
    SW_EXPECT_TRUE( log.recordWrite( kBufferA, 0 ) );
    SW_EXPECT_TRUE( log.recordWrite( kBufferB, 0 ) );
    SW_EXPECT_FALSE( log.recordWrite( kBufferA, 0 ) );
    SW_EXPECT_TRUE( log.recordWrite( kBufferA, 256 ) ); // 같은 페이지의 다른 구간
    SW_EXPECT_FALSE( log.recordWrite( kBufferA, 256 ) );

    log.beginFrame();
    SW_EXPECT_TRUE( log.recordWrite( kBufferA, 0 ) );
    SW_EXPECT_FALSE( log.recordWrite( kBufferA, 0 ) );
}
