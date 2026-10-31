#include "pch.h"

#include "Engine/Physics/Box2D/Box2DPhysicsScene.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Physics/Box2D/Box2DUtil.h"
#include "Engine/Physics/Box2D/Box2DWorkerPool.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsDebugDraw.h"

#include <box2d/box2d.h>

namespace sw
{
    SW_LOG_CALLER( "Box2DPhysicsScene" );

    namespace
    {
        struct Box2DPhysicsSceneInternal
        {
            static b2BodyType toBodyType( PhysicsBodyType type )
            {
                switch ( type )
                {
                    case PhysicsBodyType::Static:
                        return b2_staticBody;
                    case PhysicsBodyType::Kinematic:
                        return b2_kinematicBody;
                    case PhysicsBodyType::Dynamic:
                        return b2_dynamicBody;
                }
                return b2_dynamicBody;
            }

            static pair<uint64, uint64> makePairKey( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB )
            {
                return bodyB < bodyA ? pair<uint64, uint64>{ bodyB.packed(), bodyA.packed() } : pair<uint64, uint64>{ bodyA.packed(), bodyB.packed() };
            }
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

            [[nodiscard]] static bool collectPlane( b2ShapeId shapeID, const b2PlaneResult* pPlane, void* pContext )
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
    Box2DPhysicsScene::Box2DPhysicsScene( const PhysicsSettings& settings )
        : _settings{ settings }
        , _layers{}
        , _pWorkerPool{}
        , _worldID{}
        , _groundBodyID{}
        , _bodies{}
        , _joints{}
        , _characters{}
        , _shapes{}
        , _mapShapeToBody{}
        , _mapPairToJoint{}
        , _listContactScratch{}
        , _contactTracker{}
        , _listContactEvent{}
        , _listMovedBody{}
        , _bodyCount{ 0 }
    {
        _layers = _settings.makeCollisionLayers();

        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity    = Box2DUtil::toBox2D( _settings._gravity2D );
        if ( _settings._workerCount2D > 1 )
        {
            _pWorkerPool             = make_unique<Box2DWorkerPool>( _settings._workerCount2D );
            worldDef.workerCount     = static_cast<int32>( _pWorkerPool->getWorkerCount() );
            worldDef.enqueueTask     = &Box2DWorkerPool::enqueueTask;
            worldDef.finishTask      = &Box2DWorkerPool::finishTask;
            worldDef.userTaskContext = _pWorkerPool.get();
        }
        _worldID = b2CreateWorld( &worldDef );

        b2BodyDef groundDef = b2DefaultBodyDef();
        groundDef.type      = b2_staticBody;
        _groundBodyID       = b2CreateBody( _worldID, &groundDef );
    }

    Box2DPhysicsScene::~Box2DPhysicsScene()
    {
        // 월드를 지우면 바디, 셰이프, 관절이 함께 사라진다.
        b2DestroyWorld( _worldID );
        _bodies.clear();
        _joints.clear();
        _characters.clear();
        _shapes.clear();
    }

    // --- 시뮬레이션 ---------------------------------------------------------------------------------------------------------

    void Box2DPhysicsScene::step( float32 fixedDeltaTime )
    {
        SW_MEMORY_SCOPE( Physics );
        _listContactEvent.clear();
        _listMovedBody.clear();
        if ( fixedDeltaTime > 0.0f )
        {
            b2World_Step( _worldID, fixedDeltaTime, static_cast<int32>( _settings._subStepCount2D ) );
            // 이동 이벤트는 이 스텝에 풀린 바디마다 하나다 — 이 스텝에 잠든 바디(`fellAsleep`)도 든다. 바닥 바디처럼 사용자 값이 없으면 무효 핸들이다.
            const b2BodyEvents events = b2World_GetBodyEvents( _worldID );
            for ( int32 eventIndex = 0; eventIndex < events.moveCount; ++eventIndex )
            {
                const PhysicsBodyHandle body = Box2DUtil::fromUserData( events.moveEvents[eventIndex].userData );
                if ( body.isValid() )
                    _listMovedBody.push_back( body );
            }
            VectorUtil::sortUnique( _listMovedBody ); // 이벤트 순서는 일꾼 수를 따를 수 있다 — 핸들 순으로 고정한다
        }
        flushEvents();
    }

    void Box2DPhysicsScene::setGravity( const float2& gravity )
    {
        b2World_SetGravity( _worldID, Box2DUtil::toBox2D( gravity ) );
    }

    float2 Box2DPhysicsScene::getGravity() const
    {
        return Box2DUtil::toEngine( b2World_GetGravity( _worldID ) );
    }

    void Box2DPhysicsScene::setLayerCollision( const CollisionLayers& layers )
    {
        _layers = layers;
        _bodies.forEach( [this]( BodyRecord& record )
        { applyFilter( record ); } );
    }

    void Box2DPhysicsScene::drawDebug( IPhysicsDebugRenderer& renderer ) const
    {
        _bodies.forEachHandle( [&]( SlotHandle, const BodyRecord& record )
        { drawBodyRecord( record, renderer ); } );
        _characters.forEachHandle( [&]( SlotHandle, const CharacterRecord& record )
        {
            PhysicsShapeDesc2D capsule;
            capsule._type       = PhysicsShapeType2D::Capsule;
            capsule._radius     = record._desc._radius;
            capsule._halfHeight = record._desc._halfHeight;
            const float2 center = record._state._position + float2{ 0.0f, record._desc._halfHeight + record._desc._radius };
            PhysicsDebugDrawUtil::drawShape2D( renderer, capsule, center, 0.0f, float4{ 0.9f, 0.4f, 1.0f, 1.0f } );
        } );
    }

    bool Box2DPhysicsScene::drawBodyDebug( PhysicsBodyHandle body, IPhysicsDebugRenderer& renderer ) const
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr )
            return false;
        drawBodyRecord( *pRecord, renderer );
        return true;
    }

