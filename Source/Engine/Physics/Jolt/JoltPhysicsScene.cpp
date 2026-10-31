#include "pch.h"

#include "Engine/Physics/Jolt/JoltPhysicsScene.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Physics/Collision/PhysicsMeshShapeCache.h"
#include "Engine/Physics/Jolt/JoltJobSystem.h"
#include "Engine/Physics/Jolt/JoltUtil.h"
#include "Engine/Physics/PhysicsDebugDraw.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/EstimateCollisionResponse.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

namespace sw
{
    SW_LOG_CALLER( "JoltPhysicsScene" );

    namespace
    {
        struct JoltPhysicsSceneInternal
        {
            static constexpr JPH::uint kMaxBodyCount          = 65536;
            static constexpr JPH::uint kMaxBodyPairCount      = 65536;
            static constexpr JPH::uint kMaxContactCount       = 32768;
            static constexpr JPH::uint kTempAllocatorByteSize = static_cast<JPH::uint>( 16 * constant::kBytesPerMegabyte );
            /** @brief 삼각 메시 바디에 주는 질량 상자의 가장 얇은 변(m)입니다 — 평평한 메시의 질량이 0 이 되지 않게 한다. */
            static constexpr float32 kMinMeshMassBoxSize = 0.1f;

            static JPH::EMotionType toMotionType( PhysicsBodyType type )
            {
                switch ( type )
                {
                    case PhysicsBodyType::Static:
                        return JPH::EMotionType::Static;
                    case PhysicsBodyType::Kinematic:
                        return JPH::EMotionType::Kinematic;
                    case PhysicsBodyType::Dynamic:
                        return JPH::EMotionType::Dynamic;
                }
                return JPH::EMotionType::Dynamic;
            }

            static bool isIdentityLocal( const PhysicsShapeDesc3D& shape )
            {
                const float3& position = shape._localPosition;
                const float3& rotation = shape._localRotation;
                return position._x == 0.0f && position._y == 0.0f && position._z == 0.0f && rotation._x == 0.0f && rotation._y == 0.0f && rotation._z == 0.0f;
            }

            /** @brief 상자 반 크기에 맞는 볼록 반지름입니다. 기본값(0.05)이 반 크기보다 크면 Jolt 가 상자를 거부합니다. */
            static float32 computeBoxConvexRadius( const float3& halfExtents )
            {
                float32 smallest    = halfExtents._x < halfExtents._y ? halfExtents._x : halfExtents._y;
                smallest            = smallest < halfExtents._z ? smallest : halfExtents._z;
                const float32 limit = smallest * 0.5f;
                return limit < JPH::cDefaultConvexRadius ? limit : JPH::cDefaultConvexRadius;
            }

            /**
             * @brief 만들 때 넘기는 각속도를 바디 상한 안으로 줄입니다.
             * @details Jolt 는 상한을 넘는 시작 각속도에 단언한다. 상한에 붙어 돌던 바디에서 읽은 값(갈라진 파괴 덩어리가 부모 운동을 이을 때)도
             *          길이를 다시 측정하면 반올림으로 상한을 한 ulp 넘을 수 있어서, 조금 안쪽으로 줄인다.
             */
            static JPH::Vec3 clampAngularVelocity( const JPH::Vec3& velocity, float32 maxLength )
            {
                const float32 limit    = maxLength * 0.999f;
                const float32 lengthSq = velocity.LengthSq();
                return lengthSq > limit * limit ? velocity * ( limit / ::sqrtf( lengthSq ) ) : velocity;
            }

            static float32 combineFriction( float32 frictionA, float32 frictionB ) { return ::sqrtf( frictionA * frictionB ); }
            static float32 combineRestitution( float32 restitutionA, float32 restitutionB ) { return restitutionA > restitutionB ? restitutionA : restitutionB; }

            static const JoltPhysicsMaterial* findOurMaterial( const JPH::PhysicsMaterial* pMaterial )
            {
                if ( pMaterial == nullptr || pMaterial == JPH::PhysicsMaterial::sDefault.GetPtr() )
                    return nullptr;
                // 이 씬이 만드는 셰이프는 모두 `JoltPhysicsMaterial` 을 단다(기본 재질만 예외).
                return static_cast<const JoltPhysicsMaterial*>( pMaterial );
            }
            /** @brief 질의 마스크(엔진 레이어 비트)로 오브젝트 레이어를 거릅니다. */
            class LayerMaskFilter final : public JPH::ObjectLayerFilter
            {
            public:
                explicit LayerMaskFilter( uint32 layerMask )
                    : _layerMask{ layerMask }
                {
                }

                bool ShouldCollide( JPH::ObjectLayer layer ) const override { return ( _layerMask & ( 1u << JoltLayerUtil::getLayer( layer ) ) ) != 0; }

            private:
                uint32 _layerMask;
            };

            /** @brief 쏘는 쪽 바디와 쏘는 오브젝트의 모든 바디(사용자 값 기준), 그리고 원하면 트리거를 결과에서 뺍니다. */
            class QueryBodyFilter final : public JPH::BodyFilter
            {
            public:
                QueryBodyFilter( const JoltPhysicsScene& scene, JPH::BodyID ignoreBody, const PhysicsQueryFilter& filter )
                    : _scene{ scene }
                    , _ignoreBody{ ignoreBody }
                    , _ignoreUserData{ filter._ignoreUserData }
                    , _bIncludeTriggers{ filter._bIncludeTriggers }
                {
                }

                bool ShouldCollide( const JPH::BodyID& bodyID ) const override { return bodyID != _ignoreBody; }
                bool ShouldCollideLocked( const JPH::Body& body ) const override
                {
                    if ( _bIncludeTriggers == false && body.IsSensor() )
                        return false;
                    // 바디의 Jolt 사용자 값은 엔진 핸들이다. 엔진 사용자 값(오브젝트 id)은 씬의 기록에 있다.
                    return _ignoreUserData == 0 || _scene.getBodyUserData( PhysicsBodyHandle::fromPacked( body.GetUserData() ) ) != _ignoreUserData;
                }

            private:
                const JoltPhysicsScene& _scene;
                JPH::BodyID             _ignoreBody;
                uint64                  _ignoreUserData;
                bool                    _bIncludeTriggers;
            };

            /** @brief 캐릭터는 트리거에 막히지 않습니다. */
            class CharacterBodyFilter final : public JPH::BodyFilter
            {
            public:
                bool ShouldCollideLocked( const JPH::Body& body ) const override { return body.IsSensor() == false; }
            };

            /** @brief @p axis 에 수직인 단위 벡터 하나입니다. */
            static JPH::Vec3 makePerpendicular( JPH::Vec3Arg axis ) { return axis.GetNormalizedPerpendicular(); }

            static void applyMotor( JPH::TwoBodyConstraint& constraint, PhysicsJointType type, const PhysicsJointMotor& motor )
            {
                const bool bOff = motor._mode == PhysicsMotorMode::Off || motor._maxForce <= 0.0f;
                if ( type == PhysicsJointType::Hinge )
                {
                    JPH::HingeConstraint& hinge = static_cast<JPH::HingeConstraint&>( constraint );
                    if ( bOff )
                    {
                        hinge.SetMotorState( JPH::EMotorState::Off );
                        return;
                    }
                    hinge.GetMotorSettings().SetTorqueLimit( motor._maxForce );
                    if ( motor._mode == PhysicsMotorMode::Velocity )
                    {
                        hinge.SetTargetAngularVelocity( motor._target );
                        hinge.SetMotorState( JPH::EMotorState::Velocity );
                    }
                    else
                    {
                        hinge.SetTargetAngle( motor._target );
                        hinge.SetMotorState( JPH::EMotorState::Position );
                    }
                    return;
                }
                if ( type == PhysicsJointType::Slider )
                {
                    JPH::SliderConstraint& slider = static_cast<JPH::SliderConstraint&>( constraint );
                    if ( bOff )
                    {
                        slider.SetMotorState( JPH::EMotorState::Off );
                        return;
                    }
                    slider.GetMotorSettings().SetForceLimit( motor._maxForce );
                    if ( motor._mode == PhysicsMotorMode::Velocity )
                    {
                        slider.SetTargetVelocity( motor._target );
                        slider.SetMotorState( JPH::EMotorState::Velocity );
                    }
                    else
                    {
                        slider.SetTargetPosition( motor._target );
                        slider.SetMotorState( JPH::EMotorState::Position );
                    }
                }
            }

