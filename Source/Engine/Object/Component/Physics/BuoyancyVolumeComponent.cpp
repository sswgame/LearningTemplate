#include "pch.h"

#include "Engine/Object/Component/Physics/BuoyancyVolumeComponent.h"

#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/IPhysicsScene.h"

namespace sw
{
    BuoyancyVolumeComponent::BuoyancyVolumeComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Joint } // 바디가 모두 선 뒤에 띄운다
        , _halfExtents{ 5.0f, 1.0f, 5.0f }
        , _buoyancy{ 1.5f }
        , _linearDrag{ 0.5f }
        , _angularDrag{ 0.05f }
        , _flowVelocity{ 0.0f, 0.0f, 0.0f }
        , _layerMask{ MathUtil::kMaxUInt32 }
        , _listOverlapBody{}
        , _submergedBodyCount{ 0 }
    {
    }

    bool BuoyancyVolumeComponent::computeSurface( float3& outSurfacePoint, float3& outSurfaceNormal, float3& outCenter, quaternion& outRotation ) const
    {
        float3 scale;
        PhysicsComponentUtil::readWorldPose( *this, outCenter, outRotation, scale );
        const float4x4 world = getWorldMatrix();
        const float3   up    = float3::transformVector( float3{ 0.0f, 1.0f, 0.0f }, world ).normalize();
        if ( up.getLength() <= 0.0f )
            return false; // 배율 0 — 수면을 정할 수 없다
        outSurfaceNormal = up;
        outSurfacePoint  = outCenter + up * _halfExtents._y;
        return true;
    }

    void BuoyancyVolumeComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        (void)stepIndex;
        (void)stepCount;
        _submergedBodyCount           = 0;
        IPhysicsScene3D* const pScene = physics.getScene3D();
        if ( isSimulated() == false || pScene == nullptr )
            return;
        float3     surfacePoint;
        float3     surfaceNormal;
        float3     center;
        quaternion rotation;
        if ( computeSurface( surfacePoint, surfaceNormal, center, rotation ) == false )
            return;

        PhysicsShapeDesc3D box;
        box._halfExtents = _halfExtents;
        PhysicsQueryFilter filter;
        filter._layerMask        = _layerMask;
        filter._bIncludeTriggers = false;
        _listOverlapBody.clear();
        (void)pScene->overlapShape( box, center, rotation, filter, _listOverlapBody ); // 수는 목록이 말한다

        PhysicsBuoyancyDesc desc;
        desc._surfacePoint  = surfacePoint;
        desc._surfaceNormal = surfaceNormal;
        desc._buoyancy      = _buoyancy;
        desc._linearDrag    = _linearDrag;
        desc._angularDrag   = _angularDrag;
        desc._fluidVelocity = float3::transformVector( _flowVelocity, getWorldMatrix() );
        for ( const PhysicsBodyHandle body : _listOverlapBody )
        {
            if ( pScene->applyBuoyancy( body, desc, fixedDeltaTime ) )
                ++_submergedBodyCount;
        }
    }
} // namespace sw
