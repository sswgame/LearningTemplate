#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Physics/Box2D/Box2DPhysicsBackend.h"
#include "Engine/Physics/Box2D/Box2DPhysicsScene.h"
#include "Engine/Physics/Box2D/Box2DUtil.h"

#include <box2d/box2d.h>

namespace sw
{
    SW_LOG_CALLER( "Box2DPhysicsScene" );

    namespace
    {
        struct Box2DPhysicsSceneQueryInternal
        {
            /** @brief 위치 모터(관절 스프링)의 세기입니다. 목표에 1/5 초 안팎으로 붙습니다. */
            static constexpr float32 kPositionMotorHertz        = 5.0f;
            static constexpr float32 kPositionMotorDampingRatio = 0.7f;
            /** @brief 무버 반복 수와 "더 움직이지 않는다" 로 볼 거리입니다(Box2D 무버 예제의 값). */
            static constexpr int32   kMoverIterationCount = 5;
            static constexpr float32 kMoverTolerance      = 0.01f;
            static constexpr int32   kMaxMoverPlaneCount  = 16;
            /** @brief 디딤면을 찾으려고 캡슐을 내려 보는 거리입니다. */
            static constexpr float32 kGroundProbeDistance = 0.05f;

            /** @brief 질의 하나의 필터와 가장 가까운 결과입니다(콜백 컨텍스트). */
            struct CastContext
            {
                const Box2DPhysicsScene*  _pScene{ nullptr };
                const PhysicsQueryFilter* _pFilter{ nullptr };
                b2ShapeId                 _shapeID{};
                b2Vec2                    _point{};
                b2Vec2                    _normal{};
                float32                   _fraction{ 1.0f };
                bool                      _bHit{ false };
            };

            /** @brief 닿는 것을 모두 모으는 질의의 콜백 컨텍스트입니다. 결과를 출력 끝에 바로 붙입니다. */
            struct CastAllContext
            {
                const Box2DPhysicsScene*          _pScene{ nullptr };
                const PhysicsQueryFilter*         _pFilter{ nullptr };
                const vector<PhysicsMaterialDef>* _pListMaterial{ nullptr }; ///< 레이 캐스트만 준다(재질 이름을 채운다)
                vector<PhysicsCastHit2D>*         _pListHit{ nullptr };
                float32                           _maxDistance{ 0.0f };
            };

            struct OverlapContext
            {
                const Box2DPhysicsScene*   _pScene{ nullptr };
                const PhysicsQueryFilter*  _pFilter{ nullptr };
                vector<PhysicsBodyHandle>* _pListBody{ nullptr };
            };

            struct MoverContext
            {
                b2CollisionPlane* _pPlane{ nullptr };
                b2ShapeId*        _pShape{ nullptr }; ///< `_pPlane` 과 같은 순서로, 면을 만든 셰이프다(딛고 선 바디를 찾는 데 쓴다).
                int32             _planeCount{ 0 };
            };

            static bool acceptsShape( const Box2DPhysicsScene& scene, b2ShapeId shapeID, const PhysicsQueryFilter& filter )
            {
                // 센서 여부가 아니라 바디의 뜻(트리거)으로 거른다 — 겹침 전용 센서(막는 2D 콜라이더, `_bOverlapOnly`)는 막는 것으로 보인다.
                const PhysicsBodyHandle body = Box2DUtil::fromUserData( b2Body_GetUserData( b2Shape_GetBody( shapeID ) ) );
                if ( filter._bIncludeTriggers == false && scene.isBodyTrigger( body ) )
                    return false;
                if ( body == filter._ignoreBody )
                    return false;
                return filter._ignoreUserData == 0 || scene.getBodyUserData( body ) != filter._ignoreUserData;
            }

            static float32 castResult( b2ShapeId shapeID, b2Vec2 point, b2Vec2 normal, float32 fraction, void* pContext )
            {
                CastContext& context = *static_cast<CastContext*>( pContext );
                if ( acceptsShape( *context._pScene, shapeID, *context._pFilter ) == false )
                    return -1.0f; // 이 셰이프는 없는 것으로 친다
                context._shapeID  = shapeID;
                context._point    = point;
                context._normal   = normal;
                context._fraction = fraction;
                context._bHit     = true;
                return fraction; // 더 가까운 것만 찾는다
            }

