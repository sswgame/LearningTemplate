#include "pch.h"

#include "Engine/Physics/Jolt/JoltPhysicsScene.h"
#include "Engine/Physics/Jolt/JoltUtil.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
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
        struct JoltPhysicsSceneQueryInternal
        {
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

            /** @brief 쏘는 쪽 바디 · 쏘는 오브젝트의 바디 모두(사용자 값)와(원하면) 트리거를 뺍니다. */
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
                    // 바디의 Jolt 사용자 값은 엔진 핸들이다 — 엔진 사용자 값(오브젝트 id)은 씬의 기록에 있다.
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
        };
    } // namespace
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
        const JPH::BodyID bodyIDA = pRecordA->_bodyID;
        const JPH::BodyID bodyIDB = pRecordB != nullptr ? pRecordB->_bodyID : JPH::BodyID{};
        const JPH::Vec3   anchor  = JoltUtil::toJolt( desc._anchor );
        const JPH::Vec3   axis    = JoltUtil::toJolt( desc._axis ).NormalizedOr( JPH::Vec3::sAxisY() );

        JPH::TwoBodyConstraint* pConstraint   = nullptr;
        JPH::BodyInterface&     bodyInterface = _system.GetBodyInterface();
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
                settings.mNormalAxis1 = settings.mNormalAxis2 = JoltPhysicsSceneQueryInternal::makePerpendicular( axis );
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
                const JPH::Vec3 planeAxis = JoltUtil::toJolt( desc._normalAxis ).NormalizedOr( JoltPhysicsSceneQueryInternal::makePerpendicular( axis ) );
                settings.mPosition1 = settings.mPosition2 = anchor;
                settings.mTwistAxis1 = settings.mTwistAxis2 = axis;
                settings.mPlaneAxis1 = settings.mPlaneAxis2 = planeAxis;
                // 엔진은 "축 둘레의 스윙 한계" 로 적는다 — Jolt 의 plane 반각이 plane 축 둘레, normal 반각이 그 둘에 수직인 축 둘레다.
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
        if ( pConstraint == nullptr )
        {
            SW_LOG_ERROR( "createJoint: Jolt could not create the joint" );
            return PhysicsJointHandle{};
        }

        JointRecord record;
        record._pConstraint       = pConstraint;
        record._bodyA             = desc._bodyA;
        record._bodyB             = desc._bodyB;
        record._type              = desc._type;
        record._bDisableCollision = desc._bDisableCollision && desc._bodyB.isValid();
        if ( desc._type == PhysicsJointType::Distance )
        {
            // 지금 길이를 재려고 두 점을 각 바디의 로컬로 적어 둔다.
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
        JoltPhysicsSceneQueryInternal::applyMotor( *pConstraint, desc._type, desc._motor );
        _system.AddConstraint( pConstraint );
        bodyInterface.ActivateConstraint( pConstraint );
        if ( record._bDisableCollision )
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
            // 깨어나게 해 남은 바디가 그 자리에 떠 있지 않게 한다.
            wakeBody( record._bodyA );
            wakeBody( record._bodyB );
        }
        if ( record._bDisableCollision )
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
        JoltPhysicsSceneQueryInternal::applyMotor( *pRecord->_pConstraint, pRecord->_type, motor );
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
        // 캡슐 바닥이 캐릭터 원점(발)에 오게 올려 둔다.
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

        CharacterRecord record;
        record._pCharacter      = JoltUtil::createObject<JPH::CharacterVirtual>( &settings, JoltUtil::toJolt( desc._position ), JPH::Quat::sIdentity(), desc._userData,
                                                                                 &_system );
        record._desc            = desc;
        record._state._position = desc._position;
        return PhysicsCharacterHandle::fromSlot( _characters.insert( std::move( record ) ) );
    }

    void JoltPhysicsScene::destroyCharacter( PhysicsCharacterHandle character )
    {
        _characters.erase( character.getSlot() );
    }

    PhysicsCharacterState3D JoltPhysicsScene::moveCharacter( PhysicsCharacterHandle character, const float3& velocity, float32 deltaTime )
    {
        CharacterRecord* pRecord = _characters.get( character.getSlot() );
        if ( pRecord == nullptr || pRecord->_pCharacter == nullptr )
            return PhysicsCharacterState3D{};
        JPH::CharacterVirtual& virtualCharacter = *pRecord->_pCharacter;
        if ( deltaTime > 0.0f )
        {
            virtualCharacter.SetLinearVelocity( JoltUtil::toJolt( velocity ) );
            JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
            updateSettings.mWalkStairsStepUp                                     = JPH::Vec3{ 0.0f, pRecord->_desc._stepHeight, 0.0f };
            updateSettings.mStickToFloorStepDown                                 = JPH::Vec3{ 0.0f, -pRecord->_desc._stepHeight, 0.0f };
            const JPH::ObjectLayer                                   objectLayer = JoltLayerUtil::makeObjectLayer( pRecord->_desc._layer, true );
            const JoltPhysicsSceneQueryInternal::CharacterBodyFilter bodyFilter;
            const JPH::ShapeFilter                                   shapeFilter;
            virtualCharacter.ExtendedUpdate( deltaTime, _system.GetGravity(), updateSettings, _system.GetDefaultBroadPhaseLayerFilter( objectLayer ),
                                             _system.GetDefaultLayerFilter( objectLayer ), bodyFilter, shapeFilter, _tempAllocator );
        }
        PhysicsCharacterState3D& state = pRecord->_state;
        state._position                = JoltUtil::toEngine( virtualCharacter.GetPosition() );
        state._velocity                = JoltUtil::toEngine( virtualCharacter.GetLinearVelocity() );
        state._bGrounded               = virtualCharacter.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
        state._groundNormal            = state._bGrounded ? JoltUtil::toEngine( virtualCharacter.GetGroundNormal() ) : float3{};
        return state;
    }

    void JoltPhysicsScene::setCharacterPosition( PhysicsCharacterHandle character, const float3& position )
    {
        CharacterRecord* pRecord = _characters.get( character.getSlot() );
        if ( pRecord == nullptr || pRecord->_pCharacter == nullptr )
            return;
        pRecord->_pCharacter->SetPosition( JoltUtil::toJolt( position ) );
        pRecord->_state._position = position;
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
        const BodyRecord*                                    pIgnore = findBody( filter._ignoreBody );
        const JoltPhysicsSceneQueryInternal::LayerMaskFilter layerFilter{ filter._layerMask };
        const JoltPhysicsSceneQueryInternal::QueryBodyFilter bodyFilter{ *this, pIgnore != nullptr ? pIgnore->_bodyID : JPH::BodyID{}, filter };
        const JPH::RRayCast                                  ray{ JoltUtil::toJolt( origin ), JoltUtil::toJolt( direction ) * maxDistance };
        JPH::RayCastResult                                   hit;
        if ( _system.GetNarrowPhaseQuery().CastRay( ray, hit, JPH::BroadPhaseLayerFilter{}, layerFilter, bodyFilter ) == false )
            return false;
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
            // 이 씬이 짓는 셰이프는 모두 `JoltPhysicsMaterial` 을 단다(기본 재질만 예외 — 그때는 이름이 비었다).
            const JPH::PhysicsMaterial* pMaterial = lock.GetBody().GetShape()->GetMaterial( hit.mSubShapeID2 );
            if ( pMaterial != nullptr && pMaterial != JPH::PhysicsMaterial::sDefault.GetPtr() )
                outHit._material = static_cast<const JoltPhysicsMaterial*>( pMaterial )->getName();
        }
        return true;
    }

    bool JoltPhysicsScene::shapeCast( const PhysicsShapeDesc3D& shape, const float3& position, const quaternion& rotation, const float3& direction, float32 maxDistance,
                                      const PhysicsQueryFilter& filter, PhysicsCastHit3D& outHit ) const
    {
        const JPH::RefConst<JPH::Shape> pShape = buildShape( span<const PhysicsShapeDesc3D>{ &shape, 1 }, hashed_string{}, false );
        if ( pShape == nullptr )
            return false;
        const BodyRecord*                                          pIgnore = findBody( filter._ignoreBody );
        const JoltPhysicsSceneQueryInternal::LayerMaskFilter       layerFilter{ filter._layerMask };
        const JoltPhysicsSceneQueryInternal::QueryBodyFilter       bodyFilter{ *this, pIgnore != nullptr ? pIgnore->_bodyID : JPH::BodyID{}, filter };
        const JPH::RMat44                                          start = JPH::RMat44::sRotationTranslation( JoltUtil::toJolt( rotation ), JoltUtil::toJolt( position ) );
        const JPH::RShapeCast                                      cast  = JPH::RShapeCast::sFromWorldTransform( pShape.GetPtr(), JPH::Vec3::sReplicate( 1.0f ), start, JoltUtil::toJolt( direction ) * maxDistance );
        JPH::ShapeCastSettings                                     settings;
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        _system.GetNarrowPhaseQuery().CastShape( cast, settings, JPH::RVec3::sZero(), collector, JPH::BroadPhaseLayerFilter{}, layerFilter, bodyFilter );
        if ( collector.HadHit() == false )
            return false;
        const JPH::ShapeCastResult& hit = collector.mHit;
        outHit._body                    = findHandle( hit.mBodyID2 );
        outHit._userData                = getBodyUserData( outHit._body );
        outHit._point                   = JoltUtil::toEngine( hit.mContactPointOn2 );
        outHit._normal                  = JoltUtil::toEngine( -hit.mPenetrationAxis.NormalizedOr( JPH::Vec3::sZero() ) );
        outHit._fraction                = hit.mFraction;
        outHit._distance                = hit.mFraction * maxDistance;
        return true;
    }

    uint32 JoltPhysicsScene::overlapShape( const PhysicsShapeDesc3D& shape, const float3& position, const quaternion& rotation, const PhysicsQueryFilter& filter,
                                           vector<PhysicsBodyHandle>& outListBody ) const
    {
        const JPH::RefConst<JPH::Shape> pShape = buildShape( span<const PhysicsShapeDesc3D>{ &shape, 1 }, hashed_string{}, false );
        if ( pShape == nullptr )
            return 0;
        const BodyRecord*                                    pIgnore = findBody( filter._ignoreBody );
        const JoltPhysicsSceneQueryInternal::LayerMaskFilter layerFilter{ filter._layerMask };
        const JoltPhysicsSceneQueryInternal::QueryBodyFilter bodyFilter{ *this, pIgnore != nullptr ? pIgnore->_bodyID : JPH::BodyID{}, filter };
        const JPH::RMat44                                    transform = JPH::RMat44::sRotationTranslation( JoltUtil::toJolt( rotation ), JoltUtil::toJolt( position ) ) *
                                      JPH::Mat44::sTranslation( pShape->GetCenterOfMass() );
        JPH::CollideShapeSettings                                 settings;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        _system.GetNarrowPhaseQuery().CollideShape( pShape.GetPtr(), JPH::Vec3::sReplicate( 1.0f ), transform, settings, JPH::RVec3::sZero(), collector,
                                                    JPH::BroadPhaseLayerFilter{}, layerFilter, bodyFilter );
        // 같은 바디의 여러 서브 셰이프가 따로 잡힌다 — 바디 순으로 줄 세워 한 번씩 붙인다.
        vector<PhysicsBodyHandle> listFound;
        listFound.reserve( collector.mHits.size() );
        for ( const JPH::CollideShapeResult& hit : collector.mHits )
        {
            listFound.push_back( findHandle( hit.mBodyID2 ) );
        }
        std::sort( listFound.begin(), listFound.end() );
        listFound.erase( std::unique( listFound.begin(), listFound.end() ), listFound.end() );
        uint32 count = 0;
        for ( const PhysicsBodyHandle& body : listFound )
        {
            if ( body.isValid() == false )
                continue;
            outListBody.push_back( body );
            ++count;
        }
        return count;
    }
} // namespace sw
