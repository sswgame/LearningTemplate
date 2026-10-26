#include "pch.h"

#include "Engine/Object/Component/Navigation/NavMeshObstacleComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Navigation/SceneNavigation.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct NavMeshObstacleComponentInternal
        {
            /** @brief 원기둥을 뚫는 다각형의 꼭짓점 수입니다. */
            static constexpr uint32 kCylinderSideCount = 8;
        };
    } // namespace
} // namespace sw

namespace sw
{
    NavMeshObstacleComponent::NavMeshObstacleComponent()
        : _shape{ NavObstacleShape::Box }
        , _halfExtents{ 0.5f, 0.5f, 0.5f }
        , _center{ 0.0f, 0.5f, 0.0f }
        , _volume{}
        , _pSeenScene{ nullptr }
        , _carvedBounds{}
        , _carvedPosition{}
        , _carvedYaw{ 0.0f }
        , _navIndex{ SceneNavigation::kNotRegistered }
        , _seenTransformVersion{ 0 }
        , _bCarved{ SW_FALSE }
        , _bShapeDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void NavMeshObstacleComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getSubsystem<SceneNavigation>().registerObstacle( this );
    }

    void NavMeshObstacleComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getSubsystem<SceneNavigation>().unregisterObstacle( this );
        Component::onUnregister( manager );
    }

    void NavMeshObstacleComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        _bShapeDirty = SW_TRUE;
    }

    void NavMeshObstacleComponent::setShape( NavObstacleShape shape )
    {
        _shape       = shape;
        _bShapeDirty = SW_TRUE;
    }

    void NavMeshObstacleComponent::setHalfExtents( const float3& halfExtents )
    {
        _halfExtents = halfExtents;
        _bShapeDirty = SW_TRUE;
    }

    void NavMeshObstacleComponent::setCenter( const float3& center )
    {
        _center      = center;
        _bShapeDirty = SW_TRUE;
    }

    bool NavMeshObstacleComponent::makeVolume( NavConvexVolume& outVolume ) const
    {
        outVolume._listPoint.clear();
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return false;
        const float4x4 world       = pScene->getWorldMatrix();
        const float3   worldCenter = float3::transform( _center, world );
        const float3   scale       = world.getScale();
        const float32  halfHeight  = _halfExtents._y * MathUtil::abs( scale._y );
        outVolume._minY            = worldCenter._y - halfHeight;
        outVolume._maxY            = worldCenter._y + halfHeight;
        outVolume._area            = NavigationConstant::kNotWalkableArea;
        if ( _shape == NavObstacleShape::Box )
        {
            // 발자국 네 모서리 — 오브젝트 회전 · 배율을 따른다(기울면 XZ 로 눌린 사각형).
            const float32 arrSignX[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
            const float32 arrSignZ[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
            for ( uint32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex )
            {
                const float3 local{ _center._x + arrSignX[cornerIndex] * _halfExtents._x, _center._y, _center._z + arrSignZ[cornerIndex] * _halfExtents._z };
                outVolume._listPoint.push_back( float3::transform( local, world ) );
            }
            return true;
        }
        const float32 radius = _halfExtents._x * MathUtil::max( MathUtil::abs( scale._x ), MathUtil::abs( scale._z ) );
        for ( uint32 sideIndex = 0; sideIndex < NavMeshObstacleComponentInternal::kCylinderSideCount; ++sideIndex )
        {
            const float32 angle = 2.0f * MathUtil::kPi * static_cast<float32>( sideIndex ) / static_cast<float32>( NavMeshObstacleComponentInternal::kCylinderSideCount );
            outVolume._listPoint.push_back( float3{ worldCenter._x + MathUtil::cos( angle ) * radius, worldCenter._y, worldCenter._z + MathUtil::sin( angle ) * radius } );
        }
        return true;
    }

    bool NavMeshObstacleComponent::refreshVolume()
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
        {
            _volume._listPoint.clear();
            _pSeenScene = nullptr;
            return false;
        }
        const uint32 version    = pScene->getWorldTransformVersion();
        const bool   bUnchanged = pScene == _pSeenScene && version == _seenTransformVersion && _bShapeDirty == SW_FALSE;
        if ( bUnchanged )
            return false;
        _pSeenScene           = pScene;
        _seenTransformVersion = version;
        return makeVolume( _volume );
    }

    bool NavMeshObstacleComponent::makeCacheObstacle( NavCacheObstacleDesc& outDesc ) const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return false;
        // 뚫는 부피(`makeVolume`)와 같은 자세 규칙이다 — 상자는 오브젝트의 요만 따른다(장애물 캐시의 상자는 Y 축으로만 돈다).
        const float4x4 world = pScene->getWorldMatrix();
        const float3   scale = world.getScale();
        outDesc._center      = float3::transform( _center, world );
        outDesc._bCylinder   = _shape == NavObstacleShape::Cylinder;
        if ( outDesc._bCylinder )
        {
            const float32 radius = _halfExtents._x * MathUtil::max( MathUtil::abs( scale._x ), MathUtil::abs( scale._z ) );
            outDesc._halfExtents = float3{ radius, _halfExtents._y * MathUtil::abs( scale._y ), radius };
            outDesc._yaw         = 0.0f;
            return true;
        }
        outDesc._halfExtents = float3{ _halfExtents._x * MathUtil::abs( scale._x ), _halfExtents._y * MathUtil::abs( scale._y ), _halfExtents._z * MathUtil::abs( scale._z ) };
        // 오브젝트의 +X 축이 XZ 평면에서 향하는 각 — 요(Y 축 회전)다.
        const float3 axisX = float3::transformVector( float3{ 1.0f, 0.0f, 0.0f }, world );
        outDesc._yaw       = MathUtil::atan2( -axisX._z, axisX._x );
        return true;
    }

    bool NavMeshObstacleComponent::computeWorldBounds( AABB& outBounds ) const
    {
        NavConvexVolume volume;
        if ( makeVolume( volume ) == false )
            return false;
        outBounds = volume.computeBounds();
        return outBounds.isValid();
    }
} // namespace sw
