#include "pch.h"

#include "GameFramework/Navigation/GridReachability.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    bool GridReachability::isReachable( const int2& cell ) const
    {
        return isInside( cell ) && _listCost[static_cast<size_t>( computeIndex( cell ) )] != kUnreached &&
               _listStoppable[static_cast<size_t>( computeIndex( cell ) )] != SW_FALSE;
    }

    int32 GridReachability::getCost( const int2& cell ) const { return isInside( cell ) ? _listCost[static_cast<size_t>( computeIndex( cell ) )] : kUnreached; }

    void GridReachability::collectReachable( vector<int2>& outListCell ) const
    {
        outListCell.clear();
        for ( int32 index = 0; index < _width * _height; ++index )
        {
            if ( _listCost[static_cast<size_t>( index )] != kUnreached && _listStoppable[static_cast<size_t>( index )] != SW_FALSE )
                outListCell.push_back( int2{ index % _width, index / _width } );
        }
    }

    bool GridReachability::makePath( const int2& cell, vector<int2>& outListCell ) const
    {
        outListCell.clear();
        if ( isInside( cell ) == false || _listCost[static_cast<size_t>( computeIndex( cell ) )] == kUnreached )
            return false;
        for ( int32 index = computeIndex( cell ); index >= 0; index = _listParent[static_cast<size_t>( index )] )
            outListCell.push_back( int2{ index % _width, index / _width } );
        std::reverse( outListCell.begin(), outListCell.end() );
        return true;
    }

    void GridReachability::collectRangeCells( const int2& center, int32 minRange, int32 maxRange, int32 width, int32 height, vector<int2>& outListCell )
    {
        outListCell.clear();
        for ( int32 offsetY = -maxRange; offsetY <= maxRange; ++offsetY )
        {
            for ( int32 offsetX = -maxRange; offsetX <= maxRange; ++offsetX )
            {
                const int32 distance = MathUtil::abs( offsetX ) + MathUtil::abs( offsetY );
                const int2  cell{ center._x + offsetX, center._y + offsetY };
                if ( distance < minRange || distance > maxRange || cell._x < 0 || cell._y < 0 || cell._x >= width || cell._y >= height )
                    continue;
                outListCell.push_back( cell );
            }
        }
    }

    void GridReachability::collectAttackCells( int32 minRange, int32 maxRange, vector<int2>& outListCell ) const
    {
        outListCell.clear();
        // 한 칸을 한 번만 — 표시는 재사용 스크래치에(호출마다 W × H 를 잡지 않는다).
        _cellMarks.begin( _width * _height );
        vector<int2> listStand;
        vector<int2> listRange;
        collectReachable( listStand );
        for ( const int2& stand : listStand )
        {
            collectRangeCells( stand, minRange, maxRange, _width, _height, listRange );
            for ( const int2& cell : listRange )
            {
                if ( _cellMarks.visit( computeIndex( cell ), -1 ) )
                    outListCell.push_back( cell );
            }
        }
    }
} // namespace sw
