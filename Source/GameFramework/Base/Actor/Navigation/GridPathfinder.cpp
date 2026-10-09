#include "pch.h"

#include "GameFramework/Base/Actor/Navigation/GridPathfinder.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Actor/Navigation/NavGrid.h"
#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"

namespace sw
{
    namespace
    {
        struct GridPathfinderInternal
        {
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( GridPathResult result )
    {
        switch ( result )
        {
            case GridPathResult::Found:
                return "Found";
            case GridPathResult::Partial:
                return "Partial";
            case GridPathResult::NoPath:
                return "NoPath";
            case GridPathResult::InvalidStart:
                return "InvalidStart";
        }
        return "?";
    }

    GridPathfinder::GridPathfinder()
        : _listCostSoFar{}
        , _listParent{}
        , _listStamp{}
        , _listOpen{}
        , _stamp{ 0 }
        , _pCachedGrid{ nullptr }
        , _gridRevision{ 0xffffffffu }
        , _gridCellCount{ 0 }
        , _minCellCost{ 1.0f }
        , _lastExpansionCount{ 0 }
    {
    }

    void GridPathfinder::prepare( const NavGrid& grid )
    {
        const int32 cellCount  = grid.getWidth() * grid.getHeight();
        const bool  bOtherGrid = &grid != _pCachedGrid || cellCount != _gridCellCount;
        if ( cellCount != _gridCellCount )
        {
            _gridCellCount = cellCount;
            _listCostSoFar.assign( static_cast<size_t>( cellCount ), 0.0f );
            _listParent.assign( static_cast<size_t>( cellCount ), -1 );
            _listStamp.assign( static_cast<size_t>( cellCount ), 0u );
            _stamp = 0;
        }
        if ( bOtherGrid || grid.getRevision() != _gridRevision )
        {
            _pCachedGrid  = &grid;
            _gridRevision = grid.getRevision();
            uint8 minCost = kNavBlockedCost;
            for ( int32 y = 0; y < grid.getHeight(); ++y )
            {
                for ( int32 x = 0; x < grid.getWidth(); ++x )
                {
                    minCost = MathUtil::min( minCost, grid.getCost( x, y ) );
                }
            }
            _minCellCost = static_cast<float32>( minCost == kNavBlockedCost ? 1 : minCost );
        }
        // 세대 번호 — 칸마다 "보았음(_stamp)" · "닫힘(_stamp + 1)" 을 쓰므로 2 씩 오른다. 감기면 한 번 비운다.
        _stamp += 2;
        if ( _stamp < 2 )
        {
            _listStamp.assign( _listStamp.size(), 0u );
            _stamp = 2;
        }
        _listOpen.clear();
    }

    float32 GridPathfinder::computeHeuristic( const int2& from, const int2& to ) const
    {
        const int32 dx       = MathUtil::abs( to._x - from._x );
        const int32 dy       = MathUtil::abs( to._y - from._y );
        const int32 straight = MathUtil::max( dx, dy ) - MathUtil::min( dx, dy );
        const int32 diagonal = MathUtil::min( dx, dy );
        return _minCellCost * ( static_cast<float32>( straight ) + MathUtil::kSqrt2 * static_cast<float32>( diagonal ) );
    }

