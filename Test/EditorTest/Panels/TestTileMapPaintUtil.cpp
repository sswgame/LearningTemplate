#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Panels/TileMapPaintUtil.h"

#include "TestFramework/TestFramework.h"

// TileMapPaintUtilTest — 타일맵 칠하기가 빠른 드래그에서 칸을 건너뛰지 않는다(패널 점검 D18). ImGui 없음.

/**
 * @brief [TileMapPaintUtilTest] 한 프레임에 지나간 두 칸 사이는 빈틈 없는 선분이다 — 두 끝 포함, 이웃 칸은 x · y 가 1 이하로 다르다
 * @details 패널이 마우스가 눌린 프레임마다 그 자리 칸 하나만 칠해, 빠르게 끌면 지운 칸 사이에 지워지지 않은 칸이 남았다.
 */
SW_TEST_CASE( TileMapPaintUtilTest, LineCellsHaveNoGaps )
{
    using sw::int2;
    using sw::editor::TileMapPaintUtil;
    sw::vector<int2> listCell;
    TileMapPaintUtil::collectLineCells( int2{ 0, 0 }, int2{ 7, 3 }, listCell );
    SW_ASSERT_EQUAL( 8u, static_cast<uint32>( listCell.size() ) ); // 긴 축의 칸 수 + 1
    SW_EXPECT_EQUAL( 0, listCell.front()._x );
    SW_EXPECT_EQUAL( 0, listCell.front()._y );
    SW_EXPECT_EQUAL( 7, listCell.back()._x );
    SW_EXPECT_EQUAL( 3, listCell.back()._y );
    for ( size_t cellIndex = 1; cellIndex < listCell.size(); ++cellIndex )
    {
        SW_EXPECT_TRUE( sw::MathUtil::abs( listCell[cellIndex]._x - listCell[cellIndex - 1]._x ) <= 1 );
        SW_EXPECT_TRUE( sw::MathUtil::abs( listCell[cellIndex]._y - listCell[cellIndex - 1]._y ) <= 1 );
    }

    // 거꾸로 끈 획(오른쪽 아래 → 왼쪽 위)과 세로 획.
    TileMapPaintUtil::collectLineCells( int2{ 7, 3 }, int2{ 0, 0 }, listCell );
    SW_EXPECT_EQUAL( 8u, static_cast<uint32>( listCell.size() ) );
    SW_EXPECT_EQUAL( 0, listCell.back()._x );
    TileMapPaintUtil::collectLineCells( int2{ 2, 5 }, int2{ 2, 0 }, listCell );
    SW_EXPECT_EQUAL( 6u, static_cast<uint32>( listCell.size() ) );

    // 움직이지 않은 프레임은 그 칸 하나.
    TileMapPaintUtil::collectLineCells( int2{ 4, 4 }, int2{ 4, 4 }, listCell );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listCell.size() ) );
}