            /** @brief 관절 종류에 맞는 Jolt 구속을 만듭니다. 만들지 못하면 null 입니다. */
            static JPH::TwoBodyConstraint* createConstraint( JPH::BodyInterface& bodyInterface, const PhysicsJointDesc3D& desc, const JPH::BodyID& bodyIDA, const JPH::BodyID& bodyIDB )
            {
                const JPH::Vec3 anchor = JoltUtil::toJolt( desc._anchor );
                const JPH::Vec3 axis   = JoltUtil::toJolt( desc._axis ).NormalizedOr( JPH::Vec3::sAxisY() );

                JPH::TwoBodyConstraint* pConstraint = nullptr;
                switch ( desc._type )
                {
                    case PhysicsJointType::Fixed:
                    {
                        JPH::FixedConstraintSettings settings;
                        settings.SetEmbedded();
                        settings.mAutoDetectPoint = true;
                        pConstraint               = bodyInterface.CreateConstraint( &settings, bodyIDA, bodyIDB );
                        break;
                    }
                    case PhysicsJointType::Hinge:
                    {
                        JPH::HingeConstraintSettings settings;
                        settings.SetEmbedded();
                        settings.mPoint1 = settings.mPoint2 = anchor;
                        settings.mHingeAxis1 = settings.mHingeAxis2 = axis;
                        settings.mNormalAxis1 = settings.mNormalAxis2 = JoltPhysicsSceneInternal::makePerpendicular( axis );
                        if ( desc._bLimitsEnabled )
                        {
                            settings.mLimitsMin = desc._minLimit;
                            settings.mLimitsMax = desc._maxLimit;
                        }
                        pConstraint = bodyInterface.CreateConstraint( &settings, bodyIDA, bodyIDB );
                        break;
                    }
                    case PhysicsJointType::Cone:
                    {
                        JPH::SwingTwistConstraintSettings settings;
                        settings.SetEmbedded();
                        const JPH::Vec3 planeAxis = JoltUtil::toJolt( desc._normalAxis ).NormalizedOr( JoltPhysicsSceneInternal::makePerpendicular( axis ) );
                        settings.mPosition1 = settings.mPosition2 = anchor;
                        settings.mTwistAxis1 = settings.mTwistAxis2 = axis;
                        settings.mPlaneAxis1 = settings.mPlaneAxis2 = planeAxis;
                        // 엔진은 "축 둘레의 스윙 한계"로 적는다. Jolt 의 plane 반각이 plane 축 둘레, normal 반각이 그 둘에 수직인 축 둘레다.
                        settings.mPlaneHalfConeAngle  = desc._swingLimitNormal;
                        settings.mNormalHalfConeAngle = desc._swingLimitPlane;
                        settings.mTwistMinAngle       = desc._minLimit;
                        settings.mTwistMaxAngle       = desc._maxLimit;
                        pConstraint                   = bodyInterface.CreateConstraint( &settings, bodyIDA, bodyIDB );
                        break;
                    }
                    case PhysicsJointType::Distance:
                    {
                        JPH::DistanceConstraintSettings settings;
                        settings.SetEmbedded();
                        settings.mPoint1 = anchor;
                        settings.mPoint2 = JoltUtil::toJolt( desc._anchorB );
                        if ( desc._bLimitsEnabled )
                        {
                            settings.mMinDistance = desc._minLimit;
                            settings.mMaxDistance = desc._maxLimit;
                        }
                        pConstraint = bodyInterface.CreateConstraint( &settings, bodyIDA, bodyIDB );
                        break;
                    }
                    case PhysicsJointType::Slider:
                    {
                        JPH::SliderConstraintSettings settings;
                        settings.SetEmbedded();
                        settings.mPoint1 = settings.mPoint2 = anchor;
                        settings.SetSliderAxis( axis );
                        if ( desc._bLimitsEnabled )
                        {
                            settings.mLimitsMin = desc._minLimit;
                            settings.mLimitsMax = desc._maxLimit;
                        }
                        pConstraint = bodyInterface.CreateConstraint( &settings, bodyIDA, bodyIDB );
                        break;
                    }
                }
                return pConstraint;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    JoltPhysicsScene::JoltPhysicsScene( const PhysicsSettings& settings, JoltJobSystem* pJobSystem )
        : _settings{ settings }
        , _broadPhaseLayers{}
        , _objectVsBroadPhaseFilter{}
        , _objectLayerPairFilter{}
        , _contactListener{ *this }
        , _tempAllocator{ JoltPhysicsSceneInternal::kTempAllocatorByteSize }
        , _system{}
        , _pJobSystem{ pJobSystem }
        , _listJoltMaterial{}
        , _bodies{}
        , _joints{}
        , _characters{}
        , _vehicles{ _system }
        , _softBodies{ _system, _tempAllocator }
        , _shapes{}
        , _listBodyIDEntry{}
        , _pairFilter{}
        , _contactTracker{}
        , _listContactEvent{}
        , _listContactReport{}
        , _listMovedBody{}
        , _listActiveBodyID{}
        , _reportMutex{}
        , _bodyCount{ 0 }
    {
        _objectLayerPairFilter.setLayers( _settings.makeCollisionLayers() );
        _system.Init( JoltPhysicsSceneInternal::kMaxBodyCount, 0, JoltPhysicsSceneInternal::kMaxBodyPairCount, JoltPhysicsSceneInternal::kMaxContactCount,
                      _broadPhaseLayers, _objectVsBroadPhaseFilter, _objectLayerPairFilter );
        _system.SetGravity( JoltUtil::toJolt( _settings._gravity ) );
        _system.SetContactListener( &_contactListener );

        _listJoltMaterial.reserve( _settings._listMaterial.size() );
        for ( const PhysicsMaterialDef& material : _settings._listMaterial )
        {
            _listJoltMaterial.push_back( JPH::Ref<JoltPhysicsMaterial>{ JoltUtil::createObject<JoltPhysicsMaterial>( material._name, material._friction, material._restitution ) } );
        }
    }

    JoltPhysicsScene::~JoltPhysicsScene()
    {
        // 바퀴 차와 관절, 캐릭터, 바디 순서로 해제한다(차와 관절이 바디를 가리킨다).
        _vehicles.clear();
        _joints.forEach( [this]( JointRecord& joint )
        {
            if ( joint._pConstraint != nullptr )
                _system.RemoveConstraint( joint._pConstraint.GetPtr() );
            joint._pConstraint = nullptr;
        } );
        _joints.clear();
        _characters.clear();
        _softBodies.clear();
        JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        _bodies.forEach( [&bodyInterface]( BodyRecord& record )
        {
            if ( record._bCharacterInner )
                return; // 위 `_characters.clear()` 에서 Jolt 가 이미 지웠다
            if ( record._bEnabled )
                bodyInterface.RemoveBody( record._bodyID );
            bodyInterface.DestroyBody( record._bodyID );
        } );
        _bodies.clear();
        _shapes.clear();
        _system.SetContactListener( nullptr );
    }

    // --- 시뮬레이션 ---------------------------------------------------------------------------------------------------------

    void JoltPhysicsScene::step( float32 fixedDeltaTime )
    {
        SW_MEMORY_SCOPE( Physics );
        _listContactEvent.clear();
        _listContactReport.clear();
        // 스텝 앞에 깨어 있던 바디와 뒤에 깨어 있는 바디를 함께 든다(`getMovedBodies`) — 이 스텝에 잠든 바디도 마지막으로 조금 움직였다.
        _listMovedBody.clear();
        appendActiveBodies();
        if ( fixedDeltaTime > 0.0f )
        {
            const JPH::EPhysicsUpdateError error = _system.Update( fixedDeltaTime, 1, &_tempAllocator, _pJobSystem );
            if ( error != JPH::EPhysicsUpdateError::None )
                SW_LOG_WARNING( "Jolt step reported overflow flags 0x%# - raise the body pair / contact limits", static_cast<uint32>( error ) );
        }
        appendActiveBodies();
        VectorUtil::sortUnique( _listMovedBody );
        flushContactReports();
    }

    void JoltPhysicsScene::appendActiveBodies()
    {
        _system.GetActiveBodies( JPH::EBodyType::RigidBody, _listActiveBodyID );
        for ( const JPH::BodyID& bodyID : _listActiveBodyID )
        {
            const PhysicsBodyHandle body = findHandle( bodyID );
            if ( body.isValid() )
                _listMovedBody.push_back( body );
        }
    }

    void JoltPhysicsScene::setGravity( const float3& gravity )
    {
        _system.SetGravity( JoltUtil::toJolt( gravity ) );
    }

    float3 JoltPhysicsScene::getGravity() const
    {
        return JoltUtil::toEngine( _system.GetGravity() );
    }

    void JoltPhysicsScene::setLayerCollision( const CollisionLayers& layers )
    {
        _objectLayerPairFilter.setLayers( layers );
    }

    void JoltPhysicsScene::drawDebug( IPhysicsDebugRenderer& renderer ) const
    {
        _bodies.forEachHandle( [&]( SlotHandle, const BodyRecord& record )
        { drawBodyRecord( record, renderer ); } );
        _characters.forEachHandle( [&]( SlotHandle, const CharacterRecord& record )
        {
            PhysicsShapeDesc3D capsule;
            capsule._type       = PhysicsShapeType3D::Capsule;
            capsule._radius     = record._desc._radius;
            capsule._halfHeight = record._desc._halfHeight;
            const float3 center = record._state._position + float3{ 0.0f, record._desc._halfHeight + record._desc._radius, 0.0f };
            PhysicsDebugDrawUtil::drawShape3D( renderer, capsule, center, quaternion{}, float4{ 0.9f, 0.4f, 1.0f, 1.0f } );
        } );
        _softBodies.drawDebug( renderer );
    }

    bool JoltPhysicsScene::drawBodyDebug( PhysicsBodyHandle body, IPhysicsDebugRenderer& renderer ) const
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr )
            return false;
        drawBodyRecord( *pRecord, renderer );
        return true;
    }

    void JoltPhysicsScene::drawBodyRecord( const BodyRecord& record, IPhysicsDebugRenderer& renderer ) const
    {
        if ( record._bEnabled == false || record._pListShape == nullptr )
            return;
        const JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        JPH::RVec3                position;
        JPH::Quat                 rotation;
        bodyInterface.GetPositionAndRotation( record._bodyID, position, rotation );
        const bool   bSleeping = record._type != PhysicsBodyType::Static && bodyInterface.IsActive( record._bodyID ) == false;
        const float4 color     = PhysicsDebugDrawUtil::getBodyColor( static_cast<uint8>( record._type ), bSleeping, record._bTrigger );
        for ( const PhysicsShapeDesc3D& shape : *record._pListShape )
        {
            PhysicsDebugDrawUtil::drawShape3D( renderer, shape, JoltUtil::toEngine( position ), JoltUtil::toEngine( rotation ), color );
        }
    }

    // --- 셰이프 -----------------------------------------------------------------------------------------------------------

    const PhysicsMaterialDef* JoltPhysicsScene::resolveMaterial( const hashed_string& name, const hashed_string& fallback ) const
    {
        const hashed_string& wanted = name.empty() ? fallback : name;
        if ( wanted.empty() )
            return &_settings._listMaterial.front(); // 이름을 적지 않은 것은 "첫 재질" 이라는 선언이다
        const PhysicsMaterialDef* pMaterial = _settings.findMaterial( wanted );
        if ( pMaterial == nullptr )
            SW_LOG_ERROR( "Unknown physics material '%#' - the shape or body is not created", wanted.c_str() ); // 적은 이름이 틀리면 다른 재질로 만들지 않는다
        return pMaterial;
    }

    const JPH::PhysicsMaterial* JoltPhysicsScene::findJoltMaterial( const hashed_string& name ) const
    {
        for ( size_t materialIndex = 0; materialIndex < _settings._listMaterial.size(); ++materialIndex )
        {
            if ( _settings._listMaterial[materialIndex]._name == name )
                return _listJoltMaterial[materialIndex].GetPtr();
        }
        return _listJoltMaterial.front().GetPtr();
    }

