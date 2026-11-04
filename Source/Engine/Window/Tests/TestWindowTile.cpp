#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Window/IWindow.h"

// WindowTileTest — 창 바둑판 칸의 자리(`IWindow::computeTilePosition`): 칸 간격은 클라이언트 크기 + 창 틀 양쪽 두께다(nogpu).

using namespace sw;

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

    IWindow::computeTilePosition( WindowTileSlot{ 0, 2 }, 640, 360, extents, x, y );
    SW_EXPECT_EQUAL( 0, x );
    SW_EXPECT_EQUAL( 0, y );

    IWindow::computeTilePosition( WindowTileSlot{ 1, 2 }, 640, 360, extents, x, y );
    SW_EXPECT_EQUAL( 640 + 7 + 9, x );
    SW_EXPECT_EQUAL( 0, y );

    IWindow::computeTilePosition( WindowTileSlot{ 3, 2 }, 640, 360, extents, x, y );
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
    IWindow::computeTilePosition( WindowTileSlot{ 2, 1 }, 800, 600, WindowFrameExtents{}, x, y );
    SW_EXPECT_EQUAL( 0, x );
    SW_EXPECT_EQUAL( 1200, y );
}
