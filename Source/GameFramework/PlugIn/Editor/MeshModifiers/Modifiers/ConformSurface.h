/**
 * @file ConformSurface.h
 * @brief Conform 계열이 쓰는 삼각형 표면 BVH 입니다. 질의가 상태 없이 const 라서 정점 덩어리마다 다른 스레드에서 물을 수 있고, 같은 삼각형 목록이면 BVH 를 다시 만들지 않습니다.
 * @details 엔진의 `SurfaceBvh` 는 질의 후보를 멤버 스크래치에 받아 한 스레드만 묻는다. 이 클래스는 같은 규칙(상자 여유 · 양면 교차 · 최근접점)을 쓰되
 *          후보 목록을 부르는 쪽이 넘기게 해서 그 제약을 없앴다. 모디파이어 인스턴스마다 하나를 가지고(`mutable`), 적용 때마다
 *          `assign` 으로 삼각형을 넘기면 지난번과 한 바이트도 다르지 않을 때만 BVH 를 재사용한다 — 해시가 아니라 값 비교라 충돌로 낡은 BVH 를 쓰는 일이 없다.
 *          `assign` 은 한 스레드에서(적용 시작에), 질의는 여러 스레드에서 부른다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/Spatial/BVHTree3D.h"

#include "Engine/Character/Fit/CharacterGeometry.h"

namespace sw::editor
{
    /** @brief 삼각형 목록(정점 3 개씩) 하나의 BVH 입니다. */
    class ConformSurface
    {
    public:
        ConformSurface()
            : _listTriangle{}
            , _tree{}
            , _buildCount{ 0 }
        {
        }

        /**
         * @brief 삼각형 목록을 넘깁니다. 지난번 목록과 같으면 BVH 를 그대로 두고, 다르면 다시 만듭니다.
         * @return BVH 를 새로 만들었으면 true 입니다.
         */
        bool assign( const vector<float3>& listTriangle )
        {
            const size_t byteCount = listTriangle.size() * sizeof( float3 );
            const bool   bSameSize = _buildCount > 0 && listTriangle.size() == _listTriangle.size();
            if ( bSameSize && std::memcmp( listTriangle.data(), _listTriangle.data(), byteCount ) == 0 )
                return false;
            _listTriangle = listTriangle;
            _tree.clear();
            const float3 padding( kBoundsPadding );
            for ( uint32 triangle = 0; static_cast<size_t>( triangle ) * 3 + 2 < _listTriangle.size(); ++triangle )
            {
                const float3& a = _listTriangle[triangle * 3];
                const float3& b = _listTriangle[triangle * 3 + 1];
                const float3& c = _listTriangle[triangle * 3 + 2];
                AABB          bounds;
                bounds._min = float3::min( float3::min( a, b ), c ) - padding;
                bounds._max = float3::max( float3::max( a, b ), c ) + padding;
                (void)_tree.insert( SlotHandle::make( triangle, 1 ), bounds );
            }
            ++_buildCount;
            return true;
        }

        /** @brief `assign` 이 BVH 를 실제로 만든 횟수입니다(캐시 적중 확인용). */
        uint32 getBuildCount() const { return _buildCount; }
        uint32 getTriangleCount() const { return static_cast<uint32>( _listTriangle.size() / 3 ); }

        /**
         * @brief 가장 가까운 교차를 찾습니다(양면). @p inoutListScratch 는 부르는 스레드가 가진 후보 버퍼입니다.
         * @details `SurfaceBvh::findNearestHit` 와 같은 규칙이다. 상태를 바꾸지 않으므로 스크래치만 다르면 여러 스레드가 동시에 물어도 된다.
         */
        bool findNearestHit( const float3& origin, const float3& direction, float32 maxDistance, vector<SlotHandle>& inoutListScratch, GeometryRayHit& outHit ) const
        {
            const float3 unitDirection = CharacterGeometryUtil::makeUnitOr( direction, float3::Zero );
            if ( unitDirection == float3::Zero )
                return false;
            _tree.queryRay( origin, unitDirection, maxDistance, inoutListScratch );
            bool    bFound       = false;
            float32 bestDistance = maxDistance;
            for ( const SlotHandle handle : inoutListScratch )
            {
                float3 a;
                float3 b;
                float3 c;
                getTriangle( handle.index(), a, b, c );
                float32 distance   = 0.0f;
                float32 u          = 0.0f;
                float32 v          = 0.0f;
                bool    bFrontFace = false;
                if ( CharacterGeometryUtil::intersectRayTriangle( origin, unitDirection, a, b, c, distance, u, v, bFrontFace ) == false )
                    continue;
                if ( distance > bestDistance )
                    continue;
                bestDistance       = distance;
                bFound             = true;
                outHit._distance   = distance;
                outHit._baryU      = u;
                outHit._baryV      = v;
                outHit._triangle   = handle.index();
                outHit._normal     = CharacterGeometryUtil::makeUnitOr( ( b - a ).cross( c - a ), float3::UnitY );
                outHit._bFrontFace = bFrontFace ? SW_TRUE : SW_FALSE;
            }
            return bFound;
        }

        /** @brief @p maxDistance 안에서 가장 가까운 표면 점을 찾습니다. 스크래치 규칙은 `findNearestHit` 와 같습니다. */
        bool findClosestPoint( const float3& point, float32 maxDistance, vector<SlotHandle>& inoutListScratch, GeometryClosestPoint& outClosest ) const
        {
            _tree.querySphere( point, maxDistance, inoutListScratch );
            bool    bFound              = false;
            float32 bestDistanceSquared = maxDistance * maxDistance;
            for ( const SlotHandle handle : inoutListScratch )
            {
                float3 a;
                float3 b;
                float3 c;
                getTriangle( handle.index(), a, b, c );
                float32       u               = 0.0f;
                float32       v               = 0.0f;
                const float3  closest         = CharacterGeometryUtil::findClosestPointOnTriangle( point, a, b, c, u, v );
                const float32 distanceSquared = float3::getDistanceSquared( point, closest );
                if ( distanceSquared > bestDistanceSquared )
                    continue;
                bestDistanceSquared  = distanceSquared;
                bFound               = true;
                outClosest._point    = closest;
                outClosest._normal   = CharacterGeometryUtil::makeUnitOr( ( b - a ).cross( c - a ), float3::UnitY );
                outClosest._triangle = handle.index();
                outClosest._baryU    = u;
                outClosest._baryV    = v;
            }
            if ( bFound )
                outClosest._distance = MathUtil::sqrt( bestDistanceSquared );
            return bFound;
        }

    private:
        /** @brief 평평한 삼각형도 부피가 있게 상자를 조금 키운다(광선 슬랩 판정의 0 두께 모서리). `SurfaceBvh` 와 같은 값이다. */
        static constexpr float32 kBoundsPadding = 1.0e-4f;

        void getTriangle( uint32 triangle, float3& outA, float3& outB, float3& outC ) const
        {
            outA = _listTriangle[triangle * 3];
            outB = _listTriangle[triangle * 3 + 1];
            outC = _listTriangle[triangle * 3 + 2];
        }

    private:
        vector<float3> _listTriangle; ///< 마지막으로 넘겨받은 삼각형 목록(비교 · 질의용 사본)
        BVHTree3D      _tree;
        uint32         _buildCount;
    };
} // namespace sw::editor
