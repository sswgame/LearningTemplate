#include "pch.h"

#include "Engine/Scene/WorldCellGrid.h"

#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "WorldCellGrid" );

    bool WorldCellGrid::tryCreate( const WorldCellGridDef& def, WorldCellGrid& outGrid )
    {
        // 기본값으로 메우지 않는다 — 셀 크기가 없는 월드는 데이터가 빠진 것이다.
        if ( ( def._cellSize > 0.0f ) == false )
        {
            SW_LOG_ERROR( "World cell grid: cell size must be positive (got %#)", def._cellSize );
            return false;
        }
        if ( ( def._loadingRadius >= 0.0f ) == false )
        {
            SW_LOG_ERROR( "World cell grid: loading radius must not be negative (got %#)", def._loadingRadius );
            return false;
        }
        outGrid._def = def;
        return true;
    }

    WorldCellCoord WorldCellGrid::computeCell( const float3& position ) const
    {
        WorldCellCoord cell{};
        cell._x = static_cast<int32>( std::floor( position._x / _def._cellSize ) );
        cell._z = static_cast<int32>( std::floor( position._z / _def._cellSize ) );
        return cell;
    }

    float32 WorldCellGrid::computeCellDistanceSquared( const WorldCellCoord& cell, const float3& position ) const
    {
        const float32 minX = static_cast<float32>( cell._x ) * _def._cellSize;
        const float32 minZ = static_cast<float32>( cell._z ) * _def._cellSize;
        const float32 maxX = minX + _def._cellSize;
        const float32 maxZ = minZ + _def._cellSize;
        const float32 dx   = ( position._x < minX ) ? ( minX - position._x ) : ( ( position._x > maxX ) ? ( position._x - maxX ) : 0.0f );
        const float32 dz   = ( position._z < minZ ) ? ( minZ - position._z ) : ( ( position._z > maxZ ) ? ( position._z - maxZ ) : 0.0f );
        return dx * dx + dz * dz;
    }

    void WorldCellGrid::computeCellsInRange( const float3& origin, vector<WorldCellCoord>& outListCell ) const
    {
        outListCell.clear();
        const float3         minCorner{ origin._x - _def._loadingRadius, origin._y, origin._z - _def._loadingRadius };
        const float3         maxCorner{ origin._x + _def._loadingRadius, origin._y, origin._z + _def._loadingRadius };
        const WorldCellCoord minCell       = computeCell( minCorner );
        const WorldCellCoord maxCell       = computeCell( maxCorner );
        const float32        radiusSquared = _def._loadingRadius * _def._loadingRadius;
        for ( int32 z = minCell._z; z <= maxCell._z; ++z )
        {
            for ( int32 x = minCell._x; x <= maxCell._x; ++x )
            {
                const WorldCellCoord cell{ x, z };
                if ( computeCellDistanceSquared( cell, origin ) <= radiusSquared )
                    outListCell.push_back( cell );
            }
        }
    }

    void WorldCellGrid::findCrossCellReferences( const vector<WorldCellObjectRecord>& listObject, const vector<WorldCellReference>& listReference,
                                                 vector<WorldCellReference>& outListViolation )
    {
        outListViolation.clear();
        unordered_map<uint64, size_t> mapObjectIndex;
        mapObjectIndex.reserve( listObject.size() );
        for ( size_t index = 0; index < listObject.size(); ++index )
        {
            mapObjectIndex.emplace( listObject[index]._objectID, index );
        }
        for ( const WorldCellReference& reference : listReference )
        {
            const auto fromIt = mapObjectIndex.find( reference._fromObjectID );
            const auto toIt   = mapObjectIndex.find( reference._toObjectID );
            if ( fromIt == mapObjectIndex.end() || toIt == mapObjectIndex.end() )
            {
                outListViolation.push_back( reference );
                continue;
            }
            const WorldCellObjectRecord& from     = listObject[fromIt->second];
            const WorldCellObjectRecord& to       = listObject[toIt->second];
            const bool                   bAllowed = to._bAlwaysLoaded || from._cell == to._cell;
            if ( bAllowed == false )
                outListViolation.push_back( reference );
        }
    }
} // namespace sw
