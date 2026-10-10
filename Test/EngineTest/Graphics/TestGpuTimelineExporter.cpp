#include "pch.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/RHI/Support/RHIGpuTimestamp.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/GpuTimelineExporter.h"

#include "EngineTest/ProfilerTestUtil.h"

#include "TestFramework/TestFramework.h"

// GpuTimelineExporter — 엔진이 읽은 타임스탬프 한 프레임을 외부 프로파일러(Tracy) GPU 타임라인 트리로.

namespace
{
    /** @brief 시험용 지점입니다(문자열은 리터럴 — 기록용 출력은 이름만 읽는다). */
    sw::ProfileZoneSite makeSiteInternal( const utf8* pName )
    {
        return sw::ProfileZoneSite{ pName, "test", "Test/EngineTest", 0u, 0u };
    }

    /** @brief 모든 칸이 "안 적힘" 인 프레임입니다. */
    sw::RHIGpuTimestampFrame makeEmptyFrameInternal( int64 originNanos )
    {
        sw::RHIGpuTimestampFrame frame;
        frame._originNanos = originNanos;
        frame._listMicro.assign( sw::constant::kMaxGpuTimestampSlot, -1.0f );
        return frame;
    }
} // namespace

/**
 * @brief [GpuTimelineExporterTest] 한 프레임은 프레임 ⊃ (컴퓨트 · 패스들) 트리로, 시작 순으로 나가고 안 적힌 패스는 빠진다
 * @details 패스 번호 순이 시간 순이 아니다(컴퓨트 프리패스가 뒤쪽 칸이지만 먼저 돈다). 뷰어는 같은 스레드 안의 열고 닫기 순서로 트리를 만드므로
 *          순서가 틀리면 형제가 자식으로 읽힌다.
 */
SW_TEST_CASE( GpuTimelineExporterTest, ExportsTreeInTimeOrderAndSkipsUnwrittenPasses )
{
    test::RecordingProfilerBackend backend;
    sw::GpuTimelineExporter        exporter;
    int32                          deviceIdentity{ 0 };
    SW_ASSERT_TRUE( exporter.openContext( backend, sw::ProfilerGraphicsAPI::Direct3D12, "DX12", &deviceIdentity, 500 ) );
    SW_EXPECT_TRUE( exporter.isContextOpenFor( &deviceIdentity, &backend ) );

    sw::RHIGpuTimestampFrame frame                                         = makeEmptyFrameInternal( 1'000'000 );
    frame._listMicro[sw::FrameRendererUtil::kGpuTimestampSlotFrameBegin]   = 0.0f;
    frame._listMicro[sw::FrameRendererUtil::kGpuTimestampSlotComputeBegin] = 1.0f;
    frame._listMicro[sw::FrameRendererUtil::kGpuTimestampSlotComputeEnd]   = 5.0f;
    frame._listMicro[0]                                                    = 10.0f; // 패스 0 (A)
    frame._listMicro[1]                                                    = 20.0f;
    frame._listMicro[2]                                                    = 6.0f; // 패스 1 (B) — A 보다 먼저 돈다
    frame._listMicro[3]                                                    = 9.0f;
    frame._listMicro[4]                                                    = 30.0f; // 패스 2 (C) — 끝 칸이 안 적혔다

    const sw::ProfileZoneSite                    frameSite   = makeSiteInternal( "Frame" );
    const sw::ProfileZoneSite                    computeSite = makeSiteInternal( "Compute" );
    const sw::ProfileZoneSite                    siteA       = makeSiteInternal( "A" );
    const sw::ProfileZoneSite                    siteB       = makeSiteInternal( "B" );
    const sw::ProfileZoneSite                    siteC       = makeSiteInternal( "C" );
    const sw::vector<const sw::ProfileZoneSite*> listPassSite{ &siteA, &siteB, &siteC };

    SW_EXPECT_EQUAL( 4u, exporter.exportFrame( backend, frame, frameSite, computeSite, listPassSite ) );

    const sw::vector<sw::string> listExpected{
        "context DX12@500",
        "+Frame@1000000",
        "+Compute@1001000",
        "-@1005000",
        "+B@1006000",
        "-@1009000",
        "+A@1010000",
        "-@1020000",
        "-@1020000",
    };
    const sw::vector<sw::string> listEvent = backend.copyEvents();
    SW_ASSERT_EQUAL( listExpected.size(), listEvent.size() );
    for ( size_t index = 0; index < listExpected.size(); ++index )
    {
        SW_EXPECT_TRUE_MSG( listEvent[index] == listExpected[index],
                            ( sw::string( "event " ) + sw::to_string( index ) + ": " + listEvent[index] + " != " + listExpected[index] ).c_str() );
    }
}