    JPH::RefConst<JPH::Shape> JoltPhysicsScene::createSingleJoltShape( const PhysicsShapeDesc3D& shape, const PhysicsMaterialDef& material,
                                                                       const JPH::PhysicsMaterial* pMaterial ) const
    {
        JPH::Shape::ShapeResult result;
        switch ( shape._type )
        {
            case PhysicsShapeType3D::Box:
            {
                JPH::BoxShapeSettings settings{ JoltUtil::toJolt( shape._halfExtents ), JoltPhysicsSceneInternal::computeBoxConvexRadius( shape._halfExtents ), pMaterial };
                settings.SetEmbedded();
                settings.SetDensity( material._density );
                result = settings.Create();
                break;
            }
            case PhysicsShapeType3D::Sphere:
            {
                JPH::SphereShapeSettings settings{ shape._radius, pMaterial };
                settings.SetEmbedded();
                settings.SetDensity( material._density );
                result = settings.Create();
                break;
            }
            case PhysicsShapeType3D::Capsule:
            {
                JPH::CapsuleShapeSettings settings{ shape._halfHeight, shape._radius, pMaterial };
                settings.SetEmbedded();
                settings.SetDensity( material._density );
                result = settings.Create();
                break;
            }
            case PhysicsShapeType3D::ConvexHull:
            {
                const PhysicsShapeGeometry geometry = PhysicsMeshShapeCache::acquireGeometry( shape );
                JPH::Array<JPH::Vec3>      listPoint;
                listPoint.reserve( geometry._listPoint.size() );
                for ( const float3& point : geometry._listPoint )
                {
                    listPoint.push_back( JoltUtil::toJolt( point ) );
                }
                JPH::ConvexHullShapeSettings settings{ listPoint, JPH::cDefaultConvexRadius, pMaterial };
                settings.SetEmbedded();
                settings.SetDensity( material._density );
                result = settings.Create();
                break;
            }
            case PhysicsShapeType3D::TriangleMesh:
            {
                const PhysicsShapeGeometry geometry = PhysicsMeshShapeCache::acquireGeometry( shape );
                JPH::VertexList            listVertex;
                listVertex.reserve( geometry._listPoint.size() );
                for ( const float3& point : geometry._listPoint )
                {
                    listVertex.push_back( JPH::Float3{ point._x, point._y, point._z } );
                }
                JPH::IndexedTriangleList listTriangle;
                listTriangle.reserve( geometry._listIndex.size() / 3 );
                for ( size_t index = 0; index + 2 < geometry._listIndex.size(); index += 3 )
                {
                    listTriangle.push_back( JPH::IndexedTriangle{ geometry._listIndex[index], geometry._listIndex[index + 1], geometry._listIndex[index + 2], 0 } );
                }
                JPH::PhysicsMaterialList listMaterial;
                listMaterial.push_back( pMaterial );
                JPH::MeshShapeSettings settings{ std::move( listVertex ), std::move( listTriangle ), std::move( listMaterial ) };
                settings.SetEmbedded();
                result = settings.Create();
                break;
            }
        }
        if ( result.HasError() )
        {
            SW_LOG_ERROR( "Jolt rejected a %# shape: %#", static_cast<uint32>( shape._type ), result.GetError().c_str() );
            return nullptr;
        }
        return result.Get();
    }

    JPH::RefConst<JPH::Shape> JoltPhysicsScene::createJoltShape( span<const PhysicsShapeDesc3D> listShape, const hashed_string& defaultMaterial, bool bAllowMesh ) const
    {
        JPH::RefConst<JPH::Shape> pResult;
        if ( listShape.empty() )
        {
            SW_LOG_ERROR( "A physics body needs at least one shape" );
            return pResult;
        }
        JPH::StaticCompoundShapeSettings compound;
        compound.SetEmbedded();
        for ( const PhysicsShapeDesc3D& shape : listShape )
        {
            if ( shape._type == PhysicsShapeType3D::TriangleMesh && bAllowMesh == false )
            {
                SW_LOG_ERROR( "Triangle mesh shapes need a Static or Kinematic body (a moving mesh has no volume)" );
                return pResult;
            }
            const PhysicsMaterialDef* pMaterial = resolveMaterial( shape._material, defaultMaterial );
            if ( pMaterial == nullptr )
                return pResult;
            JPH::RefConst<JPH::Shape> pShape = createSingleJoltShape( shape, *pMaterial, findJoltMaterial( pMaterial->_name ) );
            if ( pShape == nullptr )
                return pResult;
            if ( listShape.size() == 1 )
            {
                if ( JoltPhysicsSceneInternal::isIdentityLocal( shape ) )
                {
                    pResult = pShape;
                    return pResult;
                }
                JPH::RotatedTranslatedShapeSettings offset{ JoltUtil::toJolt( shape._localPosition ),
                                                            JoltUtil::toJolt( quaternion::makeFromYawPitchRoll( shape._localRotation ) ), pShape.GetPtr() };
                offset.SetEmbedded();
                JPH::Shape::ShapeResult result = offset.Create();
                if ( result.HasError() )
                {
                    SW_LOG_ERROR( "Jolt rejected a shape offset: %#", result.GetError().c_str() );
                    return pResult;
                }
                pResult = result.Get();
                return pResult;
            }
            compound.AddShape( JoltUtil::toJolt( shape._localPosition ), JoltUtil::toJolt( quaternion::makeFromYawPitchRoll( shape._localRotation ) ), pShape.GetPtr() );
        }
        JPH::Shape::ShapeResult result = compound.Create();
        if ( result.HasError() )
        {
            SW_LOG_ERROR( "Jolt rejected a compound shape: %#", result.GetError().c_str() );
            return pResult;
        }
        pResult = result.Get();
        return pResult;
    }

    PhysicsShapeHandle JoltPhysicsScene::createShape( span<const PhysicsShapeDesc3D> listShape, const hashed_string& material )
    {
        ShapeRecord record;
        record._pShape = createJoltShape( listShape, material, true );
        if ( record._pShape == nullptr )
            return PhysicsShapeHandle{};
        record._pListShape = make_shared<const ShapeDescList>( listShape.begin(), listShape.end() );
        return PhysicsShapeHandle::fromSlot( _shapes.insert( std::move( record ) ) );
    }

    PhysicsShapeHandle JoltPhysicsScene::createCompoundShape( span<const PhysicsShapeHandle> listChild )
    {
        if ( listChild.empty() )
        {
            SW_LOG_ERROR( "createCompoundShape: no child shapes" );
            return PhysicsShapeHandle{};
        }
        JPH::StaticCompoundShapeSettings compound;
        compound.SetEmbedded();
        ShapeDescList listDesc; // 선 그리기용이다. 자식 서술자를 이어 붙인다.
        for ( const PhysicsShapeHandle& child : listChild )
        {
            const ShapeRecord* pChild = _shapes.get( child.getSlot() );
            if ( pChild == nullptr )
            {
                SW_LOG_ERROR( "createCompoundShape: a child shape handle is stale" );
                return PhysicsShapeHandle{};
            }
            compound.AddShape( JPH::Vec3::sZero(), JPH::Quat::sIdentity(), pChild->_pShape.GetPtr() );
            if ( pChild->_pListShape != nullptr )
                listDesc.insert( listDesc.end(), pChild->_pListShape->begin(), pChild->_pListShape->end() );
        }
        JPH::Shape::ShapeResult result = compound.Create();
        if ( result.HasError() )
        {
            SW_LOG_ERROR( "Jolt rejected a compound shape: %#", result.GetError().c_str() );
            return PhysicsShapeHandle{};
        }
        ShapeRecord record;
        record._pShape     = result.Get();
        record._pListShape = make_shared<const ShapeDescList>( std::move( listDesc ) );
        return PhysicsShapeHandle::fromSlot( _shapes.insert( std::move( record ) ) );
    }

    void JoltPhysicsScene::destroyShape( PhysicsShapeHandle shape )
    {
        _shapes.erase( shape.getSlot() );
    }

    // --- 바디 -------------------------------------------------------------------------------------------------------------

    const JoltPhysicsScene::BodyRecord* JoltPhysicsScene::findBody( PhysicsBodyHandle body ) const
    {
        return _bodies.get( body.getSlot() );
    }

    JoltPhysicsScene::BodyRecord* JoltPhysicsScene::findBody( PhysicsBodyHandle body )
    {
        return _bodies.get( body.getSlot() );
    }

    PhysicsBodyHandle JoltPhysicsScene::findHandle( const JPH::BodyID& bodyID ) const
    {
        const JPH::uint32 index = bodyID.GetIndex();
        if ( index >= _listBodyIDEntry.size() || _listBodyIDEntry[index]._bodyIDValue != bodyID.GetIndexAndSequenceNumber() )
            return PhysicsBodyHandle{};
        return _listBodyIDEntry[index]._body;
    }

    void JoltPhysicsScene::setHandle( const JPH::BodyID& bodyID, PhysicsBodyHandle body )
    {
        const JPH::uint32 index = bodyID.GetIndex();
        if ( index >= _listBodyIDEntry.size() )
            _listBodyIDEntry.resize( static_cast<size_t>( index ) + 1 );
        _listBodyIDEntry[index]._bodyIDValue = body.isValid() ? bodyID.GetIndexAndSequenceNumber() : JPH::BodyID::cInvalidBodyID;
        _listBodyIDEntry[index]._body        = body;
    }