            static float32 castAllResult( b2ShapeId shapeID, b2Vec2 point, b2Vec2 normal, float32 fraction, void* pContext )
            {
                CastAllContext& context = *static_cast<CastAllContext*>( pContext );
                if ( acceptsShape( *context._pScene, shapeID, *context._pFilter ) == false )
                    return -1.0f; // 이 셰이프는 없는 것으로 친다
                PhysicsCastHit2D hit;
                hit._body     = Box2DUtil::fromUserData( b2Body_GetUserData( b2Shape_GetBody( shapeID ) ) );
                hit._userData = context._pScene->getBodyUserData( hit._body );
                hit._point    = Box2DUtil::toEngine( point );
                hit._normal   = Box2DUtil::toEngine( normal );
                hit._fraction = fraction;
                hit._distance = fraction * context._maxDistance;
                if ( context._pListMaterial != nullptr )
                {
                    const int32 materialIndex = b2Shape_GetMaterial( shapeID );
                    if ( 0 <= materialIndex && static_cast<size_t>( materialIndex ) < context._pListMaterial->size() )
                        hit._material = ( *context._pListMaterial )[static_cast<size_t>( materialIndex )]._name;
                }
                context._pListHit->push_back( hit );
                return 1.0f; // 광선을 줄이지 않고 계속 찾는다
            }

            static bool overlapResult( b2ShapeId shapeID, void* pContext )
            {
                OverlapContext& context = *static_cast<OverlapContext*>( pContext );
                if ( acceptsShape( *context._pScene, shapeID, *context._pFilter ) )
                    context._pListBody->push_back( Box2DUtil::fromUserData( b2Body_GetUserData( b2Shape_GetBody( shapeID ) ) ) );
                return true;
            }

            static bool collectPlane( b2ShapeId shapeID, const b2PlaneResult* pPlane, void* pContext )
            {
                MoverContext& context    = *static_cast<MoverContext*>( pContext );
                const bool    bSkipPlane = pPlane->hit == false || b2Shape_IsSensor( shapeID ) || context._planeCount >= kMaxMoverPlaneCount;
                if ( bSkipPlane )
                    return true;
                context._pPlane[context._planeCount] = b2CollisionPlane{ pPlane->plane, FLT_MAX, 0.0f, true };
                context._pShape[context._planeCount] = shapeID;
                ++context._planeCount;
                return true;
            }

            /** @brief 셰이프 서술자를 질의 프록시(점 + 반지름, 월드)로 바꿉니다. 체인은 질의 셰이프가 될 수 없습니다. */
            static bool makeProxy( const PhysicsShapeDesc2D& shape, const float2& position, float32 rotation, b2ShapeProxy& outProxy )
            {
                const b2Transform body{ Box2DUtil::toBox2D( position ), b2MakeRot( rotation ) };
                const b2Transform local{ Box2DUtil::toBox2D( shape._localPosition ), b2MakeRot( shape._localAngle ) };
                const b2Transform world = b2MulTransforms( body, local );
                switch ( shape._type )
                {
                    case PhysicsShapeType2D::Box:
                    {
                        const b2Vec2 arrPoint[4] = {
                            b2Vec2{-shape._halfExtents._x, -shape._halfExtents._y},
                            b2Vec2{ shape._halfExtents._x, -shape._halfExtents._y},
                            b2Vec2{ shape._halfExtents._x,  shape._halfExtents._y},
                            b2Vec2{-shape._halfExtents._x,  shape._halfExtents._y}
                        };
                        outProxy = b2MakeOffsetProxy( arrPoint, 4, 0.0f, world.p, world.q );
                        return true;
                    }
                    case PhysicsShapeType2D::Circle:
                    {
                        const b2Vec2 center{ 0.0f, 0.0f };
                        outProxy = b2MakeOffsetProxy( &center, 1, shape._radius, world.p, world.q );
                        return true;
                    }
                    case PhysicsShapeType2D::Capsule:
                    {
                        const b2Vec2 arrPoint[2] = {
                            b2Vec2{0.0f, -shape._halfHeight},
                            b2Vec2{0.0f,  shape._halfHeight}
                        };
                        outProxy = b2MakeOffsetProxy( arrPoint, 2, shape._radius, world.p, world.q );
                        return true;
                    }
                    case PhysicsShapeType2D::Polygon:
                    {
                        vector<b2Vec2> listPoint;
                        for ( const float2& point : shape._listPoint )
                        {
                            listPoint.push_back( Box2DUtil::toBox2D( point ) );
                        }
                        const int32 count = static_cast<int32>( listPoint.size() < B2_MAX_POLYGON_VERTICES ? listPoint.size() : B2_MAX_POLYGON_VERTICES );
                        if ( count == 0 )
                            return false;
                        outProxy = b2MakeOffsetProxy( listPoint.data(), count, 0.0f, world.p, world.q );
                        return true;
                    }
                    case PhysicsShapeType2D::Chain:
                    {
                        return false;
                    }
                }
                return false;
            }

