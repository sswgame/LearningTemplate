#include "pch.h"

#include "Core/Delegate/Delegate.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/EditorTracyLauncher.h"
#include "Editor/Panels/ProfilerScopeHistory.h"

#include "Engine/Utility/Profiling/FrameProfiler.h"

#include "TestFramework/TestFramework.h"

// ProfilerScopeHistory — 에디터 프로파일러 패널의 상태 · 집계(ImGui 없음). EditorTracyLauncher — "Open Tracy" 의 경로 찾기 · 명령.

namespace
{
    /** @brief 프로파일러에 한 프레임을 접습니다. */
    void foldFrameInternal( sw::FrameProfiler& profiler )
    {
        profiler.beginFrame();
        profiler.endFrame();
    }

    /** @brief 이름이 @p name 인 줄을 찾습니다. 없으면 nullptr 입니다. */
    const sw::editor::ProfilerScopeRow* findRowInternal( const sw::vector<sw::editor::ProfilerScopeRow>& listRow, const utf8* pName )
    {
        for ( const sw::editor::ProfilerScopeRow& row : listRow )
        {
            if ( row._name == pName )
                return &row;
        }
        return nullptr;
    }
} // namespace

/**
 * @brief [ProfilerScopeHistoryTest] 창 안 표본으로 p50 · p99 · 최대 · 평균을 내고, 창을 넘은 오래된 프레임은 빠진다
 * @details 패널의 표는 세션 전체가 아니라 **최근 N 프레임**이다. 오래된 히치가 창 밖으로 나가면 p99 · 최대에서 사라져야 지금 상태가 보인다.
 */