    PhysicsBodyHandle JoltPhysicsScene::createBodyUnadded( const PhysicsBodyDesc3D& desc )
    {
        PhysicsBodyHandle         handle{};
        BodyRecord                record;
        JPH::RefConst<JPH::Shape> pShape;
        if ( desc._sharedShape.isValid() )
        {
            const ShapeRecord* pShapeRecord = _shapes.get( desc._sharedShape.getSlot() );
            if ( pShapeRecord == nullptr )
            {
                SW_LOG_ERROR( "createBody: the shared shape handle is stale" );
                return handle;
            }
            pShape             = pShapeRecord->_pShape;
            record._pListShape = pShapeRecord->_pListShape;
        }
        else
        {
            pShape = createJoltShape( span<const PhysicsShapeDesc3D>{ desc._listShape.data(), desc._listShape.size() }, desc._material, desc._type != PhysicsBodyType::Dynamic );
            if ( pShape == nullptr )
                return handle;
            record._pListShape = make_shared<const ShapeDescList>( desc._listShape );
        }
        if ( desc._type == PhysicsBodyType::Dynamic && pShape->MustBeStatic() )
        {
            SW_LOG_ERROR( "createBody: a Dynamic body cannot use a triangle mesh shape" );
            return handle;
        }

        const PhysicsMaterialDef* pMaterialDef = resolveMaterial( desc._material, hashed_string{} );
        if ( pMaterialDef == nullptr )
            return handle;
        const PhysicsMaterialDef& material = *pMaterialDef;
        const bool                bMoving  = desc._type != PhysicsBodyType::Static;
        JPH::BodyCreationSettings settings{ pShape.GetPtr(), JoltUtil::toJolt( desc._position ), JoltUtil::toJolt( desc._rotation ),
                                            JoltPhysicsSceneInternal::toMotionType( desc._type ), JoltLayerUtil::makeObjectLayer( desc._layer, bMoving ) };
        settings.mFriction                = material._friction;
        settings.mRestitution             = material._restitution;
        settings.mLinearDamping           = desc._linearDamping;
        settings.mAngularDamping          = desc._angularDamping;
        settings.mGravityFactor           = desc._gravityFactor;
        settings.mIsSensor                = desc._bTrigger || desc._bOverlapOnly;
        settings.mAllowSleeping           = desc._bAllowSleep;
        settings.mAllowDynamicOrKinematic = desc._bAllowTypeChange;
        settings.mMotionQuality           = desc._bContinuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        settings.mLinearVelocity          = JoltUtil::toJolt( desc._linearVelocity );
        settings.mAngularVelocity         = JoltPhysicsSceneInternal::clampAngularVelocity( JoltUtil::toJolt( desc._angularVelocity ), settings.mMaxAngularVelocity );
        if ( desc._bLockRotation )
            settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
        const bool bHasMotion = bMoving || desc._bAllowTypeChange;
        if ( bHasMotion && pShape->MustBeStatic() )
        {
            // 삼각 메시는 부피가 없어 Jolt 가 질량을 셀 수 없다(움직임 정보를 만들면 단언) — 경계 상자의 질량 · 관성을 준다. 평평한 메시도 두께를 둔다.
            const JPH::Vec3     boxSize = JPH::Vec3::sMax( pShape->GetLocalBounds().GetSize(), JPH::Vec3::sReplicate( JoltPhysicsSceneInternal::kMinMeshMassBoxSize ) );
            JPH::MassProperties massProperties;
            massProperties.SetMassAndInertiaOfSolidBox( boxSize, material._density );
            if ( desc._mass > 0.0f )
                massProperties.ScaleToMass( desc._mass );
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride = massProperties;
        }
        else if ( desc._mass > 0.0f && bMoving )
        {
            settings.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = desc._mass;
        }

        JPH::Body* pBody = _system.GetBodyInterface().CreateBody( settings );
        if ( pBody == nullptr )
        {
            SW_LOG_ERROR( "createBody: Jolt is out of bodies (limit %#)", JoltPhysicsSceneInternal::kMaxBodyCount );
            return handle;
        }
        record._bodyID   = pBody->GetID();
        record._userData = desc._userData;
        record._type     = desc._type;
        record._layer    = desc._layer;
        record._bTrigger = desc._bTrigger;
        record._bEnabled = true;
        handle           = PhysicsBodyHandle::fromSlot( _bodies.insert( std::move( record ) ) );
        pBody->SetUserData( handle.packed() );
        setHandle( pBody->GetID(), handle );
        ++_bodyCount;
        return handle;
    }

    PhysicsBodyHandle JoltPhysicsScene::createBody( const PhysicsBodyDesc3D& desc )
    {
        PhysicsBodyHandle handle  = createBodyUnadded( desc );
        const BodyRecord* pRecord = findBody( handle );
        if ( pRecord == nullptr )
        {
            handle = PhysicsBodyHandle{};
            return handle;
        }
        const bool bActivate = desc._type != PhysicsBodyType::Static;
        _system.GetBodyInterface().AddBody( pRecord->_bodyID, bActivate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate );
        return handle;
    }

    void JoltPhysicsScene::createBodies( span<const PhysicsBodyDesc3D> listDesc, vector<PhysicsBodyHandle>& outListBody )
    {
        // 넓은 단계 트리를 한 번에 고치려고, 움직이는 것과 정적인 것을 따로 모아 배치로 넣는다.
        vector<JPH::BodyID> listMovingID;
        vector<JPH::BodyID> listStaticID;
        outListBody.reserve( outListBody.size() + listDesc.size() );
        for ( const PhysicsBodyDesc3D& desc : listDesc )
        {
            const PhysicsBodyHandle handle  = createBodyUnadded( desc );
            const BodyRecord*       pRecord = findBody( handle );
            outListBody.push_back( handle );
            if ( pRecord == nullptr )
                continue;
            if ( desc._type == PhysicsBodyType::Static )
                listStaticID.push_back( pRecord->_bodyID );
            else
                listMovingID.push_back( pRecord->_bodyID );
        }
        JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        if ( listStaticID.empty() == false )
        {
            const JPH::BodyInterface::AddState state = bodyInterface.AddBodiesPrepare( listStaticID.data(), static_cast<int32>( listStaticID.size() ) );
            bodyInterface.AddBodiesFinalize( listStaticID.data(), static_cast<int32>( listStaticID.size() ), state, JPH::EActivation::DontActivate );
        }
        if ( listMovingID.empty() == false )
        {
            const JPH::BodyInterface::AddState state = bodyInterface.AddBodiesPrepare( listMovingID.data(), static_cast<int32>( listMovingID.size() ) );
            bodyInterface.AddBodiesFinalize( listMovingID.data(), static_cast<int32>( listMovingID.size() ), state, JPH::EActivation::Activate );
        }
    }

    void JoltPhysicsScene::destroyBody( PhysicsBodyHandle body )
    {
        destroyBodies( span<const PhysicsBodyHandle>{ &body, 1 } );
    }

    void JoltPhysicsScene::destroyJointsOf( span<const PhysicsBodyHandle> listBody )
    {
        vector<PhysicsJointHandle> listDoomed;
        _joints.forEachHandle( [&]( SlotHandle slot, const JointRecord& joint )
        {
            for ( const PhysicsBodyHandle& body : listBody )
            {
                if ( joint._bodyA == body || joint._bodyB == body )
                {
                    listDoomed.push_back( PhysicsJointHandle::fromSlot( slot ) );
                    return;
                }
            }
        } );
        for ( const PhysicsJointHandle& joint : listDoomed )
        {
            destroyJoint( joint );
        }
        // 차체가 지워지는 바퀴 차도 함께.
        _vehicles.destroyAttachedTo( listBody );
    }

    void JoltPhysicsScene::destroyBodies( span<const PhysicsBodyHandle> listBody )
    {
        if ( listBody.empty() )
            return;
        destroyJointsOf( listBody );
        vector<JPH::BodyID>       listRemoveID;
        vector<JPH::BodyID>       listDestroyID;
        vector<PhysicsBodyHandle> listDestroyed;
        for ( const PhysicsBodyHandle& body : listBody )
        {
            const BodyRecord* pRecord = findBody( body );
            if ( pRecord != nullptr && pRecord->_bCharacterInner )
            {
                SW_LOG_ERROR( "destroyBody: this body is a character's inner body - destroy the character instead" );
                continue;
            }
            BodyRecord record;
            if ( _bodies.take( body.getSlot(), record ) == false )
                continue;
            if ( record._bEnabled )
                listRemoveID.push_back( record._bodyID );
            listDestroyID.push_back( record._bodyID );
            setHandle( record._bodyID, PhysicsBodyHandle{} );
            listDestroyed.push_back( body );
            --_bodyCount;
        }
        JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        if ( listRemoveID.empty() == false )
            bodyInterface.RemoveBodies( listRemoveID.data(), static_cast<int32>( listRemoveID.size() ) );
        if ( listDestroyID.empty() == false )
            bodyInterface.DestroyBodies( listDestroyID.data(), static_cast<int32>( listDestroyID.size() ) );
        const span<const PhysicsBodyHandle> destroyed{ listDestroyed.data(), listDestroyed.size() };
        _contactTracker.removeBodies( destroyed );
        _pairFilter.removeBodies( destroyed );
    }

    bool JoltPhysicsScene::isBodyValid( PhysicsBodyHandle body ) const
    {
        return findBody( body ) != nullptr;
    }

    uint32 JoltPhysicsScene::getBodyCount() const
    {
        return _bodyCount;
    }

    void JoltPhysicsScene::setBodyEnabled( PhysicsBodyHandle body, bool bEnabled )
    {
        BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr || pRecord->_bEnabled == bEnabled )
            return;
        JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        if ( bEnabled )
        {
            bodyInterface.AddBody( pRecord->_bodyID, pRecord->_type != PhysicsBodyType::Static ? JPH::EActivation::Activate : JPH::EActivation::DontActivate );
        }
        else
        {
            bodyInterface.RemoveBody( pRecord->_bodyID );
            // 빠진 바디의 접촉은 Jolt 가 알리지 않는다. 추적기가 끝낸다.
            _contactTracker.removeBodies( span<const PhysicsBodyHandle>{ &body, 1 } );
        }
        pRecord->_bEnabled = bEnabled;
    }

    bool JoltPhysicsScene::isBodyEnabled( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr && pRecord->_bEnabled;
    }

