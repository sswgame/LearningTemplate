#include "pch.h"

#include "Editor/Viewport/Model/EditorAssetPlacement.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include "Engine/Physics/Collision/PhysicsQuery.h"

namespace sw::editor
{
    EditorPlacementQuery::EditorPlacementQuery()
        : _pPhysics{ nullptr }
        , _pManager{ nullptr }
        , _maxDistance{ 0.0f }
        , _snapStep{ 0.0f }
        , _b2DMode{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool EditorAssetPlacement::findSurface( const EditorPlacementQuery& query, const Ray& ray, EditorPlacementHit& outHit )
    {
        outHit                 = EditorPlacementHit{};
        const bool    b2DMode  = query._b2DMode == SW_TRUE;
        const float32 maxReach = query._maxDistance;
        if ( maxReach <= 0.0f )
            return false;

        EditorPlacementHit best{};
        best._distance = MathUtil::kMaxFloat;
        if ( b2DMode == false )
        {
            // 정적 콜라이더 · 지형 — 실제 면과 법선을 준다
            if ( query._pPhysics != nullptr )
            {
                PhysicsCastHit3D physicsHit{};
                if ( query._pPhysics->raycast( ray._origin, ray._direction, maxReach, PhysicsQueryFilter{}, physicsHit ) )
                {
                    best._position = physicsHit._point;
                    best._normal   = physicsHit._normal;
                    best._distance = physicsHit._distance;
                    best._surface  = EditorPlacementSurface::Collider;
                }
            }
            // 콜라이더 없는 오브젝트(메시 · 스프라이트) — 피킹 경계에 닿은 점. 콜라이더 면보다 가까울 때만 쓴다(앞에 선 메시 위에 놓는다)
            EditorPickResult pickResult{};
            const bool       bPicked = EditorViewportRayPick::pick( query._pManager, ray, false, pickResult );
            if ( bPicked && pickResult._distance >= 0.0f && pickResult._distance < best._distance && pickResult._distance <= maxReach )
            {
                best._position = ray._origin + ray._direction * pickResult._distance;
                best._normal   = float3{ 0.0f, 1.0f, 0.0f }; // 경계는 면 법선이 없다 — 고스트는 바닥처럼 눕힌다
                best._distance = pickResult._distance;
                best._surface  = EditorPlacementSurface::Bounds;
            }
        }

        if ( best._surface == EditorPlacementSurface::None )
        {
            const uint32 planeAxis = b2DMode ? 2u : 1u;
            float3       planePoint{};
            if ( EditorViewportRayPick::rayHitsAxisPlane( ray, planeAxis, planePoint ) == false )
                return false;
            const float32 along = ( planePoint - ray._origin ).dot( ray._direction );
            // 3D 는 카메라 뒤의 교점을 버린다 — 화면에 없는 자리다. 2D 는 직교 카메라라 평면 전체가 화면에 있다.
            if ( b2DMode == false && along < 0.0f )
                return false;
            if ( MathUtil::abs( along ) > maxReach )
                return false;
            best._position = planePoint;
            best._normal   = b2DMode ? float3{ 0.0f, 0.0f, 1.0f } : float3{ 0.0f, 1.0f, 0.0f };
            best._distance = MathUtil::abs( along );
            best._surface  = EditorPlacementSurface::GroundPlane;
        }

        best._position = snapToGrid( best._position, query._snapStep, b2DMode );
        outHit         = best;
        return true;
    }

    float3 EditorAssetPlacement::snapToGrid( const float3& position, float32 step, bool b2DMode )
    {
        if ( step <= 0.0f )
            return position;
        float3 snapped = position;
        snapped._x     = MathUtil::round( position._x / step ) * step;
        if ( b2DMode )
            snapped._y = MathUtil::round( position._y / step ) * step;
        else
            snapped._z = MathUtil::round( position._z / step ) * step;
        return snapped;
    }

    const utf8* EditorAssetPlacement::getSurfaceName( EditorPlacementSurface surface )
    {
        switch ( surface )
        {
            case EditorPlacementSurface::None:
                return "none";
            case EditorPlacementSurface::Collider:
                return "collider";
            case EditorPlacementSurface::Bounds:
                return "object bounds";
            case EditorPlacementSurface::GroundPlane:
                return "ground plane";
        }
        return "none";
    }

    float32 EditorAssetPlacement::stepYaw( float32 yaw, float32 step, int32 direction )
    {
        if ( step <= 0.0f )
            return yaw;
        // 걸음의 배수로 센 뒤 감는다 — 실수 덧셈을 쌓으면 네 번 돌린 값이 2π 언저리(6.2831 · 1e-7)에 남는다.
        const float32 stepIndex = MathUtil::round( yaw / step ) + static_cast<float32>( direction );
        float32       wrapped   = MathUtil::fmod( stepIndex * step, MathUtil::kTwoPi );
        if ( wrapped < 0.0f )
            wrapped += MathUtil::kTwoPi;
        if ( MathUtil::kTwoPi - wrapped < step * 0.5f )
            wrapped = 0.0f; // 한 바퀴를 다 돌았다
        return wrapped;
    }
} // namespace sw::editor
