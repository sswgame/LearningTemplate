#include "pch.h"

#include "Engine/Physics/Component/PhysicsComponent.h"

#include "Core/Foundation/EnumUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Scene/ScenePhysics.h"

namespace sw
{
    PhysicsComponent::PhysicsComponent( PhysicsComponentPhase phase )
        : _pScenePhysics{ nullptr }
        , _physicsIndex{ kNotRegistered }
        , _syncedTransformVersion{ 0 }
        , _phase{ phase }
        , _unusedFrameHooks{ PhysicsFrameHook::None }
        , _bRebuildPending{ false }
        , _bTeleportPending{ false }
    {
    }

    void PhysicsComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        (void)physics;
        _unusedFrameHooks = EnumUtil::setFlag( _unusedFrameHooks, PhysicsFrameHook::BeginFrame );
    }

    void PhysicsComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        (void)physics;
        (void)fixedDeltaTime;
        (void)stepIndex;
        (void)stepCount;
        _unusedFrameHooks = EnumUtil::setFlag( _unusedFrameHooks, PhysicsFrameHook::PreStep );
    }

    void PhysicsComponent::postPhysicsStep( ScenePhysics& physics )
    {
        (void)physics;
        _unusedFrameHooks = EnumUtil::setFlag( _unusedFrameHooks, PhysicsFrameHook::PostStep );
    }

    void PhysicsComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        (void)physics;
        (void)alpha;
        _unusedFrameHooks = EnumUtil::setFlag( _unusedFrameHooks, PhysicsFrameHook::EndFrame );
    }

    void PhysicsComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        _pScenePhysics = &manager.getSubsystem<sw::ScenePhysics>();
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
    void PhysicsComponent::readWorldPose( const SceneComponent& component, float3& outPosition, quaternion& outRotation, float3& outScale )
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

    void PhysicsComponent::writeWorldPose( SceneComponent& component, const float3& position, const quaternion& rotation, const float3& scale )
    {
        // 부모가 없으면 월드가 곧 로컬이다 — 행렬을 만들어 다시 분해하지 않는다(동적 바디 대부분이 루트에 있다).
        if ( component.getParent() == nullptr )
        {
            component.setLocalPosition( position );
            component.setLocalRotation( rotation.getEulerAngles() );
            component.setLocalScale( scale );
            return;
        }
        component.setWorldTransform( float4x4::makeTrs( position, rotation, scale ) );
    }

    float32 PhysicsComponent::getAngle2D( const quaternion& rotation )
    {
        return 2.0f * ::atan2f( rotation._z, rotation._w );
    }

    quaternion PhysicsComponent::makeRotation2D( float32 angle )
    {
        return quaternion::makeFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, angle );
    }

    PhysicsShapeDesc3D PhysicsComponent::makeScaledShape( const PhysicsShapeDesc3D& shape, const float3& scale )
    {
        const float3       absScale{ MathUtil::abs( scale._x ), MathUtil::abs( scale._y ), MathUtil::abs( scale._z ) };
        const float32      radiusScale  = absScale._x > absScale._z ? absScale._x : absScale._z;
        const float32      uniformScale = radiusScale > absScale._y ? radiusScale : absScale._y;
        PhysicsShapeDesc3D scaled       = shape;
        scaled._halfExtents             = float3::multiply( shape._halfExtents, absScale );
        scaled._radius                  = shape._type == PhysicsShapeType3D::Sphere ? shape._radius * uniformScale : shape._radius * radiusScale;
        scaled._halfHeight              = shape._halfHeight * absScale._y;
        scaled._localPosition           = float3::multiply( shape._localPosition, scale );
        for ( float3& point : scaled._listPoint )
        {
            point = float3::multiply( point, scale );
        }
        return scaled;
    }

    PhysicsShapeDesc2D PhysicsComponent::makeScaledShape( const PhysicsShapeDesc2D& shape, const float3& scale )
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