    bool JoltPhysicsScene::getBodyTransform( PhysicsBodyHandle body, float3& outPosition, quaternion& outRotation ) const
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr )
            return false;
        JPH::RVec3 position;
        JPH::Quat  rotation;
        _system.GetBodyInterface().GetPositionAndRotation( pRecord->_bodyID, position, rotation );
        outPosition = JoltUtil::toEngine( position );
        outRotation = JoltUtil::toEngine( rotation );
        return true;
    }

    void JoltPhysicsScene::setBodyTransform( PhysicsBodyHandle body, const float3& position, const quaternion& rotation )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr )
            return;
        const bool             bStatic    = pRecord->_type == PhysicsBodyType::Static;
        const JPH::EActivation activation = bStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
        JPH::BodyInterface&    bodies     = _system.GetBodyInterface();
        // 정적 바디를 옮기면 옛 자리 위에서 자던 바디가 받침을 잃는다 — Jolt 는 정적 바디 이동으로 이웃을 깨우지 않으므로 옛 자리의 경계 안 바디를 깨운다.
        const JPH::AABox oldBounds = bStatic ? bodies.GetTransformedShape( pRecord->_bodyID ).GetWorldSpaceBounds() : JPH::AABox{};
        bodies.SetPositionAndRotation( pRecord->_bodyID, JoltUtil::toJolt( position ), JoltUtil::toJolt( rotation ), activation );
        if ( bStatic )
            bodies.ActivateBodiesInAABox( oldBounds, JPH::BroadPhaseLayerFilter{}, JPH::ObjectLayerFilter{} );
    }

    void JoltPhysicsScene::moveKinematic( PhysicsBodyHandle body, const float3& targetPosition, const quaternion& targetRotation, float32 deltaTime )
    {
        const BodyRecord* pRecord           = findBody( body );
        const bool        bNotKinematicBody = pRecord == nullptr || pRecord->_type != PhysicsBodyType::Kinematic || deltaTime <= 0.0f;
        if ( bNotKinematicBody )
            return;
        _system.GetBodyInterface().MoveKinematic( pRecord->_bodyID, JoltUtil::toJolt( targetPosition ), JoltUtil::toJolt( targetRotation ), deltaTime );
    }

    float3 JoltPhysicsScene::getLinearVelocity( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? JoltUtil::toEngine( _system.GetBodyInterface().GetLinearVelocity( pRecord->_bodyID ) ) : float3{};
    }

    void JoltPhysicsScene::setLinearVelocity( PhysicsBodyHandle body, const float3& velocity )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().SetLinearVelocity( pRecord->_bodyID, JoltUtil::toJolt( velocity ) );
    }

    float3 JoltPhysicsScene::getAngularVelocity( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? JoltUtil::toEngine( _system.GetBodyInterface().GetAngularVelocity( pRecord->_bodyID ) ) : float3{};
    }

    void JoltPhysicsScene::setAngularVelocity( PhysicsBodyHandle body, const float3& velocity )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().SetAngularVelocity( pRecord->_bodyID, JoltUtil::toJolt( velocity ) );
    }

    void JoltPhysicsScene::addForce( PhysicsBodyHandle body, const float3& force )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().AddForce( pRecord->_bodyID, JoltUtil::toJolt( force ) );
    }

    void JoltPhysicsScene::addImpulse( PhysicsBodyHandle body, const float3& impulse )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().AddImpulse( pRecord->_bodyID, JoltUtil::toJolt( impulse ) );
    }

    void JoltPhysicsScene::addImpulseAtPoint( PhysicsBodyHandle body, const float3& impulse, const float3& point )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().AddImpulse( pRecord->_bodyID, JoltUtil::toJolt( impulse ), JoltUtil::toJolt( point ) );
    }

    bool JoltPhysicsScene::applyBuoyancy( PhysicsBodyHandle body, const PhysicsBuoyancyDesc& desc, float32 deltaSeconds )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr )
            return false;
        JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        // 고정 · 키네마틱 바디는 힘을 받지 않는다(Jolt 가 단언한다) — 움직이는 바디만.
        if ( bodyInterface.GetMotionType( pRecord->_bodyID ) != JPH::EMotionType::Dynamic )
            return false;
        // 잠긴 부피 · 부력 중심은 셰이프에서 구한다. 바디 인터페이스 판은 자는 바디를 깨운다(물에 떨어진 상자가 잠든 채 떠 있지 않게).
        return bodyInterface.ApplyBuoyancyImpulse( pRecord->_bodyID, JPH::RVec3( JoltUtil::toJolt( desc._surfacePoint ) ), JoltUtil::toJolt( desc._surfaceNormal ),
                                                   desc._buoyancy, desc._linearDrag, desc._angularDrag, JoltUtil::toJolt( desc._fluidVelocity ),
                                                   _system.GetGravity(), deltaSeconds );
    }

    void JoltPhysicsScene::addTorque( PhysicsBodyHandle body, const float3& torque )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().AddTorque( pRecord->_bodyID, JoltUtil::toJolt( torque ) );
    }

    void JoltPhysicsScene::setBodyType( PhysicsBodyHandle body, PhysicsBodyType type )
    {
        BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr || pRecord->_type == type )
            return;
        JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        if ( pRecord->_type == PhysicsBodyType::Static )
        {
            // 정적으로 만든 바디는 움직임 정보가 없다. `_bAllowTypeChange` 로 만든 것만 바꿀 수 있다.
            const JPH::BodyLockRead lock{ _system.GetBodyLockInterface(), pRecord->_bodyID };
            if ( lock.Succeeded() && lock.GetBody().GetMotionPropertiesUnchecked() == nullptr )
            {
                SW_LOG_ERROR( "setBodyType: the body was created Static without _bAllowTypeChange" );
                return;
            }
        }
        const JPH::EActivation activation = type == PhysicsBodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
        bodyInterface.SetMotionType( pRecord->_bodyID, JoltPhysicsSceneInternal::toMotionType( type ), activation );
        bodyInterface.SetObjectLayer( pRecord->_bodyID, JoltLayerUtil::makeObjectLayer( pRecord->_layer, type != PhysicsBodyType::Static ) );
        pRecord->_type = type;
    }

    PhysicsBodyType JoltPhysicsScene::getBodyType( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? pRecord->_type : PhysicsBodyType::Static;
    }

    void JoltPhysicsScene::setBodyLayer( PhysicsBodyHandle body, uint8 layer )
    {
        BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr || layer >= CollisionLayers::kLayerCount )
            return;
        _system.GetBodyInterface().SetObjectLayer( pRecord->_bodyID, JoltLayerUtil::makeObjectLayer( layer, pRecord->_type != PhysicsBodyType::Static ) );
        pRecord->_layer = layer;
    }

    uint8 JoltPhysicsScene::getBodyLayer( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? pRecord->_layer : 0;
    }

    void JoltPhysicsScene::setGravityFactor( PhysicsBodyHandle body, float32 factor )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().SetGravityFactor( pRecord->_bodyID, factor );
    }

    float32 JoltPhysicsScene::getBodyMass( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr )
            return 0.0f;
        const JPH::BodyLockRead lock{ _system.GetBodyLockInterface(), pRecord->_bodyID };
        if ( lock.Succeeded() == false || lock.GetBody().GetMotionPropertiesUnchecked() == nullptr )
            return 0.0f;
        const float32 inverseMass = lock.GetBody().GetMotionPropertiesUnchecked()->GetInverseMassUnchecked();
        return inverseMass > 0.0f ? 1.0f / inverseMass : 0.0f;
    }

    bool JoltPhysicsScene::isBodySleeping( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr && pRecord->_type != PhysicsBodyType::Static && _system.GetBodyInterface().IsActive( pRecord->_bodyID ) == false;
    }

    void JoltPhysicsScene::wakeBody( PhysicsBodyHandle body )
    {
        const BodyRecord* pRecord      = findBody( body );
        const bool        bActivatable = pRecord != nullptr && pRecord->_bEnabled && pRecord->_type != PhysicsBodyType::Static;
        if ( bActivatable )
            _system.GetBodyInterface().ActivateBody( pRecord->_bodyID );
    }

    uint64 JoltPhysicsScene::getBodyUserData( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? pRecord->_userData : 0;
    }

    bool JoltPhysicsScene::isBodyTrigger( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr && pRecord->_bTrigger;
    }

    void JoltPhysicsScene::setPairCollision( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, bool bCollide )
    {
        _pairFilter.setPairCollision( bodyA, bodyB, bCollide );
    }

    // --- 접촉 -------------------------------------------------------------------------------------------------------------

    JoltContactListener::JoltContactListener( JoltPhysicsScene& owner )
        : _owner{ owner }
    {
    }

    JPH::ValidateResult JoltContactListener::OnContactValidate( const JPH::Body& bodyA, const JPH::Body& bodyB, JPH::RVec3Arg, const JPH::CollideShapeResult& )
    {
        return _owner.validateContact( bodyA, bodyB );
    }

    void JoltContactListener::OnContactAdded( const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold, JPH::ContactSettings& ioSettings )
    {
        _owner.recordContact( bodyA, bodyB, manifold, ioSettings, 0 );
    }

    void JoltContactListener::OnContactPersisted( const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold,
                                                  JPH::ContactSettings& ioSettings )
    {
        _owner.recordContact( bodyA, bodyB, manifold, ioSettings, 1 );
    }

    void JoltContactListener::OnContactRemoved( const JPH::SubShapeIDPair& subShapePair )
    {
        _owner.recordContactRemoved( subShapePair );
    }

    JPH::ValidateResult JoltPhysicsScene::validateContact( const JPH::Body& bodyA, const JPH::Body& bodyB ) const
    {
        if ( _pairFilter.isEmpty() )
            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        const PhysicsBodyHandle handleA = PhysicsBodyHandle::fromPacked( bodyA.GetUserData() );
        const PhysicsBodyHandle handleB = PhysicsBodyHandle::fromPacked( bodyB.GetUserData() );
        return _pairFilter.canCollide( handleA, handleB ) ? JPH::ValidateResult::AcceptAllContactsForThisBodyPair
                                                          : JPH::ValidateResult::RejectAllContactsForThisBodyPair;
    }

    void JoltPhysicsScene::recordContact( const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold, JPH::ContactSettings& ioSettings,
                                          uint8 kind )
    {
        // 서브 셰이프의 재질로 마찰과 반발을 섞는다(바디 값은 첫 셰이프의 재질).
        const JoltPhysicsMaterial* pMaterialA = JoltPhysicsSceneInternal::findOurMaterial( bodyA.GetShape()->GetMaterial( manifold.mSubShapeID1 ) );
        const JoltPhysicsMaterial* pMaterialB = JoltPhysicsSceneInternal::findOurMaterial( bodyB.GetShape()->GetMaterial( manifold.mSubShapeID2 ) );
        if ( pMaterialA != nullptr && pMaterialB != nullptr )
        {
            ioSettings.mCombinedFriction    = JoltPhysicsSceneInternal::combineFriction( pMaterialA->getFriction(), pMaterialB->getFriction() );
            ioSettings.mCombinedRestitution = JoltPhysicsSceneInternal::combineRestitution( pMaterialA->getRestitution(), pMaterialB->getRestitution() );
        }

        ContactReport report;
        report._bodyA._body     = PhysicsBodyHandle::fromPacked( bodyA.GetUserData() );
        report._bodyB._body     = PhysicsBodyHandle::fromPacked( bodyB.GetUserData() );
        report._bodyA._bTrigger = bodyA.IsSensor();
        report._bodyB._bTrigger = bodyB.IsSensor();
        report._point           = JoltUtil::toEngine( manifold.GetWorldSpaceContactPointOn1( 0 ) );
        report._normal          = JoltUtil::toEngine( manifold.mWorldSpaceNormal );
        report._subShapeA       = manifold.mSubShapeID1.GetValue();
        report._subShapeB       = manifold.mSubShapeID2.GetValue();
        report._kind            = kind;
        if ( bodyA.IsSensor() == false && bodyB.IsSensor() == false )
        {
            JPH::CollisionEstimationResult estimate;
            JPH::EstimateCollisionResponse( bodyA, bodyB, manifold, estimate, ioSettings.mCombinedFriction, ioSettings.mCombinedRestitution );
            float32 impulse = 0.0f;
            for ( const float32 contactImpulse : estimate.mContactImpulse )
            {
                impulse += contactImpulse;
            }
            report._impulse = impulse;
        }
        ScopedLock<mutex> lock{ _reportMutex };
        _listContactReport.push_back( report );
    }

    void JoltPhysicsScene::recordContactRemoved( const JPH::SubShapeIDPair& subShapePair )
    {
        ContactReport report;
        report._bodyA._body = findHandle( subShapePair.GetBody1ID() );
        report._bodyB._body = findHandle( subShapePair.GetBody2ID() );
        report._subShapeA   = subShapePair.GetSubShapeID1().GetValue();
        report._subShapeB   = subShapePair.GetSubShapeID2().GetValue();
        report._kind        = 2;
        ScopedLock<mutex> lock{ _reportMutex };
        _listContactReport.push_back( report );
    }

    bool JoltPhysicsScene::isEarlierReport( const ContactReport& lhs, const ContactReport& rhs )
    {
        if ( lhs._bodyA._body != rhs._bodyA._body )
            return lhs._bodyA._body < rhs._bodyA._body;
        if ( lhs._bodyB._body != rhs._bodyB._body )
            return lhs._bodyB._body < rhs._bodyB._body;
        if ( lhs._subShapeA != rhs._subShapeA )
            return lhs._subShapeA < rhs._subShapeA;
        if ( lhs._subShapeB != rhs._subShapeB )
            return lhs._subShapeB < rhs._subShapeB;
        return lhs._kind < rhs._kind;
    }

    void JoltPhysicsScene::flushContactReports()
    {
        // 잡 스레드가 붙인 순서는 매번 다르다. 바디 쌍, 서브 셰이프, 종류 순으로 정렬해 같은 입력이면 같은 이벤트를 낸다.
        VectorUtil::sort( _listContactReport, &JoltPhysicsScene::isEarlierReport );
        for ( ContactReport& report : _listContactReport )
        {
            if ( report._bodyA._body.isValid() == false || report._bodyB._body.isValid() == false )
                continue;
            switch ( report._kind )
            {
                case 0:
                {
                    const BodyRecord* pRecordA = findBody( report._bodyA._body );
                    const BodyRecord* pRecordB = findBody( report._bodyB._body );
                    if ( pRecordA == nullptr || pRecordB == nullptr )
                        break;
                    report._bodyA._userData = pRecordA->_userData;
                    report._bodyB._userData = pRecordB->_userData;
                    _contactTracker.beginSubContact( report._bodyA, report._bodyB, report._point, report._normal, report._impulse );
                    break;
                }
                case 1:
                {
                    _contactTracker.persistSubContact( report._bodyA._body, report._bodyB._body, report._point, report._normal, report._impulse );
                    break;
                }
                default:
                {
                    _contactTracker.endSubContact( report._bodyA._body, report._bodyB._body );
                    break;
                }
            }
        }
        _listContactReport.clear();
        _contactTracker.finishStep( _listContactEvent );
    }

    PhysicsVehicleHandle JoltPhysicsScene::createWheeledVehicle( PhysicsBodyHandle chassis, const PhysicsWheeledVehicleDesc& desc )
    {
        const BodyRecord* pChassis = findBody( chassis );
        if ( pChassis == nullptr || pChassis->_type != PhysicsBodyType::Dynamic )
        {
            SW_LOG_ERROR( "Wheeled vehicle needs a dynamic chassis body, wheels and driven axle pairs" );
            return PhysicsVehicleHandle{};
        }
        return _vehicles.create( chassis, pChassis->_bodyID, JoltLayerUtil::makeObjectLayer( pChassis->_layer, true ), desc );
    }

    void JoltPhysicsScene::destroyVehicle( PhysicsVehicleHandle vehicle )
    {
        wakeBody( _vehicles.destroy( vehicle ) );
    }

    bool JoltPhysicsScene::isVehicleValid( PhysicsVehicleHandle vehicle ) const
    {
        return _vehicles.isValid( vehicle );
    }

    void JoltPhysicsScene::setVehicleInput( PhysicsVehicleHandle vehicle, float32 forward, float32 right, float32 brake, float32 handBrake )
    {
        wakeBody( _vehicles.setInput( vehicle, forward, right, brake, handBrake ) ); // 입력이 있으면 자는 차체를 깨운다
    }

    bool JoltPhysicsScene::getVehicleState( PhysicsVehicleHandle vehicle, PhysicsVehicleState& outState ) const
    {
        return _vehicles.getState( vehicle, outState );
    }

    PhysicsSoftBodyHandle JoltPhysicsScene::createSoftBody( const PhysicsSoftBodyDesc& desc )
    {
        return _softBodies.create( desc );
    }

    void JoltPhysicsScene::destroySoftBody( PhysicsSoftBodyHandle softBody )
    {
        _softBodies.destroy( softBody );
    }

    bool JoltPhysicsScene::isSoftBodyValid( PhysicsSoftBodyHandle softBody ) const
    {
        return _softBodies.isValid( softBody );
    }

    bool JoltPhysicsScene::getSoftBodyVertices( PhysicsSoftBodyHandle softBody, vector<float3>& outListPosition ) const
    {
        return _softBodies.getVertices( softBody, outListPosition );
    }

    void JoltPhysicsScene::setSoftBodyPinnedTransform( PhysicsSoftBodyHandle softBody, const float3& position, const quaternion& rotation )
    {
        _softBodies.setPinnedTransform( softBody, position, rotation );
    }

    void JoltPhysicsScene::setSoftBodySkinningPose( PhysicsSoftBodyHandle softBody, const vector<float4x4>& listJointWorld, bool bHardSkinAll )
    {
        _softBodies.setSkinningPose( softBody, listJointWorld, bHardSkinAll );
    }

} // namespace sw

