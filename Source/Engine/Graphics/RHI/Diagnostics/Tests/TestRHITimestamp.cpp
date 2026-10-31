/**
 * @file TestRHITimestamp.cpp
 * @brief GPU 타임스탬프 해석(`RHITimestamp::resolve`) 테스트입니다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Diagnostics/RHITimestamp.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"

using namespace sw;

/**
 * @brief [RHITimestampTest] 기준점은 가장 이른 틱이고, 안 적힌 칸은 음수다. 기준점은 GPU 시계 나노초로도 남는다
 * @details 외부 프로파일러(Tracy)는 기준점 + 칸 값으로 절대 GPU 시각을 만들어 `readGPUClockNanos` 로 맞춘 CPU 시계 위에 놓는다.
 *          기준점의 환산이 칸 값과 다르면 GPU 구간이 타임라인에서 통째로 밀린다. 마지막 칸은 32 를 넘는 비트 집합을 본다.
 */
SW_TEST_CASE( RHITimestampTest, OriginIsEarliestTickAndUnwrittenSlotsAreNegative )
{
    constexpr uint32 kLastSlot = constant::kMaxGPUTimestampSlot - 1u;
    uint64           arrTick[constant::kMaxGPUTimestampSlot]{};
    arrTick[0]                           = 1000; // 낮은 번호지만 늦은 시각 — 이것을 기준으로 삼으면 슬롯 5 가 음수(→ 0)가 된다
    arrTick[5]                           = 400;
    arrTick[kLastSlot]                   = 1600;
    const GPUTimestampSlotMask readyMask = makeGPUTimestampSlotBit( 0 ) | makeGPUTimestampSlotBit( 5 ) | makeGPUTimestampSlotBit( kLastSlot );

    RHITimestampFrame frame;
    SW_REQUIRE_TRUE( RHITimestamp::resolve( arrTick, readyMask, 500.0, frame ) ); // 틱 하나 = 500 ns = 0.5 us
    const vector<float32>& listMicro = frame._listMicro;
    SW_REQUIRE_EQUAL( size_t( constant::kMaxGPUTimestampSlot ), listMicro.size() );
    SW_EXPECT_EQUAL( int64( 400 * 500 ), frame._originNanos );
    SW_EXPECT_EQUAL( int64( 1000 * 500 ), RHITimestamp::convertTickToNanos( 1000, 500.0 ) );
    SW_EXPECT_NEAR( 300.0f, listMicro[0], 1e-3f );
    SW_EXPECT_NEAR( 0.0f, listMicro[5], 1e-3f );
    SW_EXPECT_NEAR( 600.0f, listMicro[kLastSlot], 1e-3f );
    SW_EXPECT_TRUE( listMicro[1] < 0.0f && listMicro[kLastSlot - 1u] < 0.0f );

    // 준비된 칸이 없으면 비운다 — 호출자는 빈 목록을 "이번 프레임 없음" 으로 읽는다.
    SW_EXPECT_FALSE( RHITimestamp::resolve( arrTick, 0, 500.0, frame ) );
    SW_EXPECT_TRUE( frame._listMicro.empty() );
}
