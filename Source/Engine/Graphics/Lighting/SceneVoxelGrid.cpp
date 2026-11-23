#include "pch.h"

#include "Engine/Graphics/Lighting/SceneVoxelGrid.h"

namespace sw
{
    SceneVoxelGrid SceneVoxelGrid::make( const AABB& bounds, float32 targetCellSize )
    {
        SceneVoxelGrid grid;
        if ( bounds.isValid() == false || targetCellSize <= 0.0f )
            return grid;
        const float3  size       = bounds._max - bounds._min;
        const float32 longest    = MathUtil::max( size._x, MathUtil::max( size._y, size._z ) );
        float32       cellSize   = targetCellSize;
        const float32 maxCellCnt = static_cast<float32>( kMaxCellsPerAxis );
        if ( longest / cellSize > maxCellCnt )
            cellSize = longest / maxCellCnt;
        grid._origin   = bounds._min;
        grid._cellSize = cellSize;
        grid._dimX     = MathUtil::max( static_cast<uint32>( MathUtil::ceil( size._x / cellSize ) ), 1u );
        grid._dimY     = MathUtil::max( static_cast<uint32>( MathUtil::ceil( size._y / cellSize ) ), 1u );
        grid._dimZ     = MathUtil::max( static_cast<uint32>( MathUtil::ceil( size._z / cellSize ) ), 1u );
        return grid;
    }

    AABB SceneVoxelGrid::getBounds() const
    {
        const float3 size{ static_cast<float32>( _dimX ) * _cellSize, static_cast<float32>( _dimY ) * _cellSize, static_cast<float32>( _dimZ ) * _cellSize };
        return AABB{ _origin, _origin + size };
    }

    SceneVoxelGrid::CellRange SceneVoxelGrid::getFullRange() const
    {
        CellRange range{};
        range._arrLast[0] = _dimX - 1u;
        range._arrLast[1] = _dimY - 1u;
        range._arrLast[2] = _dimZ - 1u;
        return range;
    }

    bool SceneVoxelGrid::findCellRange( const AABB& box, CellRange& outRange ) const
    {
        if ( isEmpty() || box.isValid() == false )
            return false;
        const float3  low  = box._min - _origin;
        const float3  high = box._max - _origin;
        const float32 arrLow[3]{ low._x, low._y, low._z };
        const float32 arrHigh[3]{ high._x, high._y, high._z };
        const uint32  arrDim[3]{ _dimX, _dimY, _dimZ };
        for ( uint32 axis = 0; axis < 3; ++axis )
        {
            // 상자가 격자 밖이면 덮는 칸이 없다.
            const float32 gridExtent = static_cast<float32>( arrDim[axis] ) * _cellSize;
            if ( arrHigh[axis] < 0.0f || arrLow[axis] > gridExtent )
                return false;
            outRange._arrFirst[axis] = MathUtil::min( static_cast<uint32>( MathUtil::max( MathUtil::floor( arrLow[axis] / _cellSize ), 0.0f ) ), arrDim[axis] - 1u );
            outRange._arrLast[axis]  = MathUtil::min( static_cast<uint32>( MathUtil::max( MathUtil::floor( arrHigh[axis] / _cellSize ), 0.0f ) ), arrDim[axis] - 1u );
        }
        return true;
    }

    AABB SceneVoxelGrid::computeRangeBounds( const CellRange& range ) const
    {
        const float3 first{ static_cast<float32>( range._arrFirst[0] ), static_cast<float32>( range._arrFirst[1] ), static_cast<float32>( range._arrFirst[2] ) };
        const float3 end{ static_cast<float32>( range._arrLast[0] + 1u ), static_cast<float32>( range._arrLast[1] + 1u ), static_cast<float32>( range._arrLast[2] + 1u ) };
        return AABB{ _origin + first * _cellSize, _origin + end * _cellSize };
    }
} // namespace sw