namespace sw
{
    // --- 관절 -------------------------------------------------------------------------------------------------------------

    PhysicsJointHandle JoltPhysicsScene::createJoint( const PhysicsJointDesc3D& desc )
    {
        const BodyRecord* pRecordA = findBody( desc._bodyA );
        const BodyRecord* pRecordB = desc._bodyB.isValid() ? findBody( desc._bodyB ) : nullptr;
        if ( pRecordA == nullptr || ( desc._bodyB.isValid() && pRecordB == nullptr ) )
        {
            SW_LOG_ERROR( "createJoint: a body handle is stale" );
            return PhysicsJointHandle{};
        }
        const JPH::BodyID       bodyIDA       = pRecordA->_bodyID;
        const JPH::BodyID       bodyIDB       = pRecordB != nullptr ? pRecordB->_bodyID : JPH::BodyID{};
        JPH::BodyInterface&     bodyInterface = _system.GetBodyInterface();
        JPH::TwoBodyConstraint* pConstraint   = JoltPhysicsSceneInternal::createConstraint( bodyInterface, desc, bodyIDA, bodyIDB );
        if ( pConstraint == nullptr )
        {
            SW_LOG_ERROR( "createJoint: Jolt could not create the joint" );
            return PhysicsJointHandle{};
        }

        JointRecord record;
        record._pConstraint    = pConstraint;
        record._bodyA          = desc._bodyA;
        record._bodyB          = desc._bodyB;
        record._type           = desc._type;
        record._bPairFilterSet = desc._bCollideConnectedBodies == false && desc._bodyB.isValid();
        if ( desc._type == PhysicsJointType::Distance )
        {
            // 현재 길이를 측정하려고 두 점을 각 바디의 로컬로 적어 둔다.
            float3     positionA{};
            quaternion rotationA{};
            (void)getBodyTransform( desc._bodyA, positionA, rotationA );
            rotationA.inverse();
            record._anchorA = float3::transform( desc._anchor - positionA, rotationA );
            float3     positionB{};
            quaternion rotationB{};
            record._anchorB = desc._anchorB;
            if ( getBodyTransform( desc._bodyB, positionB, rotationB ) )
            {
                rotationB.inverse();
                record._anchorB = float3::transform( desc._anchorB - positionB, rotationB );
            }
        }
        JoltPhysicsSceneInternal::applyMotor( *pConstraint, desc._type, desc._motor );
        _system.AddConstraint( pConstraint );
        bodyInterface.ActivateConstraint( pConstraint );
        if ( record._bPairFilterSet )
            _pairFilter.setPairCollision( desc._bodyA, desc._bodyB, false );
        return PhysicsJointHandle::fromSlot( _joints.insert( std::move( record ) ) );
    }

    void JoltPhysicsScene::destroyJoint( PhysicsJointHandle joint )
    {
        JointRecord record;
        if ( _joints.take( joint.getSlot(), record ) == false )
            return;
        if ( record._pConstraint != nullptr )
        {
            _system.RemoveConstraint( record._pConstraint.GetPtr() );
            // 깨워서 남은 바디가 그 위치에 떠 있지 않게 한다.
            wakeBody( record._bodyA );
            wakeBody( record._bodyB );
        }
        if ( record._bPairFilterSet )
            _pairFilter.setPairCollision( record._bodyA, record._bodyB, true );
    }

    bool JoltPhysicsScene::isJointValid( PhysicsJointHandle joint ) const
    {
        return _joints.get( joint.getSlot() ) != nullptr;
    }

    void JoltPhysicsScene::setJointMotor( PhysicsJointHandle joint, const PhysicsJointMotor& motor )
    {
        JointRecord* pRecord = _joints.get( joint.getSlot() );
        if ( pRecord == nullptr || pRecord->_pConstraint == nullptr )
            return;
        JoltPhysicsSceneInternal::applyMotor( *pRecord->_pConstraint, pRecord->_type, motor );
        wakeBody( pRecord->_bodyA );
        wakeBody( pRecord->_bodyB );
    }

    void JoltPhysicsScene::driveJointToRotation( PhysicsJointHandle joint, const quaternion& targetRotationA, const quaternion& targetRotationB, float32 maxTorque )
    {
        JointRecord* pRecord = _joints.get( joint.getSlot() );
        if ( pRecord == nullptr || pRecord->_pConstraint == nullptr )
            return;
        if ( pRecord->_type != PhysicsJointType::Cone )
        {
            SW_LOG_ERROR( "driveJointToRotation drives Cone joints only - this joint is not one" );
            return;
        }
        JPH::SwingTwistConstraint& constraint = *static_cast<JPH::SwingTwistConstraint*>( pRecord->_pConstraint.GetPtr() );
        if ( maxTorque <= 0.0f )
        {
            constraint.SetSwingMotorState( JPH::EMotorState::Off );
            constraint.SetTwistMotorState( JPH::EMotorState::Off );
            return;
        }
        // 바디 기준 목표 — B 의 자세를 A 의 자세로 나눈 상대 회전(Jolt 의 열-벡터 순서: qB = qA × q상대). 관절 프레임으로 옮기는 것은 Jolt 가 한다(`SetTargetOrientationBS`).
        const JPH::Quat relative = ( JoltUtil::toJolt( targetRotationA ).Conjugated() * JoltUtil::toJolt( targetRotationB ) ).Normalized();
        constraint.GetSwingMotorSettings().SetTorqueLimit( maxTorque );
        constraint.GetTwistMotorSettings().SetTorqueLimit( maxTorque );
        constraint.SetSwingMotorState( JPH::EMotorState::Position );
        constraint.SetTwistMotorState( JPH::EMotorState::Position );
        constraint.SetTargetOrientationBS( relative );
        wakeBody( pRecord->_bodyA );
        wakeBody( pRecord->_bodyB );
    }

