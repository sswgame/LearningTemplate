#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryProfiler.h"

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
            /** @brief 위치 모터(관절 스프링)의 세기 — 목표에 1/5 초 안팎으로 붙는다. */
            static constexpr float32 kPositionMotorHertz        = 5.0f;
            static constexpr float32 kPositionMotorDampingRatio = 0.7f;
            /** @brief 무버 반복 수와 "더 움직이지 않는다" 로 볼 거리입니다(Box2D 무버 예제의 값). */
            static constexpr int32   kMoverIterationCount = 5;
            static constexpr float32 kMoverTolerance      = 0.01f;
            static constexpr int32   kMaxMoverPlaneCount  = 16;
            /** @brief 디딤을 찾으려고 캡슐을 내려 보는 거리입니다. */
            static constexpr float32 kGroundProbeDistance = 0.05f;

            /** @brief 질의 하나의 거름 · 가장 가까운 결과입니다(콜백 문맥). */
            struct CastContext
            {
                const Box2DPhysicsScene*  _pScene{ nullptr };
                const PhysicsQueryFilter* _pFilter{ nullptr };
                b2ShapeId                 _shapeId{};
                b2Vec2                    _point{};
                b2Vec2                    _normal{};
                float32                   _fraction{ 1.0f };
                bool                      _bHit{ false };
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
                int32             _planeCount{ 0 };
            };

            static bool acceptsShape( const Box2DPhysicsScene& scene, b2ShapeId shapeId, const PhysicsQueryFilter& filter )
            {
                if ( filter._bIncludeTriggers == false && b2Shape_IsSensor( shapeId ) )
                    return false;
                const PhysicsBodyHandle body = Box2DUtil::fromUserData( b2Body_GetUserData( b2Shape_GetBody( shapeId ) ) );
                if ( body == filter._ignoreBody )
                    return false;
                return filter._ignoreUserData == 0 || scene.getBodyUserData( body ) != filter._ignoreUserData;
            }

            static float32 castResult( b2ShapeId shapeId, b2Vec2 point, b2Vec2 normal, float32 fraction, void* pContext )
            {
                CastContext& context = *static_cast<CastContext*>( pContext );
                if ( acceptsShape( *context._pScene, shapeId, *context._pFilter ) == false )
                    return -1.0f; // 이 셰이프는 없는 것으로 친다
                context._shapeId  = shapeId;
                context._point    = point;
                context._normal   = normal;
                context._fraction = fraction;
                context._bHit     = true;
                return fraction; // 더 가까운 것만 찾는다
            }

            static bool overlapResult( b2ShapeId shapeId, void* pContext )
            {
                OverlapContext& context = *static_cast<OverlapContext*>( pContext );
                if ( acceptsShape( *context._pScene, shapeId, *context._pFilter ) )
                    context._pListBody->push_back( Box2DUtil::fromUserData( b2Body_GetUserData( b2Shape_GetBody( shapeId ) ) ) );
                return true;
            }

            static bool collectPlane( b2ShapeId shapeId, const b2PlaneResult* pPlane, void* pContext )
            {
                MoverContext& context = *static_cast<MoverContext*>( pContext );
                if ( pPlane->hit == false || b2Shape_IsSensor( shapeId ) || context._planeCount >= kMaxMoverPlaneCount )
                    return true;
                context._pPlane[context._planeCount] = b2CollisionPlane{ pPlane->plane, FLT_MAX, 0.0f, true };
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
                            listPoint.push_back( Box2DUtil::toBox2D( point ) );
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
        const BodyRecord* pRecordA = findBody( desc._bodyA );
        const BodyRecord* pRecordB = desc._bodyB.isValid() ? findBody( desc._bodyB ) : nullptr;
        if ( pRecordA == nullptr || ( desc._bodyB.isValid() && pRecordB == nullptr ) )
        {
            SW_LOG_ERROR( "createJoint: a body handle is stale" );
            return PhysicsJointHandle{};
        }
        const b2BodyId bodyIdA           = pRecordA->_bodyId;
        const b2BodyId bodyIdB           = pRecordB != nullptr ? pRecordB->_bodyId : _groundBodyId;
        const b2Vec2   anchor            = Box2DUtil::toBox2D( desc._anchor );
        const bool     bCollideConnected = desc._bDisableCollision == false;
        const float32  referenceAngle    = b2Rot_GetAngle( b2InvMulRot( b2Body_GetRotation( bodyIdA ), b2Body_GetRotation( bodyIdB ) ) );

        b2JointId jointId = b2_nullJointId;
        switch ( desc._type )
        {
            case PhysicsJointType::Fixed:
            {
                b2WeldJointDef jointDef   = b2DefaultWeldJointDef();
                jointDef.bodyIdA          = bodyIdA;
                jointDef.bodyIdB          = bodyIdB;
                jointDef.localAnchorA     = b2Body_GetLocalPoint( bodyIdA, anchor );
                jointDef.localAnchorB     = b2Body_GetLocalPoint( bodyIdB, anchor );
                jointDef.referenceAngle   = referenceAngle;
                jointDef.collideConnected = bCollideConnected;
                jointId                   = b2CreateWeldJoint( _worldId, &jointDef );
                break;
            }
            case PhysicsJointType::Hinge:
            {
                b2RevoluteJointDef jointDef = b2DefaultRevoluteJointDef();
                jointDef.bodyIdA            = bodyIdA;
                jointDef.bodyIdB            = bodyIdB;
                jointDef.localAnchorA       = b2Body_GetLocalPoint( bodyIdA, anchor );
                jointDef.localAnchorB       = b2Body_GetLocalPoint( bodyIdB, anchor );
                jointDef.referenceAngle     = referenceAngle;
                jointDef.enableLimit        = desc._bLimitsEnabled;
                jointDef.lowerAngle         = desc._minLimit;
                jointDef.upperAngle         = desc._maxLimit;
                jointDef.collideConnected   = bCollideConnected;
                jointId                     = b2CreateRevoluteJoint( _worldId, &jointDef );
                break;
            }
            case PhysicsJointType::Cone:
            {
                SW_LOG_ERROR( "createJoint: 2D physics has no Cone joint - use Hinge" );
                return PhysicsJointHandle{};
            }
            case PhysicsJointType::Distance:
            {
                const b2Vec2       anchorB  = Box2DUtil::toBox2D( desc._anchorB );
                b2DistanceJointDef jointDef = b2DefaultDistanceJointDef();
                jointDef.bodyIdA            = bodyIdA;
                jointDef.bodyIdB            = bodyIdB;
                jointDef.localAnchorA       = b2Body_GetLocalPoint( bodyIdA, anchor );
                jointDef.localAnchorB       = b2Body_GetLocalPoint( bodyIdB, anchorB );
                jointDef.length             = b2Distance( anchor, anchorB );
                jointDef.enableLimit        = desc._bLimitsEnabled;
                jointDef.minLength          = desc._minLimit;
                jointDef.maxLength          = desc._maxLimit;
                if ( desc._bLimitsEnabled )
                {
                    // 범위가 있으면 막대가 아니라 줄이다 — 범위 안에서는 자유롭다.
                    jointDef.enableSpring = true;
                    jointDef.hertz        = 0.0f;
                }
                jointDef.collideConnected = bCollideConnected;
                jointId                   = b2CreateDistanceJoint( _worldId, &jointDef );
                break;
            }
            case PhysicsJointType::Slider:
            {
                b2PrismaticJointDef jointDef = b2DefaultPrismaticJointDef();
                jointDef.bodyIdA             = bodyIdA;
                jointDef.bodyIdB             = bodyIdB;
                jointDef.localAnchorA        = b2Body_GetLocalPoint( bodyIdA, anchor );
                jointDef.localAnchorB        = b2Body_GetLocalPoint( bodyIdB, anchor );
                jointDef.localAxisA          = b2Body_GetLocalVector( bodyIdA, b2Normalize( Box2DUtil::toBox2D( desc._axis ) ) );
                jointDef.referenceAngle      = referenceAngle;
                jointDef.enableLimit         = desc._bLimitsEnabled;
                jointDef.lowerTranslation    = desc._minLimit;
                jointDef.upperTranslation    = desc._maxLimit;
                jointDef.collideConnected    = bCollideConnected;
                jointId                      = b2CreatePrismaticJoint( _worldId, &jointDef );
                break;
            }
        }
        if ( B2_IS_NULL( jointId ) )
            return PhysicsJointHandle{};
        JointRecord record;
        record._jointId                 = jointId;
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
        if ( b2Joint_IsValid( record._jointId ) )
        {
            b2Joint_WakeBodies( record._jointId );
            b2DestroyJoint( record._jointId );
        }
    }

    bool Box2DPhysicsScene::isJointValid( PhysicsJointHandle joint ) const
    {
        return _joints.get( joint.getSlot() ) != nullptr;
    }

    void Box2DPhysicsScene::setJointMotor( PhysicsJointHandle joint, const PhysicsJointMotor& motor )
    {
        const JointRecord* pRecord = _joints.get( joint.getSlot() );
        if ( pRecord == nullptr || b2Joint_IsValid( pRecord->_jointId ) == false )
            return;
        const b2JointId jointId   = pRecord->_jointId;
        const bool      bOff      = motor._mode == PhysicsMotorMode::Off || motor._maxForce <= 0.0f;
        const bool      bVelocity = bOff == false && motor._mode == PhysicsMotorMode::Velocity;
        const bool      bPosition = bOff == false && motor._mode == PhysicsMotorMode::Position;
        switch ( pRecord->_type )
        {
            case PhysicsJointType::Hinge:
            {
                b2RevoluteJoint_EnableMotor( jointId, bVelocity );
                b2RevoluteJoint_SetMotorSpeed( jointId, bVelocity ? motor._target : 0.0f );
                b2RevoluteJoint_SetMaxMotorTorque( jointId, motor._maxForce );
                b2RevoluteJoint_EnableSpring( jointId, bPosition );
                b2RevoluteJoint_SetSpringHertz( jointId, Box2DPhysicsSceneQueryInternal::kPositionMotorHertz );
                b2RevoluteJoint_SetSpringDampingRatio( jointId, Box2DPhysicsSceneQueryInternal::kPositionMotorDampingRatio );
                if ( bPosition )
                    b2RevoluteJoint_SetTargetAngle( jointId, motor._target );
                break;
            }
            case PhysicsJointType::Slider:
            {
                b2PrismaticJoint_EnableMotor( jointId, bVelocity );
                b2PrismaticJoint_SetMotorSpeed( jointId, bVelocity ? motor._target : 0.0f );
                b2PrismaticJoint_SetMaxMotorForce( jointId, motor._maxForce );
                b2PrismaticJoint_EnableSpring( jointId, bPosition );
                b2PrismaticJoint_SetSpringHertz( jointId, Box2DPhysicsSceneQueryInternal::kPositionMotorHertz );
                b2PrismaticJoint_SetSpringDampingRatio( jointId, Box2DPhysicsSceneQueryInternal::kPositionMotorDampingRatio );
                if ( bPosition )
                    b2PrismaticJoint_SetTargetTranslation( jointId, motor._target );
                break;
            }
            case PhysicsJointType::Distance:
            {
                b2DistanceJoint_EnableMotor( jointId, bVelocity );
                b2DistanceJoint_SetMotorSpeed( jointId, bVelocity ? motor._target : 0.0f );
                b2DistanceJoint_SetMaxMotorForce( jointId, motor._maxForce );
                break;
            }
            case PhysicsJointType::Fixed:
            case PhysicsJointType::Cone:
            {
                return;
            }
        }
        b2Joint_WakeBodies( jointId );
    }

    float32 Box2DPhysicsScene::getJointPosition( PhysicsJointHandle joint ) const
    {
        const JointRecord* pRecord = _joints.get( joint.getSlot() );
        if ( pRecord == nullptr || b2Joint_IsValid( pRecord->_jointId ) == false )
            return 0.0f;
        switch ( pRecord->_type )
        {
            case PhysicsJointType::Hinge:
                return b2RevoluteJoint_GetAngle( pRecord->_jointId );
            case PhysicsJointType::Slider:
                return b2PrismaticJoint_GetTranslation( pRecord->_jointId );
            case PhysicsJointType::Distance:
                return b2DistanceJoint_GetCurrentLength( pRecord->_jointId );
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
        filter.categoryBits             = static_cast<uint64_t>( 1 ) << pRecord->_desc._layer;
        filter.maskBits                 = static_cast<uint64_t>( _layers.getLayerMask( pRecord->_desc._layer ) );

        b2CollisionPlane                             arrPlane[Box2DPhysicsSceneQueryInternal::kMaxMoverPlaneCount];
        Box2DPhysicsSceneQueryInternal::MoverContext context{ arrPlane, 0 };
        const float2                                 start    = state._position;
        float2                                       position = start;
        if ( deltaTime > 0.0f )
        {
            // Box2D 무버: 겹친 면을 모아(`b2World_CollideMover`) 그 면들을 지키는 이동을 풀고(`b2SolvePlanes`) 그만큼 쓸어(`b2World_CastMover`) 옮긴다.
            const float2 target = start + velocity * deltaTime;
            for ( int32 iteration = 0; iteration < Box2DPhysicsSceneQueryInternal::kMoverIterationCount; ++iteration )
            {
                const b2Capsule mover = makeMoverCapsule( *pRecord, position );
                context._planeCount   = 0;
                b2World_CollideMover( _worldId, &mover, filter, &Box2DPhysicsSceneQueryInternal::collectPlane, &context );
                const b2PlaneSolverResult result   = b2SolvePlanes( Box2DUtil::toBox2D( target - position ), arrPlane, context._planeCount );
                const float32             fraction = b2World_CastMover( _worldId, &mover, result.translation, filter );
                const b2Vec2              delta    = b2MulSV( fraction, result.translation );
                position                           = position + Box2DUtil::toEngine( delta );
                if ( b2LengthSquared( delta ) < Box2DPhysicsSceneQueryInternal::kMoverTolerance * Box2DPhysicsSceneQueryInternal::kMoverTolerance )
                    break;
            }
        }
        // 디딤: 캡슐을 조금 내려 걸리는 면 가운데 걸을 수 있는 기울기가 있는가.
        const b2Capsule probe = makeMoverCapsule( *pRecord, position - float2{ 0.0f, Box2DPhysicsSceneQueryInternal::kGroundProbeDistance } );
        context._planeCount   = 0;
        b2World_CollideMover( _worldId, &probe, filter, &Box2DPhysicsSceneQueryInternal::collectPlane, &context );
        const float32 minGroundNormalY = ::cosf( pRecord->_desc._maxSlopeAngle );
        state._bGrounded               = false;
        state._groundNormal            = float2{};
        for ( int32 planeIndex = 0; planeIndex < context._planeCount; ++planeIndex )
        {
            const b2Vec2 normal = arrPlane[planeIndex].plane.normal;
            if ( normal.y >= minGroundNormalY )
            {
                state._bGrounded    = true;
                state._groundNormal = Box2DUtil::toEngine( normal );
                break;
            }
        }
        state._position = position;
        state._velocity = deltaTime > 0.0f ? ( position - start ) * ( 1.0f / deltaTime ) : float2{};
        return state;
    }

    void Box2DPhysicsScene::setCharacterPosition( PhysicsCharacterHandle character, const float2& position )
    {
        CharacterRecord* pRecord = _characters.get( character.getSlot() );
        if ( pRecord != nullptr )
            pRecord->_state._position = position;
    }

    PhysicsVehicleHandle Box2DPhysicsScene::createWheeledVehicle( PhysicsBodyHandle chassis, const PhysicsWheeledVehicleDesc& desc )
    {
        (void)chassis;
        (void)desc;
        return PhysicsVehicleHandle{};
    }

    void Box2DPhysicsScene::setVehicleInput( PhysicsVehicleHandle vehicle, float32 forward, float32 right, float32 brake, float32 handBrake )
    {
        (void)vehicle;
        (void)forward;
        (void)right;
        (void)brake;
        (void)handBrake;
    }

    bool Box2DPhysicsScene::getVehicleState( PhysicsVehicleHandle vehicle, PhysicsVehicleState& outState ) const
    {
        (void)vehicle;
        outState = PhysicsVehicleState{};
        return false;
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
        queryFilter.categoryBits  = ~static_cast<uint64_t>( 0 );
        queryFilter.maskBits      = static_cast<uint64_t>( filter._layerMask );
        Box2DPhysicsSceneQueryInternal::CastContext context;
        context._pScene  = this;
        context._pFilter = &filter;
        b2World_CastRay( _worldId, Box2DUtil::toBox2D( origin ), Box2DUtil::toBox2D( direction * maxDistance ), queryFilter, &Box2DPhysicsSceneQueryInternal::castResult,
                         &context );
        if ( context._bHit == false )
            return false;
        outHit._body     = findHandleOfShape( context._shapeId );
        outHit._userData = getBodyUserData( outHit._body );
        outHit._point    = Box2DUtil::toEngine( context._point );
        outHit._normal   = Box2DUtil::toEngine( context._normal );
        outHit._fraction = context._fraction;
        outHit._distance = context._fraction * maxDistance;
        outHit._material = findMaterialName( b2Shape_GetMaterial( context._shapeId ) );
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
        queryFilter.categoryBits  = ~static_cast<uint64_t>( 0 );
        queryFilter.maskBits      = static_cast<uint64_t>( filter._layerMask );
        Box2DPhysicsSceneQueryInternal::CastContext context;
        context._pScene  = this;
        context._pFilter = &filter;
        b2World_CastShape( _worldId, &proxy, Box2DUtil::toBox2D( direction * maxDistance ), queryFilter, &Box2DPhysicsSceneQueryInternal::castResult, &context );
        if ( context._bHit == false )
            return false;
        outHit._body     = findHandleOfShape( context._shapeId );
        outHit._userData = getBodyUserData( outHit._body );
        outHit._point    = Box2DUtil::toEngine( context._point );
        outHit._normal   = Box2DUtil::toEngine( context._normal );
        outHit._fraction = context._fraction;
        outHit._distance = context._fraction * maxDistance;
        return true;
    }

    uint32 Box2DPhysicsScene::overlapShape( const PhysicsShapeDesc2D& shape, const float2& position, const float32& rotation, const PhysicsQueryFilter& filter,
                                            vector<PhysicsBodyHandle>& outListBody ) const
    {
        b2ShapeProxy proxy{};
        if ( Box2DPhysicsSceneQueryInternal::makeProxy( shape, position, rotation, proxy ) == false )
            return 0;
        b2QueryFilter queryFilter = b2DefaultQueryFilter();
        queryFilter.categoryBits  = ~static_cast<uint64_t>( 0 );
        queryFilter.maskBits      = static_cast<uint64_t>( filter._layerMask );
        vector<PhysicsBodyHandle>                      listFound;
        Box2DPhysicsSceneQueryInternal::OverlapContext context{ this, &filter, &listFound };
        b2World_OverlapShape( _worldId, &proxy, queryFilter, &Box2DPhysicsSceneQueryInternal::overlapResult, &context );
        std::sort( listFound.begin(), listFound.end() );
        listFound.erase( std::unique( listFound.begin(), listFound.end() ), listFound.end() );
        outListBody.insert( outListBody.end(), listFound.begin(), listFound.end() );
        return static_cast<uint32>( listFound.size() );
    }
} // namespace sw
