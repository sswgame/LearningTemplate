#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/Timeline/GPUTimingRow.h"

using namespace sw;

/**
 * @brief [GPUTimingRowTest] GPU 구간만 모아 앞머리를 떼고 느린 순서로 늘어놓으며, 나노초를 밀리초로 바꾼다(카운터 · CPU 구간은 빠진다)
 */
SW_TEST_CASE( GPUTimingRowTest, CollectsGPUScopesSlowestFirst )
{
    unique_ptr<FrameProfiler> pProfiler = make_unique<FrameProfiler>();
    pProfiler->setEnabled( true );
    const uint32 slotLighting = pProfiler->registerScope( "GPU.Lighting" );
    const uint32 slotFrame    = pProfiler->registerScope( "GPU.Frame" );
    const uint32 slotCPU      = pProfiler->registerScope( "RT.Frame" );
    const uint32 slotCounter  = pProfiler->registerScope( "GPU.Draws" );
    pProfiler->addSample( slotLighting, 1'500'000 );
    pProfiler->addSample( slotFrame, 4'000'000 );
    pProfiler->addSample( slotCPU, 9'000'000 );
    pProfiler->addCount( slotCounter, 12 );
    pProfiler->endFrame();

    const vector<GPUTimingRow> listRow = GPUTimingRow::collect( *pProfiler );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listRow.size() );
    SW_EXPECT_TRUE( listRow[0]._name == "Frame" );
    SW_EXPECT_NEAR( 4.0, listRow[0]._milliseconds, 1e-9 );
    SW_EXPECT_TRUE( listRow[1]._name == "Lighting" );
    SW_EXPECT_NEAR( 1.5, listRow[1]._milliseconds, 1e-9 );

    float64 milliseconds = 0;
    SW_EXPECT_TRUE( GPUTimingRow::findMilliseconds( *pProfiler, "Lighting", milliseconds ) );
    SW_EXPECT_NEAR( 1.5, milliseconds, 1e-9 );
    SW_EXPECT_FALSE( GPUTimingRow::findMilliseconds( *pProfiler, "Light", milliseconds ) ); // 이름 전체가 같아야 한다
    SW_EXPECT_FALSE( GPUTimingRow::findMilliseconds( *pProfiler, "Draws", milliseconds ) ); // 카운터는 시간이 아니다
}