    void Box2DPhysicsScene::drawBodyRecord( const BodyRecord& record, IPhysicsDebugRenderer& renderer ) const
    {
        if ( record._bEnabled == false || record._pListShape == nullptr )
            return;
        const b2Transform transform = b2Body_GetTransform( record._bodyID );
        const bool        bSleeping = record._type != PhysicsBodyType::Static && b2Body_IsAwake( record._bodyID ) == false;
        const float4      color     = PhysicsDebugDrawUtil::getBodyColor( static_cast<uint8>( record._type ), bSleeping, record.isSensor() );
        for ( const PhysicsShapeDesc2D& shape : *record._pListShape )
        {
            PhysicsDebugDrawUtil::drawShape2D( renderer, shape, Box2DUtil::toEngine( transform.p ), b2Rot_GetAngle( transform.q ), color );
        }
    }

    // --- 셰이프와 필터 ----------------------------------------------------------------------------------------------------

    const PhysicsMaterialDef* Box2DPhysicsScene::resolveMaterial( const hashed_string& name, const hashed_string& fallback ) const
    {
        const hashed_string& wanted = name.empty() ? fallback : name;
        if ( wanted.empty() )
            return &_settings._listMaterial.front(); // 이름을 적지 않은 것은 "첫 재질" 이라는 선언이다
        const PhysicsMaterialDef* pMaterial = _settings.findMaterial( wanted );
        if ( pMaterial == nullptr )
            SW_LOG_ERROR( "Unknown physics material '%#' - the shape is not created", wanted.c_str() ); // 적은 이름이 틀리면 다른 재질로 만들지 않는다
        return pMaterial;
    }

    b2Filter Box2DPhysicsScene::makeFilter( uint8 layer, bool bSensor ) const
    {
        b2Filter filter     = b2DefaultFilter();
        filter.categoryBits = static_cast<uint64_t>( 1 ) << layer;
        filter.maskBits     = static_cast<uint64_t>( _layers.getLayerMask( layer ) );
        if ( bSensor == false )
            filter.maskBits |= kSolidShapeBit;
        return filter;
    }

    void Box2DPhysicsScene::applyFilter( const BodyRecord& record )
    {
        const b2Filter filter = makeFilter( record._layer, record.isSensor() );
        for ( const b2ShapeId& shapeID : record._listShapeID )
        {
            if ( b2Shape_IsValid( shapeID ) )
                b2Shape_SetFilter( shapeID, filter );
        }
    }

    int32 Box2DPhysicsScene::findMaterialIndex( const hashed_string& name ) const
    {
        for ( size_t materialIndex = 0; materialIndex < _settings._listMaterial.size(); ++materialIndex )
        {
            if ( _settings._listMaterial[materialIndex]._name == name )
                return static_cast<int32>( materialIndex );
        }
        return 0;
    }

    hashed_string Box2DPhysicsScene::findMaterialName( int32 materialIndex ) const
    {
        if ( materialIndex < 0 || static_cast<size_t>( materialIndex ) >= _settings._listMaterial.size() )
            return hashed_string{};
        return _settings._listMaterial[static_cast<size_t>( materialIndex )]._name;
    }

    bool Box2DPhysicsScene::attachChain( BodyRecord& record, const PhysicsShapeDesc2D& shape, const PhysicsMaterialDef& material, PhysicsBodyHandle handle )
    {
        const b2Vec2 center    = Box2DUtil::toBox2D( shape._localPosition );
        const b2Rot  rotation  = b2MakeRot( shape._localAngle );
        void* const  pUserData = Box2DUtil::toUserData( handle );
        if ( shape._listPoint.size() < 4 )
        {
            SW_LOG_ERROR( "A chain shape needs at least 4 points (Box2D)" );
            return false;
        }
        vector<b2Vec2> listPoint;
        listPoint.reserve( shape._listPoint.size() );
        for ( const float2& point : shape._listPoint )
        {
            listPoint.push_back( b2TransformPoint( b2Transform{ center, rotation }, Box2DUtil::toBox2D( point ) ) );
        }
        b2SurfaceMaterial surface   = b2DefaultSurfaceMaterial();
        surface.friction            = material._friction;
        surface.restitution         = material._restitution;
        surface.userMaterialId      = findMaterialIndex( material._name );
        b2ChainDef chainDef         = b2DefaultChainDef();
        chainDef.points             = listPoint.data();
        chainDef.count              = static_cast<int32>( listPoint.size() );
        chainDef.materials          = &surface;
        chainDef.materialCount      = 1;
        chainDef.filter             = makeFilter( record._layer, false ); // 체인은 센서가 될 수 없다
        chainDef.isLoop             = shape._bLoop;
        chainDef.enableSensorEvents = true;
        chainDef.userData           = pUserData;
        const b2ChainId chainID     = b2CreateChain( record._bodyID, &chainDef );
        record._listChainID.push_back( chainID );
        const int32       segmentCount = b2Chain_GetSegmentCount( chainID );
        vector<b2ShapeId> listSegment( static_cast<size_t>( segmentCount ) );
        b2Chain_GetSegments( chainID, listSegment.data(), segmentCount );
        for ( const b2ShapeId& segment : listSegment )
        {
            record._listShapeID.push_back( segment );
            _mapShapeToBody[Box2DUtil::makeShapeKey( segment )] = handle;
        }
        return true;
    }