    float32 JoltPhysicsScene::getJointPosition( PhysicsJointHandle joint ) const
    {
        const JointRecord* pRecord = _joints.get( joint.getSlot() );
        if ( pRecord == nullptr || pRecord->_pConstraint == nullptr )
            return 0.0f;
        switch ( pRecord->_type )
        {
            case PhysicsJointType::Hinge:
            {
                return static_cast<const JPH::HingeConstraint*>( pRecord->_pConstraint.GetPtr() )->GetCurrentAngle();
            }
            case PhysicsJointType::Slider:
            {
                return static_cast<const JPH::SliderConstraint*>( pRecord->_pConstraint.GetPtr() )->GetCurrentPosition();
            }
            case PhysicsJointType::Distance:
            {
                float3     positionA{};
                quaternion rotationA{};
                (void)getBodyTransform( pRecord->_bodyA, positionA, rotationA );
                const float3 pointA = positionA + float3::transform( pRecord->_anchorA, rotationA );
                float3       pointB = pRecord->_anchorB;
                float3       positionB{};
                quaternion   rotationB{};
                if ( getBodyTransform( pRecord->_bodyB, positionB, rotationB ) )
                    pointB = positionB + float3::transform( pRecord->_anchorB, rotationB );
                return float3::getDistance( pointA, pointB );
            }
            case PhysicsJointType::Fixed:
            case PhysicsJointType::Cone:
            {
                return 0.0f;
            }
        }
        return 0.0f;
    }

    // --- 캐릭터 -----------------------------------------------------------------------------------------------------------

    PhysicsCharacterHandle JoltPhysicsScene::createCharacter( const PhysicsCharacterDesc3D& desc )
    {
        // 캡슐 바닥이 캐릭터 원점(발)에 오도록 올려 둔다.
        const float32             centerHeight = desc._halfHeight + desc._radius;
        JPH::CapsuleShapeSettings capsule{ desc._halfHeight, desc._radius };
        capsule.SetEmbedded();
        JPH::RotatedTranslatedShapeSettings offset{
            JPH::Vec3{ 0.0f, centerHeight, 0.0f },
            JPH::Quat::sIdentity(), &capsule
        };
        offset.SetEmbedded();
        const JPH::Shape::ShapeResult result = offset.Create();
        if ( result.HasError() )
        {
            SW_LOG_ERROR( "createCharacter: %#", result.GetError().c_str() );
            return PhysicsCharacterHandle{};
        }

        JPH::CharacterVirtualSettings settings;
        settings.SetEmbedded();
        settings.mShape         = result.Get();
        settings.mMaxSlopeAngle = desc._maxSlopeAngle;
        settings.mMass          = desc._mass;
        settings.mMaxStrength   = desc._maxStrength;
        // 캡슐 아래 반구에서 닿은 것만 디딤으로 본다(옆구리에 닿은 턱은 바닥이 아니다).
        settings.mSupportingVolume = JPH::Plane{ JPH::Vec3::sAxisY(), -desc._radius };
        // 속 바디 — 천(소프트 바디)과 센서가 캐릭터를 몸으로 느낀다(`PhysicsCharacterDesc::_bInnerBody`). Jolt 가 캐릭터 갱신 끝마다 그 자리로 옮기고 캐릭터와 함께 지운다.
        if ( desc._bInnerBody )
        {
            settings.mInnerBodyShape = result.Get();
            settings.mInnerBodyLayer = JoltLayerUtil::makeObjectLayer( desc._layer, true );
        }

        CharacterRecord record;
        record._pCharacter      = JoltUtil::createObject<JPH::CharacterVirtual>( &settings, JoltUtil::toJolt( desc._position ), JPH::Quat::sIdentity(), desc._userData,
                                                                                 &_system );
        record._desc            = desc;
        record._state._position = desc._position;
        if ( desc._bInnerBody )
        {
            // 속 바디도 엔진 바디 표에 올린다 — Jolt 는 속 바디의 사용자 값으로 캐릭터의 것(오브젝트 id)을 넣는데, 접촉 알림 · 질의 거르기는 Jolt 사용자 값을
            // 엔진 핸들로 읽는다. 핸들을 넣어야 닿음 · 이벤트가 캐릭터 오브젝트로 풀리고 `_ignoreUserData` 가 제 속 바디를 거른다.
            const JPH::BodyID innerBodyID = record._pCharacter->GetInnerBodyID();
            if ( innerBodyID.IsInvalid() )
            {
                SW_LOG_ERROR( "createCharacter: Jolt is out of bodies for the inner body" );
                return PhysicsCharacterHandle{};
            }
            PhysicsShapeDesc3D capsuleDesc;
            capsuleDesc._type          = PhysicsShapeType3D::Capsule;
            capsuleDesc._radius        = desc._radius;
            capsuleDesc._halfHeight    = desc._halfHeight;
            capsuleDesc._localPosition = float3{ 0.0f, centerHeight, 0.0f };
            BodyRecord inner;
            inner._pListShape      = make_shared<const ShapeDescList>( ShapeDescList{ capsuleDesc } );
            inner._userData        = desc._userData;
            inner._bodyID          = innerBodyID;
            inner._type            = PhysicsBodyType::Kinematic;
            inner._layer           = desc._layer;
            inner._bCharacterInner = true;
            record._innerBody      = PhysicsBodyHandle::fromSlot( _bodies.insert( std::move( inner ) ) );
            _system.GetBodyInterface().SetUserData( innerBodyID, record._innerBody.packed() );
            setHandle( innerBodyID, record._innerBody );
        }
        return PhysicsCharacterHandle::fromSlot( _characters.insert( std::move( record ) ) );
    }

    void JoltPhysicsScene::destroyCharacter( PhysicsCharacterHandle character )
    {
        CharacterRecord record;
        if ( _characters.take( character.getSlot(), record ) == false )
            return;
        BodyRecord inner;
        if ( record._innerBody.isValid() && _bodies.take( record._innerBody.getSlot(), inner ) )
        {
            setHandle( inner._bodyID, PhysicsBodyHandle{} );
            const span<const PhysicsBodyHandle> removed{ &record._innerBody, 1 };
            _contactTracker.removeBodies( removed );
            _pairFilter.removeBodies( removed );
        }
        // `record` 가 여기서 소멸하며 Jolt 가 속 바디를 지운다.
    }

    PhysicsCharacterState3D JoltPhysicsScene::moveCharacter( PhysicsCharacterHandle character, const float3& velocity, float32 deltaTime )
    {
        CharacterRecord* pRecord = _characters.get( character.getSlot() );
        if ( pRecord == nullptr || pRecord->_pCharacter == nullptr )
            return PhysicsCharacterState3D{};
        JPH::CharacterVirtual& virtualCharacter = *pRecord->_pCharacter;
        // 결정: 움직이는 바닥의 속도는 호출하는 쪽이 아니라 여기서 더한다 — 호출 쪽이 빠뜨릴 수 없다. 직전 스텝에 디딘 바닥(`OnGround`)의 선속도와
        // 회전이 발 밑에 만드는 속도를 합친 값(`GetGroundVelocity`)이다. 바닥이 직전 스텝 뒤에 속도를 바꿨을 수 있어 먼저 새로 읽는다(충돌 검사는 하지 않는다).
        // 바닥의 위 축 각속도(`_groundYawRate`)도 함께 내보낸다 — 캐릭터 방향을 돌리는 일은 트랜스폼을 쓰는 컴포넌트의 몫이다.
        float3  carriedVelocity{};
        float32 groundYawRate = 0.0f;
        if ( deltaTime > 0.0f )
        {
            // 순간이동(`setCharacterPosition`)이 `_groundBody` 를 비우므로, 옮긴 직후 한 스텝은 옛 바닥의 속도를 싣지 않는다.
            if ( pRecord->_state._groundBody.isValid() && virtualCharacter.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround )
            {
                virtualCharacter.UpdateGroundVelocity();
                carriedVelocity = JoltUtil::toEngine( virtualCharacter.GetGroundVelocity() );
                groundYawRate   = _system.GetBodyInterface().GetAngularVelocity( virtualCharacter.GetGroundBodyID() ).GetY();
            }
            virtualCharacter.SetLinearVelocity( JoltUtil::toJolt( velocity + carriedVelocity ) );
            JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
            updateSettings.mWalkStairsStepUp                                = JPH::Vec3{ 0.0f, pRecord->_desc._stepHeight, 0.0f };
            updateSettings.mStickToFloorStepDown                            = JPH::Vec3{ 0.0f, -pRecord->_desc._stepHeight, 0.0f };
            const JPH::ObjectLayer                              objectLayer = JoltLayerUtil::makeObjectLayer( pRecord->_desc._layer, true );
            const JoltPhysicsSceneInternal::CharacterBodyFilter bodyFilter;
            const JPH::ShapeFilter                              shapeFilter;
            virtualCharacter.ExtendedUpdate( deltaTime, _system.GetGravity(), updateSettings, _system.GetDefaultBroadPhaseLayerFilter( objectLayer ),
                                             _system.GetDefaultLayerFilter( objectLayer ), bodyFilter, shapeFilter, _tempAllocator );
        }
        PhysicsCharacterState3D& state = pRecord->_state;
        state._position                = JoltUtil::toEngine( virtualCharacter.GetPosition() );
        state._velocity                = JoltUtil::toEngine( virtualCharacter.GetLinearVelocity() ) - carriedVelocity;
        state._carriedVelocity         = carriedVelocity;
        state._groundYawRate           = groundYawRate;
        state._bGrounded               = virtualCharacter.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
        state._groundNormal            = state._bGrounded ? JoltUtil::toEngine( virtualCharacter.GetGroundNormal() ) : float3{};
        state._groundBody              = state._bGrounded ? findHandle( virtualCharacter.GetGroundBodyID() ) : PhysicsBodyHandle{};
        return state;
    }

    void JoltPhysicsScene::setCharacterPosition( PhysicsCharacterHandle character, const float3& position )
    {
        CharacterRecord* pRecord = _characters.get( character.getSlot() );
        if ( pRecord == nullptr || pRecord->_pCharacter == nullptr )
            return;
        pRecord->_pCharacter->SetPosition( JoltUtil::toJolt( position ) );
        pRecord->_state._position   = position;
        pRecord->_state._groundBody = PhysicsBodyHandle{};
    }

    bool JoltPhysicsScene::getCharacterState( PhysicsCharacterHandle character, PhysicsCharacterState3D& outState ) const
    {
        const CharacterRecord* pRecord = _characters.get( character.getSlot() );
        if ( pRecord == nullptr )
            return false;
        outState = pRecord->_state;
        return true;
    }

    // --- 질의 -------------------------------------------------------------------------------------------------------------

