#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Utility/Debug/FrameProfiler.h"
#include "Engine/Utility/Profiling/ProfilerBackend.h"
#include "Engine/Utility/Profiling/Tracy/TracyProfilerBackend.h"

#include "EngineTest/ProfilerTestUtil.h"

#include "TestFramework/TestFramework.h"

// ProfilerBackend — 계측 한 줄(SW_PROFILE_SCOPE)이 엔진 표와 외부 프로파일러(Tracy)에 함께 남는 경로. Shipping 에는 외부 출력이 없다.

#if SW_PROFILER_BACKEND_COMPILED
namespace
{
    /**
     * @brief 시험 동안 활성 출력을 바꾸고, 끝나면 원래 것으로 되돌립니다.
     */
    struct ScopedActiveBackendInternal
    {
        explicit ScopedActiveBackendInternal( sw::IProfilerBackend* pBackend )
            : _pPrevious{ sw::ProfilerBackend::getActiveBackend() }
        {
            sw::ProfilerBackend::setActiveBackend( pBackend );
        }

        ~ScopedActiveBackendInternal() { sw::ProfilerBackend::setActiveBackend( _pPrevious ); }

        ScopedActiveBackendInternal( const ScopedActiveBackendInternal& )            = delete;
        ScopedActiveBackendInternal& operator=( const ScopedActiveBackendInternal& ) = delete;

        sw::IProfilerBackend* _pPrevious;
    };

    /** @brief 시험용 계측 지점 하나입니다. 매크로를 함수 안에 두어 지점이 한 번만 등록되게 합니다. */
    void runProbeScopeInternal()
    {
        SW_PROFILE_SCOPE( "ProfilerBackendTest.Probe" );
    }
} // namespace

/**
 * @brief [ProfilerBackendTest] 활성 출력이 있으면 계측 지점이 그 출력에 구간을 열고 닫는다
 */
SW_TEST_CASE( ProfilerBackendTest, ScopeOpensAndClosesZoneOnActiveBackend )
{
    test::RecordingProfilerBackend backend;
    {
        ScopedActiveBackendInternal active{ &backend };
        runProbeScopeInternal();
    }

    const sw::vector<sw::string> listEvent = backend.copyEvents();
    size_t                       beginIndex{ listEvent.size() };
    for ( size_t index = 0; index < listEvent.size(); ++index )
    {
        if ( listEvent[index] == "begin ProfilerBackendTest.Probe" )
        {
            beginIndex = index;
            break;
        }
    }
    SW_ASSERT_TRUE_MSG( beginIndex < listEvent.size(), "the scope did not open a zone on the active backend" );
    SW_EXPECT_EQUAL( 1u, backend.countEvent( "end ProfilerBackendTest.Probe" ) );
}

/**
 * @brief [ProfilerBackendTest] 구간은 연 출력으로 닫는다 — 열린 동안 활성 출력이 바뀌어도 짝이 어긋나지 않는다
 * @details 에디터의 "Tracy 열기" 는 프레임 도중에 출력을 켠다. 닫을 때 그 순간의 활성 출력에 닫으면, 열지 않은 구간의 끝이 Tracy 로 간다.
 */
SW_TEST_CASE( ProfilerBackendTest, ZoneClosesOnTheBackendThatOpenedIt )
{
    test::RecordingProfilerBackend opener;
    test::RecordingProfilerBackend later;
    {
        ScopedActiveBackendInternal active{ &opener };
        SW_PROFILE_SCOPE( "ProfilerBackendTest.Switch" );
        sw::ProfilerBackend::setActiveBackend( &later );
    }
    SW_EXPECT_EQUAL( 1u, opener.countEvent( "begin ProfilerBackendTest.Switch" ) );
    SW_EXPECT_EQUAL( 1u, opener.countEvent( "end ProfilerBackendTest.Switch" ) );
    SW_EXPECT_EQUAL( 0u, later.countEvent( "end ProfilerBackendTest.Switch" ) );
}

/**
 * @brief [ProfilerBackendTest] 활성 출력이 없으면 아무 출력도 부르지 않는다
 */
SW_TEST_CASE( ProfilerBackendTest, NoActiveBackendLeavesNoTrace )
{
    test::RecordingProfilerBackend backend;
    ScopedActiveBackendInternal    active{ nullptr };
    runProbeScopeInternal();
    SW_EXPECT_TRUE( backend.copyEvents().empty() );
}

/**
 * @brief [ProfilerBackendTest] 계측 지점은 사본이고 같은 (이름 · 파일 · 줄)이면 같은 주소다
 * @details 출력(Tracy)은 지점 주소를 프로세스 끝까지 들고 문자열을 나중에 읽는다. 부른 쪽 버퍼(핫 리로드로 내려간 모듈의 상수)를
 *          가리키면 그때 사라진 메모리를 읽는다. 파일 경로는 `Source/` 부터만 남긴다.
 */
