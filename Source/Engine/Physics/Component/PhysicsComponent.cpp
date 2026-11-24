#include "pch.h"

#include "Engine/Physics/Component/PhysicsComponent.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneSpatialGrid.h"
#include "Engine/Object/GameObject/Tick/FixedStepTickList.h"
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
        , _floorMissingIndex{ kNotRegistered }
        , _syncedTransformVersion{ 0 }
        , _syncedLocalPosition{}
        , _syncedLocalRotation{}
        , _shapeScale{ 1.0f, 1.0f, 1.0f }
        , _phase{ phase }
        , _bRebuildPending{ false }
        , _bStreamingHeld{ false }
        , _bSyncedParented{ false }
        , _bBodyCreated{ false }
        , _bTeleportPending{ false }
        , _spatialEntry{}
    {
    }

    PhysicsTransformChange PhysicsComponent::consumeTransformChange()
    {
        if ( getWorldTransformVersion() == _syncedTransformVersion )
            return PhysicsTransformChange{};
        const bool             bWhole = _syncedTransformVersion == 0 || _bSyncedParented || getParent() != nullptr; // 처음이거나 부모 아래
        PhysicsTransformChange change = bWhole ? PhysicsTransformChange::makeWhole() : PhysicsTransformChange{};
        if ( bWhole == false )
        {
            const float3 position  = getLocalPosition();
            const float3 rotation  = getLocalRotation();
            change._bAny           = SW_TRUE;
            change._bPlanePosition = position._x != _syncedLocalPosition._x || position._y != _syncedLocalPosition._y ? SW_TRUE : SW_FALSE;
            change._bDepth         = position._z != _syncedLocalPosition._z ? SW_TRUE : SW_FALSE;
            change._bTilt          = rotation._x != _syncedLocalRotation._x || rotation._y != _syncedLocalRotation._y ? SW_TRUE : SW_FALSE;
            change._bPlaneAngle    = rotation._z != _syncedLocalRotation._z ? SW_TRUE : SW_FALSE;
        }
        markTransformSynced();
        return change;
    }

    bool PhysicsComponent::absorbWorldOriginShift( const float3& delta )
    {
        const bool bNotComparable = _syncedTransformVersion == 0 || _bSyncedParented || getParent() != nullptr;
        if ( bNotComparable || getLocalPosition() != _syncedLocalPosition + delta || getLocalRotation() != _syncedLocalRotation )
            return false;
        markTransformSynced();
        (void)consumeTeleport();
        return true;
    }

    void PhysicsComponent::markTransformSynced()
    {
        _syncedTransformVersion = getWorldTransformVersion();
        _syncedLocalPosition    = getLocalPosition();
        _syncedLocalRotation    = getLocalRotation();
        _bSyncedParented        = getParent() != nullptr;
    }

    void PhysicsComponent::markBodyCreated()
    {
        const GameObject* pOwner = getOwner();
        if ( _bBodyCreated || _pScenePhysics == nullptr || pOwner == nullptr )
            return;
        _bBodyCreated = true;
        _pScenePhysics->markObjectSpawned( pOwner->getObjectID() );
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

    void PhysicsComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        _pScenePhysics = &manager.getSubsystem<sw::ScenePhysics>();
        _pScenePhysics->registerComponent( this );
        _bBodyCreated = false;
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
        (void)manager.getFixedStepTickList().remove( *this );
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

    void PhysicsComponent::onTeleported( TeleportType type )
    {
        SceneComponent::onTeleported( type );
        _bTeleportPending.store( true, std::memory_order_release );
        if ( type == TeleportType::ResetPhysics )
            resetMotion();
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

    void PhysicsComponent::registerFixedStepFunction( GameObjectManager& manager, FixedStepGroup group, FixedStepBatchFunction pFunction )
    {
        // 차례는 단계 버킷 안의 등록 차례다(바디 → 관절 → 캐릭터) — 컴포넌트마다 `onRegister` 에서 넣으므로 예전 단계 목록과 같은 차례다.
        if ( manager.getFixedStepTickList().addFunction( *this, group, getFixedStepPhase(), pFunction ) == false )
            SW_LOG_ERROR_ID( "PhysicsComponent.FixedStepTwice", "Physics component registered fixed step group %# twice - the later method is ignored",
                             static_cast<uint32>( group ) );
    }

    TickPhase PhysicsComponent::getFixedStepPhase() const
    {
        switch ( _phase )
        {
            case PhysicsComponentPhase::Body:
                return TickPhase::Early;
            case PhysicsComponentPhase::Joint:
                return TickPhase::Normal; // 관절은 바디가 있어야 만든다
            case PhysicsComponentPhase::Character:
                return TickPhase::Late;
            case PhysicsComponentPhase::Count:
                break;
        }
        reportFatalError( "PhysicsComponent: unknown physics phase %#", static_cast<uint32>( _phase ) );
        return TickPhase::Early;
    }

    bool PhysicsComponent::isSimulated() const
    {
        return hasBegunPlay() && isActive() && isPendingDestroy() == false && getOwner() != nullptr && _bStreamingHeld == false;
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
#if defined( SW_DO_CHECKS )
        // 시뮬레이션이 낸 NaN(벌어진 구속 · 0 질량)을 트랜스폼 탓으로 보지 않게 물리 이름으로 먼저 알린다(언리얼 `DiagnosticCheckNaN`).
        if ( position.isFinite() == false || rotation.isFinite() == false || scale.isFinite() == false )
        {
            const GameObject*   pOwner = component.getOwner();
            const hashed_string name   = pOwner != nullptr ? pOwner->getName() : hashed_string{};
            SW_LOG_ERROR_ONCE_FOR_ID( "PhysicsComponent.NonFiniteBodyPose", name.view(),
                                      "Physics wrote a non-finite pose to '%#' (position %#, %#, %#; rotation %#, %#, %#, %#) - check the body's mass, constraints and applied forces",
                                      name.c_str(), position._x, position._y, position._z, rotation._x, rotation._y, rotation._z, rotation._w );
        }
#endif
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
