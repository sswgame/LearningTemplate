#include "pch.h"

#include "Engine/Scene/StreamingCellGrid.h"

#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"

namespace sw
{
    namespace
    {
        struct StreamingCellGridInternal
        {
            /** @brief 셀 번호의 한계(±2^30). int32 끝보다 안쪽이라 범위를 도는 `++x` 가 넘치지 않는다. */
            static constexpr int32   kMaxCellIndex      = 1 << 30;
            static constexpr float32 kMaxCellIndexFloat = static_cast<float32>( kMaxCellIndex );

            /** @brief 셀 단위 좌표(내림한 값)를 셀 번호로 포화 변환합니다 — 범위 밖 float → int32 변환은 정의되지 않은 동작이다. NaN 은 아래 끝으로 간다. */
            static int32 toCellIndex( float32 cellUnits )
            {
                if ( ( cellUnits > -kMaxCellIndexFloat ) == false )
                    return -kMaxCellIndex;
                if ( ( cellUnits < kMaxCellIndexFloat ) == false )
                    return kMaxCellIndex;
                return static_cast<int32>( cellUnits );
            }
        };
    } // namespace

    bool StreamingCellGrid::tryCreate( const StreamingCellGridDef& def, StreamingCellGrid& outGrid )
    {
        // 기본값으로 메우지 않는다 — 셀 크기가 없는 월드는 데이터가 빠진 것이다.
        if ( ( def._cellSize > 0.0f ) == false )
        {
            SW_LOG_ERROR_ID( "StreamingCellGrid.WorldCellGridCell", "World cell grid: cell size must be positive (got %#)", def._cellSize );
            return false;
        }
        if ( ( def._loadingRadius >= 0.0f ) == false )
        {
            SW_LOG_ERROR_ID( "StreamingCellGrid.WorldCellGridLoading", "World cell grid: loading radius must not be negative (got %#)", def._loadingRadius );
            return false;
        }
        outGrid._def = def;
        return true;
    }

    StreamingCellCoord StreamingCellGrid::computeCell( const float3& position ) const
    {
        StreamingCellCoord cell{};
        cell._x = StreamingCellGridInternal::toCellIndex( std::floor( position._x / _def._cellSize ) );
        cell._z = StreamingCellGridInternal::toCellIndex( std::floor( position._z / _def._cellSize ) );
        return cell;
    }

    StreamingCellBounds StreamingCellGrid::computeCellBounds( const StreamingCellCoord& cell ) const
    {
        StreamingCellBounds bounds{};
        bounds._minX = static_cast<float32>( cell._x ) * _def._cellSize;
        bounds._minZ = static_cast<float32>( cell._z ) * _def._cellSize;
        bounds._maxX = bounds._minX + _def._cellSize;
        bounds._maxZ = bounds._minZ + _def._cellSize;
        return bounds;
    }

    int64 StreamingCellGrid::countCoveredCells( const StreamingCellBounds& bounds ) const
    {
        if ( bounds.isValid() == false )
            return 0;
        const StreamingCellCoord minCell = computeCell( float3{ bounds._minX, 0.0f, bounds._minZ } );
        const StreamingCellCoord maxCell = computeCell( float3{ bounds._maxX, 0.0f, bounds._maxZ } );
        return ( static_cast<int64>( maxCell._x ) - minCell._x + 1 ) * ( static_cast<int64>( maxCell._z ) - minCell._z + 1 );
    }

    void StreamingCellGrid::collectCoveredCells( const StreamingCellBounds& bounds, vector<StreamingCellCoord>& outListCell ) const
    {
        outListCell.clear();
        if ( bounds.isValid() == false )
            return;
        const StreamingCellCoord minCell = computeCell( float3{ bounds._minX, 0.0f, bounds._minZ } );
        const StreamingCellCoord maxCell = computeCell( float3{ bounds._maxX, 0.0f, bounds._maxZ } );
        for ( int32 z = minCell._z; z <= maxCell._z; ++z )
        {
            for ( int32 x = minCell._x; x <= maxCell._x; ++x )
            {
                outListCell.push_back( StreamingCellCoord{ x, z } );
            }
        }
    }

    float32 StreamingCellGrid::computeCellDistanceSquared( const StreamingCellCoord& cell, const float3& position ) const
    {
        return computeCellBounds( cell ).computeDistanceSquared( position );
    }

    void StreamingCellGrid::computeCellsInRange( const float3& origin, vector<StreamingCellCoord>& outListCell ) const
    {
        computeCellsInRadius( origin, _def._loadingRadius, outListCell );
    }

    void StreamingCellGrid::computeCellsInRadius( const float3& origin, const float32 radius, vector<StreamingCellCoord>& outListCell ) const
    {
        outListCell.clear();
        const float3             minCorner{ origin._x - radius, origin._y, origin._z - radius };
        const float3             maxCorner{ origin._x + radius, origin._y, origin._z + radius };
        const StreamingCellCoord minCell       = computeCell( minCorner );
        const StreamingCellCoord maxCell       = computeCell( maxCorner );
        const float32            radiusSquared = radius * radius;
        for ( int32 z = minCell._z; z <= maxCell._z; ++z )
        {
            for ( int32 x = minCell._x; x <= maxCell._x; ++x )
            {
                const StreamingCellCoord cell{ x, z };
                if ( computeCellDistanceSquared( cell, origin ) <= radiusSquared )
                    outListCell.push_back( cell );
            }
        }
    }

    void StreamingCellGrid::findCrossCellReferences( const vector<StreamingCellObjectRecord>& listObject, const vector<StreamingCellReference>& listReference,
                                                     vector<StreamingCellReference>& outListViolation )
    {
        outListViolation.clear();
        unordered_map<uint64, size_t> mapObjectIndex;
        mapObjectIndex.reserve( listObject.size() );
        for ( size_t index = 0; index < listObject.size(); ++index )
        {
            mapObjectIndex.emplace( listObject[index]._objectID, index );
        }
        for ( const StreamingCellReference& reference : listReference )
        {
            const auto fromIt = mapObjectIndex.find( reference._fromObjectID );
            const auto toIt   = mapObjectIndex.find( reference._toObjectID );
            if ( fromIt == mapObjectIndex.end() || toIt == mapObjectIndex.end() )
            {
                outListViolation.push_back( reference );
                continue;
            }
            const StreamingCellObjectRecord& from = listObject[fromIt->second];
            const StreamingCellObjectRecord& to   = listObject[toIt->second];
            // 늘 올라 있는 쪽은 셀이 아니라 기본 묶음에 실린다 — 같은 셀 자리여도 셀의 오브젝트를 가리키면 그 셀이 내려갈 때 끊긴다.
            const bool bSameCell = from._bAlwaysLoaded == false && from._cell == to._cell;
            const bool bAllowed  = to._bAlwaysLoaded || bSameCell;
            if ( bAllowed == false )
                outListViolation.push_back( reference );
        }
    }
} // namespace sw
