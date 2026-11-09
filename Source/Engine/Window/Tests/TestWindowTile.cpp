#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Window/IWindow.h"
#include "Engine/Window/WindowMonitor.h"

// WindowTileTest — 창 바둑판 칸의 자리(`IWindow::computeTilePosition`): 원점은 작업 영역, 칸 간격은 클라이언트 크기 + 창 틀 양쪽 두께, 모자라면 작업 영역 안으로(nogpu).

using namespace sw;

namespace
{
    /** @brief 넉넉한 작업 영역(4K, 원점 0) — 간격 그대로 쌓인다. */
    const WindowMonitorRect kWideWorkArea{
        int2{   0,    0},
        int2{3840, 2160}
    };
} // namespace

/**
 * @brief [WindowTileTest] 열은 가로로 클라이언트 너비 + 왼 · 오른 틀만큼, 행은 세로로 클라이언트 높이 + 위 · 아래 틀만큼 띄운다
 */
SW_TEST_CASE( WindowTileTest, StrideIsClientPlusBothFrameSides )
{
    WindowFrameExtents extents;
    extents._left   = 7;
    extents._top    = 31;
    extents._right  = 9;
    extents._bottom = 8;
    int32 x         = -1;
    int32 y         = -1;

    IWindow::computeTilePosition( WindowTileSlot{ 0, 2 }, 640, 360, extents, kWideWorkArea, x, y );
    SW_EXPECT_EQUAL( 0, x );
    SW_EXPECT_EQUAL( 0, y );

    IWindow::computeTilePosition( WindowTileSlot{ 1, 2 }, 640, 360, extents, kWideWorkArea, x, y );
    SW_EXPECT_EQUAL( 640 + 7 + 9, x );
    SW_EXPECT_EQUAL( 0, y );

    IWindow::computeTilePosition( WindowTileSlot{ 3, 2 }, 640, 360, extents, kWideWorkArea, x, y );
    SW_EXPECT_EQUAL( 640 + 7 + 9, x );
    SW_EXPECT_EQUAL( 360 + 31 + 8, y );
}

/**
 * @brief [WindowTileTest] 틀이 없으면(Wayland 처럼 장식 없는 창) 칸이 클라이언트 크기로 맞붙고, 한 열이면 아래로만 쌓인다
 */
SW_TEST_CASE( WindowTileTest, NoFrameAndSingleColumn )
{
    int32 x = -1;
    int32 y = -1;
    IWindow::computeTilePosition( WindowTileSlot{ 2, 1 }, 800, 600, WindowFrameExtents{}, kWideWorkArea, x, y );
    SW_EXPECT_EQUAL( 0, x );
    SW_EXPECT_EQUAL( 1200, y );
}

/**
 * @brief [WindowTileTest] 작은 모니터(1920×1080, 작업 표시줄 40 · 왼쪽 패널 60)에서 2×2 칸(1280×720 창)은 작업 영역 안이다 — 원점은 작업 영역 왼쪽 위,
 *        가로 간격은 두 열이 너비에 들어가게 좁히고(겹침을 나눈다), 둘째 행은 작업 영역 아래끝에 붙는다
 */
SW_TEST_CASE( WindowTileTest, SmallMonitorKeepsEveryTileInsideTheWorkArea )
{
    const WindowMonitorRect workArea{
        int2{  60,    0},
        int2{1860, 1040}
    };
    int32 x = -1;
    int32 y = -1;

    IWindow::computeTilePosition( WindowTileSlot{ 0, 2 }, 1280, 720, WindowFrameExtents{}, workArea, x, y );
    SW_EXPECT_EQUAL( 60, x );
    SW_EXPECT_EQUAL( 0, y );

    IWindow::computeTilePosition( WindowTileSlot{ 3, 2 }, 1280, 720, WindowFrameExtents{}, workArea, x, y );
    SW_EXPECT_EQUAL( 60 + ( 1860 - 1280 ), x ); // 오른쪽 끝이 작업 영역 오른쪽 끝
    SW_EXPECT_EQUAL( 1040 - 720, y );           // 아래끝이 작업 표시줄 위
}
