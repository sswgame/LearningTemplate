#include "pch.h"

#include "Engine/Physics/Component/PhysicsComponent.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Service/SceneSpatialGrid.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    namespace
    {
        struct PhysicsComponentInternal
        {
            /// 지은 배율과 다르다고 볼 상대 차이 — 회전한 월드 행렬을 풀면 축 길이가 몇 ulp 흔들려 절대 `kEpsilon`(1e-6)으로는 배율 5 짜리 회전 바디가 프레임마다 다시 지어진다.
            static constexpr float32 kShapeScaleTolerance = 1e-4f;
        };
    } // namespace

    PhysicsComponent::PhysicsComponent( PhysicsComponentPhase phase )
        : _pScenePhysics{ nullptr }
        , _physicsIndex{ kNotRegistered }
        , _syncedTransformVersion{ 0 }
        , _shapeScale{ 1.0f, 1.0f, 1.0f }
        , _phase{ phase }
        , _bRebuildPending{ false }
        , _bTeleportPending{ false }
        , _spatialEntry{}
    {
    }

    const SpatialGridLayerDesc& PhysicsComponent::getCharacterSpatialLayer()
    {
        static const SpatialGridLayerDesc kLayer{ "PhysicsCharacter3D", SpatialGridPlane::XZ, 1 };
        return kLayer;
    }

    const SpatialGridLayerDesc& PhysicsComponent::getBodySpatialLayer()
    {
        static const SpatialGridLayerDesc kLayer{ "PhysicsBody3D", SpatialGridPlane::XZ, 1 };
        return kLayer;
    }

    void PhysicsComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        (void)physics;
    }

    void PhysicsComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        (void)physics;
        (void)fixedDeltaTime;
        (void)stepIndex;
        (void)stepCount;
    }

    void PhysicsComponent::postPhysicsStep( ScenePhysics& physics )
    {
        (void)physics;
    }

    void PhysicsComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        (void)physics;
        (void)alpha;
    }

    void PhysicsComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        _pScenePhysics = &manager.getSubsystem<sw::ScenePhysics>();
        _pScenePhysics->registerComponent( this );
        // 3D 캐릭터 · 강체는 지역 같은 공간 질의의 후보다 — 씬을 훑지 않게 등록 동안 공간 색인에 올린다(점, 반 크기 0).
        const SpatialGridLayerDesc* pLayer = castTo<CharacterControllerComponent>( this ) != nullptr ? &getCharacterSpatialLayer()
                                           : castTo<RigidBodyComponent>( this ) != nullptr           ? &getBodySpatialLayer()
                                                                                                     : nullptr;
        if ( pLayer != nullptr )
            _spatialEntry = manager.getSubsystem<SceneSpatialGrid>().add( *pLayer, *this, *this, 0.0f );
    }

    void PhysicsComponent::onUnregister( GameObjectManager& manager )
    {
        if ( _pScenePhysics != nullptr )
        {
            releasePhysics( *_pScenePhysics );
            _pScenePhysics->unregisterComponent( this );
        }
        _pScenePhysics = nullptr;
        manager.getSubsystem<SceneSpatialGrid>().remove( _spatialEntry );
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

    bool PhysicsComponent::isShapeScaleStale( const float3& worldScale ) const
    {
        const float3 delta{ worldScale._x - _shapeScale._x, worldScale._y - _shapeScale._y, worldScale._z - _shapeScale._z };
        const float3 limit{ PhysicsComponentInternal::kShapeScaleTolerance * MathUtil::max( 1.0f, MathUtil::abs( _shapeScale._x ) ),
                            PhysicsComponentInternal::kShapeScaleTolerance * MathUtil::max( 1.0f, MathUtil::abs( _shapeScale._y ) ),
                            PhysicsComponentInternal::kShapeScaleTolerance * MathUtil::max( 1.0f, MathUtil::abs( _shapeScale._z ) ) };
        return MathUtil::abs( delta._x ) > limit._x || MathUtil::abs( delta._y ) > limit._y || MathUtil::abs( delta._z ) > limit._z;
    }

    bool PhysicsComponent::isSimulated() const
    {
        return hasBegunPlay() && isActive() && isPendingDestroy() == false && getOwner() != nullptr;
    }
} // namespace sw

namespace sw
{
    bool PhysicsComponent::readWorldPose( const SceneComponent& component, float3& outPosition, quaternion& outRotation, float3& outScale )
    {
        float3     position{};
        quaternion rotation{};
        float3     scale{};
        if ( component.getWorldMatrix().decompose( scale, rotation, position ) == false )
        {
            const GameObject*   pOwner = component.getOwner();
            const hashed_string name   = pOwner != nullptr ? pOwner->getName() : hashed_string{};
            SW_LOG_ERROR_ONCE_FOR_ID( "PhysicsComponent.ZeroWorldScale", std::string_view( name.c_str(), name.size() ),
                                      "'%#' has a zero world scale axis (%#, %#, %#) - physics skips it until the scale is non-zero", name.c_str(), scale._x, scale._y,
                                      scale._z );
            return false;
        }
        outPosition = position;
        outRotation = rotation.normalize();
        outScale    = scale;
        return true;
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
