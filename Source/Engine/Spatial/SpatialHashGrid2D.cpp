#include "pch.h"

#include "Engine/Spatial/SpatialHashGrid2D.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/Math.h"

namespace sw
{
    SpatialHashGrid2D::SpatialHashGrid2D( float32 cellSize )
        : _cellSize{ cellSize > 1.0f ? cellSize : constant::kDefaultSpatialCellSize }
        , _mapBucket{}
        , _mapHandleBound{}
    {
    }

    void SpatialHashGrid2D::insert( ObjectHandle handle, float32 minX, float32 minY, float32 maxX, float32 maxY )
    {
        if ( handle.isValid() == false )
            return;

        if ( _mapHandleBound.find( handle ) != _mapHandleBound.end() )
            remove( handle );

        const float32 normMinX = MathUtil::min( minX, maxX );
        const float32 normMaxX = MathUtil::max( minX, maxX );
        const float32 normMinY = MathUtil::min( minY, maxY );
        const float32 normMaxY = MathUtil::max( minY, maxY );

        const AABB2D bounds{ normMinX, normMinY, normMaxX, normMaxY };
        _mapHandleBound[handle] = bounds;

        const int32 startCellX = static_cast<int32>( MathUtil::floor( normMinX / _cellSize ) );
        const int32 endCellX   = static_cast<int32>( MathUtil::floor( normMaxX / _cellSize ) );
        const int32 startCellY = static_cast<int32>( MathUtil::floor( normMinY / _cellSize ) );
        const int32 endCellY   = static_cast<int32>( MathUtil::floor( normMaxY / _cellSize ) );

        for ( int32 cellX = startCellX; cellX <= endCellX; ++cellX )
        {
            for ( int32 cellY = startCellY; cellY <= endCellY; ++cellY )
            {
                const uint64 key = getCellKey( cellX, cellY );
                _mapBucket[key].push_back( handle );
            }
        }
    }

    void SpatialHashGrid2D::update( ObjectHandle handle, float32 minX, float32 minY, float32 maxX, float32 maxY )
    {
        insert( handle, minX, minY, maxX, maxY );
    }

    void SpatialHashGrid2D::remove( ObjectHandle handle )
    {
        auto boundIt = _mapHandleBound.find( handle );
        if ( boundIt == _mapHandleBound.end() )
            return;

        const AABB2D bounds = boundIt->second;
        _mapHandleBound.erase( boundIt );

        const int32 startCellX = static_cast<int32>( MathUtil::floor( bounds._minX / _cellSize ) );
        const int32 endCellX   = static_cast<int32>( MathUtil::floor( bounds._maxX / _cellSize ) );
        const int32 startCellY = static_cast<int32>( MathUtil::floor( bounds._minY / _cellSize ) );
        const int32 endCellY   = static_cast<int32>( MathUtil::floor( bounds._maxY / _cellSize ) );

        for ( int32 cellX = startCellX; cellX <= endCellX; ++cellX )
        {
            for ( int32 cellY = startCellY; cellY <= endCellY; ++cellY )
            {
                const uint64 key      = getCellKey( cellX, cellY );
                auto         bucketIt = _mapBucket.find( key );
                if ( bucketIt != _mapBucket.end() )
                {
                    auto& listHandle = bucketIt->second;
                    for ( size_t handleIndex = 0; handleIndex < listHandle.size(); ++handleIndex )
                    {
                        if ( listHandle[handleIndex] == handle )
                        {
                            listHandle[handleIndex] = listHandle.back();
                            listHandle.pop_back();
                            break;
                        }
                    }
                    if ( listHandle.empty() )
                        _mapBucket.erase( bucketIt );
                }
            }
        }
    }

    void SpatialHashGrid2D::clear()
    {
        _mapBucket.clear();
        _mapHandleBound.clear();
    }

    void SpatialHashGrid2D::queryAABB( float32 minX, float32 minY, float32 maxX, float32 maxY, vector<ObjectHandle>& outListHandle ) const
    {
        outListHandle.clear();

        const float32 normMinX = MathUtil::min( minX, maxX );
        const float32 normMaxX = MathUtil::max( minX, maxX );
        const float32 normMinY = MathUtil::min( minY, maxY );
        const float32 normMaxY = MathUtil::max( minY, maxY );

        const AABB2D queryBounds{ normMinX, normMinY, normMaxX, normMaxY };
        const int32  startCellX = static_cast<int32>( MathUtil::floor( normMinX / _cellSize ) );
        const int32  endCellX   = static_cast<int32>( MathUtil::floor( normMaxX / _cellSize ) );
        const int32  startCellY = static_cast<int32>( MathUtil::floor( normMinY / _cellSize ) );
        const int32  endCellY   = static_cast<int32>( MathUtil::floor( normMaxY / _cellSize ) );

        for ( int32 cellX = startCellX; cellX <= endCellX; ++cellX )
        {
            for ( int32 cellY = startCellY; cellY <= endCellY; ++cellY )
            {
                const uint64 key      = getCellKey( cellX, cellY );
                auto         bucketIt = _mapBucket.find( key );
                if ( bucketIt != _mapBucket.end() )
                {
                    for ( const ObjectHandle handle : bucketIt->second )
                    {
                        auto boundIt = _mapHandleBound.find( handle );
                        if ( boundIt != _mapHandleBound.end() )
                        {
                            if ( queryBounds.intersects( boundIt->second ) )
                                outListHandle.push_back( handle );
                        }
                    }
                }
            }
        }

        std::sort( outListHandle.begin(), outListHandle.end() );
        outListHandle.erase( std::unique( outListHandle.begin(), outListHandle.end() ), outListHandle.end() );
    }

