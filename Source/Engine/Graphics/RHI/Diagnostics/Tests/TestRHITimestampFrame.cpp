/**
 * @file TestRHITimestamp.cpp
 * @brief GPU 타임스탬프 해석(`RHITimestampFrame::resolve`) 테스트입니다.
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/RHI/Diagnostics/GPUTimestampSlotMask.h"
#include "Engine/Graphics/RHI/Diagnostics/RHITimestampFrame.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"

using namespace sw;

/**
 * @brief [RHITimestampTest] 기준점은 가장 이른 틱이고, 안 적힌 칸은 음수다. 기준점은 GPU 시계 나노초로도 남는다
 * @details 외부 프로파일러(Tracy)는 기준점 + 칸 값으로 절대 GPU 시각을 만들어 `readGPUClockNanos` 로 맞춘 CPU 시계 위에 놓는다.
 *          기준점의 환산이 칸 값과 다르면 GPU 구간이 타임라인에서 통째로 밀린다. 마지막 칸은 32 를 넘는 비트 집합을 본다.
 */
SW_TEST_CASE( RHITimestampTest, OriginIsEarliestTickAndUnwrittenSlotsAreNegative )
{
    constexpr uint32 kLastSlot = constant::render::kMaxGPUTimestampSlot - 1u;
    uint64           arrTick[constant::render::kMaxGPUTimestampSlot]{};
    arrTick[0]         = 1000; // 낮은 번호지만 늦은 시각 — 이것을 기준으로 삼으면 슬롯 5 가 음수(→ 0)가 된다
    arrTick[5]         = 400;
    arrTick[kLastSlot] = 1600;
    GPUTimestampSlotMask readyMask;
    readyMask.set( 0 );
    readyMask.set( 5 );
    readyMask.set( kLastSlot ); // 64 를 넘는 칸 — 둘째 단어의 비트다

    RHITimestampFrame frame;
    SW_REQUIRE_TRUE( RHITimestampFrame::resolve( arrTick, readyMask, GPUTimestampSlotMask{}, 500.0, frame ) ); // 틱 하나 = 500 ns = 0.5 us
    const vector<float32>& listMicro = frame._listMicro;
    SW_REQUIRE_EQUAL( size_t( constant::render::kMaxGPUTimestampSlot ), listMicro.size() );
    SW_EXPECT_EQUAL( int64( 400 * 500 ), frame._originNanos );
    SW_EXPECT_EQUAL( int64( 1000 * 500 ), RHITimestampFrame::convertTickToNanos( 1000, 500.0 ) );
    SW_EXPECT_NEAR( 300.0f, listMicro[0], 1e-3f );
    SW_EXPECT_NEAR( 0.0f, listMicro[5], 1e-3f );
    SW_EXPECT_NEAR( 600.0f, listMicro[kLastSlot], 1e-3f );
    SW_EXPECT_TRUE( listMicro[1] < 0.0f && listMicro[kLastSlot - 1u] < 0.0f );

    // 준비된 칸이 없으면 비운다 — 호출자는 빈 목록을 "이번 프레임 없음" 으로 읽는다.
    SW_EXPECT_FALSE( RHITimestampFrame::resolve( arrTick, GPUTimestampSlotMask{}, GPUTimestampSlotMask{}, 500.0, frame ) );
    SW_EXPECT_TRUE( frame._listMicro.empty() );
}

/**
 * @brief [RHITimestampTest] 컴퓨트 큐 시계 — 같은 카운터면 표본 하나가 튀어도 0, 다른 카운터면 그래픽스 큐 시계로 옮기는 값을 준다
 * @details DX12 는 시계를 큐마다 묻는다. 예전에는 표본 하나로 대조해 어긋나면 그 실행 내내 컴퓨트 큐 패스를 재지 않았다.
 */
SW_TEST_CASE( RHITimestampTest, ComputeQueueClockOffsetSurvivesOneBadSample )
{
    constexpr uint64  kGPUFrequency    = 100000000u; // 틱 하나 = 10 ns
    constexpr uint64  kCPUFrequency    = 10000000u;  // 틱 하나 = 100 ns
    constexpr float64 kToleranceMicros = 100.0;
    // 두 큐의 카운터가 같다: CPU 20 틱(2 us) 뒤의 컴퓨트 표본은 GPU 200 틱 뒤다.
    vector<RHIQueueClockSample> listSame( 5, RHIQueueClockSample{ 1000000, 5000, 1000200, 5020 } );
    listSame[2]._computeCPUTick += 50000; // GPU 틱과 CPU 틱을 읽는 사이에 5 ms 선점됐다
    SW_EXPECT_EQUAL( int64{ 0 }, RHITimestampFrame::computeComputeQueueTickOffset( listSame, kGPUFrequency, kCPUFrequency, kToleranceMicros ) );

    // 컴퓨트 큐의 카운터가 1 초(1e8 틱) 앞선다 — 옮긴 틱은 그래픽스 큐의 1000200 이다.
    vector<RHIQueueClockSample> listApart( 5, RHIQueueClockSample{ 1000000, 5000, 1000200 + kGPUFrequency, 5020 } );
    listApart[0]._computeCPUTick += 50000;
    const int64 offset = RHITimestampFrame::computeComputeQueueTickOffset( listApart, kGPUFrequency, kCPUFrequency, kToleranceMicros );
    SW_EXPECT_EQUAL( -static_cast<int64>( kGPUFrequency ), offset );
    SW_EXPECT_EQUAL( uint64{ 1000200 }, 1000200u + kGPUFrequency + static_cast<uint64>( offset ) );
    // 반대로 뒤처진 카운터도 같다(부호).
    const vector<RHIQueueClockSample> listBehind( 3, RHIQueueClockSample{ 1000000 + kGPUFrequency, 5000, 1000200, 5020 } );
    SW_EXPECT_EQUAL( static_cast<int64>( kGPUFrequency ), RHITimestampFrame::computeComputeQueueTickOffset( listBehind, kGPUFrequency, kCPUFrequency, kToleranceMicros ) );
}
