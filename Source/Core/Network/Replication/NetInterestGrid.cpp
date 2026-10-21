#include "pch.h"

#include "Core/Network/Replication/NetInterestGrid.h"

namespace sw
{
    NetInterestGrid::NetInterestGrid()
        : _grid{}
        , _enterRadius{ 0.0f }
        , _leaveRadius{ 0.0f }
    {
    }

    bool NetInterestGrid::initialize( float32 cellSize, float32 enterRadius, float32 leaveRadius, string& outError )
    {
        const bool bBadValue = cellSize <= 0.0f || enterRadius < 0.0f || leaveRadius < 0.0f;
        if ( bBadValue )
        {
            outError = "cell size must be positive and radii must not be negative";
            return false;
        }
        // 나가는 반경을 들어오는 반경으로 올려 받지 않는다 — 히스테리시스가 없는 설정은 설정 결함이다.
        if ( leaveRadius < enterRadius )
        {
            outError = "leave radius is smaller than enter radius";
            return false;
        }
        _grid        = SpatialHashGrid2D{ cellSize };
        _enterRadius = enterRadius;
        _leaveRadius = leaveRadius;
        return true;
    }

    void NetInterestGrid::setPoint( uint32 entityID, const float3& position )
    {
        _grid.update( makeKey( entityID ), position._x, position._z, position._x, position._z );
    }

    void NetInterestGrid::removePoint( uint32 entityID )
    {
        _grid.remove( makeKey( entityID ) );
    }

    void NetInterestGrid::queryEntering( const float3& center, vector<SlotHandle>& outListKey ) const
    {
        _grid.queryCircle( center._x, center._z, _enterRadius, outListKey );
    }

    bool NetInterestGrid::isOutsideLeaveRadius( const float3& center, const float3& position ) const
    {
        return float3::getHorizontalDistance( center, position ) > _leaveRadius;
    }
} // namespace sw
