#include "pch.h"

#include "Engine/Spatial/SpatialHashGrid2D.h"

#include "Core/Math/Math.h"

#include <algorithm>

namespace sw
{
	SpatialHashGrid2D::SpatialHashGrid2D( float32 cellSize )
		: _cellSize{ cellSize > 1.0f ? cellSize : 64.0f }
		, _mapBuckets{}
		, _mapHandleBounds{}
	{
	}

	uint64 SpatialHashGrid2D::getCellKey( int32 cellX, int32 cellY ) const
	{
		return ( static_cast<uint64>( static_cast<uint32>( cellX ) ) << 32 ) |
			   ( static_cast<uint64>( static_cast<uint32>( cellY ) ) );
	}

	void SpatialHashGrid2D::insert( ObjectHandle handle, float32 minX, float32 minY, float32 maxX, float32 maxY )
	{
		if ( handle.isValid() == false )
			return;

		if ( _mapHandleBounds.find( handle ) != _mapHandleBounds.end() )
			remove( handle );

		const float32 normMinX = MathUtil::min( minX, maxX );
		const float32 normMaxX = MathUtil::max( minX, maxX );
		const float32 normMinY = MathUtil::min( minY, maxY );
		const float32 normMaxY = MathUtil::max( minY, maxY );

		const AABB2D bounds{ normMinX, normMinY, normMaxX, normMaxY };
		_mapHandleBounds[handle] = bounds;

		const int32 startCellX = static_cast<int32>( MathUtil::floor( normMinX / _cellSize ) );
		const int32 endCellX   = static_cast<int32>( MathUtil::floor( normMaxX / _cellSize ) );
		const int32 startCellY = static_cast<int32>( MathUtil::floor( normMinY / _cellSize ) );
		const int32 endCellY   = static_cast<int32>( MathUtil::floor( normMaxY / _cellSize ) );

		for ( int32 cellX = startCellX; cellX <= endCellX; ++cellX )
		{
			for ( int32 cellY = startCellY; cellY <= endCellY; ++cellY )
			{
				const uint64 key = getCellKey( cellX, cellY );
				_mapBuckets[key].push_back( handle );
			}
		}
	}

	void SpatialHashGrid2D::update( ObjectHandle handle, float32 minX, float32 minY, float32 maxX, float32 maxY )
	{
		insert( handle, minX, minY, maxX, maxY );
	}

	void SpatialHashGrid2D::remove( ObjectHandle handle )
	{
		auto boundIt = _mapHandleBounds.find( handle );
		if ( boundIt == _mapHandleBounds.end() )
			return;

		const AABB2D bounds = boundIt->second;
		_mapHandleBounds.erase( boundIt );

		const int32 startCellX = static_cast<int32>( MathUtil::floor( bounds._minX / _cellSize ) );
		const int32 endCellX   = static_cast<int32>( MathUtil::floor( bounds._maxX / _cellSize ) );
		const int32 startCellY = static_cast<int32>( MathUtil::floor( bounds._minY / _cellSize ) );
		const int32 endCellY   = static_cast<int32>( MathUtil::floor( bounds._maxY / _cellSize ) );

		for ( int32 cellX = startCellX; cellX <= endCellX; ++cellX )
		{
			for ( int32 cellY = startCellY; cellY <= endCellY; ++cellY )
			{
				const uint64 key	  = getCellKey( cellX, cellY );
				auto		 bucketIt = _mapBuckets.find( key );
				if ( bucketIt != _mapBuckets.end() )
				{
					auto& listHandles = bucketIt->second;
					for ( size_t handleIndex = 0; handleIndex < listHandles.size(); ++handleIndex )
					{
						if ( listHandles[handleIndex] == handle )
						{
							listHandles[handleIndex] = listHandles.back();
							listHandles.pop_back();
							break;
						}
					}
					if ( listHandles.empty() )
						_mapBuckets.erase( bucketIt );
				}
			}
		}
	}

	void SpatialHashGrid2D::clear()
	{
		_mapBuckets.clear();
		_mapHandleBounds.clear();
	}

	void SpatialHashGrid2D::queryAABB( float32 minX, float32 minY, float32 maxX, float32 maxY, vector<ObjectHandle>& outHandles ) const
	{
		const float32 normMinX = MathUtil::min( minX, maxX );
		const float32 normMaxX = MathUtil::max( minX, maxX );
		const float32 normMinY = MathUtil::min( minY, maxY );
		const float32 normMaxY = MathUtil::max( minY, maxY );

		const AABB2D queryBounds{ normMinX, normMinY, normMaxX, normMaxY };
		const int32	 startCellX = static_cast<int32>( MathUtil::floor( normMinX / _cellSize ) );
		const int32	 endCellX	= static_cast<int32>( MathUtil::floor( normMaxX / _cellSize ) );
		const int32	 startCellY = static_cast<int32>( MathUtil::floor( normMinY / _cellSize ) );
		const int32	 endCellY	= static_cast<int32>( MathUtil::floor( normMaxY / _cellSize ) );

		for ( int32 cellX = startCellX; cellX <= endCellX; ++cellX )
		{
			for ( int32 cellY = startCellY; cellY <= endCellY; ++cellY )
			{
				const uint64 key	  = getCellKey( cellX, cellY );
				auto		 bucketIt = _mapBuckets.find( key );
				if ( bucketIt != _mapBuckets.end() )
				{
					for ( const ObjectHandle handle : bucketIt->second )
					{
						auto boundIt = _mapHandleBounds.find( handle );
						if ( boundIt != _mapHandleBounds.end() )
						{
							if ( queryBounds.intersects( boundIt->second ) )
							{
								if ( std::find( outHandles.begin(), outHandles.end(), handle ) == outHandles.end() )
									outHandles.push_back( handle );
							}
						}
					}
				}
			}
		}
	}

