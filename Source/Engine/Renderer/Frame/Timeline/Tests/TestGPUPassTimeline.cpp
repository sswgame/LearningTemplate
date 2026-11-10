#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Diagnostics/GPUTimestampSlotMask.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Timeline/GPUPassTimeline.h"
#include "Engine/Renderer/Frame/Timeline/GPUTimelineExporter.h"
#include "Engine/Renderer/Frame/Timeline/GPUTimingRow.h"
#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"

using namespace sw;

/** @brief [GPUPassTimelineTest] 칸 배치: 뷰 패스 쌍 · 프레임 패스 쌍 · 예약 셋이 겹치지 않고, 측정 칸 밖의 프레임 패스는 칸이 없다. */
SW_TEST_CASE( GPUPassTimelineTest, SlotLayoutDoesNotOverlap )
{
    const uint32 lastViewSlot = FrameRendererUtil::kGPUTimedPassCapacity * 2u - 1u;
    const uint32 firstFrame   = FrameRendererUtil::findFramePassTimestampSlot( 0 );
    const uint32 lastFrame    = FrameRendererUtil::findFramePassTimestampSlot( FrameRendererUtil::kGPUTimedFramePassCapacity - 1u ) + 1u;
    SW_EXPECT_TRUE( firstFrame > lastViewSlot );
    SW_EXPECT_TRUE( lastFrame < FrameRendererUtil::kGPUTimestampSlotComputeBegin );
    SW_EXPECT_EQUAL( FrameRendererUtil::findFramePassTimestampSlot( 1 ), firstFrame + 2u );
    SW_EXPECT_EQUAL( FrameRendererUtil::findFramePassTimestampSlot( FrameRendererUtil::kGPUTimedFramePassCapacity ), constant::invalid_index::kUint32 );
    SW_EXPECT_TRUE( FrameRendererUtil::kGPUTimestampSlotFrameBegin < constant::render::kMaxGPUTimestampSlot );
}

/** @brief [GPUPassTimelineTest] 슬롯 비트 집합: 64 를 넘는 칸도 서고, 빈 집합은 any 가 거짓이다. */
SW_TEST_CASE( GPUPassTimelineTest, SlotMaskCrossesWords )
{
    GPUTimestampSlotMask mask;
    SW_EXPECT_FALSE( mask.any() );
    mask.set( 100u );
    SW_EXPECT_TRUE( mask.any() );
    SW_EXPECT_TRUE( mask.test( 100u ) );
    SW_EXPECT_FALSE( mask.test( 36u ) ); // 같은 단어 자리(100 - 64)의 첫 단어 비트가 아니다
    SW_EXPECT_FALSE( mask.test( constant::render::kMaxGPUTimestampSlot ) );

    GPUTimestampSlotAtomicMask atomicMask;
    atomicMask.set( 3u );
    atomicMask.set( 127u );
    const GPUTimestampSlotMask loaded = atomicMask.load();
    SW_EXPECT_TRUE( loaded.test( 3u ) && loaded.test( 127u ) );
    atomicMask.clear();
    SW_EXPECT_FALSE( atomicMask.load().any() );
}

/** @brief [GPUPassTimelineTest] 프레임 패스의 칸 쌍이 `GPU.<프레임 패스>` 구간이 된다(gpu.timings · 감시 값 GPU.PassMs 가 읽는다). 기록되지 않은 칸 쌍은 구간이 없다. */
SW_TEST_CASE( GPUPassTimelineTest, ReportsFramePassSpans )
{
    unique_ptr<FrameProfiler> pProfiler = make_unique<FrameProfiler>();
    pProfiler->setEnabled( true );
    GPUPassTimeline timeline;
    timeline.bindProfiler( pProfiler.get() );

    vector<RenderGraphPassDesc> listPass( 1 );
    listPass[0]._name = "Shadow";
    vector<RenderGraphPassDesc> listFramePass( 2 );
    listFramePass[0]._name = "Cull";
    listFramePass[1]._name = "Grid";

    RHITimestampFrame frame;
    frame._listMicro.assign( constant::render::kMaxGPUTimestampSlot, -1.0f );
    frame._listMicro[0]            = 100.0f; // Shadow 100 ~ 400 us
    frame._listMicro[1]            = 400.0f;
    const uint32 cullSlot          = FrameRendererUtil::findFramePassTimestampSlot( 0 );
    frame._listMicro[cullSlot]     = 10.0f; // Cull 10 ~ 60 us
    frame._listMicro[cullSlot + 1] = 60.0f;
    timeline.reportFrame( frame, listPass, listFramePass );
    pProfiler->endFrame();

    float64 milliseconds = 0;
    SW_REQUIRE_TRUE( GPUTimingRow::findMilliseconds( *pProfiler, "Cull", milliseconds ) );
    SW_EXPECT_NEAR( 0.05, milliseconds, 1e-6 );
    SW_REQUIRE_TRUE( GPUTimingRow::findMilliseconds( *pProfiler, "Shadow", milliseconds ) );
    SW_EXPECT_NEAR( 0.3, milliseconds, 1e-6 );
    // Grid 는 칸이 기록되지 않았다 — 구간은 등록됐지만 시간이 0 이다.
    SW_EXPECT_TRUE( GPUTimingRow::findMilliseconds( *pProfiler, "Grid", milliseconds ) == false || milliseconds == 0.0 );
}

