#include "pch.h"

#include "Engine/Spatial/SpatialHashGrid2D.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/Math.h"

namespace sw
{
	SpatialHashGrid2D::SpatialHashGrid2D( float32 cellSize )
		: _cellSize{ cellSize > 1.0f ? cellSize : 64.0f }
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
				const uint64 key	  = getCellKey( cellX, cellY );
				auto		 bucketIt = _mapBucket.find( key );
				if ( bucketIt != _mapBucket.end() )
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
				auto		 bucketIt = _mapBucket.find( key );
				if ( bucketIt != _mapBucket.end() )
				{
					for ( const ObjectHandle handle : bucketIt->second )
					{
						auto boundIt = _mapHandleBound.find( handle );
						if ( boundIt != _mapHandleBound.end() )
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
				auto		 bucketIt = _mapBucket.find( key );
				if ( bucketIt != _mapBucket.end() )
				{
					for ( const ObjectHandle handle : bucketIt->second )
					{
						auto boundIt = _mapHandleBound.find( handle );
						if ( boundIt != _mapHandleBound.end() )
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
			auto		 bucketIt = _mapBucket.find( key );

			if ( bucketIt != _mapBucket.end() )
			{
				for ( const ObjectHandle handle : bucketIt->second )
				{
					auto boundIt = _mapHandleBound.find( handle );
					if ( boundIt != _mapHandleBound.end() )
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

} // namespace sw