    bool JoltPhysicsScene::raycast( const float3& origin, const float3& direction, float32 maxDistance, const PhysicsQueryFilter& filter, PhysicsCastHit3D& outHit ) const
    {
        const BodyRecord*                               pIgnore = findBody( filter._ignoreBody );
        const JoltPhysicsSceneInternal::LayerMaskFilter layerFilter{ filter._layerMask };
        const JoltPhysicsSceneInternal::QueryBodyFilter bodyFilter{ *this, pIgnore != nullptr ? pIgnore->_bodyID : JPH::BodyID{}, filter };
        const JPH::RRayCast                             ray{ JoltUtil::toJolt( origin ), JoltUtil::toJolt( direction ) * maxDistance };
        JPH::RayCastResult                              hit;
        if ( _system.GetNarrowPhaseQuery().CastRay( ray, hit, JPH::BroadPhaseLayerFilter{}, layerFilter, bodyFilter ) == false )
            return false;
        makeRayHit( ray, hit, maxDistance, outHit );
        return true;
    }

    uint32 JoltPhysicsScene::raycastAll( const float3& origin, const float3& direction, float32 maxDistance, const PhysicsQueryFilter& filter,
                                         vector<PhysicsCastHit3D>& outListHit ) const
    {
        const BodyRecord*                               pIgnore = findBody( filter._ignoreBody );
        const JoltPhysicsSceneInternal::LayerMaskFilter layerFilter{ filter._layerMask };
        const JoltPhysicsSceneInternal::QueryBodyFilter bodyFilter{ *this, pIgnore != nullptr ? pIgnore->_bodyID : JPH::BodyID{}, filter };
        const JPH::RRayCast                             ray{ JoltUtil::toJolt( origin ), JoltUtil::toJolt( direction ) * maxDistance };
        // `raycast`(CastRay 의 가장 가까운 닿음)와 같은 판정이 되도록 볼록 셰이프를 속이 찬 것으로 보고, 메시는 뒷면을 버린다(Jolt 기본값).
        JPH::RayCastSettings                                 settings;
        JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
        _system.GetNarrowPhaseQuery().CastRay( ray, settings, collector, JPH::BroadPhaseLayerFilter{}, layerFilter, bodyFilter );
        const size_t firstIndex = outListHit.size();
        outListHit.reserve( firstIndex + collector.mHits.size() );
        for ( const JPH::RayCastResult& hit : collector.mHits )
        {
            PhysicsCastHit3D result;
            makeRayHit( ray, hit, maxDistance, result );
            if ( result._body.isValid() )
                outListHit.push_back( result );
        }
        return PhysicsQueryUtil::keepNearestHitPerBody( outListHit, firstIndex );
    }

    uint32 JoltPhysicsScene::shapeCastAll( const PhysicsShapeDesc3D& shape, const float3& position, const quaternion& rotation, const float3& direction,
                                           float32 maxDistance, const PhysicsQueryFilter& filter, vector<PhysicsCastHit3D>& outListHit ) const
    {
        const JPH::RefConst<JPH::Shape> pShape = createJoltShape( span<const PhysicsShapeDesc3D>{ &shape, 1 }, hashed_string{}, false );
        if ( pShape == nullptr )
            return 0;
        const BodyRecord*                                      pIgnore = findBody( filter._ignoreBody );
        const JoltPhysicsSceneInternal::LayerMaskFilter        layerFilter{ filter._layerMask };
        const JoltPhysicsSceneInternal::QueryBodyFilter        bodyFilter{ *this, pIgnore != nullptr ? pIgnore->_bodyID : JPH::BodyID{}, filter };
        const JPH::RMat44                                      start = JPH::RMat44::sRotationTranslation( JoltUtil::toJolt( rotation ), JoltUtil::toJolt( position ) );
        const JPH::RShapeCast                                  cast  = JPH::RShapeCast::sFromWorldTransform( pShape.GetPtr(), JPH::Vec3::sReplicate( 1.0f ), start, JoltUtil::toJolt( direction ) * maxDistance );
        JPH::ShapeCastSettings                                 settings;
        JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector;
        _system.GetNarrowPhaseQuery().CastShape( cast, settings, JPH::RVec3::sZero(), collector, JPH::BroadPhaseLayerFilter{}, layerFilter, bodyFilter );
        const size_t firstIndex = outListHit.size();
        outListHit.reserve( firstIndex + collector.mHits.size() );
        for ( const JPH::ShapeCastResult& hit : collector.mHits )
        {
            PhysicsCastHit3D result;
            makeShapeCastHit( hit, maxDistance, result );
            if ( result._body.isValid() )
                outListHit.push_back( result );
        }
        return PhysicsQueryUtil::keepNearestHitPerBody( outListHit, firstIndex );
    }

    void JoltPhysicsScene::makeRayHit( const JPH::RRayCast& ray, const JPH::RayCastResult& hit, float32 maxDistance, PhysicsCastHit3D& outHit ) const
    {
        const JPH::RVec3 point = ray.GetPointOnRay( hit.mFraction );
        outHit._body           = findHandle( hit.mBodyID );
        outHit._userData       = getBodyUserData( outHit._body );
        outHit._point          = JoltUtil::toEngine( point );
        outHit._fraction       = hit.mFraction;
        outHit._distance       = hit.mFraction * maxDistance;
        const JPH::BodyLockRead lock{ _system.GetBodyLockInterface(), hit.mBodyID };
        if ( lock.Succeeded() )
        {
            outHit._normal = JoltUtil::toEngine( lock.GetBody().GetWorldSpaceSurfaceNormal( hit.mSubShapeID2, point ) );
            // 이 씬이 만드는 셰이프는 모두 `JoltPhysicsMaterial` 을 단다(기본 재질만 예외이며, 그때는 이름이 비어 있다).
            const JPH::PhysicsMaterial* pMaterial = lock.GetBody().GetShape()->GetMaterial( hit.mSubShapeID2 );
            if ( pMaterial != nullptr && pMaterial != JPH::PhysicsMaterial::sDefault.GetPtr() )
                outHit._material = static_cast<const JoltPhysicsMaterial*>( pMaterial )->getName();
        }
    }

    void JoltPhysicsScene::makeShapeCastHit( const JPH::ShapeCastResult& hit, float32 maxDistance, PhysicsCastHit3D& outHit ) const
    {
        outHit._body     = findHandle( hit.mBodyID2 );
        outHit._userData = getBodyUserData( outHit._body );
        outHit._point    = JoltUtil::toEngine( hit.mContactPointOn2 );
        outHit._normal   = JoltUtil::toEngine( -hit.mPenetrationAxis.NormalizedOr( JPH::Vec3::sZero() ) );
        outHit._fraction = hit.mFraction;
        outHit._distance = hit.mFraction * maxDistance;
    }

    bool JoltPhysicsScene::shapeCast( const PhysicsShapeDesc3D& shape, const float3& position, const quaternion& rotation, const float3& direction, float32 maxDistance,
                                      const PhysicsQueryFilter& filter, PhysicsCastHit3D& outHit ) const
    {
        const JPH::RefConst<JPH::Shape> pShape = createJoltShape( span<const PhysicsShapeDesc3D>{ &shape, 1 }, hashed_string{}, false );
        if ( pShape == nullptr )
            return false;
        const BodyRecord*                                          pIgnore = findBody( filter._ignoreBody );
        const JoltPhysicsSceneInternal::LayerMaskFilter            layerFilter{ filter._layerMask };
        const JoltPhysicsSceneInternal::QueryBodyFilter            bodyFilter{ *this, pIgnore != nullptr ? pIgnore->_bodyID : JPH::BodyID{}, filter };
        const JPH::RMat44                                          start = JPH::RMat44::sRotationTranslation( JoltUtil::toJolt( rotation ), JoltUtil::toJolt( position ) );
        const JPH::RShapeCast                                      cast  = JPH::RShapeCast::sFromWorldTransform( pShape.GetPtr(), JPH::Vec3::sReplicate( 1.0f ), start, JoltUtil::toJolt( direction ) * maxDistance );
        JPH::ShapeCastSettings                                     settings;
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        _system.GetNarrowPhaseQuery().CastShape( cast, settings, JPH::RVec3::sZero(), collector, JPH::BroadPhaseLayerFilter{}, layerFilter, bodyFilter );
        if ( collector.HadHit() == false )
            return false;
        makeShapeCastHit( collector.mHit, maxDistance, outHit );
        return true;
    }

    uint32 JoltPhysicsScene::overlapShape( const PhysicsShapeDesc3D& shape, const float3& position, const quaternion& rotation, const PhysicsQueryFilter& filter,
                                           vector<PhysicsBodyHandle>& outListBody ) const
    {
        const JPH::RefConst<JPH::Shape> pShape = createJoltShape( span<const PhysicsShapeDesc3D>{ &shape, 1 }, hashed_string{}, false );
        if ( pShape == nullptr )
            return 0;
        const BodyRecord*                               pIgnore = findBody( filter._ignoreBody );
        const JoltPhysicsSceneInternal::LayerMaskFilter layerFilter{ filter._layerMask };
        const JoltPhysicsSceneInternal::QueryBodyFilter bodyFilter{ *this, pIgnore != nullptr ? pIgnore->_bodyID : JPH::BodyID{}, filter };
        const JPH::RMat44                               transform = JPH::RMat44::sRotationTranslation( JoltUtil::toJolt( rotation ), JoltUtil::toJolt( position ) ) *
                                      JPH::Mat44::sTranslation( pShape->GetCenterOfMass() );
        JPH::CollideShapeSettings                                 settings;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        _system.GetNarrowPhaseQuery().CollideShape( pShape.GetPtr(), JPH::Vec3::sReplicate( 1.0f ), transform, settings, JPH::RVec3::sZero(), collector,
                                                    JPH::BroadPhaseLayerFilter{}, layerFilter, bodyFilter );
        // 같은 바디의 여러 서브 셰이프가 따로 잡힌다. 출력 끝에 바로 붙이고 그 구간만 바디 순으로 정렬해 한 번씩 남긴다(임시 버퍼를 할당하지 않는다).
        const size_t firstIndex = outListBody.size();
        outListBody.reserve( firstIndex + collector.mHits.size() );
        for ( const JPH::CollideShapeResult& hit : collector.mHits )
        {
            const PhysicsBodyHandle body = findHandle( hit.mBodyID2 );
            if ( body.isValid() )
                outListBody.push_back( body );
        }
        const vector<PhysicsBodyHandle>::iterator first = outListBody.begin() + static_cast<ptrdiff_t>( firstIndex );
        std::sort( first, outListBody.end() );
        outListBody.erase( std::unique( first, outListBody.end() ), outListBody.end() );
        return static_cast<uint32>( outListBody.size() - firstIndex );
    }
} // namespace sw
