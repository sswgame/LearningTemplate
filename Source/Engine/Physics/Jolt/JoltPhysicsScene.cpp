#include "pch.h"

#include "Engine/Physics/Jolt/JoltPhysicsScene.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryProfiler.h"

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
            static constexpr JPH::uint kTempAllocatorByteSize = 16u * 1024u * 1024u;

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

            /** @brief 상자 반 크기에 맞는 볼록 반지름 — 기본값(0.05)이 반 크기보다 크면 Jolt 가 상자를 거부한다. */
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
             *          길이를 다시 재면 반올림으로 상한을 한 ulp 넘을 수 있어 조금 안쪽으로 줄인다.
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
                // 이 씬이 짓는 셰이프는 모두 `JoltPhysicsMaterial` 을 단다(기본 재질만 예외).
                return static_cast<const JoltPhysicsMaterial*>( pMaterial );
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
        , _tempAllocator{ JoltPhysicsSceneInternal::kTempAllocatorByteSize }
        , _system{}
        , _pJobSystem{ pJobSystem }
        , _listJoltMaterial{}
        , _bodies{}
        , _joints{}
        , _characters{}
        , _shapes{}
        , _listBodyIdEntry{}
        , _pairFilter{}
        , _contactTracker{}
        , _listContactEvent{}
        , _listContactReport{}
        , _reportMutex{}
        , _bodyCount{ 0 }
    {
        _settings.ensureDefaults();
        _objectLayerPairFilter.setLayers( _settings.makeCollisionLayers() );
        _system.Init( JoltPhysicsSceneInternal::kMaxBodyCount, 0, JoltPhysicsSceneInternal::kMaxBodyPairCount, JoltPhysicsSceneInternal::kMaxContactCount,
                      _broadPhaseLayers, _objectVsBroadPhaseFilter, _objectLayerPairFilter );
        _system.SetGravity( JoltUtil::toJolt( _settings._gravity ) );
        _system.SetContactListener( this );

        _listJoltMaterial.reserve( _settings._listMaterial.size() );
        for ( const PhysicsMaterialDef& material : _settings._listMaterial )
            _listJoltMaterial.push_back( JPH::Ref<JoltPhysicsMaterial>{ JoltUtil::createObject<JoltPhysicsMaterial>( material._name, material._friction, material._restitution ) } );
    }

    JoltPhysicsScene::~JoltPhysicsScene()
    {
        // 관절 → 캐릭터 → 바디 순서로 내린다(관절이 바디를 가리킨다).
        _joints.forEach( [this]( JointRecord& joint )
        {
            if ( joint._pConstraint != nullptr )
                _system.RemoveConstraint( joint._pConstraint.GetPtr() );
            joint._pConstraint = nullptr;
        } );
        _joints.clear();
        _characters.clear();
        JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        _bodies.forEach( [&bodyInterface]( BodyRecord& record )
        {
            if ( record._bEnabled )
                bodyInterface.RemoveBody( record._bodyId );
            bodyInterface.DestroyBody( record._bodyId );
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
        if ( fixedDeltaTime > 0.0f )
        {
            const JPH::EPhysicsUpdateError error = _system.Update( fixedDeltaTime, 1, &_tempAllocator, _pJobSystem );
            if ( error != JPH::EPhysicsUpdateError::None )
                SW_LOG_WARNING( "Jolt step reported overflow flags 0x%# - raise the body pair / contact limits", static_cast<uint32>( error ) );
        }
        flushContactReports();
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
        const JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        _bodies.forEachHandle( [&]( SlotHandle, const BodyRecord& record )
        {
            if ( record._bEnabled == false || record._pListShape == nullptr )
                return;
            JPH::RVec3 position;
            JPH::Quat  rotation;
            bodyInterface.GetPositionAndRotation( record._bodyId, position, rotation );
            const bool   bSleeping = record._type != PhysicsBodyType::Static && bodyInterface.IsActive( record._bodyId ) == false;
            const float4 color     = PhysicsDebugDrawUtil::getBodyColor( static_cast<uint8>( record._type ), bSleeping, record._bTrigger );
            for ( const PhysicsShapeDesc3D& shape : *record._pListShape )
                PhysicsDebugDrawUtil::drawShape3D( renderer, shape, JoltUtil::toEngine( position ), JoltUtil::toEngine( rotation ), color );
        } );
        _characters.forEachHandle( [&]( SlotHandle, const CharacterRecord& record )
        {
            PhysicsShapeDesc3D capsule;
            capsule._type       = PhysicsShapeType3D::Capsule;
            capsule._radius     = record._desc._radius;
            capsule._halfHeight = record._desc._halfHeight;
            const float3 center = record._state._position + float3{ 0.0f, record._desc._halfHeight + record._desc._radius, 0.0f };
            PhysicsDebugDrawUtil::drawShape3D( renderer, capsule, center, quaternion{}, float4{ 0.9f, 0.4f, 1.0f, 1.0f } );
        } );
    }

    // --- 셰이프 -----------------------------------------------------------------------------------------------------------

    const PhysicsMaterialDef& JoltPhysicsScene::resolveMaterial( const hashed_string& name, const hashed_string& fallback ) const
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

    const JPH::PhysicsMaterial* JoltPhysicsScene::findJoltMaterial( const hashed_string& name ) const
    {
        for ( size_t materialIndex = 0; materialIndex < _settings._listMaterial.size(); ++materialIndex )
        {
            if ( _settings._listMaterial[materialIndex]._name == name )
                return _listJoltMaterial[materialIndex].GetPtr();
        }
        return _listJoltMaterial.front().GetPtr();
    }

    JPH::RefConst<JPH::Shape> JoltPhysicsScene::buildSingleShape( const PhysicsShapeDesc3D& shape, const PhysicsMaterialDef& material,
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
                JPH::Array<JPH::Vec3> listPoint;
                listPoint.reserve( shape._listPoint.size() );
                for ( const float3& point : shape._listPoint )
                    listPoint.push_back( JoltUtil::toJolt( point ) );
                JPH::ConvexHullShapeSettings settings{ listPoint, JPH::cDefaultConvexRadius, pMaterial };
                settings.SetEmbedded();
                settings.SetDensity( material._density );
                result = settings.Create();
                break;
            }
            case PhysicsShapeType3D::TriangleMesh:
            {
                JPH::VertexList listVertex;
                listVertex.reserve( shape._listPoint.size() );
                for ( const float3& point : shape._listPoint )
                    listVertex.push_back( JPH::Float3{ point._x, point._y, point._z } );
                JPH::IndexedTriangleList listTriangle;
                listTriangle.reserve( shape._listIndex.size() / 3 );
                for ( size_t index = 0; index + 2 < shape._listIndex.size(); index += 3 )
                    listTriangle.push_back( JPH::IndexedTriangle{ shape._listIndex[index], shape._listIndex[index + 1], shape._listIndex[index + 2], 0 } );
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

    JPH::RefConst<JPH::Shape> JoltPhysicsScene::buildShape( span<const PhysicsShapeDesc3D> listShape, const hashed_string& defaultMaterial, bool bAllowMesh ) const
    {
        if ( listShape.empty() )
        {
            SW_LOG_ERROR( "A physics body needs at least one shape" );
            return nullptr;
        }
        JPH::StaticCompoundShapeSettings compound;
        compound.SetEmbedded();
        JPH::RefConst<JPH::Shape> pSingle;
        for ( const PhysicsShapeDesc3D& shape : listShape )
        {
            if ( shape._type == PhysicsShapeType3D::TriangleMesh && bAllowMesh == false )
            {
                SW_LOG_ERROR( "Triangle mesh shapes need a Static or Kinematic body (a moving mesh has no volume)" );
                return nullptr;
            }
            const PhysicsMaterialDef& material = resolveMaterial( shape._material, defaultMaterial );
            JPH::RefConst<JPH::Shape> pShape   = buildSingleShape( shape, material, findJoltMaterial( material._name ) );
            if ( pShape == nullptr )
                return nullptr;
            if ( listShape.size() == 1 )
            {
                pSingle = pShape;
                if ( JoltPhysicsSceneInternal::isIdentityLocal( shape ) )
                    return pSingle;
                JPH::RotatedTranslatedShapeSettings offset{ JoltUtil::toJolt( shape._localPosition ),
                                                            JoltUtil::toJolt( quaternion::createFromYawPitchRoll( shape._localRotation ) ), pSingle.GetPtr() };
                offset.SetEmbedded();
                JPH::Shape::ShapeResult result = offset.Create();
                if ( result.HasError() )
                {
                    SW_LOG_ERROR( "Jolt rejected a shape offset: %#", result.GetError().c_str() );
                    return nullptr;
                }
                return result.Get();
            }
            compound.AddShape( JoltUtil::toJolt( shape._localPosition ), JoltUtil::toJolt( quaternion::createFromYawPitchRoll( shape._localRotation ) ), pShape.GetPtr() );
        }
        JPH::Shape::ShapeResult result = compound.Create();
        if ( result.HasError() )
        {
            SW_LOG_ERROR( "Jolt rejected a compound shape: %#", result.GetError().c_str() );
            return nullptr;
        }
        return result.Get();
    }

    PhysicsShapeHandle JoltPhysicsScene::createShape( span<const PhysicsShapeDesc3D> listShape, const hashed_string& material )
    {
        ShapeRecord record;
        record._pShape = buildShape( listShape, material, true );
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
        ShapeDescList listDesc; // 선 그리기용 — 자식 서술자를 이어 붙인다
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

    PhysicsBodyHandle JoltPhysicsScene::findHandle( const JPH::BodyID& bodyId ) const
    {
        const JPH::uint32 index = bodyId.GetIndex();
        if ( index >= _listBodyIdEntry.size() || _listBodyIdEntry[index]._bodyIdValue != bodyId.GetIndexAndSequenceNumber() )
            return PhysicsBodyHandle{};
        return _listBodyIdEntry[index]._body;
    }

    void JoltPhysicsScene::setHandle( const JPH::BodyID& bodyId, PhysicsBodyHandle body )
    {
        const JPH::uint32 index = bodyId.GetIndex();
        if ( index >= _listBodyIdEntry.size() )
            _listBodyIdEntry.resize( static_cast<size_t>( index ) + 1 );
        _listBodyIdEntry[index]._bodyIdValue = body.isValid() ? bodyId.GetIndexAndSequenceNumber() : JPH::BodyID::cInvalidBodyID;
        _listBodyIdEntry[index]._body        = body;
    }

    PhysicsBodyHandle JoltPhysicsScene::createBodyUnadded( const PhysicsBodyDesc3D& desc )
    {
        BodyRecord                record;
        JPH::RefConst<JPH::Shape> pShape;
        if ( desc._sharedShape.isValid() )
        {
            const ShapeRecord* pShapeRecord = _shapes.get( desc._sharedShape.getSlot() );
            if ( pShapeRecord == nullptr )
            {
                SW_LOG_ERROR( "createBody: the shared shape handle is stale" );
                return PhysicsBodyHandle{};
            }
            pShape             = pShapeRecord->_pShape;
            record._pListShape = pShapeRecord->_pListShape;
        }
        else
        {
            pShape = buildShape( span<const PhysicsShapeDesc3D>{ desc._listShape.data(), desc._listShape.size() }, desc._material, desc._type != PhysicsBodyType::Dynamic );
            if ( pShape == nullptr )
                return PhysicsBodyHandle{};
            record._pListShape = make_shared<const ShapeDescList>( desc._listShape );
        }
        if ( desc._type == PhysicsBodyType::Dynamic && pShape->MustBeStatic() )
        {
            SW_LOG_ERROR( "createBody: a Dynamic body cannot use a triangle mesh shape" );
            return PhysicsBodyHandle{};
        }

        const PhysicsMaterialDef& material = resolveMaterial( desc._material, hashed_string{} );
        const bool                bMoving  = desc._type != PhysicsBodyType::Static;
        JPH::BodyCreationSettings settings{ pShape.GetPtr(), JoltUtil::toJolt( desc._position ), JoltUtil::toJolt( desc._rotation ),
                                            JoltPhysicsSceneInternal::toMotionType( desc._type ), JoltLayerUtil::makeObjectLayer( desc._layer, bMoving ) };
        settings.mFriction                = material._friction;
        settings.mRestitution             = material._restitution;
        settings.mLinearDamping           = desc._linearDamping;
        settings.mAngularDamping          = desc._angularDamping;
        settings.mGravityFactor           = desc._gravityFactor;
        settings.mIsSensor                = desc._bTrigger;
        settings.mAllowSleeping           = desc._bAllowSleep;
        settings.mAllowDynamicOrKinematic = desc._bAllowTypeChange;
        settings.mMotionQuality           = desc._bContinuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        settings.mLinearVelocity          = JoltUtil::toJolt( desc._linearVelocity );
        settings.mAngularVelocity         = JoltPhysicsSceneInternal::clampAngularVelocity( JoltUtil::toJolt( desc._angularVelocity ), settings.mMaxAngularVelocity );
        if ( desc._bLockRotation )
            settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
        if ( desc._mass > 0.0f && bMoving )
        {
            settings.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = desc._mass;
        }

        JPH::Body* pBody = _system.GetBodyInterface().CreateBody( settings );
        if ( pBody == nullptr )
        {
            SW_LOG_ERROR( "createBody: Jolt is out of bodies (limit %#)", JoltPhysicsSceneInternal::kMaxBodyCount );
            return PhysicsBodyHandle{};
        }
        record._bodyId                 = pBody->GetID();
        record._userData               = desc._userData;
        record._type                   = desc._type;
        record._layer                  = desc._layer;
        record._bTrigger               = desc._bTrigger;
        record._bEnabled               = true;
        const PhysicsBodyHandle handle = PhysicsBodyHandle::fromSlot( _bodies.insert( std::move( record ) ) );
        pBody->SetUserData( handle.packed() );
        setHandle( pBody->GetID(), handle );
        ++_bodyCount;
        return handle;
    }

    PhysicsBodyHandle JoltPhysicsScene::createBody( const PhysicsBodyDesc3D& desc )
    {
        const PhysicsBodyHandle handle  = createBodyUnadded( desc );
        const BodyRecord*       pRecord = findBody( handle );
        if ( pRecord == nullptr )
            return PhysicsBodyHandle{};
        const bool bActivate = desc._type != PhysicsBodyType::Static;
        _system.GetBodyInterface().AddBody( pRecord->_bodyId, bActivate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate );
        return handle;
    }

    void JoltPhysicsScene::createBodies( span<const PhysicsBodyDesc3D> listDesc, vector<PhysicsBodyHandle>& outListBody )
    {
        // 넓은 단계 트리를 한 번에 고치려고 움직이는 것과 정적인 것을 따로 모아 묶음으로 넣는다.
        vector<JPH::BodyID> listMovingId;
        vector<JPH::BodyID> listStaticId;
        outListBody.reserve( outListBody.size() + listDesc.size() );
        for ( const PhysicsBodyDesc3D& desc : listDesc )
        {
            const PhysicsBodyHandle handle  = createBodyUnadded( desc );
            const BodyRecord*       pRecord = findBody( handle );
            outListBody.push_back( handle );
            if ( pRecord == nullptr )
                continue;
            if ( desc._type == PhysicsBodyType::Static )
                listStaticId.push_back( pRecord->_bodyId );
            else
                listMovingId.push_back( pRecord->_bodyId );
        }
        JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        if ( listStaticId.empty() == false )
        {
            const JPH::BodyInterface::AddState state = bodyInterface.AddBodiesPrepare( listStaticId.data(), static_cast<int32>( listStaticId.size() ) );
            bodyInterface.AddBodiesFinalize( listStaticId.data(), static_cast<int32>( listStaticId.size() ), state, JPH::EActivation::DontActivate );
        }
        if ( listMovingId.empty() == false )
        {
            const JPH::BodyInterface::AddState state = bodyInterface.AddBodiesPrepare( listMovingId.data(), static_cast<int32>( listMovingId.size() ) );
            bodyInterface.AddBodiesFinalize( listMovingId.data(), static_cast<int32>( listMovingId.size() ), state, JPH::EActivation::Activate );
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
            destroyJoint( joint );
    }

    void JoltPhysicsScene::destroyBodies( span<const PhysicsBodyHandle> listBody )
    {
        if ( listBody.empty() )
            return;
        destroyJointsOf( listBody );
        vector<JPH::BodyID>       listRemoveId;
        vector<JPH::BodyID>       listDestroyId;
        vector<PhysicsBodyHandle> listDestroyed;
        for ( const PhysicsBodyHandle& body : listBody )
        {
            BodyRecord record;
            if ( _bodies.take( body.getSlot(), record ) == false )
                continue;
            if ( record._bEnabled )
                listRemoveId.push_back( record._bodyId );
            listDestroyId.push_back( record._bodyId );
            setHandle( record._bodyId, PhysicsBodyHandle{} );
            listDestroyed.push_back( body );
            --_bodyCount;
        }
        JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        if ( listRemoveId.empty() == false )
            bodyInterface.RemoveBodies( listRemoveId.data(), static_cast<int32>( listRemoveId.size() ) );
        if ( listDestroyId.empty() == false )
            bodyInterface.DestroyBodies( listDestroyId.data(), static_cast<int32>( listDestroyId.size() ) );
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
            bodyInterface.AddBody( pRecord->_bodyId, pRecord->_type != PhysicsBodyType::Static ? JPH::EActivation::Activate : JPH::EActivation::DontActivate );
        }
        else
        {
            bodyInterface.RemoveBody( pRecord->_bodyId );
            // 빠진 바디의 접촉은 Jolt 가 알리지 않는다 — 추적기가 끝낸다.
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
        _system.GetBodyInterface().GetPositionAndRotation( pRecord->_bodyId, position, rotation );
        outPosition = JoltUtil::toEngine( position );
        outRotation = JoltUtil::toEngine( rotation );
        return true;
    }

    void JoltPhysicsScene::setBodyTransform( PhysicsBodyHandle body, const float3& position, const quaternion& rotation )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr )
            return;
        const JPH::EActivation activation = pRecord->_type == PhysicsBodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
        _system.GetBodyInterface().SetPositionAndRotation( pRecord->_bodyId, JoltUtil::toJolt( position ), JoltUtil::toJolt( rotation ), activation );
    }

    void JoltPhysicsScene::moveKinematic( PhysicsBodyHandle body, const float3& targetPosition, const quaternion& targetRotation, float32 deltaTime )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr || pRecord->_type != PhysicsBodyType::Kinematic || deltaTime <= 0.0f )
            return;
        _system.GetBodyInterface().MoveKinematic( pRecord->_bodyId, JoltUtil::toJolt( targetPosition ), JoltUtil::toJolt( targetRotation ), deltaTime );
    }

    float3 JoltPhysicsScene::getLinearVelocity( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? JoltUtil::toEngine( _system.GetBodyInterface().GetLinearVelocity( pRecord->_bodyId ) ) : float3{};
    }

    void JoltPhysicsScene::setLinearVelocity( PhysicsBodyHandle body, const float3& velocity )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().SetLinearVelocity( pRecord->_bodyId, JoltUtil::toJolt( velocity ) );
    }

    float3 JoltPhysicsScene::getAngularVelocity( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? JoltUtil::toEngine( _system.GetBodyInterface().GetAngularVelocity( pRecord->_bodyId ) ) : float3{};
    }

    void JoltPhysicsScene::setAngularVelocity( PhysicsBodyHandle body, const float3& velocity )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().SetAngularVelocity( pRecord->_bodyId, JoltUtil::toJolt( velocity ) );
    }

    void JoltPhysicsScene::addForce( PhysicsBodyHandle body, const float3& force )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().AddForce( pRecord->_bodyId, JoltUtil::toJolt( force ) );
    }

    void JoltPhysicsScene::addImpulse( PhysicsBodyHandle body, const float3& impulse )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().AddImpulse( pRecord->_bodyId, JoltUtil::toJolt( impulse ) );
    }

    void JoltPhysicsScene::addImpulseAtPoint( PhysicsBodyHandle body, const float3& impulse, const float3& point )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().AddImpulse( pRecord->_bodyId, JoltUtil::toJolt( impulse ), JoltUtil::toJolt( point ) );
    }

    void JoltPhysicsScene::addTorque( PhysicsBodyHandle body, const float3& torque )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr )
            _system.GetBodyInterface().AddTorque( pRecord->_bodyId, JoltUtil::toJolt( torque ) );
    }

    void JoltPhysicsScene::setBodyType( PhysicsBodyHandle body, PhysicsBodyType type )
    {
        BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr || pRecord->_type == type )
            return;
        JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        if ( pRecord->_type == PhysicsBodyType::Static )
        {
            // 정적으로 만든 바디는 움직임 정보가 없다 — `_bAllowTypeChange` 로 만든 것만 바꿀 수 있다.
            const JPH::BodyLockRead lock{ _system.GetBodyLockInterface(), pRecord->_bodyId };
            if ( lock.Succeeded() && lock.GetBody().GetMotionPropertiesUnchecked() == nullptr )
            {
                SW_LOG_ERROR( "setBodyType: the body was created Static without _bAllowTypeChange" );
                return;
            }
        }
        const JPH::EActivation activation = type == PhysicsBodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
        bodyInterface.SetMotionType( pRecord->_bodyId, JoltPhysicsSceneInternal::toMotionType( type ), activation );
        bodyInterface.SetObjectLayer( pRecord->_bodyId, JoltLayerUtil::makeObjectLayer( pRecord->_layer, type != PhysicsBodyType::Static ) );
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
        _system.GetBodyInterface().SetObjectLayer( pRecord->_bodyId, JoltLayerUtil::makeObjectLayer( layer, pRecord->_type != PhysicsBodyType::Static ) );
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
            _system.GetBodyInterface().SetGravityFactor( pRecord->_bodyId, factor );
    }

    float32 JoltPhysicsScene::getBodyMass( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord == nullptr )
            return 0.0f;
        const JPH::BodyLockRead lock{ _system.GetBodyLockInterface(), pRecord->_bodyId };
        if ( lock.Succeeded() == false || lock.GetBody().GetMotionPropertiesUnchecked() == nullptr )
            return 0.0f;
        const float32 inverseMass = lock.GetBody().GetMotionPropertiesUnchecked()->GetInverseMassUnchecked();
        return inverseMass > 0.0f ? 1.0f / inverseMass : 0.0f;
    }

    bool JoltPhysicsScene::isBodySleeping( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr && pRecord->_type != PhysicsBodyType::Static && _system.GetBodyInterface().IsActive( pRecord->_bodyId ) == false;
    }

    void JoltPhysicsScene::wakeBody( PhysicsBodyHandle body )
    {
        const BodyRecord* pRecord = findBody( body );
        if ( pRecord != nullptr && pRecord->_bEnabled && pRecord->_type != PhysicsBodyType::Static )
            _system.GetBodyInterface().ActivateBody( pRecord->_bodyId );
    }

    uint64 JoltPhysicsScene::getBodyUserData( PhysicsBodyHandle body ) const
    {
        const BodyRecord* pRecord = findBody( body );
        return pRecord != nullptr ? pRecord->_userData : 0;
    }

    void JoltPhysicsScene::setPairCollision( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, bool bCollide )
    {
        _pairFilter.setPairCollision( bodyA, bodyB, bCollide );
    }

    // --- 접촉 -------------------------------------------------------------------------------------------------------------

    JPH::ValidateResult JoltPhysicsScene::OnContactValidate( const JPH::Body& bodyA, const JPH::Body& bodyB, JPH::RVec3Arg, const JPH::CollideShapeResult& )
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
        // 서브 셰이프의 재질로 마찰 · 반발을 섞는다(바디 값은 첫 셰이프의 재질).
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
                impulse += contactImpulse;
            report._impulse = impulse;
        }
        std::scoped_lock<mutex> lock{ _reportMutex };
        _listContactReport.push_back( report );
    }

    void JoltPhysicsScene::OnContactAdded( const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold, JPH::ContactSettings& ioSettings )
    {
        recordContact( bodyA, bodyB, manifold, ioSettings, 0 );
    }

    void JoltPhysicsScene::OnContactPersisted( const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold,
                                               JPH::ContactSettings& ioSettings )
    {
        recordContact( bodyA, bodyB, manifold, ioSettings, 1 );
    }

    void JoltPhysicsScene::OnContactRemoved( const JPH::SubShapeIDPair& subShapePair )
    {
        ContactReport report;
        report._bodyA._body = findHandle( subShapePair.GetBody1ID() );
        report._bodyB._body = findHandle( subShapePair.GetBody2ID() );
        report._subShapeA   = subShapePair.GetSubShapeID1().GetValue();
        report._subShapeB   = subShapePair.GetSubShapeID2().GetValue();
        report._kind        = 2;
        std::scoped_lock<mutex> lock{ _reportMutex };
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
        // 잡 스레드가 붙인 순서는 매번 다르다 — 바디 쌍 · 서브 셰이프 · 종류로 줄 세워 같은 입력이면 같은 이벤트를 낸다.
        std::sort( _listContactReport.begin(), _listContactReport.end(), &JoltPhysicsScene::isEarlierReport );
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
} // namespace sw