SW_TEST_CASE( ProfilerScopeHistoryTest, WindowStatisticsFollowRecentFrames )
{
    sw::FrameProfiler profiler;
    profiler.setEnabled( true );
    const uint32 slot = profiler.registerScope( "GT.Work" );

    sw::editor::ProfilerScopeHistory history{ 10 };
    // 히치 한 프레임(10 ms) 뒤에 조용한 프레임(100 us) 아홉 — 창이 꽉 찬다.
    profiler.addSample( slot, 10'000'000 );
    foldFrameInternal( profiler );
    SW_ASSERT_TRUE( history.capture( profiler ) );
    SW_EXPECT_FALSE( history.capture( profiler ) ); // 같은 프레임을 두 번 담지 않는다
    for ( uint32 frame = 0; frame < 9; ++frame )
    {
        profiler.addSample( slot, 100'000 );
        foldFrameInternal( profiler );
        SW_ASSERT_TRUE( history.capture( profiler ) );
    }

    sw::editor::ProfilerRowQuery query{};
    query._kind = sw::editor::ProfilerScopeKind::Cpu;
    sw::vector<sw::editor::ProfilerScopeRow> listRow;
    history.makeRows( query, listRow );
    const sw::editor::ProfilerScopeRow* pRow = findRowInternal( listRow, "GT.Work" );
    SW_ASSERT_NOT_NULL( pRow );
    SW_EXPECT_EQUAL( 10u, pRow->_sampleCount );
    SW_EXPECT_NEAR_EQUAL( 100.0, pRow->_last, 1e-3 );
    SW_EXPECT_NEAR_EQUAL( 100.0, pRow->_p50, 1e-3 );
    SW_EXPECT_NEAR_EQUAL( 10'000.0, pRow->_p99, 1e-3 );
    SW_EXPECT_NEAR_EQUAL( 10'000.0, pRow->_max, 1e-3 );
    SW_EXPECT_NEAR_EQUAL( ( 10'000.0 + 9 * 100.0 ) / 10.0, pRow->_average, 1e-3 );

    // 한 프레임 더 — 히치가 창 밖으로 밀려난다.
    profiler.addSample( slot, 100'000 );
    foldFrameInternal( profiler );
    SW_ASSERT_TRUE( history.capture( profiler ) );
    history.makeRows( query, listRow );
    pRow = findRowInternal( listRow, "GT.Work" );
    SW_ASSERT_NOT_NULL( pRow );
    SW_EXPECT_NEAR_EQUAL( 100.0, pRow->_max, 1e-3 );

    // 그래프 값은 오래된 것부터, 창 크기만큼이다.
    sw::vector<float32> listSeries;
    SW_ASSERT_TRUE( history.copySeries( "GT.Work", listSeries ) );
    SW_EXPECT_EQUAL( size_t( 10 ), listSeries.size() );
    SW_EXPECT_NEAR_EQUAL( 100.0f, listSeries.back(), 1e-3f );
    SW_EXPECT_FALSE( history.copySeries( "No.Such.Scope", listSeries ) );
}

/**
 * @brief [ProfilerScopeHistoryTest] 종류(CPU · GPU · 카운터)로 나누고, 검색어(대소문자 무시)로 거르고, 고른 열로 정렬한다
 */
SW_TEST_CASE( ProfilerScopeHistoryTest, RowsAreSplitFilteredAndSorted )
{
    sw::FrameProfiler profiler;
    profiler.setEnabled( true );
    const uint32 slotSlow    = profiler.registerScope( "RT.Slow" );
    const uint32 slotFast    = profiler.registerScope( "RT.Fast" );
    const uint32 slotGpu     = profiler.registerScope( "GPU.Shadow" );
    const uint32 slotCounter = profiler.registerScope( "Draw.Count" );
    const uint32 slotIdle    = profiler.registerScope( "RT.NeverCalled" );
    SW_ASSERT_TRUE( slotIdle != sw::FrameProfiler::kInvalidSlot );

    sw::editor::ProfilerScopeHistory history{ 4 };
    profiler.addSample( slotSlow, 900'000 );
    profiler.addSample( slotFast, 50'000 );
    profiler.addSample( slotGpu, 300'000 );
    profiler.addCount( slotCounter, 42 );
    foldFrameInternal( profiler );
    SW_ASSERT_TRUE( history.capture( profiler ) );

    sw::vector<sw::editor::ProfilerScopeRow> listRow;
    sw::editor::ProfilerRowQuery             query{};
    query._kind        = sw::editor::ProfilerScopeKind::Cpu;
    query._sortColumn  = sw::editor::ProfilerSortColumn::P99;
    query._bDescending = true;
    history.makeRows( query, listRow );
    // 불리지 않은 구간은 줄이 없다. GPU · 카운터는 다른 표다.
    SW_ASSERT_EQUAL( size_t( 2 ), listRow.size() );
    SW_EXPECT_TRUE( listRow[0]._name == "RT.Slow" );
    SW_EXPECT_TRUE( listRow[1]._name == "RT.Fast" );

    query._bDescending = false;
    history.makeRows( query, listRow );
    SW_EXPECT_TRUE( listRow[0]._name == "RT.Fast" );

    query._filterText = "slow";
    history.makeRows( query, listRow );
    SW_ASSERT_EQUAL( size_t( 1 ), listRow.size() );
    SW_EXPECT_TRUE( listRow[0]._name == "RT.Slow" );

    query._filterText.clear();
    query._kind = sw::editor::ProfilerScopeKind::Gpu;
    history.makeRows( query, listRow );
    SW_ASSERT_EQUAL( size_t( 1 ), listRow.size() );
    SW_EXPECT_NEAR_EQUAL( 300.0, listRow[0]._last, 1e-3 );

    query._kind = sw::editor::ProfilerScopeKind::Counter;
    history.makeRows( query, listRow );
    SW_ASSERT_EQUAL( size_t( 1 ), listRow.size() );
    SW_EXPECT_NEAR_EQUAL( 42.0, listRow[0]._last, 1e-3 );
}

/**
 * @brief [ProfilerScopeHistoryTest] 프로파일러가 꺼져 있으면 담지 않는다 — 꺼진 동안 0 이 쌓여 통계가 내려앉지 않게
 */
SW_TEST_CASE( ProfilerScopeHistoryTest, DisabledProfilerIsNotCaptured )
{
    sw::FrameProfiler                profiler;
    sw::editor::ProfilerScopeHistory history{ 4 };
    SW_EXPECT_FALSE( history.capture( profiler ) );
    SW_EXPECT_EQUAL( uint64( 0 ), history.getCapturedFrameCount() );
}

/**
 * @brief [EditorTracyLauncherTest] 뷰어는 설정 경로(파일 · 폴더) → 프로젝트의 Tools/Tracy 순서로 찾고, 명령은 localhost 에 붙는다
 */
SW_TEST_CASE( EditorTracyLauncherTest, FindsViewerAndBuildsConnectCommand )
{
    const sw::string root   = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_tracy_launcher_test" );
    const sw::string folder = sw::FileUtil::joinPath( root, sw::editor::EditorTracyLauncher::kDefaultViewerFolder );
    const sw::string viewer = sw::FileUtil::joinPath( folder, sw::editor::EditorTracyLauncher::getViewerFileName() );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( folder ) );
    const uint8 arrByte[1] = { 0 };
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( viewer, arrByte, 1 ) );
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [root]()
    {
        std::ignore = sw::FileUtil::removeDirectory( root );
    } ) );

    sw::string found;
    // 설정이 없으면 프로젝트의 Tools/Tracy.
    SW_ASSERT_TRUE( sw::editor::EditorTracyLauncher::findViewerPath( "", root, found ) );
    SW_EXPECT_TRUE( sw::FileUtil::pathsEqualNormalized( found, viewer ) );
    // 설정이 폴더를 가리키면 그 안의 실행 파일, 파일을 가리키면 그대로.
    SW_ASSERT_TRUE( sw::editor::EditorTracyLauncher::findViewerPath( folder, "", found ) );
    SW_EXPECT_TRUE( sw::FileUtil::pathsEqualNormalized( found, viewer ) );
    SW_ASSERT_TRUE( sw::editor::EditorTracyLauncher::findViewerPath( viewer, "", found ) );
    SW_EXPECT_TRUE( sw::FileUtil::pathsEqualNormalized( found, viewer ) );
    // 없는 곳만 주면 못 찾는다.
    SW_EXPECT_FALSE( sw::editor::EditorTracyLauncher::findViewerPath( "", sw::FileUtil::joinPath( root, "missing" ), found ) );
    SW_EXPECT_TRUE( found.empty() );

    const sw::string command = sw::editor::EditorTracyLauncher::makeViewerCommand( "C:/Tools/Tracy/tracy-profiler.exe", 8087 );
    SW_EXPECT_TRUE_MSG( command.find( "-a 127.0.0.1 -p 8087" ) != sw::string::npos, command.c_str() );
    SW_EXPECT_TRUE_MSG( command.front() == '"', command.c_str() );
}