namespace
{
    /** @brief GPU 구간 열고 닫기를 깊이와 함께 적어 두는 출력입니다. */
    class RecordingGPUTimeline final : public IProfilerGPUTimeline
    {
    public:
        struct Zone
        {
            const ProfileZoneSite* _pSite;
            uint32                 _depth;
            int64                  _beginNanos;
        };

        vector<Zone> _listZone;
        uint32       _depth{ 0 };

        uint32 createGPUContext( ProfilerGraphicsAPI, const utf8*, int64 ) override { return 0; }
        void   syncGPUClock( uint32, int64 ) override {}
        void   beginGPUZone( uint32, const ProfileZoneSite& site, int64 gpuBeginNanos ) override
        {
            _listZone.push_back( Zone{ &site, _depth, gpuBeginNanos } );
            ++_depth;
        }
        void endGPUZone( uint32, int64 ) override { --_depth; }
    };
} // namespace

/** @brief [GPUPassTimelineTest] Tracy 내보내기: 프레임 패스는 프레임 그래프(Compute) 구간의 자식으로, 뷰 패스는 그 형제로 나간다. */
SW_TEST_CASE( GPUPassTimelineTest, ExportNestsFramePassesUnderCompute )
{
    const ProfileZoneSite frameSite{ "Frame", "", "", 0, 0 };
    const ProfileZoneSite computeSite{ "Compute", "", "", 1, 0 };
    const ProfileZoneSite viewPassSite{ "Opaque", "", "", 2, 0 };
    const ProfileZoneSite framePassSite{ "Culling", "", "", 3, 0 };

    RHITimestampFrame frame;
    frame._listMicro.assign( constant::render::kMaxGPUTimestampSlot, -1.0f );
    frame._listMicro[FrameRendererUtil::kGPUTimestampSlotFrameBegin]   = 0.0f;
    frame._listMicro[FrameRendererUtil::kGPUTimestampSlotComputeBegin] = 1.0f;
    frame._listMicro[FrameRendererUtil::kGPUTimestampSlotComputeEnd]   = 10.0f;
    const uint32 framePassSlot                                         = FrameRendererUtil::findFramePassTimestampSlot( 0 );
    frame._listMicro[framePassSlot]                                    = 2.0f;
    frame._listMicro[framePassSlot + 1u]                               = 5.0f;
    frame._listMicro[0]                                                = 11.0f; // 뷰 패스 0
    frame._listMicro[1]                                                = 20.0f;

    RecordingGPUTimeline backend;
    GPUTimelineExporter  exporter;
    SW_EXPECT_TRUE( exporter.openContext( backend, ProfilerGraphicsAPI::Vulkan, "test", &backend, 0 ) );
    const vector<const ProfileZoneSite*> listPassSite{ &viewPassSite };
    const vector<const ProfileZoneSite*> listFramePassSite{ &framePassSite };
    SW_EXPECT_EQUAL( exporter.exportFrame( backend, frame, frameSite, computeSite, listPassSite, listFramePassSite ), 4u );

    SW_EXPECT_EQUAL( backend._depth, 0u );
    SW_EXPECT_EQUAL( backend._listZone.size(), size_t{ 4 } );
    if ( backend._listZone.size() != 4 )
        return;
    SW_EXPECT_TRUE( backend._listZone[0]._pSite == &frameSite );
    SW_EXPECT_TRUE( backend._listZone[1]._pSite == &computeSite );
    SW_EXPECT_EQUAL( backend._listZone[1]._depth, 1u );
    SW_EXPECT_TRUE( backend._listZone[2]._pSite == &framePassSite );
    SW_EXPECT_EQUAL( backend._listZone[2]._depth, 2u );
    SW_EXPECT_TRUE( backend._listZone[3]._pSite == &viewPassSite );
    SW_EXPECT_EQUAL( backend._listZone[3]._depth, 1u );
}
