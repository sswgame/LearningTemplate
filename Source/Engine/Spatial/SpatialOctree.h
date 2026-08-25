#pragma once
#include "Engine/Spatial/SpatialTree.h"

namespace sw
{
	/**
	 * @class SpatialOctree
	 * @brief 대규모 3D 씬 프러스텀 컬링 및 고속 공간 검색을 위한 8진 트리(Octree) 인덱서
	 */
	class SpatialOctree : public SpatialTree<OctreeTraits>
	{
	public:
		using SpatialTree<OctreeTraits>::SpatialTree;

		/** @brief 3D 점 좌표를 포함하는 요소를 검색합니다. */
		void queryPoint( const float3& point, vector<SpatialElement3D>& listOutElements ) const
		{
			queryRange( AABB3D{ point, point }, listOutElements );
		}

		/** @brief 구체 영역에 교차하는 요소를 검색합니다. */
		void querySphere( const float3& center, float32 radius, vector<SpatialElement3D>& listOutElements ) const
		{
			const AABB3D sphereBounds{
				float3{center._x - radius, center._y - radius, center._z - radius},
				float3{center._x + radius, center._y + radius, center._z + radius}
			 };

			vector<SpatialElement3D> listCandidates;
			queryRange( sphereBounds, listCandidates );

			for ( const SpatialElement3D& candidate : listCandidates )
			{
				const float3  elemCenter = candidate._bounds.getCenter();
				const float32 diffX		 = elemCenter._x - center._x;
				const float32 diffY		 = elemCenter._y - center._y;
				const float32 diffZ		 = elemCenter._z - center._z;
				const float32 distSq	 = diffX * diffX + diffY * diffY + diffZ * diffZ;

				const float3  extents	= candidate._bounds.getExtents();
				const float32 maxExtent = MathUtil::max( extents._x, MathUtil::max( extents._y, extents._z ) );

				if ( distSq <= ( radius + maxExtent ) * ( radius + maxExtent ) )
					listOutElements.push_back( candidate );
			}
		}
	};
} // namespace sw
