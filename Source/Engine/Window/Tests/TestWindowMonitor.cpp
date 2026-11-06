#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Window/WindowMonitor.h"

// WindowMonitorTest - 창 시스템이 알린 모니터 값을 목록으로 맞추는 규칙(nogpu). 실제 다중 모니터가 없는 곳(Xvfb)에서도 해석을 시험한다.

using namespace sw;

namespace
{
    struct WindowMonitorTestUtil
    {
        static WindowMonitor makeMonitor( const utf8* pName, int2 position, int2 size, bool bPrimary )
        {
            WindowMonitor monitor;
            monitor._name     = pName;
            monitor._area     = WindowMonitorRect{ position, size };
            monitor._workArea = monitor._area;
            monitor._bPrimary = bPrimary;
            return monitor;
        }
    };
} // namespace

/**
 * @brief [WindowMonitorTest] `_NET_WORKAREA` 는 데스크톱마다 네 칸이다. 지금 데스크톱의 칸을 읽고, 없거나 크기가 0 이면 알리지 않은 것이다
 */
SW_TEST_CASE( WindowMonitorTest, NetWorkAreaReadsTheCurrentDesktop )
{
    const vector<int64> listValue{ 0, 27, 1920, 1053, 0, 0, 3840, 1080, 0, 0, 0, 0 };
    WindowMonitorRect   area;

    SW_REQUIRE_TRUE( WindowMonitor::tryParseNetWorkArea( listValue, 0, area ) );
    SW_EXPECT_EQUAL( 27, area._position._y );
    SW_EXPECT_EQUAL( 1053, area._size._y );

    SW_REQUIRE_TRUE( WindowMonitor::tryParseNetWorkArea( listValue, 1, area ) );
    SW_EXPECT_EQUAL( 3840, area._size._x );

    SW_EXPECT_FALSE( WindowMonitor::tryParseNetWorkArea( listValue, 2, area ) ); // 크기 0
    SW_EXPECT_FALSE( WindowMonitor::tryParseNetWorkArea( listValue, 3, area ) ); // 그 데스크톱의 칸이 없다
    SW_EXPECT_FALSE( WindowMonitor::tryParseNetWorkArea( listValue, -1, area ) );
    SW_EXPECT_FALSE( WindowMonitor::tryParseNetWorkArea( vector<int64>{ 0, 0, 1920 }, 0, area ) );
}

/**
 * @brief [WindowMonitorTest] 데스크톱 하나의 작업 영역은 모니터마다 겹침이다. 겹치지 않으면 그 모니터 전체다
 */
SW_TEST_CASE( WindowMonitorTest, WorkAreaIsTheOverlapWithTheMonitor )
{
    using Util = WindowMonitorTestUtil;
    // 왼쪽 1920x1080 과 오른쪽 2560x1440. 패널이 위 27 픽셀을 뺐다.
    const WindowMonitorRect desktopWorkArea{
        int2{   0,   27},
        int2{4480, 1413}
    };

    WindowMonitor left = Util::makeMonitor( "left", int2{ 0, 0 }, int2{ 1920, 1080 }, true );
    left.clipWorkArea( desktopWorkArea );
    SW_EXPECT_EQUAL( 27, left._workArea._position._y );
    SW_EXPECT_EQUAL( 1920, left._workArea._size._x );
    SW_EXPECT_EQUAL( 1053, left._workArea._size._y );

    WindowMonitor right = Util::makeMonitor( "right", int2{ 1920, 0 }, int2{ 2560, 1440 }, false );
    right.clipWorkArea( desktopWorkArea );
    SW_EXPECT_EQUAL( 1920, right._workArea._position._x );
    SW_EXPECT_EQUAL( 2560, right._workArea._size._x );
    SW_EXPECT_EQUAL( 1413, right._workArea._size._y );

    WindowMonitor apart = Util::makeMonitor( "apart", int2{ 5000, 0 }, int2{ 800, 600 }, false );
    apart.clipWorkArea( desktopWorkArea );
    SW_EXPECT_EQUAL( 5000, apart._workArea._position._x );
    SW_EXPECT_EQUAL( 800, apart._workArea._size._x );
}

/**
 * @brief [WindowMonitorTest] 출력의 모드 번호를 표에서 찾아 해상도로 바꾼다. 재생률만 다른 모드는 하나이고 작은 모드는 뺀다. 돌린 출력은 가로세로를 바꾼다
 */
SW_TEST_CASE( WindowMonitorTest, ModeIdsBecomeSortedUniqueResolutions )
{
    const vector<WindowMonitorMode> listMode{
        {10, int2{ 1920, 1080 }},
        {11, int2{ 1920, 1080 }},
        {12,  int2{ 1280, 720 }},
        {13,   int2{ 640, 480 }},
        {14, int2{ 3840, 2160 }}
    };
    const vector<uint64> listModeId{ 11, 10, 12, 13, 99 }; // 14 는 이 출력이 내지 않고, 99 는 표에 없다

    WindowMonitor monitor;
    monitor.appendModeResolutions( listMode, listModeId, false );
    monitor.normalizeResolutions();
    SW_REQUIRE_EQUAL( size_t{ 2 }, monitor._listResolution.size() );
    SW_EXPECT_EQUAL( 1280, monitor._listResolution[0]._x );
    SW_EXPECT_EQUAL( 1920, monitor._listResolution[1]._x );

    WindowMonitor rotated;
    rotated.appendModeResolutions( listMode, vector<uint64>{ 12 }, true );
    rotated.normalizeResolutions();
    SW_REQUIRE_EQUAL( size_t{ 1 }, rotated._listResolution.size() );
    SW_EXPECT_EQUAL( 720, rotated._listResolution[0]._x );
    SW_EXPECT_EQUAL( 1280, rotated._listResolution[0]._y );
}

/**
 * @brief [WindowMonitorTest] 주 모니터가 0 번으로 온다. 표시가 없으면 첫 모니터가 주 모니터다. 빈 목록은 주 모니터가 없다
 */
SW_TEST_CASE( WindowMonitorTest, PrimaryComesFirst )
{
    using Util = WindowMonitorTestUtil;
    vector<WindowMonitor> listMonitor;
    SW_EXPECT_TRUE( WindowMonitor::findPrimary( listMonitor ) == nullptr );
    WindowMonitor::orderPrimaryFirst( listMonitor );
    SW_EXPECT_TRUE( listMonitor.empty() );

    listMonitor.push_back( Util::makeMonitor( "a", int2{ 0, 0 }, int2{ 1920, 1080 }, false ) );
    listMonitor.push_back( Util::makeMonitor( "b", int2{ 1920, 0 }, int2{ 1920, 1080 }, true ) );
    WindowMonitor::orderPrimaryFirst( listMonitor );
    SW_EXPECT_TRUE( listMonitor[0]._name == "b" );
    SW_EXPECT_TRUE( WindowMonitor::findPrimary( listMonitor ) == &listMonitor[0] );

    vector<WindowMonitor> listUnmarked;
    listUnmarked.push_back( Util::makeMonitor( "a", int2{ 0, 0 }, int2{ 1920, 1080 }, false ) );
    WindowMonitor::orderPrimaryFirst( listUnmarked );
    SW_EXPECT_TRUE( listUnmarked[0]._bPrimary );
}