    bool Box2DPhysicsScene::attachShape( BodyRecord& record, const PhysicsShapeDesc2D& shape, const hashed_string& defaultMaterial, PhysicsBodyHandle handle )
    {
        const PhysicsMaterialDef* pMaterial = resolveMaterial( shape._material, defaultMaterial );
        if ( pMaterial == nullptr )
            return false;
        const PhysicsMaterialDef& material  = *pMaterial;
        const b2Vec2              center    = Box2DUtil::toBox2D( shape._localPosition );
        const b2Rot               rotation  = b2MakeRot( shape._localAngle );
        void* const               pUserData = Box2DUtil::toUserData( handle );

        if ( shape._type == PhysicsShapeType2D::Chain )
            return attachChain( record, shape, material, handle );

        b2ShapeDef shapeDef              = b2DefaultShapeDef();
        shapeDef.userData                = pUserData;
        shapeDef.material.friction       = material._friction;
        shapeDef.material.restitution    = material._restitution;
        shapeDef.material.userMaterialId = findMaterialIndex( material._name );
        shapeDef.density                 = material._density;
        shapeDef.filter                  = makeFilter( record._layer, record.isSensor() );
        shapeDef.isSensor                = record.isSensor();
        shapeDef.enableSensorEvents      = true;
        shapeDef.enableContactEvents     = true;
        shapeDef.enableHitEvents         = true;

        b2ShapeId shapeID = b2_nullShapeId;
        switch ( shape._type )
        {
            case PhysicsShapeType2D::Box:
            {
                const b2Polygon box = b2MakeOffsetBox( shape._halfExtents._x, shape._halfExtents._y, center, rotation );
                shapeID             = b2CreatePolygonShape( record._bodyID, &shapeDef, &box );
                break;
            }
            case PhysicsShapeType2D::Circle:
            {
                const b2Circle circle{ center, shape._radius };
                shapeID = b2CreateCircleShape( record._bodyID, &shapeDef, &circle );
                break;
            }
            case PhysicsShapeType2D::Capsule:
            {
                const b2Transform local{ center, rotation };
                const b2Capsule   capsule{ b2TransformPoint( local, b2Vec2{ 0.0f, -shape._halfHeight } ), b2TransformPoint( local, b2Vec2{ 0.0f, shape._halfHeight } ),
                                         shape._radius };
                shapeID = b2CreateCapsuleShape( record._bodyID, &shapeDef, &capsule );
                break;
            }
            case PhysicsShapeType2D::Polygon:
            {
                vector<b2Vec2> listPoint;
                listPoint.reserve( shape._listPoint.size() );
                for ( const float2& point : shape._listPoint )
                {
                    listPoint.push_back( Box2DUtil::toBox2D( point ) );
                }
                const int32  pointCount = static_cast<int32>( listPoint.size() < B2_MAX_POLYGON_VERTICES ? listPoint.size() : B2_MAX_POLYGON_VERTICES );
                const b2Hull hull       = b2ComputeHull( listPoint.data(), pointCount );
                if ( hull.count == 0 )
                {
                    SW_LOG_ERROR( "A polygon shape has no valid convex hull (needs 3..8 points that are not collinear)" );
                    return false;
                }
                const b2Polygon polygon = b2MakeOffsetPolygon( &hull, center, rotation );
                shapeID                 = b2CreatePolygonShape( record._bodyID, &shapeDef, &polygon );
                break;
            }
            case PhysicsShapeType2D::Chain:
            {
                break;
            }
        }
        if ( B2_IS_NULL( shapeID ) )
            return false;
        record._listShapeID.push_back( shapeID );
        _mapShapeToBody[Box2DUtil::makeShapeKey( shapeID )] = handle;
        return true;
    }

    PhysicsShapeHandle Box2DPhysicsScene::createShape( span<const PhysicsShapeDesc2D> listShape, const hashed_string& material )
    {
        if ( listShape.empty() )
            return PhysicsShapeHandle{};
        ShapeRecord record;
        record._pListShape = make_shared<const ShapeDescList>( listShape.begin(), listShape.end() );
        record._material   = material;
        return PhysicsShapeHandle::fromSlot( _shapes.insert( std::move( record ) ) );
    }

    PhysicsShapeHandle Box2DPhysicsScene::createCompoundShape( span<const PhysicsShapeHandle> listChild )
    {
        if ( listChild.empty() )
        {
            SW_LOG_ERROR( "createCompoundShape: no child shapes" );
            return PhysicsShapeHandle{};
        }
        // Box2D 바디는 셰이프를 여럿 붙일 수 있으므로 자식 서술자를 이어 붙인다. 재질을 지정하지 않은 자식 셰이프에는 그 자식의 재질을 적어 둔다.
        ShapeDescList listDesc;
        for ( const PhysicsShapeHandle& child : listChild )
        {
            const ShapeRecord* pChild = _shapes.get( child.getSlot() );
            if ( pChild == nullptr )
            {
                SW_LOG_ERROR( "createCompoundShape: a child shape handle is stale" );
                return PhysicsShapeHandle{};
            }
            for ( PhysicsShapeDesc2D shape : *pChild->_pListShape )
            {
                if ( shape._material.empty() )
                    shape._material = pChild->_material;
                listDesc.push_back( std::move( shape ) );
            }
        }
        ShapeRecord record;
        record._material   = _shapes.get( listChild[0].getSlot() )->_material;
        record._pListShape = make_shared<const ShapeDescList>( std::move( listDesc ) );
        return PhysicsShapeHandle::fromSlot( _shapes.insert( std::move( record ) ) );
    }

    void Box2DPhysicsScene::destroyShape( PhysicsShapeHandle shape )
    {
        _shapes.erase( shape.getSlot() );
    }

    // --- 바디 -------------------------------------------------------------------------------------------------------------

    const Box2DPhysicsScene::BodyRecord* Box2DPhysicsScene::findBody( PhysicsBodyHandle body ) const
    {
        return _bodies.get( body.getSlot() );
    }

    Box2DPhysicsScene::BodyRecord* Box2DPhysicsScene::findBody( PhysicsBodyHandle body )
    {
        return _bodies.get( body.getSlot() );
    }