            /** @brief 관절 종류에 맞는 Box2D 관절을 만듭니다. 만들지 못하는 종류(Cone)면 널 관절입니다. */
            static b2JointId createJointOfType( b2WorldId worldID, const PhysicsJointDesc2D& desc, b2BodyId bodyIDA, b2BodyId bodyIDB, const b2Vec2& anchor, float32 referenceAngle )
            {
                const bool bCollideConnected = desc._bCollideConnectedBodies;
                b2JointId  jointID           = b2_nullJointId;
                switch ( desc._type )
                {
                    case PhysicsJointType::Fixed:
                    {
                        b2WeldJointDef jointDef   = b2DefaultWeldJointDef();
                        jointDef.bodyIdA          = bodyIDA;
                        jointDef.bodyIdB          = bodyIDB;
                        jointDef.localAnchorA     = b2Body_GetLocalPoint( bodyIDA, anchor );
                        jointDef.localAnchorB     = b2Body_GetLocalPoint( bodyIDB, anchor );
                        jointDef.referenceAngle   = referenceAngle;
                        jointDef.collideConnected = bCollideConnected;
                        jointID                   = b2CreateWeldJoint( worldID, &jointDef );
                        break;
                    }
                    case PhysicsJointType::Hinge:
                    {
                        b2RevoluteJointDef jointDef = b2DefaultRevoluteJointDef();
                        jointDef.bodyIdA            = bodyIDA;
                        jointDef.bodyIdB            = bodyIDB;
                        jointDef.localAnchorA       = b2Body_GetLocalPoint( bodyIDA, anchor );
                        jointDef.localAnchorB       = b2Body_GetLocalPoint( bodyIDB, anchor );
                        jointDef.referenceAngle     = referenceAngle;
                        jointDef.enableLimit        = desc._bLimitsEnabled;
                        jointDef.lowerAngle         = desc._minLimit;
                        jointDef.upperAngle         = desc._maxLimit;
                        jointDef.collideConnected   = bCollideConnected;
                        jointID                     = b2CreateRevoluteJoint( worldID, &jointDef );
                        break;
                    }
                    case PhysicsJointType::Cone:
                    {
                        break; // createJoint 첫머리의 supportsJoint 가 이미 걸렀다
                    }
                    case PhysicsJointType::Distance:
                    {
                        const b2Vec2       anchorB  = Box2DUtil::toBox2D( desc._anchorB );
                        b2DistanceJointDef jointDef = b2DefaultDistanceJointDef();
                        jointDef.bodyIdA            = bodyIDA;
                        jointDef.bodyIdB            = bodyIDB;
                        jointDef.localAnchorA       = b2Body_GetLocalPoint( bodyIDA, anchor );
                        jointDef.localAnchorB       = b2Body_GetLocalPoint( bodyIDB, anchorB );
                        jointDef.length             = b2Distance( anchor, anchorB );
                        jointDef.enableLimit        = desc._bLimitsEnabled;
                        jointDef.minLength          = desc._minLimit;
                        jointDef.maxLength          = desc._maxLimit;
                        if ( desc._bLimitsEnabled )
                        {
                            // 범위가 있으면 막대가 아니라 줄이다. 범위 안에서는 자유롭다.
                            jointDef.enableSpring = true;
                            jointDef.hertz        = 0.0f;
                        }
                        jointDef.collideConnected = bCollideConnected;
                        jointID                   = b2CreateDistanceJoint( worldID, &jointDef );
                        break;
                    }
                    case PhysicsJointType::Slider:
                    {
                        b2PrismaticJointDef jointDef = b2DefaultPrismaticJointDef();
                        jointDef.bodyIdA             = bodyIDA;
                        jointDef.bodyIdB             = bodyIDB;
                        jointDef.localAnchorA        = b2Body_GetLocalPoint( bodyIDA, anchor );
                        jointDef.localAnchorB        = b2Body_GetLocalPoint( bodyIDB, anchor );
                        jointDef.localAxisA          = b2Body_GetLocalVector( bodyIDA, b2Normalize( Box2DUtil::toBox2D( desc._axis ) ) );
                        jointDef.referenceAngle      = referenceAngle;
                        jointDef.enableLimit         = desc._bLimitsEnabled;
                        jointDef.lowerTranslation    = desc._minLimit;
                        jointDef.upperTranslation    = desc._maxLimit;
                        jointDef.collideConnected    = bCollideConnected;
                        jointID                      = b2CreatePrismaticJoint( worldID, &jointDef );
                        break;
                    }
                }
                return jointID;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<IPhysicsScene2D> Box2DPhysicsBackend::createScene( const PhysicsSettings& settings )
    {
        SW_MEMORY_SCOPE( Physics );
        return make_unique<Box2DPhysicsScene>( settings );
    }

    // --- 관절 -------------------------------------------------------------------------------------------------------------

    PhysicsJointHandle Box2DPhysicsScene::createJoint( const PhysicsJointDesc2D& desc )
    {
        if ( supportsJoint( desc._type ) == false )
        {
            SW_LOG_ERROR( "createJoint: 2D physics has no Cone joint - use Hinge" );
            return PhysicsJointHandle{};
        }
        const BodyRecord* pRecordA = findBody( desc._bodyA );
        const BodyRecord* pRecordB = desc._bodyB.isValid() ? findBody( desc._bodyB ) : nullptr;
        if ( pRecordA == nullptr || ( desc._bodyB.isValid() && pRecordB == nullptr ) )
        {
            SW_LOG_ERROR( "createJoint: a body handle is stale" );
            return PhysicsJointHandle{};
        }
        const b2BodyId bodyIDA        = pRecordA->_bodyID;
        const b2BodyId bodyIDB        = pRecordB != nullptr ? pRecordB->_bodyID : _groundBodyID;
        const b2Vec2   anchor         = Box2DUtil::toBox2D( desc._anchor );
        const float32  referenceAngle = b2Rot_GetAngle( b2InvMulRot( b2Body_GetRotation( bodyIDA ), b2Body_GetRotation( bodyIDB ) ) );

        const b2JointId jointID = Box2DPhysicsSceneQueryInternal::createJointOfType( _worldID, desc, bodyIDA, bodyIDB, anchor, referenceAngle );
        if ( B2_IS_NULL( jointID ) )
            return PhysicsJointHandle{};
        JointRecord record;
        record._jointID                 = jointID;
        record._bodyA                   = desc._bodyA;
        record._bodyB                   = desc._bodyB;
        record._type                    = desc._type;
        const PhysicsJointHandle handle = PhysicsJointHandle::fromSlot( _joints.insert( std::move( record ) ) );
        setJointMotor( handle, desc._motor );
        return handle;
    }

    void Box2DPhysicsScene::destroyJoint( PhysicsJointHandle joint )
    {
        JointRecord record;
        if ( _joints.take( joint.getSlot(), record ) == false )
            return;
        if ( b2Joint_IsValid( record._jointID ) )
        {
            b2Joint_WakeBodies( record._jointID );
            b2DestroyJoint( record._jointID );
        }
    }

    bool Box2DPhysicsScene::isJointValid( PhysicsJointHandle joint ) const
    {
        return _joints.get( joint.getSlot() ) != nullptr;
    }

    void Box2DPhysicsScene::setJointMotor( PhysicsJointHandle joint, const PhysicsJointMotor& motor )
    {
        const JointRecord* pRecord = _joints.get( joint.getSlot() );
        if ( pRecord == nullptr || b2Joint_IsValid( pRecord->_jointID ) == false )
            return;
        const b2JointId jointID   = pRecord->_jointID;
        const bool      bOff      = motor._mode == PhysicsMotorMode::Off || motor._maxForce <= 0.0f;
        const bool      bVelocity = bOff == false && motor._mode == PhysicsMotorMode::Velocity;
        const bool      bPosition = bOff == false && motor._mode == PhysicsMotorMode::Position;
        switch ( pRecord->_type )
        {
            case PhysicsJointType::Hinge:
            {
                b2RevoluteJoint_EnableMotor( jointID, bVelocity );
                b2RevoluteJoint_SetMotorSpeed( jointID, bVelocity ? motor._target : 0.0f );
                b2RevoluteJoint_SetMaxMotorTorque( jointID, motor._maxForce );
                b2RevoluteJoint_EnableSpring( jointID, bPosition );
                b2RevoluteJoint_SetSpringHertz( jointID, Box2DPhysicsSceneQueryInternal::kPositionMotorHertz );
                b2RevoluteJoint_SetSpringDampingRatio( jointID, Box2DPhysicsSceneQueryInternal::kPositionMotorDampingRatio );
                if ( bPosition )
                    b2RevoluteJoint_SetTargetAngle( jointID, motor._target );
                break;
            }
            case PhysicsJointType::Slider:
            {
                b2PrismaticJoint_EnableMotor( jointID, bVelocity );
                b2PrismaticJoint_SetMotorSpeed( jointID, bVelocity ? motor._target : 0.0f );
                b2PrismaticJoint_SetMaxMotorForce( jointID, motor._maxForce );
                b2PrismaticJoint_EnableSpring( jointID, bPosition );
                b2PrismaticJoint_SetSpringHertz( jointID, Box2DPhysicsSceneQueryInternal::kPositionMotorHertz );
                b2PrismaticJoint_SetSpringDampingRatio( jointID, Box2DPhysicsSceneQueryInternal::kPositionMotorDampingRatio );
                if ( bPosition )
                    b2PrismaticJoint_SetTargetTranslation( jointID, motor._target );
                break;
            }
            case PhysicsJointType::Distance:
            {
                b2DistanceJoint_EnableMotor( jointID, bVelocity );
                b2DistanceJoint_SetMotorSpeed( jointID, bVelocity ? motor._target : 0.0f );
                b2DistanceJoint_SetMaxMotorForce( jointID, motor._maxForce );
                break;
            }
            case PhysicsJointType::Fixed:
            case PhysicsJointType::Cone:
            {
                return;
            }
        }
        b2Joint_WakeBodies( jointID );
    }

    void Box2DPhysicsScene::driveJointToRotation( PhysicsJointHandle joint, const float32& targetRotationA, const float32& targetRotationB, float32 maxTorque )
    {
        (void)targetRotationA;
        (void)targetRotationB;
        (void)maxTorque;
        SW_LOG_ERROR( "driveJointToRotation drives Cone joints, which 2D physics does not have (joint slot %#) - use setJointMotor on the revolute joint",
                      joint.getSlot().index() );
    }

    float32 Box2DPhysicsScene::getJointPosition( PhysicsJointHandle joint ) const
    {
        const JointRecord* pRecord = _joints.get( joint.getSlot() );
        if ( pRecord == nullptr || b2Joint_IsValid( pRecord->_jointID ) == false )
            return 0.0f;
        switch ( pRecord->_type )
        {
            case PhysicsJointType::Hinge:
                return b2RevoluteJoint_GetAngle( pRecord->_jointID );
            case PhysicsJointType::Slider:
                return b2PrismaticJoint_GetTranslation( pRecord->_jointID );
            case PhysicsJointType::Distance:
                return b2DistanceJoint_GetCurrentLength( pRecord->_jointID );
            case PhysicsJointType::Fixed:
            case PhysicsJointType::Cone:
                return 0.0f;
        }
        return 0.0f;
    }

    // --- 캐릭터(무버) -----------------------------------------------------------------------------------------------------

    b2Capsule Box2DPhysicsScene::makeMoverCapsule( const CharacterRecord& record, const float2& position ) const
    {
        const PhysicsCharacterDesc2D& desc = record._desc;
        const b2Vec2                  feet = Box2DUtil::toBox2D( position );
        return b2Capsule{
            b2Vec2{feet.x,                           feet.y + desc._radius},
            b2Vec2{feet.x, feet.y + desc._radius + 2.0f * desc._halfHeight},
            desc._radius
        };
    }

    PhysicsCharacterHandle Box2DPhysicsScene::createCharacter( const PhysicsCharacterDesc2D& desc )
    {
        CharacterRecord record;
        record._desc            = desc;
        record._state._position = desc._position;
        return PhysicsCharacterHandle::fromSlot( _characters.insert( std::move( record ) ) );
    }

    void Box2DPhysicsScene::destroyCharacter( PhysicsCharacterHandle character )
    {
        _characters.erase( character.getSlot() );
    }

    PhysicsCharacterState2D Box2DPhysicsScene::moveCharacter( PhysicsCharacterHandle character, const float2& velocity, float32 deltaTime )
    {
        CharacterRecord* pRecord = _characters.get( character.getSlot() );
        if ( pRecord == nullptr )
            return PhysicsCharacterState2D{};
        PhysicsCharacterState2D& state  = pRecord->_state;
        b2QueryFilter            filter = b2DefaultQueryFilter();
        // 카테고리는 막는 셰이프 비트 하나다 — 무버 캐스트는 센서를 거르지 않으므로 센서(트리거 · 겹침 전용 콜라이더)는 그 비트가 없어 걸리지 않는다(`makeFilter`).
        // 레이어는 마스크 쪽 한 방향으로만 거른다(레이어 행렬이 대칭이라 같은 답이다).
        filter.categoryBits = kSolidShapeBit;
        filter.maskBits     = static_cast<uint64_t>( _layers.getLayerMask( pRecord->_desc._layer ) );

        b2CollisionPlane                             arrPlane[Box2DPhysicsSceneQueryInternal::kMaxMoverPlaneCount];
        b2ShapeId                                    arrShape[Box2DPhysicsSceneQueryInternal::kMaxMoverPlaneCount];
        Box2DPhysicsSceneQueryInternal::MoverContext context{ arrPlane, arrShape, 0 };
        const float2                                 start    = state._position;
        float2                                       position = start;
        // 결정: 움직이는 바닥의 속도는 호출하는 쪽이 아니라 여기서 더한다(3D 와 같다). 직전 스텝에 디딘 바디(`_groundBody`)의 발 밑 점 속도(선속도 + 회전)다.
        // 바디가 사라졌으면(`findBody` 가 null) 실을 것이 없다.
        float2            carriedVelocity{};
        float32           groundYawRate = 0.0f;
        const BodyRecord* pGround       = findBody( state._groundBody );
        if ( pGround != nullptr )
        {
            carriedVelocity = Box2DUtil::toEngine( b2Body_GetWorldPointVelocity( pGround->_bodyID, Box2DUtil::toBox2D( start ) ) );
            groundYawRate   = b2Body_GetAngularVelocity( pGround->_bodyID );
        }
        if ( deltaTime > 0.0f )
        {
            // Box2D 무버는 겹친 면을 모아(`b2World_CollideMover`) 그 면들을 지키는 이동을 풀고(`b2SolvePlanes`), 그만큼 쓸어(`b2World_CastMover`) 옮긴다.
            const float2 target = start + ( velocity + carriedVelocity ) * deltaTime;
            for ( int32 iteration = 0; iteration < Box2DPhysicsSceneQueryInternal::kMoverIterationCount; ++iteration )
            {
                const b2Capsule mover = makeMoverCapsule( *pRecord, position );
                context._planeCount   = 0;
                b2World_CollideMover( _worldID, &mover, filter, &Box2DPhysicsSceneQueryInternal::collectPlane, &context );
                const b2PlaneSolverResult result   = b2SolvePlanes( Box2DUtil::toBox2D( target - position ), arrPlane, context._planeCount );
                const float32             fraction = b2World_CastMover( _worldID, &mover, result.translation, filter );
                const b2Vec2              delta    = b2MulSV( fraction, result.translation );
                position                           = position + Box2DUtil::toEngine( delta );
                if ( b2LengthSquared( delta ) < Box2DPhysicsSceneQueryInternal::kMoverTolerance * Box2DPhysicsSceneQueryInternal::kMoverTolerance )
                    break;
            }
        }
        // 디딤: 캡슐을 조금 내려 걸리는 면 가운데 걸을 수 있는 기울기가 있는지 본다.
        const b2Capsule probe = makeMoverCapsule( *pRecord, position - float2{ 0.0f, Box2DPhysicsSceneQueryInternal::kGroundProbeDistance } );
        context._planeCount   = 0;
        b2World_CollideMover( _worldID, &probe, filter, &Box2DPhysicsSceneQueryInternal::collectPlane, &context );
        const float32 minGroundNormalY = ::cosf( pRecord->_desc._maxSlopeAngle );
        state._bGrounded               = false;
        state._groundNormal            = float2{};
        state._groundBody              = PhysicsBodyHandle{};
        for ( int32 planeIndex = 0; planeIndex < context._planeCount; ++planeIndex )
        {
            const b2Vec2 normal = arrPlane[planeIndex].plane.normal;
            if ( normal.y >= minGroundNormalY )
            {
                state._bGrounded    = true;
                state._groundNormal = Box2DUtil::toEngine( normal );
                state._groundBody   = Box2DUtil::fromUserData( b2Body_GetUserData( b2Shape_GetBody( arrShape[planeIndex] ) ) );
                break;
            }
        }
        state._position        = position;
        state._velocity        = deltaTime > 0.0f ? ( position - start ) * ( 1.0f / deltaTime ) - carriedVelocity : float2{};
        state._carriedVelocity = deltaTime > 0.0f ? carriedVelocity : float2{};
        state._groundYawRate   = groundYawRate;
        return state;
    }

    void Box2DPhysicsScene::setCharacterPosition( PhysicsCharacterHandle character, const float2& position )
    {
        CharacterRecord* pRecord = _characters.get( character.getSlot() );
        if ( pRecord == nullptr )
            return;
        pRecord->_state._position   = position;
        pRecord->_state._groundBody = PhysicsBodyHandle{}; // 옮긴 직후 한 스텝은 옛 바닥의 속도를 싣지 않는다
    }

    bool Box2DPhysicsScene::getCharacterState( PhysicsCharacterHandle character, PhysicsCharacterState2D& outState ) const
    {
        const CharacterRecord* pRecord = _characters.get( character.getSlot() );
        if ( pRecord == nullptr )
            return false;
        outState = pRecord->_state;
        return true;
    }

    // --- 질의 -------------------------------------------------------------------------------------------------------------

    bool Box2DPhysicsScene::raycast( const float2& origin, const float2& direction, float32 maxDistance, const PhysicsQueryFilter& filter, PhysicsCastHit2D& outHit ) const
    {
        b2QueryFilter queryFilter = b2DefaultQueryFilter();
        queryFilter.categoryBits  = ~kSolidShapeBit; // 막는 셰이프 비트는 무버 전용이다(셰이프 마스크가 레이어 행렬만으로 거르게)
        queryFilter.maskBits      = static_cast<uint64_t>( filter._layerMask );
        Box2DPhysicsSceneQueryInternal::CastContext context;
        context._pScene  = this;
        context._pFilter = &filter;
        b2World_CastRay( _worldID, Box2DUtil::toBox2D( origin ), Box2DUtil::toBox2D( direction * maxDistance ), queryFilter, &Box2DPhysicsSceneQueryInternal::castResult,
                         &context );
        if ( context._bHit == false )
            return false;
        outHit._body     = findHandleOfShape( context._shapeID );
        outHit._userData = getBodyUserData( outHit._body );
        outHit._point    = Box2DUtil::toEngine( context._point );
        outHit._normal   = Box2DUtil::toEngine( context._normal );
        outHit._fraction = context._fraction;
        outHit._distance = context._fraction * maxDistance;
        outHit._material = findMaterialName( b2Shape_GetMaterial( context._shapeID ) );
        return true;
    }

    bool Box2DPhysicsScene::shapeCast( const PhysicsShapeDesc2D& shape, const float2& position, const float32& rotation, const float2& direction, float32 maxDistance,
                                       const PhysicsQueryFilter& filter, PhysicsCastHit2D& outHit ) const
    {
        b2ShapeProxy proxy{};
        if ( Box2DPhysicsSceneQueryInternal::makeProxy( shape, position, rotation, proxy ) == false )
        {
            SW_LOG_ERROR( "shapeCast: a chain cannot be cast" );
            return false;
        }
        b2QueryFilter queryFilter = b2DefaultQueryFilter();
        queryFilter.categoryBits  = ~kSolidShapeBit; // 막는 셰이프 비트는 무버 전용이다(셰이프 마스크가 레이어 행렬만으로 거르게)
        queryFilter.maskBits      = static_cast<uint64_t>( filter._layerMask );
        Box2DPhysicsSceneQueryInternal::CastContext context;
        context._pScene  = this;
        context._pFilter = &filter;
        b2World_CastShape( _worldID, &proxy, Box2DUtil::toBox2D( direction * maxDistance ), queryFilter, &Box2DPhysicsSceneQueryInternal::castResult, &context );
        if ( context._bHit == false )
            return false;
        outHit._body     = findHandleOfShape( context._shapeID );
        outHit._userData = getBodyUserData( outHit._body );
        outHit._point    = Box2DUtil::toEngine( context._point );
        outHit._normal   = Box2DUtil::toEngine( context._normal );
        outHit._fraction = context._fraction;
        outHit._distance = context._fraction * maxDistance;
        return true;
    }

    uint32 Box2DPhysicsScene::raycastAll( const float2& origin, const float2& direction, float32 maxDistance, const PhysicsQueryFilter& filter,
                                          vector<PhysicsCastHit2D>& outListHit ) const
    {
        b2QueryFilter queryFilter                                 = b2DefaultQueryFilter();
        queryFilter.categoryBits                                  = ~kSolidShapeBit;
        queryFilter.maskBits                                      = static_cast<uint64_t>( filter._layerMask );
        const size_t                                   firstIndex = outListHit.size();
        Box2DPhysicsSceneQueryInternal::CastAllContext context{ this, &filter, &_settings._listMaterial, &outListHit, maxDistance };
        b2World_CastRay( _worldID, Box2DUtil::toBox2D( origin ), Box2DUtil::toBox2D( direction * maxDistance ), queryFilter,
                         &Box2DPhysicsSceneQueryInternal::castAllResult, &context );
        return PhysicsQueryUtil::keepNearestHitPerBody( outListHit, firstIndex );
    }

    uint32 Box2DPhysicsScene::shapeCastAll( const PhysicsShapeDesc2D& shape, const float2& position, const float32& rotation, const float2& direction,
                                            float32 maxDistance, const PhysicsQueryFilter& filter, vector<PhysicsCastHit2D>& outListHit ) const
    {
        b2ShapeProxy proxy{};
        if ( Box2DPhysicsSceneQueryInternal::makeProxy( shape, position, rotation, proxy ) == false )
        {
            SW_LOG_ERROR( "shapeCastAll: a chain cannot be cast" );
            return 0;
        }
        b2QueryFilter queryFilter                                 = b2DefaultQueryFilter();
        queryFilter.categoryBits                                  = ~kSolidShapeBit;
        queryFilter.maskBits                                      = static_cast<uint64_t>( filter._layerMask );
        const size_t                                   firstIndex = outListHit.size();
        Box2DPhysicsSceneQueryInternal::CastAllContext context{ this, &filter, nullptr, &outListHit, maxDistance };
        b2World_CastShape( _worldID, &proxy, Box2DUtil::toBox2D( direction * maxDistance ), queryFilter, &Box2DPhysicsSceneQueryInternal::castAllResult, &context );
        return PhysicsQueryUtil::keepNearestHitPerBody( outListHit, firstIndex );
    }

    uint32 Box2DPhysicsScene::overlapShape( const PhysicsShapeDesc2D& shape, const float2& position, const float32& rotation, const PhysicsQueryFilter& filter,
                                            vector<PhysicsBodyHandle>& outListBody ) const
    {
        b2ShapeProxy proxy{};
        if ( Box2DPhysicsSceneQueryInternal::makeProxy( shape, position, rotation, proxy ) == false )
            return 0;
        b2QueryFilter queryFilter = b2DefaultQueryFilter();
        queryFilter.categoryBits  = ~kSolidShapeBit; // 막는 셰이프 비트는 무버 전용이다(셰이프 마스크가 레이어 행렬만으로 거르게)
        queryFilter.maskBits      = static_cast<uint64_t>( filter._layerMask );
        // 찾은 바디를 출력 끝에 바로 붙이고 그 구간만 정렬해 중복을 지운다(호출마다 임시 버퍼를 할당하지 않는다).
        const size_t                                   firstIndex = outListBody.size();
        Box2DPhysicsSceneQueryInternal::OverlapContext context{ this, &filter, &outListBody };
        b2World_OverlapShape( _worldID, &proxy, queryFilter, &Box2DPhysicsSceneQueryInternal::overlapResult, &context );
        const vector<PhysicsBodyHandle>::iterator first = outListBody.begin() + static_cast<ptrdiff_t>( firstIndex );
        std::sort( first, outListBody.end() );
        outListBody.erase( std::unique( first, outListBody.end() ), outListBody.end() );
        return static_cast<uint32>( outListBody.size() - firstIndex );
    }
} // namespace sw
