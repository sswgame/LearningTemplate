#include "pch.h"

#include "Core/Network/Replication/NetInterestGrid.h"

#include "Core/Container/SlotHandle.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "NetInterestGrid" );

    namespace
    {
        /**
         * @brief 엔티티 id 를 공간 해시의 키로 바꿉니다.
         * @details 격자는 키를 비교하고 해시하기만 하므로 세대는 항상 1이다. 세대 0은 무효 핸들이라 격자가 받지 않는다.
         */
        SlotHandle makeInterestKey( uint32 entityID )
        {
            return SlotHandle::make( entityID, 1u );
        }
    } // namespace

    NetInterestGrid::NetInterestGrid()
        : _grid{}
        , _mapPosition{}
        , _cellSize{ 0.0f }
    {
    }

    bool NetInterestGrid::reset( float32 cellSize )
    {
        // 칸 크기를 대신 정하지 않는다 — 없으면 데이터가 빠진 것이다.
        if ( ( cellSize > 0.0f ) == false )
        {
            SW_LOG_ERROR( "Net interest grid: cell size must be positive (got %#)", cellSize );
            return false;
        }
        _cellSize = cellSize;
        _grid     = SpatialHashGrid2D{ cellSize };
        _mapPosition.clear();
        return true;
    }

    void NetInterestGrid::setPosition( uint32 entityID, const float3& position )
    {
        if ( isReady() == false )
            return;
        _mapPosition[entityID] = position;
        _grid.update( makeInterestKey( entityID ), position._x, position._z, position._x, position._z );
    }

    void NetInterestGrid::remove( uint32 entityID )
    {
        if ( _mapPosition.erase( entityID ) > 0 )
            _grid.remove( makeInterestKey( entityID ) );
    }

    void NetInterestGrid::clearPositions()
    {
        _mapPosition.clear();
        _grid.clear();
    }

    const float3* NetInterestGrid::findPosition( uint32 entityID ) const
    {
        const auto positionIter = _mapPosition.find( entityID );
        return positionIter != _mapPosition.end() ? &positionIter->second : nullptr;
    }

    void NetInterestGrid::queryCircle( const float3& center, float32 radius, vector<SlotHandle>& inoutListScratchKey, vector<uint32>& outListEntityID ) const
    {
        outListEntityID.clear();
        inoutListScratchKey.clear();
        if ( isReady() == false )
            return;
        // 자리는 점이라 해시의 원 판정이 곧 거리 판정이다(경계 상자가 점이다).
        _grid.queryCircle( center._x, center._z, radius, inoutListScratchKey );
        outListEntityID.reserve( inoutListScratchKey.size() );
        for ( const SlotHandle key : inoutListScratchKey )
        {
            outListEntityID.push_back( key.index() );
        }
    }

    bool NetInterestGrid::isInInterest( const float3& center, const float3& position, float32 enterRadius, float32 leaveRadius, bool bWasVisible )
    {
        const float32 distance = float3::getHorizontalDistance( center, position );
        return distance <= ( bWasVisible ? leaveRadius : enterRadius );
    }
} // namespace sw
