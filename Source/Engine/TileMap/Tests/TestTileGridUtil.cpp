// TileGridUtilTest — 타일 격자에서 충돌 사각형 병합 · 외곽선 · 청크 배치(nogpu). 이동 비용을 경로 찾기에 넣는 케이스는 GameFramework 의 `TileNavCostTest` 가 본다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/TileMap/TileGridUtil.h"

/**
 * @brief [TileGridUtilTest] 병합 사각형은 단단한 칸을 하나도 빠짐없이 꼭 한 번 덮고 빈 칸은 덮지 않으며, 칸마다 상자보다 훨씬 적다
 * @details ㄱ 자(위 한 줄 · 왼쪽 세로 · 오른아래 한 칸): 위 줄 3 칸이 하나, 왼쪽 아래 2 칸이 하나, 외딴 칸 하나 — 사각형 셋(칸 여섯). 벽 16 × 16 의 테두리는
 *          칸 60 개지만 사각형 넷이다.
 */
SW_TEST_CASE( TileGridUtilTest, MergedRectanglesCoverEverySolidCellOnce )
{
    // X X X
    // X . .
    // X . X
    const sw::vector<uint8>  listSolid = { 1, 1, 1, 1, 0, 0, 1, 0, 1 };
    sw::vector<sw::TileRect> listRect;
    sw::TileGridUtil::mergeSolidRectangles( listSolid, 3, 3, listRect );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listRect.size() ) );
    sw::vector<uint32> listCover( 9, 0 );
    for ( const sw::TileRect& rect : listRect )
    {
        for ( int32 y = rect._y; y < rect._y + rect._height; ++y )
        {
            for ( int32 x = rect._x; x < rect._x + rect._width; ++x )
            {
                ++listCover[static_cast<size_t>( y * 3 + x )];
            }
        }
    }
    for ( size_t cellIndex = 0; cellIndex < 9; ++cellIndex )
    {
        SW_EXPECT_EQUAL( static_cast<uint32>( listSolid[cellIndex] ), listCover[cellIndex] );
    }

    // 테두리 벽: 칸 60 개 → 사각형 4 개.
    constexpr int32   kSize = 16;
    sw::vector<uint8> listBorder( static_cast<size_t>( kSize * kSize ), 0 );
    for ( int32 index = 0; index < kSize; ++index )
    {
        listBorder[static_cast<size_t>( index )]                         = 1;
        listBorder[static_cast<size_t>( ( kSize - 1 ) * kSize + index )] = 1;
        listBorder[static_cast<size_t>( index * kSize )]                 = 1;
        listBorder[static_cast<size_t>( index * kSize + kSize - 1 )]     = 1;
    }
    sw::TileGridUtil::mergeSolidRectangles( listBorder, kSize, kSize, listRect );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( listRect.size() ) );
}

/**
 * @brief [TileGridUtilTest] 외곽선은 단단한 칸과 빈 칸(맵 밖 포함) 사이의 변을 같은 방향끼리 이어 붙이고 바깥쪽 방향을 단다
 * @details 2 × 3 덩어리는 토막 넷(위 · 아래 · 왼 · 오른, 각각 바깥쪽이 그 방향)이다. 붙어 있는 두 칸 사이의 변은 외곽선이 아니다. 오목한 ㄱ 자는 토막이 여섯이다.
 */
