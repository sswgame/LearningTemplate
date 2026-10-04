#include "pch.h"

#include "Engine/Character/SurfaceBvh.h"

#include "Core/Math/MathUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct SurfaceBvhInternal
        {
            /** @brief 평평한 삼각형도 부피가 있게 상자를 조금 키운다(광선 슬랩 판정의 0 두께 모서리). */
            static constexpr float32 kBoundsPadding = 1.0e-4f;

            struct HitDistanceLess
            {
                bool operator()( const GeometryRayHit& lhs, const GeometryRayHit& rhs ) const { return lhs._distance < rhs._distance; }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SurfaceBvh::SurfaceBvh()
        : _tree{}
        , _listSurface{}
        , _listTriangleSurface{}
        , _listTriangleLocal{}
        , _listScratchHandle{}
    {
    }

    void SurfaceBvh::clear()
    {
        _tree.clear();
        _listSurface.clear();
        _listTriangleSurface.clear();
        _listTriangleLocal.clear();
    }

    uint32 SurfaceBvh::addSurface( uint16 part, vector_reference<const float3> listPosition, vector_reference<const uint32> listIndex )
    {
        Surface surface;
        surface._listPosition.assign( listPosition.begin(), listPosition.end() );
        surface._listIndex.assign( listIndex.begin(), listIndex.end() );
        surface._part = part;
        _listSurface.push_back( std::move( surface ) );
        return static_cast<uint32>( _listSurface.size() - 1 );
    }

    void SurfaceBvh::setSurfacePositions( uint32 surfaceIndex, vector_reference<const float3> listPosition )
    {
        if ( surfaceIndex >= _listSurface.size() )
            return;
        Surface& surface = _listSurface[surfaceIndex];
        if ( surface._listPosition.size() != listPosition.size() )
            return;
        surface._listPosition.assign( listPosition.begin(), listPosition.end() );
    }

    void SurfaceBvh::build()
    {
        _tree.clear();
        _listTriangleSurface.clear();
        _listTriangleLocal.clear();
        const float3 padding( SurfaceBvhInternal::kBoundsPadding );
        for ( uint32 surfaceIndex = 0; surfaceIndex < _listSurface.size(); ++surfaceIndex )
        {
            const Surface& surface       = _listSurface[surfaceIndex];
            const uint32   triangleCount = static_cast<uint32>( surface._listIndex.size() / 3 );
            for ( uint32 triangle = 0; triangle < triangleCount; ++triangle )
            {
                const float3& a = surface._listPosition[surface._listIndex[triangle * 3]];
                const float3& b = surface._listPosition[surface._listIndex[triangle * 3 + 1]];
                const float3& c = surface._listPosition[surface._listIndex[triangle * 3 + 2]];
                AABB          bounds;
                bounds._min                 = float3::min( float3::min( a, b ), c ) - padding;
                bounds._max                 = float3::max( float3::max( a, b ), c ) + padding;
                const uint32 globalTriangle = static_cast<uint32>( _listTriangleSurface.size() );
                _listTriangleSurface.push_back( surfaceIndex );
                _listTriangleLocal.push_back( triangle );
                (void)_tree.insert( SlotHandle::make( globalTriangle, 1 ), bounds );
            }
        }
    }

    void SurfaceBvh::getTriangle( uint32 globalTriangle, float3& outA, float3& outB, float3& outC ) const
    {
        const Surface& surface = _listSurface[_listTriangleSurface[globalTriangle]];
        const uint32   local   = _listTriangleLocal[globalTriangle];
        outA                   = surface._listPosition[surface._listIndex[local * 3]];
        outB                   = surface._listPosition[surface._listIndex[local * 3 + 1]];
        outC                   = surface._listPosition[surface._listIndex[local * 3 + 2]];
    }

    bool SurfaceBvh::findNearestHit( const float3& origin, const float3& direction, float32 maxDistance, GeometryRayHit& outHit, uint16 partFilter ) const
    {
        const float3 unitDirection = CharacterGeometryUtil::makeUnitOr( direction, float3::Zero );
        if ( unitDirection == float3::Zero )
            return false;
        _tree.queryRay( origin, unitDirection, maxDistance, _listScratchHandle );
        bool    bFound       = false;
        float32 bestDistance = maxDistance;
        for ( const SlotHandle handle : _listScratchHandle )
        {
            const uint32   globalTriangle = handle.index();
            const Surface& surface        = _listSurface[_listTriangleSurface[globalTriangle]];
            if ( partFilter != CharacterGeometryConstant::kNoPart && surface._part != partFilter )
                continue;
            float3 a;
            float3 b;
            float3 c;
            getTriangle( globalTriangle, a, b, c );
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
            outHit._triangle   = _listTriangleLocal[globalTriangle];
            outHit._part       = surface._part;
            outHit._normal     = CharacterGeometryUtil::makeUnitOr( ( b - a ).cross( c - a ), float3::UnitY );
            outHit._bFrontFace = bFrontFace ? SW_TRUE : SW_FALSE;
        }
        return bFound;
    }

    void SurfaceBvh::collectHits( const float3& origin, const float3& direction, float32 maxDistance, vector<GeometryRayHit>& outListHit, uint16 partFilter ) const
    {
        outListHit.clear();
        const float3 unitDirection = CharacterGeometryUtil::makeUnitOr( direction, float3::Zero );
        if ( unitDirection == float3::Zero )
            return;
        _tree.queryRay( origin, unitDirection, maxDistance, _listScratchHandle );
        for ( const SlotHandle handle : _listScratchHandle )
        {
            const uint32   globalTriangle = handle.index();
            const Surface& surface        = _listSurface[_listTriangleSurface[globalTriangle]];
            if ( partFilter != CharacterGeometryConstant::kNoPart && surface._part != partFilter )
                continue;
            float3 a;
            float3 b;
            float3 c;
            getTriangle( globalTriangle, a, b, c );
            GeometryRayHit hit;
            bool           bFrontFace = false;
            if ( CharacterGeometryUtil::intersectRayTriangle( origin, unitDirection, a, b, c, hit._distance, hit._baryU, hit._baryV, bFrontFace ) == false )
                continue;
            if ( hit._distance > maxDistance )
                continue;
            hit._triangle   = _listTriangleLocal[globalTriangle];
            hit._part       = surface._part;
            hit._normal     = CharacterGeometryUtil::makeUnitOr( ( b - a ).cross( c - a ), float3::UnitY );
            hit._bFrontFace = bFrontFace ? SW_TRUE : SW_FALSE;
            outListHit.push_back( hit );
        }
        std::sort( outListHit.begin(), outListHit.end(), SurfaceBvhInternal::HitDistanceLess{} );
    }

    bool SurfaceBvh::findClosestPoint( const float3& point, float32 maxDistance, GeometryClosestPoint& outClosest, uint16 partFilter ) const
    {
        _tree.querySphere( point, maxDistance, _listScratchHandle );
        bool    bFound              = false;
        float32 bestDistanceSquared = maxDistance * maxDistance;
        for ( const SlotHandle handle : _listScratchHandle )
        {
            const uint32   globalTriangle = handle.index();
            const Surface& surface        = _listSurface[_listTriangleSurface[globalTriangle]];
            if ( partFilter != CharacterGeometryConstant::kNoPart && surface._part != partFilter )
                continue;
            float3 a;
            float3 b;
            float3 c;
            getTriangle( globalTriangle, a, b, c );
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
            outClosest._triangle = _listTriangleLocal[globalTriangle];
            outClosest._part     = surface._part;
            outClosest._baryU    = u;
            outClosest._baryV    = v;
        }
        if ( bFound )
            outClosest._distance = MathUtil::sqrt( bestDistanceSquared );
        return bFound;
    }
} // namespace sw