/**
 * @brief [GpuTimelineExporterTest] 겹치는 형제는 앞 형제의 끝에서 자른다 — 뷰어가 겹침을 중첩으로 읽지 않게
 */
SW_TEST_CASE( GpuTimelineExporterTest, OverlappingSiblingsAreClippedToPreviousEnd )
{
    test::RecordingProfilerBackend backend;
    sw::GpuTimelineExporter        exporter;
    int32                          deviceIdentity{ 0 };
    SW_ASSERT_TRUE( exporter.openContext( backend, sw::ProfilerGraphicsAPI::Vulkan, "Vulkan", &deviceIdentity, 0 ) );

    sw::RHIGpuTimestampFrame frame = makeEmptyFrameInternal( 0 );
    frame._listMicro[0]            = 0.0f; // A: 0 ~ 10 us
    frame._listMicro[1]            = 10.0f;
    frame._listMicro[2]            = 8.0f; // B: 8 ~ 12 us — A 와 2 us 겹친다
    frame._listMicro[3]            = 12.0f;

    const sw::ProfileZoneSite                    frameSite   = makeSiteInternal( "Frame" );
    const sw::ProfileZoneSite                    computeSite = makeSiteInternal( "Compute" );
    const sw::ProfileZoneSite                    siteA       = makeSiteInternal( "A" );
    const sw::ProfileZoneSite                    siteB       = makeSiteInternal( "B" );
    const sw::vector<const sw::ProfileZoneSite*> listPassSite{ &siteA, &siteB };
    SW_EXPECT_EQUAL( 3u, exporter.exportFrame( backend, frame, frameSite, computeSite, listPassSite ) );

    // 프레임 시작 칸이 없으면 가장 이른 시작이 프레임 시작이다.
    SW_EXPECT_EQUAL( 1u, backend.countEvent( "+Frame@0" ) );
    SW_EXPECT_EQUAL( 1u, backend.countEvent( "+B@10000" ) );
    SW_EXPECT_EQUAL( 2u, backend.countEvent( "-@12000" ) ); // B 의 끝과 프레임의 끝
}

/**
 * @brief [GpuTimelineExporterTest] 컨텍스트가 없거나 디바이스 · 출력이 바뀌면 내지 않고, 시계는 정해진 프레임마다 다시 맞춘다
 */
SW_TEST_CASE( GpuTimelineExporterTest, ContextFollowsDeviceAndResyncsPeriodically )
{
    test::RecordingProfilerBackend backend;
    sw::GpuTimelineExporter        exporter;
    const sw::ProfileZoneSite      frameSite                               = makeSiteInternal( "Frame" );
    const sw::ProfileZoneSite      computeSite                             = makeSiteInternal( "Compute" );
    sw::RHIGpuTimestampFrame       frame                                   = makeEmptyFrameInternal( 0 );
    frame._listMicro[sw::FrameRendererUtil::kGpuTimestampSlotComputeBegin] = 1.0f;
    frame._listMicro[sw::FrameRendererUtil::kGpuTimestampSlotComputeEnd]   = 2.0f;
    const sw::vector<const sw::ProfileZoneSite*> listNoPass;

    // 열기 전에는 아무것도 내지 않는다.
    SW_EXPECT_EQUAL( 0u, exporter.exportFrame( backend, frame, frameSite, computeSite, listNoPass ) );

    int32 firstDevice{ 0 };
    int32 secondDevice{ 0 };
    SW_ASSERT_TRUE( exporter.openContext( backend, sw::ProfilerGraphicsAPI::OpenGl, "GL", &firstDevice, 0 ) );
    SW_EXPECT_FALSE( exporter.isContextOpenFor( &secondDevice, &backend ) );
    test::RecordingProfilerBackend otherBackend;
    SW_EXPECT_FALSE( exporter.isContextOpenFor( &firstDevice, &otherBackend ) );

    uint32 resyncDueCount{ 0 };
    for ( uint32 frameIndex = 0; frameIndex < sw::GpuTimelineExporter::kResyncFrameInterval * 2; ++frameIndex )
    {
        if ( exporter.advanceAndCheckResync() )
        {
            ++resyncDueCount;
            exporter.resyncClock( backend, 7 );
        }
    }
    SW_EXPECT_EQUAL( 2u, resyncDueCount );
    SW_EXPECT_EQUAL( 2u, backend.countEvent( "sync@7" ) );

    exporter.forgetContext();
    SW_EXPECT_FALSE( exporter.isContextOpenFor( &firstDevice, &backend ) );
    SW_EXPECT_EQUAL( 0u, exporter.exportFrame( backend, frame, frameSite, computeSite, listNoPass ) );
}
