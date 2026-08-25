#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/ECS/Entity.h"
#include "Engine/Spatial/SpatialTree.h"

namespace sw
{
	/**
	 * @brief 대규모 2D/2.5D 엔티티를 위한 고속 O(1) 공간 분할 해시 그리드
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

		void insert( Entity entity, float32 minX, float32 minY, float32 maxX, float32 maxY );
		void update( Entity entity, float32 minX, float32 minY, float32 maxX, float32 maxY );
		void remove( Entity entity );
		void clear();

		void queryAABB( float32 minX, float32 minY, float32 maxX, float32 maxY, vector<Entity>& outEntities ) const;
		void queryCircle( float32 centerX, float32 centerY, float32 radius, vector<Entity>& outEntities ) const;
		void queryRay( float32 startX, float32 startY, float32 dirX, float32 dirY, float32 maxDist, vector<Entity>& outEntities ) const;

		float32 getCellSize() const;
		size_t	getEntityCount() const;
		size_t	getActiveBucketCount() const;

	private:
		uint64 getCellKey( int32 cellX, int32 cellY ) const;

		float32								  _cellSize;
		unordered_map<uint64, vector<Entity>> _mapBuckets;
		unordered_map<Entity, AABB2D>		  _mapEntityBounds;
	};
} // namespace sw
