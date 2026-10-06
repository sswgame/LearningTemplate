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
} // namespace sw::editor
