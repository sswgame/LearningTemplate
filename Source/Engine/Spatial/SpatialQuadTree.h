#pragma once
#include "Engine/Spatial/SpatialTree.h"

namespace sw
{
	/**
	 * @class SpatialQuadTree
	 * @brief 대규모 씬 컬링 및 고속 공간 검색을 위한 쿼드트리(QuadTree) 공간 분할 인덱서
	 * @details O(N) 선형 순회 대신 O(log N) 범위 쿼리 및 프러스텀 컬링을 제공합니다.
	 */
	class SpatialQuadTree : public SpatialTree<QuadTreeTraits>
	{
	public:
		using SpatialTree<QuadTreeTraits>::SpatialTree;

		/** @brief 2D 점 좌표를 포함하는 요소를 검색합니다. */
		void queryPoint( float32 pointX, float32 pointY, vector<SpatialElement>& listOutElements ) const
		{
			queryRange( AABB2D{ pointX, pointY, pointX, pointY }, listOutElements );
		}
	};
} // namespace sw
