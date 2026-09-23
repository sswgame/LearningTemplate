#pragma once
#include "Engine/Spatial/SpatialTree.h"

namespace sw
{
    /**
     * @class SpatialQuadTree
     * @brief 큰 씬의 컬링과 공간 검색을 빠르게 하는 쿼드트리(QuadTree) 공간 분할 색인입니다.
     * @details O(N) 선형 순회 대신 O(log N) 범위 질의와 절두체 컬링을 제공합니다.
     */
    class SpatialQuadTree : public SpatialTree<QuadTreeTraits>
    {
    public:
        using SpatialTree<QuadTreeTraits>::SpatialTree;

        /** @brief 2D 점을 포함하는 요소를 찾습니다. */
        void queryPoint( float32 pointX, float32 pointY, vector<SpatialElement>& outListElement ) const
        {
            queryRange( AABB2D{
                            float2{pointX, pointY},
                            float2{pointX, pointY}
            },
                        outListElement );
        }
    };
} // namespace sw