    void SpatialHashGrid2D::queryCircle( float32 centerX, float32 centerY, float32 radius, vector<ObjectHandle>& outListHandle ) const
    {
        outListHandle.clear();

        const float32 radiusSq = radius * radius;
        const float32 minX     = centerX - radius;
        const float32 maxX     = centerX + radius;
        const float32 minY     = centerY - radius;
        const float32 maxY     = centerY + radius;

        const int32 startCellX = static_cast<int32>( MathUtil::floor( minX / _cellSize ) );
        const int32 endCellX   = static_cast<int32>( MathUtil::floor( maxX / _cellSize ) );
        const int32 startCellY = static_cast<int32>( MathUtil::floor( minY / _cellSize ) );
        const int32 endCellY   = static_cast<int32>( MathUtil::floor( maxY / _cellSize ) );

        for ( int32 cellX = startCellX; cellX <= endCellX; ++cellX )
        {
            for ( int32 cellY = startCellY; cellY <= endCellY; ++cellY )
            {
                const uint64 key      = getCellKey( cellX, cellY );
                auto         bucketIt = _mapBucket.find( key );
                if ( bucketIt != _mapBucket.end() )
                {
                    for ( const ObjectHandle handle : bucketIt->second )
                    {
                        auto boundIt = _mapHandleBound.find( handle );
                        if ( boundIt != _mapHandleBound.end() )
                        {
                            const AABB2D& b = boundIt->second;
                            const float2  center{ centerX, centerY };
                            const float2  closePoint = center.clamped( float2{ b._minX, b._minY }, float2{ b._maxX, b._maxY } );
                            if ( float2::getDistanceSquared( center, closePoint ) <= radiusSq )
                                outListHandle.push_back( handle );
                        }
                    }
                }
            }
        }

        std::sort( outListHandle.begin(), outListHandle.end() );
        outListHandle.erase( std::unique( outListHandle.begin(), outListHandle.end() ), outListHandle.end() );
    }

    void SpatialHashGrid2D::queryRay( float32 startX, float32 startY, float32 dirX, float32 dirY, float32 maxDist, vector<ObjectHandle>& outListHandle ) const
    {
        outListHandle.clear();

        float2 dir{ dirX, dirY };
        if ( dir.getLengthSquared() <= MathUtil::Epsilon || maxDist <= 0.0f )
            return;

        dir.normalize();
        const float32 ndx = dir._x;
        const float32 ndy = dir._y;

        int32 cellX = static_cast<int32>( MathUtil::floor( startX / _cellSize ) );
        int32 cellY = static_cast<int32>( MathUtil::floor( startY / _cellSize ) );

        const int32 stepX = ( ndx > 0.0f ) ? 1 : ( ( ndx < 0.0f ) ? -1 : 0 );
        const int32 stepY = ( ndy > 0.0f ) ? 1 : ( ( ndy < 0.0f ) ? -1 : 0 );

        const float32 nextBoundaryX = ( stepX > 0 ) ? static_cast<float32>( cellX + 1 ) * _cellSize : static_cast<float32>( cellX ) * _cellSize;
        const float32 nextBoundaryY = ( stepY > 0 ) ? static_cast<float32>( cellY + 1 ) * _cellSize : static_cast<float32>( cellY ) * _cellSize;

        float32 tMaxX = ( stepX != 0 ) ? ( nextBoundaryX - startX ) / ndx : MathUtil::MaxFloat;
        float32 tMaxY = ( stepY != 0 ) ? ( nextBoundaryY - startY ) / ndy : MathUtil::MaxFloat;

        const float32 tDeltaX = ( stepX != 0 ) ? ( _cellSize * static_cast<float32>( stepX ) ) / ndx : MathUtil::MaxFloat;
        const float32 tDeltaY = ( stepY != 0 ) ? ( _cellSize * static_cast<float32>( stepY ) ) / ndy : MathUtil::MaxFloat;

        float32          currentT     = 0.0f;
        constexpr uint32 kMaxRaySteps = 2048;
        uint32           stepCount    = 0;

        while ( currentT <= maxDist && stepCount++ < kMaxRaySteps )
        {
            const uint64 key      = getCellKey( cellX, cellY );
            auto         bucketIt = _mapBucket.find( key );

            if ( bucketIt != _mapBucket.end() )
            {
                for ( const ObjectHandle handle : bucketIt->second )
                {
                    auto boundIt = _mapHandleBound.find( handle );
                    if ( boundIt != _mapHandleBound.end() )
                    {
                        outListHandle.push_back( handle );
                    }
                }
            }

            if ( tMaxX < tMaxY )
            {
                currentT = tMaxX;
                cellX += stepX;
                tMaxX += tDeltaX;
            }
            else
            {
                currentT = tMaxY;
                cellY += stepY;
                tMaxY += tDeltaY;
            }
        }

        std::sort( outListHandle.begin(), outListHandle.end() );
        outListHandle.erase( std::unique( outListHandle.begin(), outListHandle.end() ), outListHandle.end() );
    }

} // namespace sw