    PhysicsBodyHandle Box2DPhysicsScene::findHandleOfShape( b2ShapeId shapeID ) const
    {
        const unordered_map<uint64, PhysicsBodyHandle>::const_iterator iter = _mapShapeToBody.find( Box2DUtil::makeShapeKey( shapeID ) );
        return iter != _mapShapeToBody.end() ? iter->second : PhysicsBodyHandle{};
    }

    PhysicsBodyHandle Box2DPhysicsScene::createBody( const PhysicsBodyDesc2D& desc )
    {
        PhysicsBodyHandle               handle{};
        shared_ptr<const ShapeDescList> pListShape;
        hashed_string                   defaultMaterial = desc._material;
        if ( desc._sharedShape.isValid() )
        {
            const ShapeRecord* pShapeRecord = _shapes.get( desc._sharedShape.getSlot() );
            if ( pShapeRecord == nullptr )
            {
                SW_LOG_ERROR( "createBody: the shared shape handle is stale" );
                return handle;
            }
            pListShape = pShapeRecord->_pListShape;
            if ( defaultMaterial.empty() )
                defaultMaterial = pShapeRecord->_material;
        }
        else
        {
            pListShape = make_shared<const ShapeDescList>( desc._listShape );
        }
        if ( pListShape->empty() )
        {
            SW_LOG_ERROR( "A physics body needs at least one shape" );
            return handle;
        }

        b2BodyDef bodyDef       = b2DefaultBodyDef();
        bodyDef.type            = Box2DPhysicsSceneInternal::toBodyType( desc._type );
        bodyDef.position        = Box2DUtil::toBox2D( desc._position );
        bodyDef.rotation        = b2MakeRot( desc._rotation );
        bodyDef.linearVelocity  = Box2DUtil::toBox2D( desc._linearVelocity );
        bodyDef.angularVelocity = desc._angularVelocity;
        bodyDef.linearDamping   = desc._linearDamping;
        bodyDef.angularDamping  = desc._angularDamping;
        bodyDef.gravityScale    = desc._gravityFactor;
        bodyDef.enableSleep     = desc._bAllowSleep;
        bodyDef.isBullet        = desc._bContinuous;
        bodyDef.fixedRotation   = desc._bLockRotation;

        BodyRecord record;
        record._pListShape    = pListShape;
        record._bodyID        = b2CreateBody( _worldID, &bodyDef );
        record._userData      = desc._userData;
        record._type          = desc._type;
        record._layer         = desc._layer;
        record._bTrigger      = desc._bTrigger;
        record._bOverlapOnly  = desc._bOverlapOnly;
        record._bEnabled      = true;
        const b2BodyId bodyID = record._bodyID;
        handle                = PhysicsBodyHandle::fromSlot( _bodies.insert( std::move( record ) ) );
        BodyRecord& stored    = *findBody( handle );
        b2Body_SetUserData( bodyID, Box2DUtil::toUserData( handle ) );
        for ( const PhysicsShapeDesc2D& shape : *pListShape )
        {
            if ( attachShape( stored, shape, defaultMaterial, handle ) == false )
            {
                destroyBody( handle );
                handle = PhysicsBodyHandle{};
                return handle;
            }
        }
        if ( desc._mass > 0.0f && desc._type == PhysicsBodyType::Dynamic )
        {
            b2MassData    massData = b2Body_GetMassData( bodyID );
            const float32 scale    = massData.mass > 0.0f ? desc._mass / massData.mass : 1.0f;
            massData.rotationalInertia *= scale;
            massData.mass = desc._mass;
            b2Body_SetMassData( bodyID, massData );
        }
        ++_bodyCount;
        return handle;
    }

    void Box2DPhysicsScene::createBodies( span<const PhysicsBodyDesc2D> listDesc, vector<PhysicsBodyHandle>& outListBody )
    {
        // Box2D 는 바디를 넣을 때 넓은 단계 트리를 항상 고치므로 배치 경로가 따로 없다. 하나씩 만든다.
        outListBody.reserve( outListBody.size() + listDesc.size() );
        for ( const PhysicsBodyDesc2D& desc : listDesc )
        {
            outListBody.push_back( createBody( desc ) );
        }
    }

    void Box2DPhysicsScene::destroyBody( PhysicsBodyHandle body )
    {
        destroyBodies( span<const PhysicsBodyHandle>{ &body, 1 } );
    }

    void Box2DPhysicsScene::forgetPairJointsOf( PhysicsBodyHandle body )
    {
        for ( map<pair<uint64, uint64>, b2JointId>::iterator iter = _mapPairToJoint.begin(); iter != _mapPairToJoint.end(); )
        {
            if ( iter->first.first == body.packed() || iter->first.second == body.packed() )
                iter = _mapPairToJoint.erase( iter );
            else
                ++iter;
        }
    }

    void Box2DPhysicsScene::destroyBodies( span<const PhysicsBodyHandle> listBody )
    {
        vector<PhysicsBodyHandle> listDestroyed;
        for ( const PhysicsBodyHandle& body : listBody )
        {
            BodyRecord record;
            if ( _bodies.take( body.getSlot(), record ) == false )
                continue;
            // Box2D 가 바디에 붙은 관절을 함께 지운다. 엔진 쪽 관절 기록도 지운다.
            vector<PhysicsJointHandle> listDoomed;
            _joints.forEachHandle( [&]( SlotHandle slot, const JointRecord& joint )
            {
                if ( joint._bodyA == body || joint._bodyB == body )
                    listDoomed.push_back( PhysicsJointHandle::fromSlot( slot ) );
            } );
            for ( const PhysicsJointHandle& joint : listDoomed )
            {
                _joints.erase( joint.getSlot() );
            }
            forgetPairJointsOf( body );
            for ( const b2ShapeId& shapeID : record._listShapeID )
            {
                _mapShapeToBody.erase( Box2DUtil::makeShapeKey( shapeID ) );
            }
            b2DestroyBody( record._bodyID );
            listDestroyed.push_back( body );
            --_bodyCount;
        }
        _contactTracker.removeBodies( span<const PhysicsBodyHandle>{ listDestroyed.data(), listDestroyed.size() } );
    }