SW_TEST_CASE( ProfilerBackendTest, ZoneSiteIsCopiedAndDeduplicated )
{
    utf8                       arrName[] = "ProfilerBackendTest.Site";
    const sw::ProfileZoneSite* pSite =
        sw::ProfilerBackend::registerZoneSite( arrName, "probeFunction", "D:/work/repo/Source/Engine/Probe.cpp", 42u );
    SW_ASSERT_NOT_NULL( pSite );
    arrName[0] = 'Z';
    SW_EXPECT_TRUE( sw::string( pSite->_pName ) == "ProfilerBackendTest.Site" );
    SW_EXPECT_TRUE( sw::string( pSite->_pFile ) == "Source/Engine/Probe.cpp" );
    SW_EXPECT_TRUE( sw::string( pSite->_pFunction ) == "probeFunction" );
    SW_EXPECT_EQUAL( 42u, pSite->_line );

    SW_EXPECT_TRUE( sw::ProfilerBackend::registerZoneSite( "ProfilerBackendTest.Site", "probeFunction", "E:/other/Source/Engine/Probe.cpp", 42u ) ==
                    pSite );
    SW_EXPECT_TRUE( sw::ProfilerBackend::registerZoneSite( "ProfilerBackendTest.Site", "probeFunction", "Source/Engine/Probe.cpp", 43u ) !=
                    pSite );

    // 함수 · 파일이 없어도 빈 문자열이다 — 출력이 nullptr 을 문자열로 읽지 않게.
    const sw::ProfileZoneSite* pBare = sw::ProfilerBackend::registerZoneSite( "ProfilerBackendTest.Bare", nullptr, nullptr, 0u );
    SW_ASSERT_NOT_NULL( pBare );
    SW_EXPECT_TRUE( pBare->_pFunction != nullptr && pBare->_pFile != nullptr );
}

/**
 * @brief [ProfilerBackendTest] 카운터는 프레임마다 합 하나가 출력 그래프의 점이 된다
 */
SW_TEST_CASE( ProfilerBackendTest, CounterBecomesOnePlotPointPerFrame )
{
    sw::FrameProfiler profiler;
    const uint32      slot = profiler.registerScope( "ProfilerBackendTest.Count" );
    SW_ASSERT_TRUE( slot != sw::FrameProfiler::kInvalidSlot );
    profiler.setEnabled( true );

    test::RecordingProfilerBackend backend;
    {
        ScopedActiveBackendInternal active{ &backend };
        profiler.beginFrame();
        profiler.addCount( slot, 3 );
        profiler.addCount( slot, 4 );
        profiler.endFrame();
    }
    SW_EXPECT_EQUAL( 1u, backend.countEvent( "plot ProfilerBackendTest.Count=7" ) );
    SW_EXPECT_TRUE( profiler.isCounterScope( slot ) );
    SW_EXPECT_EQUAL( uint64( 7 ), profiler.getLastFrameCount( slot ) );
}

/**
 * @brief [ProfilerBackendTest] Tracy 출력은 뷰어 없이도 구간 · GPU 타임라인을 받는다(이 빌드에 Tracy 가 있을 때)
 * @details 소켓 너머 뷰어가 실제로 받았는지는 여기서 볼 수 없다(Tracy 는 뷰어가 붙을 때까지 큐에 쌓는다). 이 시험은 TracyClient.dll 이
 *          지연 로드로 올라오고, C API 호출 순서(GPU 컨텍스트 → 구간 열기 · 시각 → 닫기 · 시각)가 클라이언트를 죽이지 않는지까지 본다.
 */
SW_TEST_CASE( ProfilerBackendTest, TracyBackendAcceptsZonesWithoutViewer )
{
    if ( sw::ProfilerBackend::isTracyCompiled() == false )
        SW_TEST_SKIP( "Tracy is not linked into this build (SW_ENABLE_TRACY=OFF)" );

    sw::TracyProfilerBackend   tracy;
    const sw::ProfileZoneSite* pSite = sw::ProfilerBackend::registerZoneSite( "ProfilerBackendTest.Tracy", "test", "Test/EngineTest", 1u );
    SW_ASSERT_NOT_NULL( pSite );

    const uint64 zoneToken = tracy.beginZone( *pSite );
    tracy.endZone( zoneToken );
    tracy.plotValue( "ProfilerBackendTest.Plot", 1.0 );
    tracy.markFrame( "ProfilerBackendTest.Frame" );
    SW_EXPECT_EQUAL( uint64( 1 ), tracy.getZoneCount() );

    const uint32 gpuContext = tracy.createGpuContext( sw::ProfilerGpuApi::Direct3D12, "ProfilerBackendTest", 1'000'000 );
    SW_ASSERT_TRUE( gpuContext != sw::IProfilerBackend::kInvalidGpuContext );
    tracy.beginGpuZone( gpuContext, *pSite, 1'000'100 );
    tracy.beginGpuZone( gpuContext, *pSite, 1'000'200 );
    tracy.endGpuZone( gpuContext, 1'000'300 );
    tracy.endGpuZone( gpuContext, 1'000'400 );
    tracy.syncGpuClock( gpuContext, 2'000'000 );
    SW_EXPECT_EQUAL( uint64( 2 ), tracy.getGpuZoneCount() );
    SW_EXPECT_TRUE( sw::ProfilerBackend::getTracyPort() != 0 );
}
#endif
