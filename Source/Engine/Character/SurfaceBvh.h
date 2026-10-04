/**
 * @file SurfaceBvh.h
 * @brief 삼각형 표면 여럿을 한 BVH(`BVHTree3D`)에 담아 광선 · 가장 가까운 점을 묻는 구조입니다. 피팅이 겹마다 한 번 짓고 연산들이 나눠 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Character/CharacterGeometry.h"
#include "Engine/Spatial/BVHTree3D.h"

namespace sw
{
    /**
     * @brief 삼각형 표면 BVH 입니다. 표면(부품)마다 위치 · 인덱스를 복사해 들고, 삼각형 하나가 잎 하나입니다.
     * @details 질의는 const 지만 후보 목록을 멤버 스크래치에 받으므로 **한 인스턴스를 여러 스레드가 동시에 묻지 않습니다**.
     *          위치가 바뀌면(`setSurfacePositions`) `build` 를 다시 부릅니다.
     */
    class SW_API SurfaceBvh
    {
    public:
        SurfaceBvh();

        /** @brief 표면을 모두 비웁니다. */
        void clear();
        /** @brief 표면 하나를 더하고 그 번호를 돌려줍니다. 삼각형은 @p part 번호를 달고 나옵니다. `build` 전에는 질의에 보이지 않습니다. */
        uint32 addSurface( uint16 part, vector_reference<const float3> listPosition, vector_reference<const uint32> listIndex );
        /** @brief 표면의 위치를 바꿉니다(정점 수가 같아야 합니다). 질의에 보이려면 `build` 를 다시 부릅니다. */
        void setSurfacePositions( uint32 surfaceIndex, vector_reference<const float3> listPosition );
        /** @brief 지금 표면들로 BVH 를 다시 짓습니다. */
        void build();

        /** @brief 가장 가까운 교차를 찾습니다. @p partFilter 가 `kNoPart` 가 아니면 그 표면만 봅니다. */
        bool findNearestHit( const float3& origin, const float3& direction, float32 maxDistance, GeometryRayHit& outHit,
                             uint16 partFilter = CharacterGeometryConstant::kNoPart ) const;
        /** @brief 모든 교차를 거리 순으로 채웁니다(비우고 채움). */
        void collectHits( const float3& origin, const float3& direction, float32 maxDistance, vector<GeometryRayHit>& outListHit,
                          uint16 partFilter = CharacterGeometryConstant::kNoPart ) const;
        /** @brief @p maxDistance 안에서 가장 가까운 표면 점을 찾습니다. */
        bool findClosestPoint( const float3& point, float32 maxDistance, GeometryClosestPoint& outClosest,
                               uint16 partFilter = CharacterGeometryConstant::kNoPart ) const;

        /** @brief 지은 삼각형 수입니다. */
        uint32 getTriangleCount() const { return static_cast<uint32>( _listTriangleSurface.size() ); }
        /** @brief 표면 수입니다. */
        uint32 getSurfaceCount() const { return static_cast<uint32>( _listSurface.size() ); }

    private:
        struct Surface
        {
            vector<float3> _listPosition{};
            vector<uint32> _listIndex{};
            uint16         _part{ 0 };
        };

        void getTriangle( uint32 globalTriangle, float3& outA, float3& outB, float3& outC ) const;

    private:
        BVHTree3D                  _tree;
        vector<Surface>            _listSurface;
        vector<uint32>             _listTriangleSurface; /**< 전역 삼각형 → 표면 번호 */
        vector<uint32>             _listTriangleLocal;   /**< 전역 삼각형 → 표면 안 삼각형 번호 */
        mutable vector<SlotHandle> _listScratchHandle;   /**< 질의 후보 스크래치 */
    };
} // namespace sw