    bool Box2DPhysicsScene::isBodyValid( PhysicsBodyHandle body ) const
    {
        return findBody( body ) != nullptr;
    }

    uint32 Box2DPhysicsScene::getBodyCount() const
    {
        return _bodyCount;
    }

    void Box2DPhysicsScene::setBodyEnabled( PhysicsBodyHandle body, bool bEnabled )
    {
        BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr || pRecord->_bEnabled == bEnabled )
            return;
        if ( bEnabled )
        {
            b2Body_Enable( pRecord->_bodyID );
        }
        else
        {
            b2Body_Disable( pRecord->_bodyID );
            _contactTracker.removeBodies( span<const PhysicsBodyHandle>{ &body, 1 } );
        }
        pRecord->_bEnabled = bEnabled;
    }

    bool Box2DPhysicsScene::isBodyEnabled( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr && pRecord->_bEnabled;
    }

    bool Box2DPhysicsScene::getBodyTransform( PhysicsBodyHandle body, float2& outPosition, float32& outRotation ) const
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr )
            return false;
        const b2Transform transform = b2Body_GetTransform( pRecord->_bodyID );
        outPosition                 = Box2DUtil::toEngine( transform.p );
        outRotation                 = b2Rot_GetAngle( transform.q );
        return true;
    }

    void Box2DPhysicsScene::setBodyTransform( PhysicsBodyHandle body, const float2& position, const float32& rotation )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr )
            return;
        b2Body_SetTransform( pRecord->_bodyID, Box2DUtil::toBox2D( position ), b2MakeRot( rotation ) );
        if ( pRecord->_type != PhysicsBodyType::Static )
            b2Body_SetAwake( pRecord->_bodyID, true );
    }

    void Box2DPhysicsScene::moveKinematic( PhysicsBodyHandle body, const float2& targetPosition, const float32& targetRotation, float32 deltaTime )
    {
        const BodyRecord* pRecord             = findBody( body );
        const bool        bNotKinematicTarget = pRecord == nullptr || pRecord->_type != PhysicsBodyType::Kinematic || deltaTime <= 0.0f;
        if ( bNotKinematicTarget )
            return;
        b2Body_SetTargetTransform( pRecord->_bodyID, b2Transform{ Box2DUtil::toBox2D( targetPosition ), b2MakeRot( targetRotation ) }, deltaTime );
    }

    float2 Box2DPhysicsScene::getLinearVelocity( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? Box2DUtil::toEngine( b2Body_GetLinearVelocity( pRecord->_bodyID ) ) : float2{};
    }

    void Box2DPhysicsScene::setLinearVelocity( PhysicsBodyHandle body, const float2& velocity )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            b2Body_SetLinearVelocity( pRecord->_bodyID, Box2DUtil::toBox2D( velocity ) );
    }

    float32 Box2DPhysicsScene::getAngularVelocity( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? b2Body_GetAngularVelocity( pRecord->_bodyID ) : 0.0f;
    }

    void Box2DPhysicsScene::setAngularVelocity( PhysicsBodyHandle body, const float32& velocity )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            b2Body_SetAngularVelocity( pRecord->_bodyID, velocity );
    }

    void Box2DPhysicsScene::addForce( PhysicsBodyHandle body, const float2& force )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            b2Body_ApplyForceToCenter( pRecord->_bodyID, Box2DUtil::toBox2D( force ), true );
    }

    void Box2DPhysicsScene::addImpulse( PhysicsBodyHandle body, const float2& impulse )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            b2Body_ApplyLinearImpulseToCenter( pRecord->_bodyID, Box2DUtil::toBox2D( impulse ), true );
    }

    void Box2DPhysicsScene::addImpulseAtPoint( PhysicsBodyHandle body, const float2& impulse, const float2& point )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            b2Body_ApplyLinearImpulse( pRecord->_bodyID, Box2DUtil::toBox2D( impulse ), Box2DUtil::toBox2D( point ), true );
    }

    void Box2DPhysicsScene::addTorque( PhysicsBodyHandle body, const float32& torque )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            b2Body_ApplyTorque( pRecord->_bodyID, torque, true );
    }

    void Box2DPhysicsScene::setBodyType( PhysicsBodyHandle body, PhysicsBodyType type )
    {
        BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr || pRecord->_type == type )
            return;
        b2Body_SetType( pRecord->_bodyID, Box2DPhysicsSceneInternal::toBodyType( type ) );
        pRecord->_type = type;
    }

    PhysicsBodyType Box2DPhysicsScene::getBodyType( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? pRecord->_type : PhysicsBodyType::Static;
    }

    void Box2DPhysicsScene::setBodyLayer( PhysicsBodyHandle body, uint8 layer )
    {
        BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr || layer >= CollisionLayers::kLayerCount )
            return;
        pRecord->_layer = layer;
        applyFilter( *pRecord );
    }

    uint8 Box2DPhysicsScene::getBodyLayer( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? pRecord->_layer : 0;
    }

    void Box2DPhysicsScene::setGravityFactor( PhysicsBodyHandle body, float32 factor )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            b2Body_SetGravityScale( pRecord->_bodyID, factor );
    }

    float32 Box2DPhysicsScene::getBodyMass( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? b2Body_GetMass( pRecord->_bodyID ) : 0.0f;
    }

    bool Box2DPhysicsScene::isBodySleeping( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr && pRecord->_type != PhysicsBodyType::Static && b2Body_IsAwake( pRecord->_bodyID ) == false;
    }

    void Box2DPhysicsScene::wakeBody( PhysicsBodyHandle body )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr && pRecord->_type != PhysicsBodyType::Static )
            b2Body_SetAwake( pRecord->_bodyID, true );
    }

    uint64 Box2DPhysicsScene::getBodyUserData( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? pRecord->_userData : 0;
    }

    bool Box2DPhysicsScene::isBodyTrigger( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr && pRecord->_bTrigger;
    }

    void Box2DPhysicsScene::setPairCollision( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, bool bCollide )
    {
        const BodyRecord* pRecordA            = findBody( bodyA );
        const BodyRecord* pRecordB            = findBody( bodyB );
        const bool        bInvalidJointBodies = pRecordA == nullptr || pRecordB == nullptr || bodyA == bodyB;
        if ( bInvalidJointBodies )
            return;
        const pair<uint64, uint64>                           key  = Box2DPhysicsSceneInternal::makePairKey( bodyA, bodyB );
        const map<pair<uint64, uint64>, b2JointId>::iterator iter = _mapPairToJoint.find( key );
        if ( bCollide )
        {
            if ( iter == _mapPairToJoint.end() )
                return;
            if ( b2Joint_IsValid( iter->second ) )
                b2DestroyJoint( iter->second );
            _mapPairToJoint.erase( iter );
            return;
        }
        if ( iter != _mapPairToJoint.end() )
            return;
        // 필터 관절은 힘을 내지 않고 두 바디의 충돌만 끈다(Box2D 의 "null joint").
        b2FilterJointDef jointDef = b2DefaultFilterJointDef();
        jointDef.bodyIdA          = pRecordA->_bodyID;
        jointDef.bodyIdB          = pRecordB->_bodyID;
        _mapPairToJoint[key]      = b2CreateFilterJoint( _worldID, &jointDef );
    }

    // --- 이벤트 -----------------------------------------------------------------------------------------------------------

    float32 Box2DPhysicsScene::computePairImpulse( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB ) const
    {
        const BodyRecord* pRecordA = findBody( bodyA );
        if ( pRecordA == nullptr )
            return 0.0f;
        const int32 capacity = b2Body_GetContactCapacity( pRecordA->_bodyID );
        if ( capacity <= 0 )
            return 0.0f;
        _listContactScratch.resize( static_cast<size_t>( capacity ) );
        const int32 count   = b2Body_GetContactData( pRecordA->_bodyID, _listContactScratch.data(), capacity );
        float32     impulse = 0.0f;
        for ( int32 contactIndex = 0; contactIndex < count; ++contactIndex )
        {
            const b2ContactData&    contact = _listContactScratch[static_cast<size_t>( contactIndex )];
            const PhysicsBodyHandle bodyOfA = findHandleOfShape( contact.shapeIdA );
            const PhysicsBodyHandle bodyOfB = findHandleOfShape( contact.shapeIdB );
            const bool              bPair   = ( bodyOfA == bodyA && bodyOfB == bodyB ) || ( bodyOfA == bodyB && bodyOfB == bodyA );
            if ( bPair == false )
                continue;
            for ( int32 pointIndex = 0; pointIndex < contact.manifold.pointCount; ++pointIndex )
            {
                impulse += contact.manifold.points[pointIndex].normalImpulse;
            }
        }
        // `normalImpulse` 는 마지막 서브 스텝의 값이다. 한 스텝의 충격량은 서브 스텝 수를 곱한 값이다(`totalNormalImpulse` 는 이완 반복까지 더해서 약 두 배가 된다).
        return impulse * static_cast<float32>( _settings._subStepCount2D );
    }

    void Box2DPhysicsScene::flushEvents()
    {
        const b2ContactEvents contactEvents = b2World_GetContactEvents( _worldID );
        for ( int32 eventIndex = 0; eventIndex < contactEvents.beginCount; ++eventIndex )
        {
            const b2ContactBeginTouchEvent& event    = contactEvents.beginEvents[eventIndex];
            const PhysicsBodyHandle         bodyA    = findHandleOfShape( event.shapeIdA );
            const PhysicsBodyHandle         bodyB    = findHandleOfShape( event.shapeIdB );
            const BodyRecord*               pRecordA = findBody( bodyA );
            const BodyRecord*               pRecordB = findBody( bodyB );
            if ( pRecordA == nullptr || pRecordB == nullptr )
                continue;
            float2 point{};
            if ( event.manifold.pointCount > 0 )
                point = Box2DUtil::toEngine( event.manifold.points[0].point );
            _contactTracker.beginSubContact( PhysicsContactBody{ bodyA, pRecordA->_userData, false }, PhysicsContactBody{ bodyB, pRecordB->_userData, false }, point,
                                             Box2DUtil::toEngine( event.manifold.normal ), 0.0f );
        }
        for ( int32 eventIndex = 0; eventIndex < contactEvents.endCount; ++eventIndex )
        {
            const b2ContactEndTouchEvent& event = contactEvents.endEvents[eventIndex];
            _contactTracker.endSubContact( findHandleOfShape( event.shapeIdA ), findHandleOfShape( event.shapeIdB ) );
        }
        // 부딪힘(hit): 다가오던 속도로 충격량을 어림한다. 유효 질량 × 속도 × (1 + 반발)이며, Jolt 의 `EstimateCollisionResponse` 에 해당한다. Box2D 의 시작은
        // 투기적 접촉(아직 떨어져 있다)에서도 발생하고 그 매니폴드는 풀기 전이라, 충돌의 크기는 이 이벤트가 가장 정확하다.
        for ( int32 eventIndex = 0; eventIndex < contactEvents.hitCount; ++eventIndex )
        {
            const b2ContactHitEvent& event    = contactEvents.hitEvents[eventIndex];
            const PhysicsBodyHandle  bodyA    = findHandleOfShape( event.shapeIdA );
            const PhysicsBodyHandle  bodyB    = findHandleOfShape( event.shapeIdB );
            const BodyRecord*        pRecordA = findBody( bodyA );
            const BodyRecord*        pRecordB = findBody( bodyB );
            if ( pRecordA == nullptr || pRecordB == nullptr )
                continue;
            const float32 massA        = b2Body_GetMass( pRecordA->_bodyID );
            const float32 massB        = b2Body_GetMass( pRecordB->_bodyID );
            const float32 inverseMass  = ( massA > 0.0f ? 1.0f / massA : 0.0f ) + ( massB > 0.0f ? 1.0f / massB : 0.0f );
            const float32 restitutionA = b2Shape_IsValid( event.shapeIdA ) ? b2Shape_GetRestitution( event.shapeIdA ) : 0.0f;
            const float32 restitutionB = b2Shape_IsValid( event.shapeIdB ) ? b2Shape_GetRestitution( event.shapeIdB ) : 0.0f;
            const float32 restitution  = restitutionA > restitutionB ? restitutionA : restitutionB;
            const float32 impulse      = inverseMass > 0.0f ? event.approachSpeed * ( 1.0f + restitution ) / inverseMass : 0.0f;
            _contactTracker.persistSubContact( bodyA, bodyB, Box2DUtil::toEngine( event.point ), Box2DUtil::toEngine( event.normal ), impulse );
        }

        const b2SensorEvents sensorEvents = b2World_GetSensorEvents( _worldID );
        for ( int32 eventIndex = 0; eventIndex < sensorEvents.beginCount; ++eventIndex )
        {
            const b2SensorBeginTouchEvent& event    = sensorEvents.beginEvents[eventIndex];
            const PhysicsBodyHandle        sensor   = findHandleOfShape( event.sensorShapeId );
            const PhysicsBodyHandle        visitor  = findHandleOfShape( event.visitorShapeId );
            const BodyRecord*              pSensor  = findBody( sensor );
            const BodyRecord*              pVisitor = findBody( visitor );
            if ( pSensor == nullptr || pVisitor == nullptr )
                continue;
            // 트리거 표시는 바디의 뜻(`_bTrigger`)을 싣는다 — 겹침 전용 센서(막는 2D 콜라이더)는 트리거가 아니다. 센서끼리는 서로를 보므로 쌍 하나가 셰이프 쌍 둘이다
            // (추적기가 바디 쌍 하나로 센다).
            _contactTracker.beginSubContact( PhysicsContactBody{ sensor, pSensor->_userData, pSensor->_bTrigger, pSensor->_bOverlapOnly },
                                             PhysicsContactBody{ visitor, pVisitor->_userData, pVisitor->_bTrigger, pVisitor->_bOverlapOnly }, float2{}, float2{}, 0.0f );
        }
        for ( int32 eventIndex = 0; eventIndex < sensorEvents.endCount; ++eventIndex )
        {
            const b2SensorEndTouchEvent& event = sensorEvents.endEvents[eventIndex];
            _contactTracker.endSubContact( findHandleOfShape( event.sensorShapeId ), findHandleOfShape( event.visitorShapeId ) );
        }

        _contactTracker.finishStep( _listContactEvent );
        // 유지의 충격량은 그 스텝에 푼 접촉 충격량이다(쉬는 상자는 무게 × 스텝). 3D(Jolt 의 유지 어림값)와 같은 의미다.
        for ( PhysicsContactEvent2D& event : _listContactEvent )
        {
            if ( event._phase == PhysicsContactPhase::Stay && event.isOverlap() == false )
                event._impulse = computePairImpulse( event._bodyA, event._bodyB );
        }
    }

} // namespace sw

