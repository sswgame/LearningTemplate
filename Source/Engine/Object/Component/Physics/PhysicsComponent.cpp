#include "pch.h"

#include "Engine/Object/Component/Physics/PhysicsComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"

namespace sw
{
    PhysicsComponent::PhysicsComponent( PhysicsComponentPhase phase )
        : _pScenePhysics{ nullptr }
        , _physicsIndex{ kNotRegistered }
        , _phase{ phase }
        , _bRebuildPending{ false }
        , _bTeleportPending{ false }
    {
    }

    void PhysicsComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        _pScenePhysics = &manager.getScenePhysics();
        _pScenePhysics->registerComponent( this );
    }

    void PhysicsComponent::onUnregister( GameObjectManager& manager )
    {
        if ( _pScenePhysics != nullptr )
        {
            releasePhysics( *_pScenePhysics );
            _pScenePhysics->unregisterComponent( this );
        }
        _pScenePhysics = nullptr;
        SceneComponent::onUnregister( manager );
    }

    void PhysicsComponent::onEndPlay()
    {
        if ( _pScenePhysics != nullptr )
            releasePhysics( *_pScenePhysics );
        SceneComponent::onEndPlay();
    }

    void PhysicsComponent::onTeleported()
    {
        SceneComponent::onTeleported();
        _bTeleportPending.store( true, std::memory_order_release );
    }

    void PhysicsComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        _bRebuildPending = true;
    }

    bool PhysicsComponent::isSimulated() const
    {
        return hasBegunPlay() && isActive() && isPendingDestroy() == false && getOwner() != nullptr;
    }
} // namespace sw

namespace sw
{
    void PhysicsComponentUtil::readWorldPose( const SceneComponent& component, float3& outPosition, quaternion& outRotation, float3& outScale )
    {
        const float4x4 world = component.getWorldMatrix();
        if ( world.decompose( outScale, outRotation, outPosition ) == false )
        {
            outPosition = world.getTranslation();
            outRotation = quaternion{};
            outScale    = float3{ 1.0f, 1.0f, 1.0f };
        }
        outRotation.normalize();
    }

    void PhysicsComponentUtil::writeWorldPose( SceneComponent& component, const float3& position, const quaternion& rotation, float3& outWrittenPosition,
                                               quaternion& outWrittenRotation )
    {
        float3     currentPosition{};
        quaternion currentRotation{};
        float3     scale{};
        readWorldPose( component, currentPosition, currentRotation, scale );
        component.setWorldTransform( float4x4::makeTrs( position, rotation, scale ) );
        readWorldPose( component, outWrittenPosition, outWrittenRotation, scale );
    }

    bool PhysicsComponentUtil::hasMoved( const float3& position, const quaternion& rotation, const float3& otherPosition, const quaternion& otherRotation )
    {
        if ( float3::getDistanceSquared( position, otherPosition ) > 1.0e-6f )
            return true;
        // |q · r| 가 1 에서 멀면 다른 회전이다(q 와 -q 는 같은 회전). 1 - cos(θ/2) 로 약 0.06 도.
        const float32 dot = MathUtil::abs( rotation.dot( otherRotation ) );
        return dot < 1.0f - 1.0e-7f;
    }

    float32 PhysicsComponentUtil::getAngle2D( const quaternion& rotation )
    {
        return 2.0f * ::atan2f( rotation._z, rotation._w );
    }

    quaternion PhysicsComponentUtil::makeRotation2D( float32 angle )
    {
        return quaternion::makeFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, angle );
    }

    PhysicsShapeDesc3D PhysicsComponentUtil::makeScaledShape( const PhysicsShapeDesc3D& shape, const float3& scale )
    {
        const float3       absScale{ MathUtil::abs( scale._x ), MathUtil::abs( scale._y ), MathUtil::abs( scale._z ) };
        const float32      radiusScale  = absScale._x > absScale._z ? absScale._x : absScale._z;
        const float32      uniformScale = radiusScale > absScale._y ? radiusScale : absScale._y;
        PhysicsShapeDesc3D scaled       = shape;
        scaled._halfExtents             = float3{ shape._halfExtents._x * absScale._x, shape._halfExtents._y * absScale._y, shape._halfExtents._z * absScale._z };
        scaled._radius                  = shape._type == PhysicsShapeType3D::Sphere ? shape._radius * uniformScale : shape._radius * radiusScale;
        scaled._halfHeight              = shape._halfHeight * absScale._y;
        scaled._localPosition           = float3{ shape._localPosition._x * scale._x, shape._localPosition._y * scale._y, shape._localPosition._z * scale._z };
        for ( float3& point : scaled._listPoint )
        {
            point = float3{ point._x * scale._x, point._y * scale._y, point._z * scale._z };
        }
        return scaled;
    }

    PhysicsShapeDesc2D PhysicsComponentUtil::makeScaledShape( const PhysicsShapeDesc2D& shape, const float3& scale )
    {
        const float32      absX        = MathUtil::abs( scale._x );
        const float32      absY        = MathUtil::abs( scale._y );
        const float32      radiusScale = absX > absY ? absX : absY;
        PhysicsShapeDesc2D scaled      = shape;
        scaled._halfExtents            = float2{ shape._halfExtents._x * absX, shape._halfExtents._y * absY };
        scaled._radius                 = shape._type == PhysicsShapeType2D::Capsule ? shape._radius * absX : shape._radius * radiusScale;
        scaled._halfHeight             = shape._halfHeight * absY;
        scaled._localPosition          = float2{ shape._localPosition._x * scale._x, shape._localPosition._y * scale._y };
        for ( float2& point : scaled._listPoint )
        {
            point = float2{ point._x * scale._x, point._y * scale._y };
        }
        return scaled;
    }
} // namespace sw
