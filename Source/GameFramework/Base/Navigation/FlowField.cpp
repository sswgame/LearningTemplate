#include "pch.h"

#include "GameFramework/Base/Navigation/FlowField.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Navigation/NavGrid.h"
#include "GameFramework/Base/Utility/GridTopology.h"

namespace sw
{
    namespace
    {
        struct FlowFieldInternal
        {
            static constexpr float32 kDiagonalFactor = 1.41421356f;
            static constexpr float32 kUnreached      = -1.0f;

            /** @brief 대각선 걸음이 두 직교 이웃을 지나도 되는가(모서리 깎기 금지)입니다. */
            static bool canStep( const NavGrid& grid, const int2& from, int32 direction )
            {
                const int2 next = GridTopology::getNeighbor( from, direction );
                if ( grid.isWalkable( next ) == false )
                    return false;
                return direction < 4 || ( grid.isWalkable( next._x, from._y ) && grid.isWalkable( from._x, next._y ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FlowField::FlowField()
        : _listDistance{}
        , _listDirection{}
        , _listOpen{}
        , _width{ 0 }
        , _height{ 0 }
        , _pGrid{ nullptr }
        , _gridRevision{ 0xffffffffu }
    {
    }

    int32 FlowField::compute( const NavGrid& grid, const vector<int2>& listGoal )
    {
        _width                 = grid.getWidth();
        _height                = grid.getHeight();
        _pGrid                 = &grid;
        _gridRevision          = grid.getRevision();
        const size_t cellCount = static_cast<size_t>( _width * _height );
        _listDistance.assign( cellCount, FlowFieldInternal::kUnreached );
        _listDirection.assign( cellCount, kNoDirection );
        _listOpen.clear();
        const auto compareOpen = []( const OpenEntry& lhs, const OpenEntry& rhs )
        { return lhs._distance > rhs._distance; };

        for ( const int2& goal : listGoal )
        {
            if ( grid.isWalkable( goal ) == false )
                continue;
            _listDistance[static_cast<size_t>( grid.computeIndex( goal ) )] = 0.0f;
            _listOpen.push_back( OpenEntry{ 0.0f, grid.computeIndex( goal ) } );
        }
        std::make_heap( _listOpen.begin(), _listOpen.end(), compareOpen );

        // 거꾸로 퍼진다 — 이웃 n 에서 이 칸 c 로 오는 비용은 c 에 들어가는 값이다(경로 찾기와 같은 비용).
        int32 reachedCount = 0;
        while ( _listOpen.empty() == false )
        {
            std::pop_heap( _listOpen.begin(), _listOpen.end(), compareOpen );
            const OpenEntry current = _listOpen.back();
            _listOpen.pop_back();
            if ( current._distance > _listDistance[static_cast<size_t>( current._index )] )
                continue;
            ++reachedCount;
            const int2    cell{ current._index % _width, current._index / _width };
            const float32 enterCost = static_cast<float32>( grid.getCost( cell._x, cell._y ) );
            for ( int32 direction = 0; direction < 8; ++direction )
            {
                if ( FlowFieldInternal::canStep( grid, cell, direction ) == false )
                    continue;
                const int2    next      = GridTopology::getNeighbor( cell, direction );
                const int32   nextIndex = grid.computeIndex( next );
                const float32 distance  = current._distance + enterCost * ( direction >= 4 ? FlowFieldInternal::kDiagonalFactor : 1.0f );
                float32&      slot      = _listDistance[static_cast<size_t>( nextIndex )];
                if ( slot >= 0.0f && slot <= distance )
                    continue;
                slot = distance;
                _listOpen.push_back( OpenEntry{ distance, nextIndex } );
                std::push_heap( _listOpen.begin(), _listOpen.end(), compareOpen );
            }
        }

        // 방향 — 거리가 가장 작은 이웃(갈 수 있는 걸음만).
        for ( int32 y = 0; y < _height; ++y )
        {
            for ( int32 x = 0; x < _width; ++x )
            {
                const int32   index    = y * _width + x;
                const float32 distance = _listDistance[static_cast<size_t>( index )];
                if ( distance <= 0.0f )
                    continue; // 목적지이거나 닿지 못했다
                float32 bestDistance  = distance;
                int8    bestDirection = kNoDirection;
                for ( int32 direction = 0; direction < 8; ++direction )
                {
                    if ( FlowFieldInternal::canStep( grid, int2{ x, y }, direction ) == false )
                        continue;
                    const float32 neighborDistance = _listDistance[static_cast<size_t>( ( y + GridTopology::kArrOffsetY[direction] ) * _width + x + GridTopology::kArrOffsetX[direction] )];
                    if ( neighborDistance >= 0.0f && neighborDistance < bestDistance )
                    {
                        bestDistance  = neighborDistance;
                        bestDirection = static_cast<int8>( direction );
                    }
                }
                _listDirection[static_cast<size_t>( index )] = bestDirection;
            }
        }
        return reachedCount;
    }

    int32 FlowField::computeToCell( const NavGrid& grid, const int2& goal )
    {
        int2 target = goal;
        if ( grid.isWalkable( goal ) == false && grid.findNearestWalkable( goal, 16, target ) == false )
            target = goal;
        vector<int2> listGoal;
        listGoal.push_back( target );
        return compute( grid, listGoal );
    }

    bool FlowField::isReachable( const int2& cell ) const
    {
        return getDistance( cell ) >= 0.0f;
    }

    float32 FlowField::getDistance( const int2& cell ) const
    {
        if ( cell._x < 0 || cell._y < 0 || cell._x >= _width || cell._y >= _height )
            return FlowFieldInternal::kUnreached;
        return _listDistance[static_cast<size_t>( cell._y * _width + cell._x )];
    }

    int8 FlowField::getDirectionIndex( const int2& cell ) const
    {
        if ( cell._x < 0 || cell._y < 0 || cell._x >= _width || cell._y >= _height )
            return kNoDirection;
        return _listDirection[static_cast<size_t>( cell._y * _width + cell._x )];
    }

    int2 FlowField::getNextCell( const int2& cell ) const
    {
        const int8 direction = getDirectionIndex( cell );
        if ( direction == kNoDirection )
            return cell;
        return GridTopology::getNeighbor( cell, direction );
    }

    float3 FlowField::sampleDirection( const NavGrid& grid, const float3& worldPosition ) const
    {
        const int2 cell      = grid.computeCell( worldPosition );
        const int8 direction = getDirectionIndex( cell );
        if ( direction == kNoDirection )
            return float3{ 0.0f, 0.0f, 0.0f };
        // 다음 칸 가운데를 향한다 — 칸 안의 어디에 있든 칸 사이를 미끄러지지 않고 가운데로 모인다.
        const float3  toNext = grid.computeCellCenter( getNextCell( cell ) ) - worldPosition;
        const float3  flat{ toNext._x, 0.0f, toNext._z };
        const float32 length = flat.getLength();
        return length > 1.0e-5f ? flat * ( 1.0f / length ) : float3{ 0.0f, 0.0f, 0.0f };
    }

    bool FlowField::isStale( const NavGrid& grid ) const
    {
        return &grid != _pGrid || grid.getRevision() != _gridRevision || grid.getWidth() != _width || grid.getHeight() != _height;
    }
} // namespace sw