namespace sw
{
    // 2D 백엔드(Box2D v3)의 입구. Box2D 는 전역 상태가 없어(월드마다 따로 있다) 초기화하고 해제할 것이 없다. 월드는 기본으로 한 스레드로 돈다.
    // Box2D 의 솔버 태스크는 워커들이 **동시에** 돌며 서로를 바쁘게 기다리므로(스테이지 동기화), 엔진의 공유 작업 풀(다른 일로 바쁜 워커가 있을 수 있다)에
    // 그대로 넘기면 교착할 수 있다. 바디가 수천이면 설정(`PhysicsSettings::_workerCount2D`)으로 씬마다 전용 워커를 붙인다(`Box2DWorkerPool`).
    unique_ptr<IPhysicsScene2D> PhysicsBackend::createScene2D( const PhysicsSettings& settings )
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

        const b2JointId jointID = Box2DPhysicsSceneInternal::createJointOfType( _worldID, desc, bodyIDA, bodyIDB, anchor, referenceAngle );
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
                b2RevoluteJoint_SetSpringHertz( jointID, Box2DPhysicsSceneInternal::kPositionMotorHertz );
                b2RevoluteJoint_SetSpringDampingRatio( jointID, Box2DPhysicsSceneInternal::kPositionMotorDampingRatio );
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
                b2PrismaticJoint_SetSpringHertz( jointID, Box2DPhysicsSceneInternal::kPositionMotorHertz );
                b2PrismaticJoint_SetSpringDampingRatio( jointID, Box2DPhysicsSceneInternal::kPositionMotorDampingRatio );
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