	void SpatialHashGrid2D::queryCircle( float32 centerX, float32 centerY, float32 radius, vector<ObjectHandle>& outHandles ) const
	{
		const float32 radiusSq = radius * radius;
		const float32 minX	   = centerX - radius;
		const float32 maxX	   = centerX + radius;
		const float32 minY	   = centerY - radius;
		const float32 maxY	   = centerY + radius;

		const int32 startCellX = static_cast<int32>( MathUtil::floor( minX / _cellSize ) );
		const int32 endCellX   = static_cast<int32>( MathUtil::floor( maxX / _cellSize ) );
		const int32 startCellY = static_cast<int32>( MathUtil::floor( minY / _cellSize ) );
		const int32 endCellY   = static_cast<int32>( MathUtil::floor( maxY / _cellSize ) );

		for ( int32 cellX = startCellX; cellX <= endCellX; ++cellX )
		{
			for ( int32 cellY = startCellY; cellY <= endCellY; ++cellY )
			{
				const uint64 key	  = getCellKey( cellX, cellY );
				auto		 bucketIt = _mapBuckets.find( key );
				if ( bucketIt != _mapBuckets.end() )
				{
					for ( const ObjectHandle handle : bucketIt->second )
					{
						auto boundIt = _mapHandleBounds.find( handle );
						if ( boundIt != _mapHandleBounds.end() )
						{
							const AABB2D& b		 = boundIt->second;
							const float32 closeX = MathUtil::clamp( centerX, b._minX, b._maxX );
							const float32 closeY = MathUtil::clamp( centerY, b._minY, b._maxY );
							const float32 dx	 = centerX - closeX;
							const float32 dy	 = centerY - closeY;
							if ( ( dx * dx + dy * dy ) <= radiusSq )
							{
								if ( std::find( outHandles.begin(), outHandles.end(), handle ) == outHandles.end() )
									outHandles.push_back( handle );
							}
						}
					}
				}
			}
		}
	}

	void SpatialHashGrid2D::queryRay( float32 startX, float32 startY, float32 dirX, float32 dirY, float32 maxDist, vector<ObjectHandle>& outHandles ) const
	{
		const float32 len = MathUtil::sqrt( dirX * dirX + dirY * dirY );
		if ( len <= 0.0001f || maxDist <= 0.0f )
			return;

		const float32 ndx = dirX / len;
		const float32 ndy = dirY / len;

		int32 cellX = static_cast<int32>( MathUtil::floor( startX / _cellSize ) );
		int32 cellY = static_cast<int32>( MathUtil::floor( startY / _cellSize ) );

		const int32 stepX = ( ndx > 0.0f ) ? 1 : ( ( ndx < 0.0f ) ? -1 : 0 );
		const int32 stepY = ( ndy > 0.0f ) ? 1 : ( ( ndy < 0.0f ) ? -1 : 0 );

		const float32 nextBoundaryX = ( stepX > 0 ) ? static_cast<float32>( cellX + 1 ) * _cellSize : static_cast<float32>( cellX ) * _cellSize;
		const float32 nextBoundaryY = ( stepY > 0 ) ? static_cast<float32>( cellY + 1 ) * _cellSize : static_cast<float32>( cellY ) * _cellSize;

		float32 tMaxX = ( stepX != 0 ) ? ( nextBoundaryX - startX ) / ndx : 1e30f;
		float32 tMaxY = ( stepY != 0 ) ? ( nextBoundaryY - startY ) / ndy : 1e30f;

		const float32 tDeltaX = ( stepX != 0 ) ? ( _cellSize * static_cast<float32>( stepX ) ) / ndx : 1e30f;
		const float32 tDeltaY = ( stepY != 0 ) ? ( _cellSize * static_cast<float32>( stepY ) ) / ndy : 1e30f;

		float32 currentT = 0.0f;

		while ( currentT <= maxDist )
		{
			const uint64 key	  = getCellKey( cellX, cellY );
			auto		 bucketIt = _mapBuckets.find( key );

			if ( bucketIt != _mapBuckets.end() )
			{
				for ( const ObjectHandle handle : bucketIt->second )
				{
					auto boundIt = _mapHandleBounds.find( handle );
					if ( boundIt != _mapHandleBounds.end() )
					{
						if ( std::find( outHandles.begin(), outHandles.end(), handle ) == outHandles.end() )
							outHandles.push_back( handle );
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
	}

	float32 SpatialHashGrid2D::getCellSize() const
	{
		return _cellSize;
	}

	size_t SpatialHashGrid2D::getHandleCount() const
	{
		return _mapHandleBounds.size();
	}

	size_t SpatialHashGrid2D::getActiveBucketCount() const
	{
		return _mapBuckets.size();
	}
} // namespace sw
