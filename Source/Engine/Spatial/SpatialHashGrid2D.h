#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ObjectHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Spatial/SpatialTree.h"

namespace sw
{
	/**
	 * @brief 대규모 2D/2.5D 객체를 위한 고속 O(1) 공간 분할 해시 그리드
	 */
	class SW_API SpatialHashGrid2D
	{
	public:
		explicit SpatialHashGrid2D( float32 cellSize = 64.0f );
		~SpatialHashGrid2D()										 = default;
		SpatialHashGrid2D( const SpatialHashGrid2D& )				 = default;
		SpatialHashGrid2D& operator=( const SpatialHashGrid2D& )	 = default;
		SpatialHashGrid2D( SpatialHashGrid2D&& ) noexcept			 = default;
		SpatialHashGrid2D& operator=( SpatialHashGrid2D&& ) noexcept = default;

		void insert( ObjectHandle handle, float32 minX, float32 minY, float32 maxX, float32 maxY );
		void update( ObjectHandle handle, float32 minX, float32 minY, float32 maxX, float32 maxY );
		void remove( ObjectHandle handle );
		void clear();

		void queryAABB( float32 minX, float32 minY, float32 maxX, float32 maxY, vector<ObjectHandle>& outListHandle ) const;
		void queryCircle( float32 centerX, float32 centerY, float32 radius, vector<ObjectHandle>& outListHandle ) const;
		void queryRay( float32 startX, float32 startY, float32 dirX, float32 dirY, float32 maxDist, vector<ObjectHandle>& outListHandle ) const;

		float32 getCellSize() const { return _cellSize; }
		size_t	getHandleCount() const { return _mapHandleBound.size(); }
		size_t	getActiveBucketCount() const { return _mapBucket.size(); }

	private:
		uint64 getCellKey( int32 cellX, int32 cellY ) const
		{
			return ( static_cast<uint64>( static_cast<uint32>( cellX ) ) << 32 ) |
				   ( static_cast<uint64>( static_cast<uint32>( cellY ) ) );
		}

		float32										_cellSize;
		unordered_map<uint64, vector<ObjectHandle>> _mapBucket;
		unordered_map<ObjectHandle, AABB2D>			_mapHandleBound;
	};
} // namespace sw