        b2CollisionPlane                        arrPlane[Box2DPhysicsSceneInternal::kMaxMoverPlaneCount];
        b2ShapeId                               arrShape[Box2DPhysicsSceneInternal::kMaxMoverPlaneCount];
        Box2DPhysicsSceneInternal::MoverContext context{ arrPlane, arrShape, 0 };
        const float2                            start    = state._position;
        float2                                  position = start;
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
            for ( int32 iteration = 0; iteration < Box2DPhysicsSceneInternal::kMoverIterationCount; ++iteration )
            {
                const b2Capsule mover = makeMoverCapsule( *pRecord, position );
                context._planeCount   = 0;
                b2World_CollideMover( _worldID, &mover, filter, &Box2DPhysicsSceneInternal::collectPlane, &context );
                const b2PlaneSolverResult result   = b2SolvePlanes( Box2DUtil::toBox2D( target - position ), arrPlane, context._planeCount );
                const float32             fraction = b2World_CastMover( _worldID, &mover, result.translation, filter );
                const b2Vec2              delta    = b2MulSV( fraction, result.translation );
                position                           = position + Box2DUtil::toEngine( delta );
                if ( b2LengthSquared( delta ) < Box2DPhysicsSceneInternal::kMoverTolerance * Box2DPhysicsSceneInternal::kMoverTolerance )
                    break;
            }
        }
        // 디딤: 캡슐을 조금 내려 걸리는 면 가운데 걸을 수 있는 기울기가 있는지 본다.
        const b2Capsule probe = makeMoverCapsule( *pRecord, position - float2{ 0.0f, Box2DPhysicsSceneInternal::kGroundProbeDistance } );
        context._planeCount   = 0;
        b2World_CollideMover( _worldID, &probe, filter, &Box2DPhysicsSceneInternal::collectPlane, &context );
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
        Box2DPhysicsSceneInternal::CastContext context;
        context._pScene  = this;
        context._pFilter = &filter;
        b2World_CastRay( _worldID, Box2DUtil::toBox2D( origin ), Box2DUtil::toBox2D( direction * maxDistance ), queryFilter, &Box2DPhysicsSceneInternal::castResult,
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
        if ( Box2DPhysicsSceneInternal::makeProxy( shape, position, rotation, proxy ) == false )
        {
            SW_LOG_ERROR( "shapeCast: a chain cannot be cast" );
            return false;
        }
        b2QueryFilter queryFilter = b2DefaultQueryFilter();
        queryFilter.categoryBits  = ~kSolidShapeBit; // 막는 셰이프 비트는 무버 전용이다(셰이프 마스크가 레이어 행렬만으로 거르게)
        queryFilter.maskBits      = static_cast<uint64_t>( filter._layerMask );
        Box2DPhysicsSceneInternal::CastContext context;
        context._pScene  = this;
        context._pFilter = &filter;
        b2World_CastShape( _worldID, &proxy, Box2DUtil::toBox2D( direction * maxDistance ), queryFilter, &Box2DPhysicsSceneInternal::castResult, &context );
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
        b2QueryFilter queryFilter                            = b2DefaultQueryFilter();
        queryFilter.categoryBits                             = ~kSolidShapeBit;
        queryFilter.maskBits                                 = static_cast<uint64_t>( filter._layerMask );
        const size_t                              firstIndex = outListHit.size();
        Box2DPhysicsSceneInternal::CastAllContext context{ this, &filter, &_settings._listMaterial, &outListHit, maxDistance };
        b2World_CastRay( _worldID, Box2DUtil::toBox2D( origin ), Box2DUtil::toBox2D( direction * maxDistance ), queryFilter,
                         &Box2DPhysicsSceneInternal::castAllResult, &context );
        return PhysicsQueryUtil::keepNearestHitPerBody( outListHit, firstIndex );
    }

    uint32 Box2DPhysicsScene::shapeCastAll( const PhysicsShapeDesc2D& shape, const float2& position, const float32& rotation, const float2& direction,
                                            float32 maxDistance, const PhysicsQueryFilter& filter, vector<PhysicsCastHit2D>& outListHit ) const
    {
        b2ShapeProxy proxy{};
        if ( Box2DPhysicsSceneInternal::makeProxy( shape, position, rotation, proxy ) == false )
        {
            SW_LOG_ERROR( "shapeCastAll: a chain cannot be cast" );
            return 0;
        }
        b2QueryFilter queryFilter                            = b2DefaultQueryFilter();
        queryFilter.categoryBits                             = ~kSolidShapeBit;
        queryFilter.maskBits                                 = static_cast<uint64_t>( filter._layerMask );
        const size_t                              firstIndex = outListHit.size();
        Box2DPhysicsSceneInternal::CastAllContext context{ this, &filter, nullptr, &outListHit, maxDistance };
        b2World_CastShape( _worldID, &proxy, Box2DUtil::toBox2D( direction * maxDistance ), queryFilter, &Box2DPhysicsSceneInternal::castAllResult, &context );
        return PhysicsQueryUtil::keepNearestHitPerBody( outListHit, firstIndex );
    }

    uint32 Box2DPhysicsScene::overlapShape( const PhysicsShapeDesc2D& shape, const float2& position, const float32& rotation, const PhysicsQueryFilter& filter,
                                            vector<PhysicsBodyHandle>& outListBody ) const
    {
        b2ShapeProxy proxy{};
        if ( Box2DPhysicsSceneInternal::makeProxy( shape, position, rotation, proxy ) == false )
            return 0;
        b2QueryFilter queryFilter = b2DefaultQueryFilter();
        queryFilter.categoryBits  = ~kSolidShapeBit; // 막는 셰이프 비트는 무버 전용이다(셰이프 마스크가 레이어 행렬만으로 거르게)
        queryFilter.maskBits      = static_cast<uint64_t>( filter._layerMask );
        // 찾은 바디를 출력 끝에 바로 붙이고 그 구간만 정렬해 중복을 지운다(호출마다 임시 버퍼를 할당하지 않는다).
        const size_t                              firstIndex = outListBody.size();
        Box2DPhysicsSceneInternal::OverlapContext context{ this, &filter, &outListBody };
        b2World_OverlapShape( _worldID, &proxy, queryFilter, &Box2DPhysicsSceneInternal::overlapResult, &context );
        const vector<PhysicsBodyHandle>::iterator first = outListBody.begin() + static_cast<ptrdiff_t>( firstIndex );
        std::sort( first, outListBody.end() );
        outListBody.erase( std::unique( first, outListBody.end() ), outListBody.end() );
        return static_cast<uint32>( outListBody.size() - firstIndex );
    }
} // namespace sw
