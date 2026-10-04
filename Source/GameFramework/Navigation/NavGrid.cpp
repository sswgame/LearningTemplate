#include "pch.h"

#include "GameFramework/Navigation/NavGrid.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    NavGrid::NavGrid()
        : _listCost{}
        , _origin{ 0.0f, 0.0f, 0.0f }
        , _cellSize{ 1.0f }
        , _width{ 0 }
        , _height{ 0 }
        , _revision{ 0 }
    {
    }

    void NavGrid::initialize( int32 width, int32 height, float32 cellSize, const float3& origin )
    {
        _width    = MathUtil::max( 1, width );
        _height   = MathUtil::max( 1, height );
        _cellSize = cellSize > 0.0f ? cellSize : 1.0f;
        _origin   = origin;
        _listCost.assign( static_cast<size_t>( _width * _height ), kNavDefaultCost );
        ++_revision;
    }

    void NavGrid::setCost( int32 x, int32 y, uint8 cost )
    {
        if ( isInside( x, y ) == false )
            return;
        uint8& slot = _listCost[static_cast<size_t>( y * _width + x )];
        if ( slot == cost )
            return;
        slot = cost == 0 ? 1 : cost; // 0 은 휴리스틱을 깨뜨린다(공짜 칸)
        ++_revision;
    }

    void NavGrid::setAreaCost( int32 minX, int32 minY, int32 maxX, int32 maxY, uint8 cost )
    {
        const uint8 safeCost = cost == 0 ? 1 : cost;
        bool        bChanged = false;
        for ( int32 y = MathUtil::max( 0, minY ); y <= MathUtil::min( _height - 1, maxY ); ++y )
        {
            for ( int32 x = MathUtil::max( 0, minX ); x <= MathUtil::min( _width - 1, maxX ); ++x )
            {
                uint8& slot = _listCost[static_cast<size_t>( y * _width + x )];
                bChanged    = bChanged || slot != safeCost;
                slot        = safeCost;
            }
        }
        if ( bChanged )
            ++_revision;
    }

    int2 NavGrid::computeCell( const float3& worldPosition ) const
    {
        return int2{ static_cast<int32>( MathUtil::floor( ( worldPosition._x - _origin._x ) / _cellSize ) ),
                     static_cast<int32>( MathUtil::floor( ( worldPosition._z - _origin._z ) / _cellSize ) ) };
    }

    float3 NavGrid::computeCellCenter( const int2& cell ) const
    {
        return float3{ _origin._x + ( static_cast<float32>( cell._x ) + 0.5f ) * _cellSize, _origin._y, _origin._z + ( static_cast<float32>( cell._y ) + 0.5f ) * _cellSize };
    }

    bool NavGrid::hasLineOfSight( const int2& from, const int2& to ) const
    {
        // 슈퍼커버 선 — 선이 모서리를 정확히 지나면 양옆 칸을 모두 본다(대각선 틈 빠지기 막기).
        int32       x      = from._x;
        int32       y      = from._y;
        const int32 deltaX = MathUtil::abs( to._x - from._x );
        const int32 deltaY = MathUtil::abs( to._y - from._y );
        const int32 stepX  = to._x > from._x ? 1 : -1;
        const int32 stepY  = to._y > from._y ? 1 : -1;
        int32       error  = deltaX - deltaY;
        if ( isWalkable( x, y ) == false )
            return false;
        for ( int32 stepIndex = 0; stepIndex < deltaX + deltaY; ++stepIndex )
        {
            const int32 doubled = 2 * error;
            if ( doubled > -deltaY && doubled < deltaX )
            {
                // 정확히 모서리 — 대각선 한 걸음 대신 두 이웃이 다 열려 있어야 한다.
                if ( isWalkable( x + stepX, y ) == false || isWalkable( x, y + stepY ) == false )
                    return false;
                error -= deltaY;
                error += deltaX;
                x += stepX;
                y += stepY;
                ++stepIndex;
            }
            else if ( doubled > -deltaY )
            {
                error -= deltaY;
                x += stepX;
            }
            else
            {
                error += deltaX;
                y += stepY;
            }
            if ( isWalkable( x, y ) == false )
                return false;
        }
        return true;
    }

    bool NavGrid::findNearestWalkable( const int2& cell, int32 maxRadius, int2& outCell ) const
    {
        if ( isWalkable( cell ) )
        {
            outCell = cell;
            return true;
        }
        // 고리로 넓혀 간다 — 같은 고리 안에서는 실제 거리(제곱)가 가장 짧은 칸.
        for ( int32 radius = 1; radius <= maxRadius; ++radius )
        {
            int32 bestDistance = -1;
            for ( int32 dy = -radius; dy <= radius; ++dy )
            {
                for ( int32 dx = -radius; dx <= radius; ++dx )
                {
                    if ( MathUtil::max( MathUtil::abs( dx ), MathUtil::abs( dy ) ) != radius || isWalkable( cell._x + dx, cell._y + dy ) == false )
                        continue;
                    const int32 distance = dx * dx + dy * dy;
                    if ( bestDistance < 0 || distance < bestDistance )
                    {
                        bestDistance = distance;
                        outCell      = int2{ cell._x + dx, cell._y + dy };
                    }
                }
            }
            if ( bestDistance >= 0 )
                return true;
        }
        return false;
    }
} // namespace sw
