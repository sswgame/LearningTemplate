#include "pch.h"

#include "Engine/Physics/Box2D/Box2DPhysicsScene.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Physics/Box2D/Box2DUtil.h"
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    Box2DPhysicsScene::Box2DPhysicsScene( const PhysicsSettings& settings )
        : _settings{ settings }
        , _layers{}
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
        , _bodyCount{ 0 }
    {
        _settings.ensureDefaults();
        _layers = _settings.makeCollisionLayers();

        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity    = Box2DUtil::toBox2D( _settings._gravity2D );
        _worldID            = b2CreateWorld( &worldDef );

        b2BodyDef groundDef = b2DefaultBodyDef();
        groundDef.type      = b2_staticBody;
        _groundBodyID       = b2CreateBody( _worldID, &groundDef );
    }

    Box2DPhysicsScene::~Box2DPhysicsScene()
    {
        // 월드를 지우면 바디 · 셰이프 · 관절이 함께 사라진다.
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
        if ( fixedDeltaTime > 0.0f )
            b2World_Step( _worldID, fixedDeltaTime, static_cast<int32>( _settings._subStepCount2D ) );
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
        {
            if ( record._bEnabled == false || record._pListShape == nullptr )
                return;
            const b2Transform transform = b2Body_GetTransform( record._bodyID );
            const bool        bSleeping = record._type != PhysicsBodyType::Static && b2Body_IsAwake( record._bodyID ) == false;
            const float4      color     = PhysicsDebugDrawUtil::getBodyColor( static_cast<uint8>( record._type ), bSleeping, record._bTrigger );
            for ( const PhysicsShapeDesc2D& shape : *record._pListShape )
            {
                PhysicsDebugDrawUtil::drawShape2D( renderer, shape, Box2DUtil::toEngine( transform.p ), b2Rot_GetAngle( transform.q ), color );
            }
        } );
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

    // --- 셰이프 · 거름 ----------------------------------------------------------------------------------------------------

    const PhysicsMaterialDef& Box2DPhysicsScene::resolveMaterial( const hashed_string& name, const hashed_string& fallback ) const
    {
        const hashed_string& wanted = name.empty() ? fallback : name;
        if ( wanted.empty() == false )
        {
            const PhysicsMaterialDef* pMaterial = _settings.findMaterial( wanted );
            if ( pMaterial != nullptr )
                return *pMaterial;
            SW_LOG_ERROR( "Unknown physics material '%#' - using '%#'", wanted.c_str(), _settings._listMaterial.front()._name.c_str() );
        }
        return _settings._listMaterial.front();
    }

    b2Filter Box2DPhysicsScene::makeFilter( uint8 layer ) const
    {
        b2Filter filter     = b2DefaultFilter();
        filter.categoryBits = static_cast<uint64_t>( 1 ) << layer;
        filter.maskBits     = static_cast<uint64_t>( _layers.getLayerMask( layer ) );
        return filter;
    }

    void Box2DPhysicsScene::applyFilter( const BodyRecord& record )
    {
        const b2Filter filter = makeFilter( record._layer );
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

    bool Box2DPhysicsScene::attachShape( BodyRecord& record, const PhysicsShapeDesc2D& shape, const hashed_string& defaultMaterial, PhysicsBodyHandle handle )
    {
        const PhysicsMaterialDef& material  = resolveMaterial( shape._material, defaultMaterial );
        const b2Vec2              center    = Box2DUtil::toBox2D( shape._localPosition );
        const b2Rot               rotation  = b2MakeRot( shape._localAngle );
        void* const               pUserData = Box2DUtil::toUserData( handle );

        if ( shape._type == PhysicsShapeType2D::Chain )
        {
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
            chainDef.filter             = makeFilter( record._layer );
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

        b2ShapeDef shapeDef              = b2DefaultShapeDef();
        shapeDef.userData                = pUserData;
        shapeDef.material.friction       = material._friction;
        shapeDef.material.restitution    = material._restitution;
        shapeDef.material.userMaterialId = findMaterialIndex( material._name );
        shapeDef.density                 = material._density;
        shapeDef.filter                  = makeFilter( record._layer );
        shapeDef.isSensor                = record._bTrigger;
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
        // Box2D 바디는 셰이프를 여럿 붙이므로 자식 서술자를 이어 붙인다 — 재질을 적지 않은 자식 셰이프에는 그 자식의 재질을 적어 둔다.
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
        shared_ptr<const ShapeDescList> pListShape;
        hashed_string                   defaultMaterial = desc._material;
        if ( desc._sharedShape.isValid() )
        {
            const ShapeRecord* pShapeRecord = _shapes.get( desc._sharedShape.getSlot() );
            if ( pShapeRecord == nullptr )
            {
                SW_LOG_ERROR( "createBody: the shared shape handle is stale" );
                return PhysicsBodyHandle{};
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
            return PhysicsBodyHandle{};
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
        record._pListShape             = pListShape;
        record._bodyID                 = b2CreateBody( _worldID, &bodyDef );
        record._userData               = desc._userData;
        record._type                   = desc._type;
        record._layer                  = desc._layer;
        record._bTrigger               = desc._bTrigger;
        record._bEnabled               = true;
        const b2BodyId          bodyID = record._bodyID;
        const PhysicsBodyHandle handle = PhysicsBodyHandle::fromSlot( _bodies.insert( std::move( record ) ) );
        BodyRecord&             stored = *findBody( handle );
        b2Body_SetUserData( bodyID, Box2DUtil::toUserData( handle ) );
        for ( const PhysicsShapeDesc2D& shape : *pListShape )
        {
            if ( attachShape( stored, shape, defaultMaterial, handle ) == false )
            {
                destroyBody( handle );
                return PhysicsBodyHandle{};
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
        // Box2D 는 바디를 넣을 때 넓은 단계 트리를 늘 고쳐 묶음 경로가 따로 없다 — 하나씩 만든다.
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
            // Box2D 가 바디에 붙은 관절을 함께 지운다 — 엔진 쪽 관절 기록도 지운다.
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
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr || pRecord->_type != PhysicsBodyType::Kinematic || deltaTime <= 0.0f )
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

    void Box2DPhysicsScene::setPairCollision( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, bool bCollide )
    {
        const BodyRecord* pRecordA = findBody( bodyA );
        const BodyRecord* pRecordB = findBody( bodyB );
        if ( pRecordA == nullptr || pRecordB == nullptr || bodyA == bodyB )
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
        // `normalImpulse` 는 마지막 서브 스텝의 것이다 — 한 스텝의 충격량은 서브 스텝 수를 곱한 값이다(`totalNormalImpulse` 는 이완 반복까지 더해 약 두 배다).
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
        // 부딪힘(hit) — 다가오던 속도로 충격량을 어림한다: 유효 질량 × 속도 × (1 + 반발)(Jolt `EstimateCollisionResponse` 와 같은 자리). Box2D 의 시작은
        // 투기적 접촉(아직 떨어져 있다)에서도 나고 그 매니폴드는 풀기 전이라, 충돌의 크기는 이 이벤트가 가장 정확하다.
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
            _contactTracker.beginSubContact( PhysicsContactBody{ sensor, pSensor->_userData, true }, PhysicsContactBody{ visitor, pVisitor->_userData, pVisitor->_bTrigger },
                                             float2{}, float2{}, 0.0f );
        }
        for ( int32 eventIndex = 0; eventIndex < sensorEvents.endCount; ++eventIndex )
        {
            const b2SensorEndTouchEvent& event = sensorEvents.endEvents[eventIndex];
            _contactTracker.endSubContact( findHandleOfShape( event.sensorShapeId ), findHandleOfShape( event.visitorShapeId ) );
        }

        _contactTracker.finishStep( _listContactEvent );
        // 유지의 충격량은 그 스텝에 푼 접촉 충격량이다(쉬는 상자는 무게 × 스텝) — 3D(Jolt 의 유지 어림값)와 같은 뜻이다.
        for ( PhysicsContactEvent2D& event : _listContactEvent )
        {
            if ( event._phase == PhysicsContactPhase::Stay && event.involvesTrigger() == false )
                event._impulse = computePairImpulse( event._bodyA, event._bodyB );
        }
    }
} // namespace sw