    GridPathResult GridPathfinder::findPath( const NavGrid& grid, const GridPathQuery& query, vector<int2>& outListCell )
    {
        outListCell.clear();
        _lastExpansionCount = 0;
        if ( grid.isWalkable( query._start ) == false )
            return GridPathResult::InvalidStart;
        prepare( grid );

        const int32 width       = grid.getWidth();
        const int32 startIndex  = grid.computeIndex( query._start );
        const bool  bGoalInside = grid.isInside( query._goal );
        const int32 goalIndex   = bGoalInside ? grid.computeIndex( query._goal ) : -1;
        const auto  compareOpen = []( const OpenEntry& lhs, const OpenEntry& rhs )
        { return lhs._score > rhs._score; };

        _listCostSoFar[static_cast<size_t>( startIndex )] = 0.0f;
        _listParent[static_cast<size_t>( startIndex )]    = -1;
        _listStamp[static_cast<size_t>( startIndex )]     = _stamp;
        _listOpen.push_back( OpenEntry{ computeHeuristic( query._start, query._goal ), startIndex } );

        int32       bestIndex      = startIndex;
        float32     bestHeuristic  = computeHeuristic( query._start, query._goal );
        bool        bReached       = false;
        const int32 directionCount = query._bAllowDiagonal != SW_FALSE ? 8 : 4;
        while ( _listOpen.empty() == false )
        {
            std::pop_heap( _listOpen.begin(), _listOpen.end(), compareOpen );
            const OpenEntry current = _listOpen.back();
            _listOpen.pop_back();
            uint32& currentStamp = _listStamp[static_cast<size_t>( current._index )];
            if ( currentStamp == _stamp + 1 )
                continue; // 더 싼 길로 이미 닫혔다(힙의 낡은 항목)
            currentStamp = _stamp + 1;

            if ( current._index == goalIndex )
            {
                bestIndex = current._index;
                bReached  = true;
                break;
            }
            const int2    cell{ current._index % width, current._index / width };
            const float32 heuristic = computeHeuristic( cell, query._goal );
            if ( heuristic < bestHeuristic )
            {
                bestHeuristic = heuristic;
                bestIndex     = current._index;
            }
            ++_lastExpansionCount;
            if ( query._maxExpansions > 0 && _lastExpansionCount >= query._maxExpansions )
                break;

            const float32 costSoFar = _listCostSoFar[static_cast<size_t>( current._index )];
            for ( int32 direction = 0; direction < directionCount; ++direction )
            {
                const int2 next = GridTopology::getNeighbor( cell, direction ); // 앞 넷이 직교, 뒤 넷이 대각선
                if ( grid.isWalkable( next ) == false )
                    continue;
                const bool bDiagonal = direction >= 4;
                // 모서리 깎기 금지 — 대각선은 두 직교 이웃이 다 열려 있어야 한다.
                if ( bDiagonal && ( grid.isWalkable( next._x, cell._y ) == false || grid.isWalkable( cell._x, next._y ) == false ) )
                    continue;
                const int32 nextIndex = grid.computeIndex( next );
                uint32&     nextStamp = _listStamp[static_cast<size_t>( nextIndex )];
                if ( nextStamp == _stamp + 1 )
                    continue;
                const float32 stepCost = static_cast<float32>( grid.getCost( next._x, next._y ) ) * ( bDiagonal ? MathUtil::kSqrt2 : 1.0f );
                const float32 newCost  = costSoFar + stepCost;
                if ( nextStamp == _stamp && newCost >= _listCostSoFar[static_cast<size_t>( nextIndex )] )
                    continue;
                nextStamp                                        = _stamp;
                _listCostSoFar[static_cast<size_t>( nextIndex )] = newCost;
                _listParent[static_cast<size_t>( nextIndex )]    = current._index;
                _listOpen.push_back( OpenEntry{ newCost + computeHeuristic( next, query._goal ), nextIndex } );
                std::push_heap( _listOpen.begin(), _listOpen.end(), compareOpen );
            }
        }

        if ( bReached == false && query._bAcceptPartial == SW_FALSE )
            return GridPathResult::NoPath;

        for ( int32 index = bestIndex; index >= 0; index = _listParent[static_cast<size_t>( index )] )
        {
            outListCell.push_back( int2{ index % width, index / width } );
            if ( index == startIndex )
                break;
        }
        std::reverse( outListCell.begin(), outListCell.end() );
        if ( query._bSmooth != SW_FALSE )
            smoothPath( grid, outListCell );
        return bReached ? GridPathResult::Found : GridPathResult::Partial;
    }

    void GridPathfinder::smoothPath( const NavGrid& grid, vector<int2>& inoutListCell ) const
    {
        // 줄 당기기 — 지금 칸에서 시선이 닿는 가장 먼 칸으로 건너뛴다. 값이 다른 칸(길 · 늪)을 가로지를 수 있으므로 값이 다 같은 구간에서 가장 잘 맞는다.
        if ( inoutListCell.size() <= 2 )
            return;
        // 앞으로 한 칸씩 — 기준점에서 다음 칸이 안 보이면 지금 칸을 꺾는 점으로 남긴다. 시선 검사가 칸 수만큼이라 긴 경로도 선형이다.
        vector<int2> listSmoothed;
        listSmoothed.reserve( inoutListCell.size() );
        listSmoothed.push_back( inoutListCell[0] );
        size_t anchor = 0;
        for ( size_t cellIndex = 1; cellIndex + 1 < inoutListCell.size(); ++cellIndex )
        {
            if ( grid.hasLineOfSight( inoutListCell[anchor], inoutListCell[cellIndex + 1] ) )
                continue;
            listSmoothed.push_back( inoutListCell[cellIndex] );
            anchor = cellIndex;
        }
        listSmoothed.push_back( inoutListCell.back() );
        inoutListCell.swap( listSmoothed );
    }

    void GridPathfinder::makeWorldPath( const NavGrid& grid, const vector<int2>& listCell, vector<float3>& outListPoint )
    {
        outListPoint.clear();
        outListPoint.reserve( listCell.size() );
        for ( const int2& cell : listCell )
        {
            outListPoint.push_back( grid.computeCellCenter( cell ) );
        }
    }

    float32 GridPathfinder::computePathLength( const NavGrid& grid, const vector<int2>& listCell )
    {
        float32 length = 0.0f;
        for ( size_t cellIndex = 1; cellIndex < listCell.size(); ++cellIndex )
        {
            length += float3::getDistance( grid.computeCellCenter( listCell[cellIndex - 1] ), grid.computeCellCenter( listCell[cellIndex] ) );
        }
        return length;
    }
} // namespace sw
