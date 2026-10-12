#include "pch.h"

#include "Engine/Environment/Water/BuoyancyComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"

namespace sw
{
    float32 BuoyancyUtil::computeSubmergedFraction( float32 surfaceAboveCenter, float32 radius )
    {
        if ( radius <= 0.0f )
            return 0.0f;
        const float32 height = MathUtil::clamp( surfaceAboveCenter + radius, 0.0f, 2.0f * radius );
        return height * height * ( 3.0f * radius - height ) / ( 4.0f * radius * radius * radius );
    }

    float32 BuoyancyUtil::computeBuoyantForce( float32 submergedFraction, float32 radius, float32 waterDensity, float32 gravityMagnitude )
    {
        const float32 sphereVolume = 4.0f / 3.0f * MathUtil::kPi * radius * radius * radius;
        return waterDensity * gravityMagnitude * sphereVolume * submergedFraction;
    }
} // namespace sw

namespace sw
{
    BuoyancyComponent::BuoyancyComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Joint }
        , _listPontoon{ BuoyancyPontoon{} }
        , _waterDensity{ 1000.0f }
        , _linearDrag{ 200.0f }
        , _submergedPontoonCount{ 0 }
    {
    }

    void BuoyancyComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        (void)stepIndex;
        (void)stepCount;
        _submergedPontoonCount = 0;
        if ( isSimulated() == false )
            return;
        IPhysicsBodyScene3D*      pScene    = physics.findScene3D();
        const RigidBodyComponent* pRigid    = getOwner()->getComponent<RigidBodyComponent>();
        const GameObjectManager*  pManager  = getOwner()->getManager();
        const bool                bHasInput = pScene != nullptr && pRigid != nullptr && pManager != nullptr;
        if ( bHasInput == false || pRigid->getBodyType() != PhysicsBodyType::Dynamic )
            return;
        const PhysicsBodyHandle body = pRigid->getBodyHandle();
        if ( body.isValid() == false || pScene->isBodyValid( body ) == false )
            return;

        float3     bodyPosition{};
        quaternion bodyRotation{};
        if ( pScene->getBodyTransform( body, bodyPosition, bodyRotation ) == false )
            return;
        const PhysicsSettings* pSettings        = physics.findSettings();
        const float3           gravity          = pSettings != nullptr ? pSettings->_gravity : PhysicsSystem::getConfiguredGravity();
        const float32          gravityMagnitude = gravity.getLength();
        if ( gravityMagnitude <= 0.0f )
            return;
        const float3 up              = gravity * ( -1.0f / gravityMagnitude );
        const float3 linearVelocity  = pScene->getLinearVelocity( body );
        const float3 angularVelocity = pScene->getAngularVelocity( body );

        for ( const BuoyancyPontoon& pontoon : _listPontoon )
        {
            const float3        offset = float3::transform( pontoon._localPosition, bodyRotation );
            const float3        center = bodyPosition + offset;
            WaterBodyComponent* pWater = WaterBodyComponent::findWaterAt( *pManager, center._x, center._z );
            float32             surfaceHeight{ 0.0f };
            if ( pWater == nullptr || pWater->computeSurfaceHeight( center._x, center._z, surfaceHeight ) == false )
                continue;
            const float32 fraction = BuoyancyUtil::computeSubmergedFraction( surfaceHeight - center._y, pontoon._radius );
            if ( fraction <= 0.0f )
                continue;
            ++_submergedPontoonCount;
            const float32 buoyantForce  = BuoyancyUtil::computeBuoyantForce( fraction, pontoon._radius, _waterDensity, gravityMagnitude );
            const float3  pointVelocity = linearVelocity + angularVelocity.cross( offset );
            const float3  force         = up * buoyantForce - pointVelocity * ( _linearDrag * fraction );
            pScene->addImpulseAtPoint( body, force * fixedDeltaTime, center );
        }
    }

    void BuoyancyComponent::releasePhysics( ScenePhysics& physics )
    {
        (void)physics;
        _submergedPontoonCount = 0;
    }
} // namespace sw
