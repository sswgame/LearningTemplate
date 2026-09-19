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
        void queryPoint( const float3& point, vector<SpatialElement3D>& outListElement ) const
        {
            queryRange( AABB{ point, point }, outListElement );
        }

        /** @brief 구체 영역에 교차하는 요소를 검색합니다. `outListElement` 의 기존 내용은 지워집니다. */
        void querySphere( const float3& center, float32 radius, vector<SpatialElement3D>& outListElement ) const
        {
            outListElement.clear();

            const AABB sphereBounds{ center - float3{ radius }, center + float3{ radius } };

            vector<SpatialElement3D> listCandidate;
            queryRange( sphereBounds, listCandidate );

            // 상자 위의 **가장 가까운 점**까지의 거리로 본다 — 형제 둘(`BVHTree3D::querySphere` ·
            // `SpatialHashGrid2D::queryCircle`)이 쓰는 것과 같은 정확한 판정이다. 예전에는 상자
            // 중심까지의 거리를 `radius + 가장 긴 반지름` 과 견줬는데, 상자의 외접구 반지름은
            // 대각선(가장 긴 반지름 x sqrt(3))이라 그 값이 **모자란다** — 상자 안에 완전히 들어
            // 있는 구조차 걸러졌다. 어림잡되 넉넉한 쪽이 아니라, 그냥 틀린 쪽이었다.
            const float32 radiusSq = radius * radius;
            for ( const SpatialElement3D& candidate : listCandidate )
            {
                const float3 closestPoint = center.clamped( candidate._bounds._min, candidate._bounds._max );
                if ( float3::getDistanceSquared( center, closestPoint ) <= radiusSq )
                    outListElement.push_back( candidate );
            }
        }
    };
} // namespace sw