SW_TEST_CASE( TileGridUtilTest, OutlineJoinsEdgesAndPointsOutward )
{
    // . . . .
    // X X X .
    // X X X .
    const sw::vector<uint8>  listBlock = { 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0 };
    sw::vector<sw::TileEdge> listEdge;
    sw::TileGridUtil::traceSolidOutline( listBlock, 4, 3, listEdge );
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( listEdge.size() ) );
    bool bTop   = false;
    bool bRight = false;
    for ( const sw::TileEdge& edge : listEdge )
    {
        if ( edge._outward._y == -1 )
            bTop = edge._start._x == 0 && edge._start._y == 1 && edge._end._x == 3 && edge._end._y == 1;
        if ( edge._outward._x == 1 )
            bRight = edge._start._x == 3 && edge._start._y == 1 && edge._end._x == 3 && edge._end._y == 3;
    }
    SW_EXPECT_TRUE( bTop );
    SW_EXPECT_TRUE( bRight );

    // X X
    // X .   — 오목한 ㄱ 자: 위 · 왼 · 아래 둘 · 오른 둘 = 여섯.
    const sw::vector<uint8> listCorner = { 1, 1, 1, 0 };
    sw::TileGridUtil::traceSolidOutline( listCorner, 2, 2, listEdge );
    SW_EXPECT_EQUAL( 6u, static_cast<uint32>( listEdge.size() ) );
}

/**
 * @brief [TileGridUtilTest] 청크 배치는 맵을 한 변 N 칸 청크로 행 우선 나누고, 끝 청크는 남은 칸만큼 작으며, 칸의 청크 안 번호는 그 청크의 실제 폭으로 센다
 * @details 70 x 40 맵 · 청크 32 — 청크 3 x 2. 오른쪽 끝 청크는 폭 6, 아래 끝은 높이 8 이다. 칸 (69, 39) 는 마지막 청크(5)의 (5, 7) 이라 청크 안 번호 7 x 6 + 5 = 47.
 */
SW_TEST_CASE( TileGridUtilTest, ChunkGridSplitsTheMapRowMajorWithShorterEdgeChunks )
{
    const sw::TileChunkGrid grid = sw::TileChunkGrid::make( 70, 40, 32 );
    SW_EXPECT_EQUAL( 3, grid._countX );
    SW_EXPECT_EQUAL( 2, grid._countY );
    SW_EXPECT_EQUAL( 6, grid.getChunkCount() );
    SW_EXPECT_EQUAL( 0, grid.computeChunkIndex( 0, 0 ) );
    SW_EXPECT_EQUAL( 2, grid.computeChunkIndex( 64, 0 ) );
    SW_EXPECT_EQUAL( 5, grid.computeChunkIndex( 69, 39 ) );
    const sw::TileRect edge = grid.computeChunkRect( 5 );
    SW_EXPECT_EQUAL( 64, edge._x );
    SW_EXPECT_EQUAL( 32, edge._y );
    SW_EXPECT_EQUAL( 6, edge._width );
    SW_EXPECT_EQUAL( 8, edge._height );
    SW_EXPECT_EQUAL( 47u, grid.computeEntryIndex( 69, 39 ) );
    SW_EXPECT_EQUAL( 33u, grid.computeEntryIndex( 33, 1 ) ); // 청크 1 의 (1, 1) — 폭 32
    // 모든 칸이 꼭 한 청크의 한 번호에 들어간다(청크 안 번호는 그 청크의 칸 수보다 작고, 겹치지 않는다).
    constexpr uint32  kEntryStride = 32u * 32u;
    sw::vector<uint8> listSeen( static_cast<size_t>( grid.getChunkCount() ) * kEntryStride, 0 );
    uint32            duplicateCount{ 0 };
    uint32            outsideCount{ 0 };
    for ( int32 y = 0; y < 40; ++y )
    {
        for ( int32 x = 0; x < 70; ++x )
        {
            const int32        chunkIndex = grid.computeChunkIndex( x, y );
            const sw::TileRect rect       = grid.computeChunkRect( chunkIndex );
            const uint32       entry      = grid.computeEntryIndex( x, y );
            if ( entry >= static_cast<uint32>( rect._width * rect._height ) )
                ++outsideCount;
            const size_t slot = static_cast<size_t>( chunkIndex ) * kEntryStride + entry;
            if ( listSeen[slot] != 0 )
                ++duplicateCount;
            listSeen[slot] = 1;
        }
    }
    SW_EXPECT_EQUAL( 0u, outsideCount );
    SW_EXPECT_EQUAL( 0u, duplicateCount );
}
