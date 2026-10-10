#include "pch.h"

#include "Editor/Panels/TileMapPaintUtil.h"

#include "Core/Math/MathUtil.h"

namespace sw::editor
{
    void TileMapPaintUtil::collectLineCells( const int2& from, const int2& to, vector<int2>& outListCell )
    {
        outListCell.clear();
        const int32 deltaX = MathUtil::abs( to._x - from._x );
        const int32 deltaY = -MathUtil::abs( to._y - from._y );
        const int32 stepX  = from._x < to._x ? 1 : -1;
        const int32 stepY  = from._y < to._y ? 1 : -1;
        int32       error  = deltaX + deltaY;
        int2        cell   = from;
        for ( ;; )
        {
            outListCell.push_back( cell );
            if ( cell._x == to._x && cell._y == to._y )
                break;
            const int32 doubledError = 2 * error;
            if ( doubledError >= deltaY )
            {
                error += deltaY;
                cell._x += stepX;
            }
            if ( doubledError <= deltaX )
            {
                error += deltaX;
                cell._y += stepY;
            }
        }
    }

    void TileMapPaintUtil::collectRectCells( const int2& cornerA, const int2& cornerB, int32 width, int32 height, vector<int2>& outListCell )
    {
        outListCell.clear();
        const int32 minX = MathUtil::max( 0, MathUtil::min( cornerA._x, cornerB._x ) );
        const int32 maxX = MathUtil::min( width - 1, MathUtil::max( cornerA._x, cornerB._x ) );
        const int32 minY = MathUtil::max( 0, MathUtil::min( cornerA._y, cornerB._y ) );
        const int32 maxY = MathUtil::min( height - 1, MathUtil::max( cornerA._y, cornerB._y ) );
        for ( int32 cellY = minY; cellY <= maxY; ++cellY )
        {
            for ( int32 cellX = minX; cellX <= maxX; ++cellX )
            {
                outListCell.push_back( int2{ cellX, cellY } );
            }
        }
    }

    void TileMapPaintUtil::collectFloodFillCells( const vector<uint64>& listCellValue, int32 width, int32 height, const int2& start,
                                                  vector<int2>& outListCell )
    {
        outListCell.clear();
        const size_t cellCount = static_cast<size_t>( MathUtil::max( 0, width ) ) * static_cast<size_t>( MathUtil::max( 0, height ) );
        if ( start._x < 0 || start._y < 0 || start._x >= width || start._y >= height || listCellValue.size() != cellCount )
            return;

        auto indexOf = [width]( int32 x, int32 y )
        { return static_cast<size_t>( y ) * static_cast<size_t>( width ) + static_cast<size_t>( x ); };

        const uint64  target = listCellValue[indexOf( start._x, start._y )];
        vector<uint8> listVisited( cellCount, 0 );
        listVisited[indexOf( start._x, start._y )] = 1;
        outListCell.push_back( start );
        // 결과 목록이 곧 큐다 — 앞에서부터 읽으며 이웃을 뒤에 붙인다(너비 우선).
        for ( size_t readIndex = 0; readIndex < outListCell.size(); ++readIndex )
        {
            const int2 cell          = outListCell[readIndex];
            const int2 arrNeighbor[] = {
                int2{cell._x - 1,     cell._y},
                int2{cell._x + 1,     cell._y},
                int2{    cell._x, cell._y - 1},
                int2{    cell._x, cell._y + 1}
            };
            for ( const int2& neighbor : arrNeighbor )
            {
                if ( neighbor._x < 0 || neighbor._y < 0 || neighbor._x >= width || neighbor._y >= height )
                    continue;
                const size_t neighborIndex = indexOf( neighbor._x, neighbor._y );
                if ( listVisited[neighborIndex] != 0 || listCellValue[neighborIndex] != target )
                    continue;
                listVisited[neighborIndex] = 1;
                outListCell.push_back( neighbor );
            }
        }
    }

    int2 TileMapPaintUtil::findCellAt( float32 localX, float32 localY, float32 cellSize )
    {
        if ( cellSize <= 0.0f )
            return int2{ -1, -1 };
        return int2{ static_cast<int32>( MathUtil::floor( localX / cellSize ) ), static_cast<int32>( MathUtil::floor( localY / cellSize ) ) };
    }
} // namespace sw::editor

namespace sw::editor
{
    float4 AtlasGridUtil::computeCellUvRect( int32 cell, int32 columnCount, int32 rowCount )
    {
        const int32   columns = MathUtil::max( 1, columnCount );
        const int32   rows    = MathUtil::max( 1, rowCount );
        const int32   column  = MathUtil::max( 0, cell ) % columns;
        const int32   row     = MathUtil::max( 0, cell ) / columns;
        const float32 width   = 1.0f / static_cast<float32>( columns );
        const float32 height  = 1.0f / static_cast<float32>( rows );
        return float4{ static_cast<float32>( column ) * width, static_cast<float32>( row ) * height, width, height };
    }

    int32 AtlasGridUtil::findCellAtUv( float32 u, float32 v, int32 columnCount, int32 rowCount )
    {
        if ( u < 0.0f || v < 0.0f || u >= 1.0f || v >= 1.0f )
            return -1;
        const int32 columns = MathUtil::max( 1, columnCount );
        const int32 rows    = MathUtil::max( 1, rowCount );
        const int32 column  = MathUtil::min( columns - 1, static_cast<int32>( u * static_cast<float32>( columns ) ) );
        const int32 row     = MathUtil::min( rows - 1, static_cast<int32>( v * static_cast<float32>( rows ) ) );
        return row * columns + column;
    }
} // namespace sw::editor
