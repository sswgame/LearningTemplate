#include "pch.h"

#include "Engine/Destruction/FractureComponentBase.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Animation/Skeleton.h"
#include "Engine/Destruction/DestructionRandom.h"
#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureRenderUtil.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/RigidBody2DComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsShape.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    SW_GLOBAL_VARIABLE_INT( gv_destructionMaxDebrisBodies, 512, "모든 파괴 오브젝트가 함께 드는 떨어진 덩어리 바디의 상한(넘으면 오래된 작은 것부터 사라진다)" );
} // namespace sw

namespace sw
{
    /**
     * @class IFracturePhysics
     * @brief 조각 바디를 3D(Jolt) · 2D(Box2D) 씬에 같은 말로 만드는 창구입니다. 자세는 늘 3D(2D 는 XY · Z 축 회전)로 주고받습니다.
     */
    class IFracturePhysics
    {
    public:
        IFracturePhysics()                                     = default;
        virtual ~IFracturePhysics()                            = default;
        IFracturePhysics( const IFracturePhysics& )            = delete;
        IFracturePhysics& operator=( const IFracturePhysics& ) = delete;

        /** @brief 잎마다 껍질 셰이프를 지금 모두 짓습니다(오브젝트 원점 기준, 배율 · 줄임을 건 점). 앞서 지은 것은 놓습니다. */
        virtual void               prepareLeafShapes( const FractureAsset& asset, float32 scale, float32 shrink, const hashed_string& material ) = 0;
        virtual void               releaseLeafShapes()                                                                                           = 0;
        virtual void               createStaticLeafBodies( vector_reference<const uint32> listLeaf, const float3& position, const quaternion& rotation, uint8 layer,
                                                           uint64 userData, vector<PhysicsBodyHandle>& outListBody )                             = 0;
        virtual PhysicsBodyHandle  createGroupBody( vector_reference<const uint32> listLeaf, const float3& position, const quaternion& rotation, float32 mass,
                                                    const float3& linearVelocity, const float3& angularVelocity, uint8 layer, uint64 userData,
                                                    PhysicsShapeHandle& outShape )                                                               = 0;
        virtual void               destroyBodies( vector_reference<const PhysicsBodyHandle> listBody )                                           = 0;
        virtual void               destroyShape( PhysicsShapeHandle shape )                                                                      = 0;
        [[nodiscard]] virtual bool readPose( PhysicsBodyHandle body, float3& outPosition, quaternion& outRotation ) const                        = 0;
        virtual void               readVelocity( PhysicsBodyHandle body, float3& outLinear, float3& outAngular ) const                           = 0;
        virtual bool               isSleeping( PhysicsBodyHandle body ) const                                                                    = 0;
        virtual void               addImpulseAtPoint( PhysicsBodyHandle body, const float3& impulse, const float3& point )                       = 0;
        virtual void               setBodyEnabled( PhysicsBodyHandle body, bool bEnabled )                                                       = 0;
        /** @brief 잎 껍질을 그 자세에 놓았을 때 다른 오브젝트(@p ignoreUserData 가 아닌)의 정적 바디에 닿는지입니다(앵커 World). */
        virtual bool overlapsStaticWorld( const FractureAsset& asset, uint32 leaf, float32 scale, const float3& position, const quaternion& rotation,
                                          uint64 ignoreUserData ) const = 0;
    };
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "FractureComponent" );

    namespace
    {
        struct FractureComponentBaseInternal
        {
            static constexpr uint32  kBakeAfterStillFrames = 30;
            static constexpr float32 kMoveEpsilon          = 1.0e-4f;
            static constexpr uint32  kMaxPolygonPoint2D    = 8;
            static constexpr float32 kSpawnGraceTime       = 0.3f; ///< 갓 태어난 조각끼리의 부딪힘을 피해로 보지 않는 시간(초)

            static uint32& getLiveDebrisBodyCount()
            {
                static uint32 s_count = 0;
                return s_count;
            }

            /** @brief 잎 껍질 점을 오브젝트 원점 기준으로(무게 중심 + 줄인 껍질) × 배율. */
            static void makeLeafHull( const FractureAsset& asset, uint32 leaf, float32 scale, float32 shrink, vector<float3>& outListPoint )
            {
                outListPoint.clear();
                const float3& centroid = asset._graph._listNode[leaf]._centroid;
                for ( const float3& offset : asset.getPieceHull( leaf ) )
                {
                    const float32 length = offset.getLength();
                    const float32 keep   = length > 0.0f ? MathUtil::max( 0.0f, 1.0f - shrink / ( length * scale ) ) : 0.0f;
                    outListPoint.push_back( ( centroid + offset * keep ) * scale );
                }
            }

            /** @brief 2D 볼록 껍질(반시계, 모노톤 체인)을 짓고 점이 많으면 넓이를 가장 적게 잃는 점부터 빼 @p maxPoint 개로 줄입니다. */
            static void makeConvexPolygon( const vector<float3>& listPoint, uint32 maxPoint, vector<float2>& outListPolygon )
            {
                vector<float2> listSorted;
                for ( const float3& point : listPoint )
                    listSorted.push_back( float2{ point._x, point._y } );
                std::sort( listSorted.begin(), listSorted.end(), []( const float2& lhs, const float2& rhs )
                { return lhs._x < rhs._x || ( lhs._x == rhs._x && lhs._y < rhs._y ); } );
                outListPolygon.clear();
                const auto cross = []( const float2& origin, const float2& a, const float2& b )
                { return ( a._x - origin._x ) * ( b._y - origin._y ) - ( a._y - origin._y ) * ( b._x - origin._x ); };
                for ( const float2& point : listSorted )
                {
                    while ( outListPolygon.size() >= 2 && cross( outListPolygon[outListPolygon.size() - 2], outListPolygon.back(), point ) <= 0.0f )
                        outListPolygon.pop_back();
                    outListPolygon.push_back( point );
                }
                const size_t lowerCount = outListPolygon.size();
                for ( size_t index = listSorted.size(); index > 0; --index )
                {
                    const float2& point = listSorted[index - 1];
                    while ( outListPolygon.size() > lowerCount && cross( outListPolygon[outListPolygon.size() - 2], outListPolygon.back(), point ) <= 0.0f )
                        outListPolygon.pop_back();
                    outListPolygon.push_back( point );
                }
                if ( outListPolygon.size() > 1 )
                    outListPolygon.pop_back();
                while ( outListPolygon.size() > maxPoint )
                {
                    size_t  weakest  = 0;
                    float32 smallest = MathUtil::MaxFloat;
                    for ( size_t index = 0; index < outListPolygon.size(); ++index )
                    {
                        const size_t  count = outListPolygon.size();
                        const float32 area  = MathUtil::abs( cross( outListPolygon[( index + count - 1 ) % count], outListPolygon[index], outListPolygon[( index + 1 ) % count] ) );
                        if ( area < smallest )
                        {
                            smallest = area;
                            weakest  = index;
                        }
                    }
                    outListPolygon.erase( outListPolygon.begin() + static_cast<std::ptrdiff_t>( weakest ) );
                }
            }

            static float32 getAngleZ( const quaternion& rotation )
            {
                const float3 axis = float3::transform( float3{ 1.0f, 0.0f, 0.0f }, rotation );
                return MathUtil::atan2( axis._y, axis._x );
            }

            static quaternion makeRotationZ( float32 angle ) { return quaternion::createFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, angle ); }

            /** @brief 역회전입니다(const 로 불러 제자리 뒤집기가 아니라 값을 받는다). */
            static quaternion invert( const quaternion& rotation ) { return rotation.inverse(); }
        };

        /** @brief 3D(Jolt) 창구입니다. */
        class FracturePhysics3D final : public IFracturePhysics
        {
        public:
            explicit FracturePhysics3D( IPhysicsScene3D& scene )
                : _scene{ scene }
                , _listLeafShape{}
                , _listLeafPoint{}
                , _material{}
            {
            }

            ~FracturePhysics3D() override { releaseLeafShapes(); }

            void prepareLeafShapes( const FractureAsset& asset, float32 scale, float32 shrink, const hashed_string& material ) override
            {
                releaseLeafShapes();
                _material = material;
                _listLeafPoint.assign( asset.getPieceCount(), vector<float3>{} );
                _listLeafShape.assign( asset.getPieceCount(), PhysicsShapeHandle{} );
                for ( uint32 leaf = 0; leaf < asset.getPieceCount(); ++leaf )
                {
                    FractureComponentBaseInternal::makeLeafHull( asset, leaf, scale, shrink, _listLeafPoint[leaf] );
                    (void)findLeafShape( leaf );
                }
            }

            void releaseLeafShapes() override
            {
                for ( const PhysicsShapeHandle& shape : _listLeafShape )
                {
                    if ( shape.isValid() )
                        _scene.destroyShape( shape );
                }
                _listLeafShape.clear();
            }

            void createStaticLeafBodies( vector_reference<const uint32> listLeaf, const float3& position, const quaternion& rotation, uint8 layer, uint64 userData,
                                         vector<PhysicsBodyHandle>& outListBody ) override
            {
                vector<PhysicsBodyDesc3D> listDesc;
                listDesc.reserve( listLeaf.size() );
                for ( const uint32 leaf : listLeaf )
                {
                    PhysicsBodyDesc3D desc;
                    desc._sharedShape = findLeafShape( leaf );
                    desc._position    = position;
                    desc._rotation    = rotation;
                    desc._type        = PhysicsBodyType::Static;
                    desc._layer       = layer;
                    desc._userData    = userData;
                    desc._material    = _material;
                    listDesc.push_back( desc );
                }
                _scene.createBodies( listDesc, outListBody );
            }

            PhysicsBodyHandle createGroupBody( vector_reference<const uint32> listLeaf, const float3& position, const quaternion& rotation, float32 mass,
                                               const float3& linearVelocity, const float3& angularVelocity, uint8 layer, uint64 userData, PhysicsShapeHandle& outShape ) override
            {
                PhysicsBodyDesc3D desc;
                outShape = PhysicsShapeHandle{};
                if ( listLeaf.size() == 1 )
                {
                    desc._sharedShape = findLeafShape( listLeaf[0] );
                }
                else
                {
                    // 지어 둔 잎 껍질을 묶기만 한다(껍질을 다시 지으면 덩어리마다 잎 수 × 수십 us).
                    vector<PhysicsShapeHandle> listChild;
                    listChild.reserve( listLeaf.size() );
                    for ( const uint32 leaf : listLeaf )
                        listChild.push_back( findLeafShape( leaf ) );
                    outShape          = _scene.createCompoundShape( listChild );
                    desc._sharedShape = outShape;
                }
                desc._position        = position;
                desc._rotation        = rotation;
                desc._linearVelocity  = linearVelocity;
                desc._angularVelocity = angularVelocity;
                desc._mass            = mass;
                desc._layer           = layer;
                desc._userData        = userData;
                desc._material        = _material;
                desc._type            = PhysicsBodyType::Dynamic;
                return _scene.createBody( desc );
            }

            void destroyBodies( vector_reference<const PhysicsBodyHandle> listBody ) override { _scene.destroyBodies( listBody ); }
            void destroyShape( PhysicsShapeHandle shape ) override { _scene.destroyShape( shape ); }

            [[nodiscard]] bool readPose( PhysicsBodyHandle body, float3& outPosition, quaternion& outRotation ) const override
            {
                return _scene.getBodyTransform( body, outPosition, outRotation );
            }

            void readVelocity( PhysicsBodyHandle body, float3& outLinear, float3& outAngular ) const override
            {
                outLinear  = _scene.getLinearVelocity( body );
                outAngular = _scene.getAngularVelocity( body );
            }

            bool isSleeping( PhysicsBodyHandle body ) const override { return _scene.isBodySleeping( body ); }
            void addImpulseAtPoint( PhysicsBodyHandle body, const float3& impulse, const float3& point ) override { _scene.addImpulseAtPoint( body, impulse, point ); }
            void setBodyEnabled( PhysicsBodyHandle body, bool bEnabled ) override { _scene.setBodyEnabled( body, bEnabled ); }

            bool overlapsStaticWorld( const FractureAsset& asset, uint32 leaf, float32 scale, const float3& position, const quaternion& rotation,
                                      uint64 ignoreUserData ) const override
            {
                PhysicsShapeDesc3D shape;
                shape._type = PhysicsShapeType3D::ConvexHull;
                FractureComponentBaseInternal::makeLeafHull( asset, leaf, scale, -0.01f, shape._listPoint );
                vector<PhysicsBodyHandle> listBody;
                (void)_scene.overlapShape( shape, position, rotation, PhysicsQueryFilter{}, listBody );
                for ( const PhysicsBodyHandle& body : listBody )
                {
                    if ( _scene.getBodyType( body ) == PhysicsBodyType::Static && _scene.getBodyUserData( body ) != ignoreUserData )
                        return true;
                }
                return false;
            }

        private:
            PhysicsShapeHandle findLeafShape( uint32 leaf )
            {
                if ( _listLeafShape[leaf].isValid() == false )
                {
                    PhysicsShapeDesc3D shape;
                    shape._type          = PhysicsShapeType3D::ConvexHull;
                    shape._listPoint     = _listLeafPoint[leaf];
                    _listLeafShape[leaf] = _scene.createShape( vector_reference<const PhysicsShapeDesc3D>{ &shape, 1 }, _material );
                }
                return _listLeafShape[leaf];
            }

            IPhysicsScene3D&           _scene;
            vector<PhysicsShapeHandle> _listLeafShape;
            vector<vector<float3>>     _listLeafPoint;
            hashed_string              _material;
        };

        /** @brief 2D(Box2D) 창구입니다. 껍질은 XY 볼록 다각형(최대 8 점), 깊이(Z)는 오브젝트 것 그대로입니다. */
        class FracturePhysics2D final : public IFracturePhysics
        {
        public:
            explicit FracturePhysics2D( IPhysicsScene2D& scene )
                : _scene{ scene }
                , _listLeafShape{}
                , _listLeafPolygon{}
                , _material{}
                , _depth{ 0.0f }
            {
            }

            ~FracturePhysics2D() override { releaseLeafShapes(); }

            void prepareLeafShapes( const FractureAsset& asset, float32 scale, float32 shrink, const hashed_string& material ) override
            {
                releaseLeafShapes();
                _material = material;
                _listLeafPolygon.assign( asset.getPieceCount(), vector<float2>{} );
                _listLeafShape.assign( asset.getPieceCount(), PhysicsShapeHandle{} );
                vector<float3> listPoint;
                for ( uint32 leaf = 0; leaf < asset.getPieceCount(); ++leaf )
                {
                    FractureComponentBaseInternal::makeLeafHull( asset, leaf, scale, shrink, listPoint );
                    FractureComponentBaseInternal::makeConvexPolygon( listPoint, FractureComponentBaseInternal::kMaxPolygonPoint2D, _listLeafPolygon[leaf] );
                    (void)findLeafShape( leaf );
                }
            }

            void releaseLeafShapes() override
            {
                for ( const PhysicsShapeHandle& shape : _listLeafShape )
                {
                    if ( shape.isValid() )
                        _scene.destroyShape( shape );
                }
                _listLeafShape.clear();
            }

            void createStaticLeafBodies( vector_reference<const uint32> listLeaf, const float3& position, const quaternion& rotation, uint8 layer, uint64 userData,
                                         vector<PhysicsBodyHandle>& outListBody ) override
            {
                _depth = position._z;
                vector<PhysicsBodyDesc2D> listDesc;
                listDesc.reserve( listLeaf.size() );
                for ( const uint32 leaf : listLeaf )
                {
                    PhysicsBodyDesc2D desc;
                    desc._sharedShape = findLeafShape( leaf );
                    desc._position    = float2{ position._x, position._y };
                    desc._rotation    = FractureComponentBaseInternal::getAngleZ( rotation );
                    desc._type        = PhysicsBodyType::Static;
                    desc._layer       = layer;
                    desc._userData    = userData;
                    desc._material    = _material;
                    listDesc.push_back( desc );
                }
                _scene.createBodies( listDesc, outListBody );
            }

            PhysicsBodyHandle createGroupBody( vector_reference<const uint32> listLeaf, const float3& position, const quaternion& rotation, float32 mass,
                                               const float3& linearVelocity, const float3& angularVelocity, uint8 layer, uint64 userData, PhysicsShapeHandle& outShape ) override
            {
                _depth = position._z;
                PhysicsBodyDesc2D desc;
                outShape = PhysicsShapeHandle{};
                if ( listLeaf.size() == 1 )
                {
                    desc._sharedShape = findLeafShape( listLeaf[0] );
                }
                else
                {
                    vector<PhysicsShapeHandle> listChild;
                    listChild.reserve( listLeaf.size() );
                    for ( const uint32 leaf : listLeaf )
                        listChild.push_back( findLeafShape( leaf ) );
                    outShape          = _scene.createCompoundShape( listChild );
                    desc._sharedShape = outShape;
                }
                desc._position        = float2{ position._x, position._y };
                desc._rotation        = FractureComponentBaseInternal::getAngleZ( rotation );
                desc._linearVelocity  = float2{ linearVelocity._x, linearVelocity._y };
                desc._angularVelocity = angularVelocity._z;
                desc._mass            = mass;
                desc._layer           = layer;
                desc._userData        = userData;
                desc._material        = _material;
                desc._type            = PhysicsBodyType::Dynamic;
                return _scene.createBody( desc );
            }

            void destroyBodies( vector_reference<const PhysicsBodyHandle> listBody ) override { _scene.destroyBodies( listBody ); }
            void destroyShape( PhysicsShapeHandle shape ) override { _scene.destroyShape( shape ); }

            [[nodiscard]] bool readPose( PhysicsBodyHandle body, float3& outPosition, quaternion& outRotation ) const override
            {
                float2  position{};
                float32 angle = 0.0f;
                if ( _scene.getBodyTransform( body, position, angle ) == false )
                    return false;
                outPosition = float3{ position._x, position._y, _depth };
                outRotation = FractureComponentBaseInternal::makeRotationZ( angle );
                return true;
            }

            void readVelocity( PhysicsBodyHandle body, float3& outLinear, float3& outAngular ) const override
            {
                const float2 linear = _scene.getLinearVelocity( body );
                outLinear           = float3{ linear._x, linear._y, 0.0f };
                outAngular          = float3{ 0.0f, 0.0f, _scene.getAngularVelocity( body ) };
            }

            bool isSleeping( PhysicsBodyHandle body ) const override { return _scene.isBodySleeping( body ); }

            void addImpulseAtPoint( PhysicsBodyHandle body, const float3& impulse, const float3& point ) override
            {
                _scene.addImpulseAtPoint( body, float2{ impulse._x, impulse._y }, float2{ point._x, point._y } );
            }

            void setBodyEnabled( PhysicsBodyHandle body, bool bEnabled ) override { _scene.setBodyEnabled( body, bEnabled ); }

            bool overlapsStaticWorld( const FractureAsset& asset, uint32 leaf, float32 scale, const float3& position, const quaternion& rotation,
                                      uint64 ignoreUserData ) const override
            {
                vector<float3> listPoint;
                FractureComponentBaseInternal::makeLeafHull( asset, leaf, scale, -0.01f, listPoint );
                PhysicsShapeDesc2D shape;
                shape._type = PhysicsShapeType2D::Polygon;
                FractureComponentBaseInternal::makeConvexPolygon( listPoint, FractureComponentBaseInternal::kMaxPolygonPoint2D, shape._listPoint );
                vector<PhysicsBodyHandle> listBody;
                (void)_scene.overlapShape( shape, float2{ position._x, position._y }, FractureComponentBaseInternal::getAngleZ( rotation ), PhysicsQueryFilter{}, listBody );
                for ( const PhysicsBodyHandle& body : listBody )
                {
                    if ( _scene.getBodyType( body ) == PhysicsBodyType::Static && _scene.getBodyUserData( body ) != ignoreUserData )
                        return true;
                }
                return false;
            }

        private:
            PhysicsShapeHandle findLeafShape( uint32 leaf )
            {
                if ( _listLeafShape[leaf].isValid() == false )
                {
                    PhysicsShapeDesc2D shape;
                    shape._type          = PhysicsShapeType2D::Polygon;
                    shape._listPoint     = _listLeafPolygon[leaf];
                    _listLeafShape[leaf] = _scene.createShape( vector_reference<const PhysicsShapeDesc2D>{ &shape, 1 }, _material );
                }
                return _listLeafShape[leaf];
            }

            IPhysicsScene2D&           _scene;
            vector<PhysicsShapeHandle> _listLeafShape;
            vector<vector<float2>>     _listLeafPolygon;
            hashed_string              _material;
            float32                    _depth;
        };
    } // namespace
} // namespace sw

namespace sw
{
    FractureComponentBase::FractureComponentBase()
        : PhysicsComponent( PhysicsComponentPhase::Body )
        , _profilePath{}
        , _interiorMaterialPath{}
        , _staticLayer{ "Static" }
        , _chunkLayer{ "Default" }
        , _debrisLayer{ "Debris" }
        , _listAnchorVolume{}
        , _seed{ 1 }
        , _anchorTolerance{ 0.05f }
        , _anchorMode{ FractureAnchorMode::None }
        , _bAuthority{ true }
        , _asset{}
        , _physics{}
        , _profile{}
        , _state{}
        , _eventLog{}
        , _pendingMutex{}
        , _listPendingDamage{}
        , _listLeafStaticBody{}
        , _listRuntime{}
        , _listPiecePose{}
        , _listRuntimeOfLeaf{}
        , _arrSkinnedMesh{}
        , _arrSkinnedUnit{}
        , _arrBakedUnit{}
        , _intactMesh{}
        , _intactBody{}
        , _objectPosition{}
        , _objectRotation{}
        , _intactLinearVelocity{}
        , _intactAngularVelocity{}
        , _objectScale{ 1.0f }
        , _leafShapeScale{ 0.0f }
        , _lastApplyMicroseconds{ 0.0f }
        , _lastPoseMicroseconds{ 0.0f }
        , _frameSimulatedTime{ 0.0f }
        , _nextSpawnOrder{ 0 }
        , _stillFrameCount{ 0 }
        , _bStateReady{ SW_FALSE }
        , _bFractured{ SW_FALSE }
        , _bBaked{ SW_FALSE }
        , _bPoseDirty{ SW_FALSE }
        , _bFractureMissing{ SW_FALSE }
    {
    }

    FractureComponentBase::~FractureComponentBase()
    {
        releaseAllBodies();
    }

    void FractureComponentBase::onBeginPlay()
    {
        PhysicsComponent::onBeginPlay();
        _eventLog._seed = _seed;
    }

    void FractureComponentBase::readObjectPose( float3& outPosition, quaternion& outRotation, float32& outScale ) const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            pScene = this;
        float3 scale{ 1.0f, 1.0f, 1.0f };
        PhysicsComponentUtil::readWorldPose( *pScene, outPosition, outRotation, scale );
        outScale = uses2DPhysics() ? ( scale._x + scale._y ) * 0.5f : ( scale._x + scale._y + scale._z ) / 3.0f;
        if ( outScale <= 0.0f )
            outScale = 1.0f;
    }

    // --- 피해 받기 -------------------------------------------------------------------------------------------------------------

    void FractureComponentBase::applyDamage( const DestructionDamageEvent& event )
    {
        FracturePendingDamage pending;
        pending._kind  = FracturePendingDamage::Kind::Mesh;
        pending._event = event;
        std::scoped_lock<mutex> lock{ _pendingMutex };
        _listPendingDamage.push_back( pending );
    }

    void FractureComponentBase::applyPointDamageAtWorld( const float3& worldPoint, const float3& worldDirection, float32 strain, float32 impulse, PhysicsBodyHandle hitBody )
    {
        FracturePendingDamage pending;
        pending._kind           = FracturePendingDamage::Kind::WorldPoint;
        pending._worldPoint     = worldPoint;
        pending._worldDirection = worldDirection;
        pending._hitBody        = hitBody;
        pending._event._strain  = strain;
        pending._event._impulse = impulse;
        pending._event._kind    = DestructionDamageKind::Point;
        std::scoped_lock<mutex> lock{ _pendingMutex };
        _listPendingDamage.push_back( pending );
    }

    void FractureComponentBase::applyRadialDamageAtWorld( const float3& worldCenter, float32 radius, float32 strain, float32 impulse )
    {
        FracturePendingDamage pending;
        pending._kind           = FracturePendingDamage::Kind::WorldRadial;
        pending._worldPoint     = worldCenter;
        pending._event._radius  = radius;
        pending._event._strain  = strain;
        pending._event._impulse = impulse;
        pending._event._kind    = DestructionDamageKind::Radial;
        std::scoped_lock<mutex> lock{ _pendingMutex };
        _listPendingDamage.push_back( pending );
    }

    bool FractureComponentBase::applyRaycastDamage( GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, float32 strain,
                                                    float32 impulse, float3* pOutPoint )
    {
        IPhysicsScene3D* pScene = manager.getScenePhysics().findScene3D();
        if ( pScene == nullptr )
            return false;
        PhysicsCastHit3D hit;
        if ( pScene->raycast( origin, direction.normalize(), maxDistance, PhysicsQueryFilter{}, hit ) == false )
            return false;
        GameObject*            pObject   = manager.findGameObjectById( hit._userData );
        FractureComponentBase* pFracture = pObject != nullptr ? pObject->getComponent<FractureComponentBase>() : nullptr;
        if ( pFracture == nullptr )
            return false;
        if ( pOutPoint != nullptr )
            *pOutPoint = hit._point;
        pFracture->applyPointDamageAtWorld( hit._point, direction.normalize(), strain, impulse, hit._body );
        return true;
    }

    void FractureComponentBase::onCollisionBegin( const CollisionInfo& collision )
    {
        PhysicsComponent::onCollisionBegin( collision );
        if ( _bAuthority == false || _bStateReady == SW_FALSE || collision._impulse <= _profile._minImpulse )
            return;
        // 갓 갈라진 조각끼리는 맞닿은 채 태어난다 — 그 밀어냄은 피해가 아니다(같은 오브젝트 조각끼리, 태어난 지 kSpawnGraceTime 안).
        if ( collision._pOther == getOwner() )
        {
            const FractureGroupRuntime* pSelf       = findRuntime( findGroupOfBody( collision._selfBody ) );
            const FractureGroupRuntime* pOther      = findRuntime( findGroupOfBody( collision._otherBody ) );
            const bool                  bYoungSelf  = pSelf != nullptr && pSelf->_bAnchored == SW_FALSE && pSelf->_age < FractureComponentBaseInternal::kSpawnGraceTime;
            const bool                  bYoungOther = pOther != nullptr && pOther->_bAnchored == SW_FALSE && pOther->_age < FractureComponentBaseInternal::kSpawnGraceTime;
            if ( bYoungSelf || bYoungOther )
                return;
        }
        FracturePendingDamage pending;
        pending._kind           = FracturePendingDamage::Kind::WorldPoint;
        pending._worldPoint     = collision._point;
        pending._worldDirection = -collision._normal;
        pending._hitBody        = collision._selfBody;
        pending._event          = DestructionDamageUtil::makeImpactEvent( _profile, float3{}, float3{}, collision._impulse );
        if ( pending._event._strain <= 0.0f )
            return;
        std::scoped_lock<mutex> lock{ _pendingMutex };
        _listPendingDamage.push_back( pending );
    }

    // --- 물리 프레임 -------------------------------------------------------------------------------------------------------------

    void FractureComponentBase::beginPhysicsFrame( ScenePhysics& physics )
    {
        _frameSimulatedTime = 0.0f;
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        if ( _physics == nullptr )
        {
            if ( uses2DPhysics() )
            {
                IPhysicsScene2D* pScene = physics.getScene2D();
                if ( pScene != nullptr )
                    _physics = make_unique<FracturePhysics2D>( *pScene );
            }
            else
            {
                IPhysicsScene3D* pScene = physics.getScene3D();
                if ( pScene != nullptr )
                    _physics = make_unique<FracturePhysics3D>( *pScene );
            }
            if ( _physics == nullptr )
                return;
        }
        // 온전한 동안 데이터가 다시 읽혔으면(핫 리로드) 상태를 다시 시작한다 — 쪼갠 뒤에는 지금 조각을 지킨다.
        if ( _bStateReady == SW_TRUE && _bFractured == SW_FALSE && isFractureStale() )
            _bStateReady = SW_FALSE;
        if ( _bFractureMissing == SW_TRUE && isFractureStale() == false )
            return;
        if ( _bStateReady == SW_FALSE && initializeState( physics ) == false )
            return;
        processPendingEvents( physics );
    }

    void FractureComponentBase::postPhysicsStep( ScenePhysics& physics )
    {
        _frameSimulatedTime += physics.getAccumulator().getFixedTimeStep();
    }

    void FractureComponentBase::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        (void)physics;
        (void)alpha;
        if ( _bFractured == SW_FALSE || _physics == nullptr )
            return;
        Stopwatch  stopwatch;
        const bool bMoved  = updateGroups( _frameSimulatedTime );
        bool       bFading = false;
        for ( const FractureGroupRuntime& runtime : _listRuntime )
            bFading = bFading || runtime._bFading == SW_TRUE;
        if ( bMoved || _bPoseDirty == SW_TRUE )
        {
            if ( _bBaked == SW_TRUE )
                setBaked( false );
            writePiecePoses();
            _stillFrameCount = 0;
            _bPoseDirty      = SW_FALSE;
        }
        else if ( bFading == false && _bBaked == SW_FALSE && ++_stillFrameCount >= FractureComponentBaseInternal::kBakeAfterStillFrames )
        {
            setBaked( true );
        }
        _lastPoseMicroseconds = static_cast<float32>( stopwatch.getElapsedMicroseconds() );
    }

    void FractureComponentBase::releasePhysics( ScenePhysics& physics )
    {
        (void)physics;
        releaseAllBodies();
    }

    void FractureComponentBase::releaseAllBodies()
    {
        if ( _physics == nullptr )
            return;
        vector<PhysicsBodyHandle> listDestroy;
        for ( FractureGroupRuntime& runtime : _listRuntime )
        {
            if ( runtime._body.isValid() )
            {
                listDestroy.push_back( runtime._body );
                --FractureComponentBaseInternal::getLiveDebrisBodyCount();
                runtime._body = PhysicsBodyHandle{};
            }
            if ( runtime._shape.isValid() )
            {
                _physics->destroyShape( runtime._shape );
                runtime._shape = PhysicsShapeHandle{};
            }
        }
        for ( PhysicsBodyHandle& body : _listLeafStaticBody )
        {
            if ( body.isValid() )
                listDestroy.push_back( body );
            body = PhysicsBodyHandle{};
        }
        if ( listDestroy.empty() == false )
            _physics->destroyBodies( listDestroy );
        _physics->releaseLeafShapes();
        _physics.reset();
        _leafShapeScale = 0.0f;
    }

    // --- 상태 -------------------------------------------------------------------------------------------------------------------

    bool FractureComponentBase::initializeState( ScenePhysics& physics )
    {
        _asset            = acquireFracture();
        _bFractureMissing = _asset == nullptr ? SW_TRUE : SW_FALSE;
        if ( _asset == nullptr )
            return false;
        const string profilePath = _profilePath.empty() ? string{ DestructionProfile::kDefaultPath } : _profilePath;
        if ( _profile.loadFromResource( profilePath ) == false )
            SW_LOG_ERROR( "'%#': destruction profile '%#' could not be read - using built-in values", getOwner() != nullptr ? getOwner()->getName().c_str() : "?",
                          profilePath.c_str() );
        vector<uint8> listAnchor;
        computeAnchors( physics, listAnchor );
        _state.initialize( _asset->_graph, _profile, listAnchor );
        _listPiecePose.assign( _asset->getPieceCount(), BoneTransform{} );
        for ( uint32 leaf = 0; leaf < _asset->getPieceCount(); ++leaf )
            _listPiecePose[leaf] = FractureRenderUtil::makeBoneTransform( _asset->_graph._listNode[leaf]._centroid, quaternion::Identity, float3{}, 1.0f );
        _listLeafStaticBody.assign( _asset->getPieceCount(), PhysicsBodyHandle{} );
        _eventLog._seed = _seed;
        _bStateReady    = SW_TRUE;
        float3     position{};
        quaternion rotation{};
        float32    scale = 1.0f;
        readObjectPose( position, rotation, scale );
        _leafShapeScale = 0.0f;
        prepareLeafShapes( scale );
        return true;
    }

    void FractureComponentBase::computeAnchors( ScenePhysics& physics, vector<uint8>& outListAnchor ) const
    {
        (void)physics;
        const FractureAsset& asset = *_asset;
        outListAnchor.assign( asset.getPieceCount(), 0 );
        float3     position{};
        quaternion rotation{};
        float32    scale = 1.0f;
        readObjectPose( position, rotation, scale );
        const uint64 selfId = getOwner() != nullptr ? getOwner()->getObjectId() : 0;
        for ( uint32 leaf = 0; leaf < asset.getPieceCount(); ++leaf )
        {
            bool bAnchored = false;
            if ( _anchorMode == FractureAnchorMode::Bottom )
                bAnchored = asset._listPiece[leaf]._boundsMin._y <= asset._boundsMin._y + _anchorTolerance / scale;
            else if ( _anchorMode == FractureAnchorMode::World && _physics != nullptr )
                bAnchored = _physics->overlapsStaticWorld( asset, leaf, scale, position, rotation, selfId );
            const float3& centroid = asset._graph._listNode[leaf]._centroid;
            for ( const FractureAnchorVolume& volume : _listAnchorVolume )
            {
                const float3 delta = centroid - volume._center;
                const bool   bIn   = MathUtil::abs( delta._x ) <= volume._halfExtents._x && MathUtil::abs( delta._y ) <= volume._halfExtents._y &&
                                 MathUtil::abs( delta._z ) <= volume._halfExtents._z;
                bAnchored = bAnchored || bIn;
            }
            outListAnchor[leaf] = bAnchored ? 1 : 0;
        }
    }

    void FractureComponentBase::processPendingEvents( ScenePhysics& physics )
    {
        vector<FracturePendingDamage> listPending;
        {
            std::scoped_lock<mutex> lock{ _pendingMutex };
            listPending.swap( _listPendingDamage );
        }
        if ( listPending.empty() )
            return;
        Stopwatch                      stopwatch;
        vector<DestructionDamageEvent> listEvent;
        for ( const FracturePendingDamage& pending : listPending )
        {
            listEvent.clear();
            if ( pending._kind == FracturePendingDamage::Kind::Mesh )
                listEvent.push_back( pending._event );
            else
                convertWorldDamage( pending, listEvent );
            for ( const DestructionDamageEvent& event : listEvent )
                applyMeshEvent( physics, event );
        }
        _lastApplyMicroseconds = static_cast<float32>( stopwatch.getElapsedMicroseconds() );
    }

    float3 FractureComponentBase::toMeshSpace( const FractureGroupRuntime& runtime, const float3& worldPoint ) const
    {
        return float3::transform( worldPoint - runtime._position, FractureComponentBaseInternal::invert( runtime._rotation ) ) / _objectScale;
    }

    void FractureComponentBase::convertWorldDamage( const FracturePendingDamage& pending, vector<DestructionDamageEvent>& outListEvent ) const
    {
        if ( _bFractured == SW_FALSE )
        {
            // 온전하다 — 오브젝트의 지금 자세(움직이는 상자일 수 있다)로 옮긴다.
            float3     position{};
            quaternion rotation{};
            float32    scale = 1.0f;
            readObjectPose( position, rotation, scale );
            DestructionDamageEvent event = pending._event;
            event._position              = float3::transform( pending._worldPoint - position, FractureComponentBaseInternal::invert( rotation ) ) / scale;
            event._direction             = float3::transform( pending._worldDirection, FractureComponentBaseInternal::invert( rotation ) );
            event._radius                = pending._event._radius / scale;
            if ( pending._kind == FracturePendingDamage::Kind::WorldPoint && pending._event._kind == DestructionDamageKind::Impact )
                event._radius = _profile._impactRadius / scale;
            outListEvent.push_back( event );
            return;
        }
        if ( pending._kind == FracturePendingDamage::Kind::WorldPoint )
        {
            const uint32                groupId  = findGroupOfBody( pending._hitBody );
            const FractureGroupRuntime* pRuntime = groupId != 0 ? findRuntime( groupId ) : nullptr;
            if ( pRuntime == nullptr )
            {
                // 바디를 모르면(이미 빠진 파편 · 광선이 아닌 요청) 붙은 구조 기준.
                for ( const FractureGroupRuntime& runtime : _listRuntime )
                {
                    if ( runtime._bAnchored == SW_TRUE )
                    {
                        pRuntime = &runtime;
                        break;
                    }
                }
            }
            if ( pRuntime == nullptr )
                return;
            DestructionDamageEvent event = pending._event;
            event._position              = toMeshSpace( *pRuntime, pending._worldPoint );
            event._direction             = float3::transform( pending._worldDirection, FractureComponentBaseInternal::invert( pRuntime->_rotation ) );
            event._groupId               = pRuntime->_groupId;
            event._radius                = pending._event._kind == DestructionDamageKind::Impact ? _profile._impactRadius / _objectScale : pending._event._radius / _objectScale;
            outListEvent.push_back( event );
            return;
        }
        // 폭발 — 반경 안의 그룹마다 그 그룹 자세로 사건 하나(떨어진 덩어리는 지금 있는 자리로 맞는다).
        for ( const FractureGroupRuntime& runtime : _listRuntime )
        {
            if ( runtime._bGone == SW_TRUE )
                continue;
            const DestructionGroup* pGroup = _state.findGroup( runtime._groupId );
            if ( pGroup == nullptr )
                continue;
            const float3  center = toMeshSpace( runtime, pending._worldPoint );
            const float32 radius = pending._event._radius / _objectScale;
            bool          bReach = false;
            for ( const uint32 node : pGroup->_listNode )
            {
                const FractureNode& data = _asset->_graph._listNode[node];
                for ( uint32 leaf = data._firstLeaf; leaf < data._firstLeaf + data._leafCount && bReach == false; ++leaf )
                    bReach = float3::getDistance( _asset->_graph._listNode[leaf]._centroid, center ) < radius;
            }
            if ( bReach == false )
                continue;
            DestructionDamageEvent event = pending._event;
            event._position              = center;
            event._radius                = radius;
            event._groupId               = runtime._groupId;
            outListEvent.push_back( event );
        }
    }

    void FractureComponentBase::applyMeshEvent( ScenePhysics& physics, const DestructionDamageEvent& event )
    {
        DestructionChange change;
        const uint32      eventIndex = static_cast<uint32>( _eventLog._listEvent.size() );
        const bool        bChanged   = _state.applyDamage( event, change );
        _eventLog._listEvent.push_back( event );
        if ( bChanged == false )
            return;
        if ( _bFractured == SW_FALSE )
        {
            if ( change.hasGroupChange() == false )
                return;
            activate( physics );
        }
        applyGroupChange( physics, change, &event, eventIndex );
    }

    // --- 쪼갠 상태 -----------------------------------------------------------------------------------------------------------------

    void FractureComponentBase::createRenderUnits()
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        const MeshComponent*       pIntact    = static_cast<const MeshComponent*>( resolveOwnedComponent( _intactMesh ) );
        shared_ptr<const Skeleton> skeleton   = FractureRenderUtil::createPieceSkeleton( *_asset );
        const FractureSurfaceSlot  arrSlot[2] = { FractureSurfaceSlot::Outer, FractureSurfaceSlot::Interior };
        for ( uint32 slot = 0; slot < 2; ++slot )
        {
            _arrSkinnedMesh[slot] = FractureRenderUtil::createSkinnedMesh( *_asset, arrSlot[slot] );
            if ( _arrSkinnedMesh[slot] == nullptr )
                continue;
            SkeletalMeshComponent* pUnit = pOwner->addComponent<SkeletalMeshComponent>();
            if ( pUnit == nullptr )
                continue;
            pUnit->setSkeleton( skeleton );
            pUnit->setMesh( _arrSkinnedMesh[slot] );
            pUnit->setAnimateWhenOffscreen( true );
            const bool bInterior = slot == 1 && _interiorMaterialPath.empty() == false;
            if ( bInterior )
                pUnit->setMaterialPath( _interiorMaterialPath.c_str() );
            else if ( pIntact != nullptr && pIntact->getMaterialPath().empty() == false )
                pUnit->setMaterialPath( pIntact->getMaterialPath().c_str() );
            else if ( pIntact != nullptr )
                pUnit->setMaterial( pIntact->getMaterial() );
            _arrSkinnedUnit[slot] = pUnit->getHandle();
        }
    }

    void FractureComponentBase::prepareLeafShapes( float32 scale )
    {
        if ( _physics == nullptr || _asset == nullptr || scale == _leafShapeScale )
            return;
        _physics->prepareLeafShapes( *_asset, scale, _profile._hullShrink, _profile._physicsMaterial );
        _leafShapeScale = scale;
    }

    void FractureComponentBase::activate( ScenePhysics& physics )
    {
        (void)physics;
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || _physics == nullptr )
            return;
        readObjectPose( _objectPosition, _objectRotation, _objectScale );

        // 오브젝트의 온전한 메시 · 강체를 넘겨받는다 — 메시는 숨기고 바디는 지금 꺼(새 조각 바디와 겹치지 않게) 컴포넌트도 끈다.
        for ( Component* pComp : pOwner->getComponents() )
        {
            MeshComponent* pMesh = pComp != nullptr ? castTo<MeshComponent>( pComp ) : nullptr;
            if ( pMesh != nullptr && _intactMesh.isValid() == false )
            {
                _intactMesh = pMesh->getHandle();
                pMesh->setVisible( false );
            }
        }
        if ( uses2DPhysics() )
        {
            RigidBody2DComponent* pBody = pOwner->getComponent<RigidBody2DComponent>();
            if ( pBody != nullptr )
            {
                const float2 linear    = pBody->getLinearVelocity();
                _intactLinearVelocity  = float3{ linear._x, linear._y, 0.0f };
                _intactAngularVelocity = float3{ 0.0f, 0.0f, pBody->getAngularVelocity() };
                _intactBody            = pBody->getHandle();
                if ( pBody->getBodyHandle().isValid() )
                    _physics->setBodyEnabled( pBody->getBodyHandle(), false );
                pBody->setActive( false );
            }
        }
        else
        {
            RigidBodyComponent* pBody = pOwner->getComponent<RigidBodyComponent>();
            if ( pBody != nullptr )
            {
                _intactLinearVelocity  = pBody->getLinearVelocity();
                _intactAngularVelocity = pBody->getAngularVelocity();
                _intactBody            = pBody->getHandle();
                if ( pBody->getBodyHandle().isValid() )
                    _physics->setBodyEnabled( pBody->getBodyHandle(), false );
                pBody->setActive( false );
            }
        }

        prepareLeafShapes( _objectScale );
        createRenderUnits();
        _bFractured = SW_TRUE;
        _bPoseDirty = SW_TRUE;
    }

    void FractureComponentBase::applyGroupChange( ScenePhysics& physics, const DestructionChange& change, const DestructionDamageEvent* pEvent, uint32 eventIndex )
    {
        (void)physics;
        vector<PhysicsBodyHandle>    listDestroy;
        vector<FractureGroupRuntime> listRemoved;
        // 갓 쪼갰으면 런타임이 아직 없다 — 지금 그룹 모두가 오브젝트 자세에서 새로 선다.
        const bool bFirst = _listRuntime.empty();
        for ( const uint32 groupId : change._listRemovedGroup )
        {
            FractureGroupRuntime* pRuntime = findRuntime( groupId );
            if ( pRuntime == nullptr )
                continue;
            listRemoved.push_back( *pRuntime );
            if ( pRuntime->_body.isValid() )
            {
                listDestroy.push_back( pRuntime->_body );
                --FractureComponentBaseInternal::getLiveDebrisBodyCount();
            }
            if ( pRuntime->_shape.isValid() )
                _physics->destroyShape( pRuntime->_shape );
            _listRuntime.erase( _listRuntime.begin() + ( pRuntime - _listRuntime.data() ) );
        }

        vector<uint32> listCreated;
        if ( bFirst )
        {
            for ( const DestructionGroup& group : _state.getGroups() )
                listCreated.push_back( group._id );
        }
        else
        {
            listCreated = change._listCreatedGroup;
        }

        DestructionRandom random{ DestructionRandom::combineSeed( _seed, eventIndex ) };
        for ( const uint32 groupId : listCreated )
        {
            const DestructionGroup* pGroup = _state.findGroup( groupId );
            if ( pGroup == nullptr )
                continue;
            FractureGroupRuntime runtime;
            runtime._groupId       = groupId;
            runtime._position      = _objectPosition;
            runtime._rotation      = _objectRotation;
            float3 linearVelocity  = bFirst ? _intactLinearVelocity : float3{};
            float3 angularVelocity = bFirst ? _intactAngularVelocity : float3{};
            for ( const FractureGroupRuntime& parent : listRemoved )
            {
                if ( parent._groupId != pGroup->_parentId )
                    continue;
                runtime._position = parent._position;
                runtime._rotation = parent._rotation;
                if ( parent._body.isValid() )
                {
                    // 부모 질량 중심의 속도 + 각속도 × (자식 질량 중심 - 부모 원점) — 떨어지던 덩어리가 갈라져도 같은 운동을 잇는다.
                    float3 parentLinear{};
                    float3 parentAngular{};
                    _physics->readVelocity( parent._body, parentLinear, parentAngular );
                    float3 childCenter{};
                    (void)_state.computeGroupMass( *pGroup, childCenter );
                    const float3 offset = float3::transform( childCenter * _objectScale, parent._rotation );
                    linearVelocity      = parentLinear + parentAngular.cross( offset );
                    angularVelocity     = parentAngular;
                }
            }
            runtime._bAnchored = pGroup->_bAnchored;
            if ( runtime._bAnchored == SW_FALSE )
            {
                if ( pEvent != nullptr && pEvent->_impulse > 0.0f )
                {
                    float3 center{};
                    (void)_state.computeGroupMass( *pGroup, center );
                    float3 push = pEvent->_kind == DestructionDamageKind::Radial ? ( center - pEvent->_position ) : pEvent->_direction;
                    if ( push.getLengthSquared() < 1.0e-8f )
                        push = float3{ 0.0f, 1.0f, 0.0f };
                    push = push.normalize();
                    // 같은 씨앗 · 같은 사건이면 같은 흩기(보기 좋게 조금 비튼다 — 상태에는 들지 않는다).
                    push            = ( push + random.nextPointInUnitSphere() * 0.25f ).normalize();
                    float32 falloff = 1.0f;
                    if ( pEvent->_kind == DestructionDamageKind::Radial && pEvent->_radius > 0.0f )
                        falloff = MathUtil::max( 0.0f, 1.0f - float3::getDistance( center, pEvent->_position ) / pEvent->_radius );
                    float3        mass3{};
                    const float32 mass = _state.computeGroupMass( *pGroup, mass3 ) * _objectScale * _objectScale * _objectScale;
                    linearVelocity += float3::transform( push, runtime._rotation ) * ( pEvent->_impulse * falloff / MathUtil::max( mass, 1.0f ) );
                }
                spawnGroup( runtime, *pGroup, linearVelocity, angularVelocity );
            }
            _listRuntime.push_back( runtime );
        }
        std::sort( _listRuntime.begin(), _listRuntime.end(), []( const FractureGroupRuntime& lhs, const FractureGroupRuntime& rhs )
        { return lhs._groupId < rhs._groupId; } );
        syncStaticBodies( listDestroy );
        if ( listDestroy.empty() == false )
            _physics->destroyBodies( listDestroy );
        rebuildLeafRuntimeMap();
        _bPoseDirty = SW_TRUE;
    }

    void FractureComponentBase::spawnGroup( FractureGroupRuntime& inoutRuntime, const DestructionGroup& group, const float3& linearVelocity, const float3& angularVelocity )
    {
        const float32  cube = uses2DPhysics() ? _objectScale * _objectScale : _objectScale * _objectScale * _objectScale;
        vector<uint32> listLeaf;
        float32        volume = 0.0f;
        for ( const uint32 node : group._listNode )
        {
            const FractureNode& data = _asset->_graph._listNode[node];
            volume += data._volume;
            for ( uint32 leaf = data._firstLeaf; leaf < data._firstLeaf + data._leafCount; ++leaf )
                listLeaf.push_back( leaf );
        }
        inoutRuntime._volume       = volume * cube;
        inoutRuntime._spawnOrder   = _nextSpawnOrder++;
        const bool          bChunk = inoutRuntime._volume >= _profile._keepCollisionVolume;
        const ScenePhysics* pScene = getScenePhysics();
        const uint8         layer  = pScene != nullptr ? pScene->resolveLayer( bChunk ? _chunkLayer : _debrisLayer ) : 0;
        const uint64        selfId = getOwner() != nullptr ? getOwner()->getObjectId() : 0;
        inoutRuntime._body         = _physics->createGroupBody( listLeaf, inoutRuntime._position, inoutRuntime._rotation, inoutRuntime._volume * _profile._density, linearVelocity,
                                                                angularVelocity, layer, selfId, inoutRuntime._shape );
        if ( inoutRuntime._body.isValid() )
            ++FractureComponentBaseInternal::getLiveDebrisBodyCount();
    }

    void FractureComponentBase::syncStaticBodies( vector<PhysicsBodyHandle>& inoutListDestroy )
    {
        vector<uint32> listCreate;
        for ( uint32 leaf = 0; leaf < static_cast<uint32>( _listLeafStaticBody.size() ); ++leaf )
        {
            const DestructionGroup* pGroup     = _state.findGroup( _state.getGroupOfLeaf( leaf ) );
            const bool              bAnchored  = pGroup != nullptr && pGroup->_bAnchored == SW_TRUE;
            const bool              bHasStatic = _listLeafStaticBody[leaf].isValid();
            if ( bAnchored && bHasStatic == false )
                listCreate.push_back( leaf );
            else if ( bAnchored == false && bHasStatic )
            {
                inoutListDestroy.push_back( _listLeafStaticBody[leaf] );
                _listLeafStaticBody[leaf] = PhysicsBodyHandle{};
            }
        }
        if ( listCreate.empty() )
            return;
        const ScenePhysics*       pScene = getScenePhysics();
        const uint8               layer  = pScene != nullptr ? pScene->resolveLayer( _staticLayer ) : 0;
        vector<PhysicsBodyHandle> listBody;
        _physics->createStaticLeafBodies( listCreate, _objectPosition, _objectRotation, layer, getOwner() != nullptr ? getOwner()->getObjectId() : 0, listBody );
        for ( size_t index = 0; index < listCreate.size() && index < listBody.size(); ++index )
            _listLeafStaticBody[listCreate[index]] = listBody[index];
    }

    void FractureComponentBase::rebuildLeafRuntimeMap()
    {
        _listRuntimeOfLeaf.assign( _asset->getPieceCount(), 0xFFFFFFFFu );
        for ( uint32 index = 0; index < static_cast<uint32>( _listRuntime.size() ); ++index )
        {
            const DestructionGroup* pGroup = _state.findGroup( _listRuntime[index]._groupId );
            if ( pGroup == nullptr )
                continue;
            for ( const uint32 node : pGroup->_listNode )
            {
                const FractureNode& data = _asset->_graph._listNode[node];
                for ( uint32 leaf = data._firstLeaf; leaf < data._firstLeaf + data._leafCount; ++leaf )
                    _listRuntimeOfLeaf[leaf] = index;
            }
        }
    }

    bool FractureComponentBase::updateGroups( float32 deltaTime )
    {
        using Internal                   = FractureComponentBaseInternal;
        bool                      bMoved = false;
        vector<PhysicsBodyHandle> listDestroy;
        uint32                    liveBodyCount = 0;
        for ( FractureGroupRuntime& runtime : _listRuntime )
        {
            if ( runtime._bAnchored == SW_TRUE || runtime._bGone == SW_TRUE )
                continue;
            runtime._age += deltaTime;
            if ( runtime._body.isValid() )
            {
                float3     position{};
                quaternion rotation{};
                if ( _physics->readPose( runtime._body, position, rotation ) )
                {
                    const bool bChanged = float3::getDistanceSquared( position, runtime._position ) > Internal::kMoveEpsilon * Internal::kMoveEpsilon ||
                                          MathUtil::abs( rotation.dot( runtime._rotation ) ) < 1.0f - 1.0e-7f;
                    bMoved            = bMoved || bChanged;
                    runtime._position = position;
                    runtime._rotation = rotation;
                }
                runtime._sleepTime = _physics->isSleeping( runtime._body ) ? runtime._sleepTime + deltaTime : 0.0f;
                // 잠들어 쉰 덩어리 — 작은 것은 바디를 빼고(자세 고정, 정적 그림에 합쳐진다), 큰 것은 잠든 바디로 충돌을 남긴다.
                const bool bRemoveOnSleep = runtime._sleepTime >= _profile._sleepRemoveTime && runtime._volume < _profile._keepCollisionVolume;
                if ( bRemoveOnSleep )
                {
                    listDestroy.push_back( runtime._body );
                    --Internal::getLiveDebrisBodyCount();
                    runtime._body    = PhysicsBodyHandle{};
                    runtime._bFrozen = SW_TRUE;
                }
            }
            const bool bSmall = runtime._volume < _profile._smallDebrisVolume;
            if ( bSmall && runtime._bFading == SW_FALSE && runtime._age >= _profile._debrisLifetime )
                runtime._bFading = SW_TRUE;
            if ( runtime._bFading == SW_TRUE )
            {
                runtime._fade = _profile._fadeTime > 0.0f ? runtime._fade - deltaTime / _profile._fadeTime : 0.0f;
                bMoved        = true;
                if ( runtime._fade <= 0.0f )
                {
                    runtime._fade  = 0.0f;
                    runtime._bGone = SW_TRUE;
                    if ( runtime._body.isValid() )
                    {
                        listDestroy.push_back( runtime._body );
                        --Internal::getLiveDebrisBodyCount();
                        runtime._body = PhysicsBodyHandle{};
                    }
                }
            }
            if ( runtime._body.isValid() )
                ++liveBodyCount;
        }

        // 예산 — 이 오브젝트(표의 maxBodies)와 전역(gv_destructionMaxDebrisBodies)을 넘으면 오래된 것부터, 작은 것을 먼저 사라지게 한다.
        const uint32 globalLimit = static_cast<uint32>( MathUtil::max( 0, static_cast<int32>( gv_destructionMaxDebrisBodies ) ) );
        uint32       globalCount = Internal::getLiveDebrisBodyCount();
        while ( liveBodyCount > _profile._maxDebrisBody || globalCount > globalLimit )
        {
            FractureGroupRuntime* pOldest = nullptr;
            for ( FractureGroupRuntime& runtime : _listRuntime )
            {
                if ( runtime._body.isValid() == false || runtime._bFading == SW_TRUE )
                    continue;
                const bool bSmaller = pOldest != nullptr && runtime._volume < _profile._keepCollisionVolume && pOldest->_volume >= _profile._keepCollisionVolume;
                const bool bOlder   = pOldest != nullptr && runtime._spawnOrder < pOldest->_spawnOrder &&
                                    ( runtime._volume < _profile._keepCollisionVolume ) == ( pOldest->_volume < _profile._keepCollisionVolume );
                if ( pOldest == nullptr || bSmaller || bOlder )
                    pOldest = &runtime;
            }
            if ( pOldest == nullptr )
                break;
            pOldest->_bFading = SW_TRUE;
            listDestroy.push_back( pOldest->_body );
            --Internal::getLiveDebrisBodyCount();
            pOldest->_body = PhysicsBodyHandle{};
            --liveBodyCount;
            globalCount = Internal::getLiveDebrisBodyCount();
            bMoved      = true;
        }
        if ( listDestroy.empty() == false )
            _physics->destroyBodies( listDestroy );
        return bMoved;
    }

    void FractureComponentBase::writePiecePoses()
    {
        const quaternion inverseObject = FractureComponentBaseInternal::invert( _objectRotation );
        float32          maxOffset     = 0.0f;
        for ( uint32 leaf = 0; leaf < static_cast<uint32>( _listPiecePose.size() ); ++leaf )
        {
            const uint32 runtimeIndex = leaf < _listRuntimeOfLeaf.size() ? _listRuntimeOfLeaf[leaf] : 0xFFFFFFFFu;
            if ( runtimeIndex >= _listRuntime.size() )
                continue;
            const FractureGroupRuntime& runtime  = _listRuntime[runtimeIndex];
            const quaternion            rotation = inverseObject * runtime._rotation;
            const float3                offset   = float3::transform( runtime._position - _objectPosition, inverseObject ) / _objectScale;
            const float32               scale    = runtime._bGone == SW_TRUE ? 0.0f : runtime._fade;
            _listPiecePose[leaf]                 = FractureRenderUtil::makeBoneTransform( _asset->_graph._listNode[leaf]._centroid, rotation, offset, scale );
            maxOffset                            = MathUtil::max( maxOffset, offset.getLength() );
        }
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        const float32 assetRadius = MathUtil::max( _asset->_boundsMax.getLength(), _asset->_boundsMin.getLength() );
        for ( const ComponentHandle& handle : _arrSkinnedUnit )
        {
            SkeletalMeshComponent* pUnit = static_cast<SkeletalMeshComponent*>( resolveOwnedComponent( handle ) );
            if ( pUnit == nullptr )
                continue;
            Pose& pose = pUnit->getLocalPose();
            if ( pose.getBoneCount() != _listPiecePose.size() )
                continue;
            for ( uint32 leaf = 0; leaf < static_cast<uint32>( _listPiecePose.size() ); ++leaf )
                pose.setBoneTransform( leaf, _listPiecePose[leaf] );
            pUnit->applyExternalPose();
            pUnit->setBoundsRadius( assetRadius + maxOffset );
            pUnit->markRenderStateDirty();
        }
    }

    void FractureComponentBase::setBaked( bool bBaked )
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || _asset == nullptr )
            return;
        const FractureSurfaceSlot arrSlot[2] = { FractureSurfaceSlot::Outer, FractureSurfaceSlot::Interior };
        for ( uint32 slot = 0; slot < 2; ++slot )
        {
            SkeletalMeshComponent* pSkinned = static_cast<SkeletalMeshComponent*>( resolveOwnedComponent( _arrSkinnedUnit[slot] ) );
            if ( pSkinned == nullptr )
                continue;
            MeshComponent* pBaked = static_cast<MeshComponent*>( resolveOwnedComponent( _arrBakedUnit[slot] ) );
            if ( bBaked && pBaked == nullptr )
            {
                pBaked = pOwner->addComponent<MeshComponent>();
                if ( pBaked == nullptr )
                    continue;
                if ( pSkinned->getMaterialPath().empty() == false )
                    pBaked->setMaterialPath( pSkinned->getMaterialPath().c_str() );
                else
                    pBaked->setMaterial( pSkinned->getMaterial() );
                _arrBakedUnit[slot] = pBaked->getHandle();
            }
            if ( bBaked )
            {
                shared_ptr<Mesh> baked = FractureRenderUtil::createBakedMesh( *_asset, arrSlot[slot], _listPiecePose );
                pBaked->setVisible( baked != nullptr );
                if ( baked != nullptr )
                    pBaked->setMesh( std::move( baked ) );
                pSkinned->setVisible( false );
            }
            else
            {
                if ( pBaked != nullptr )
                    pBaked->setVisible( false );
                pSkinned->setVisible( true );
            }
        }
        _bBaked = bBaked ? SW_TRUE : SW_FALSE;
    }

    // --- 질의 -------------------------------------------------------------------------------------------------------------------

    uint32 FractureComponentBase::findGroupOfBody( PhysicsBodyHandle body ) const
    {
        if ( body.isValid() == false )
            return 0;
        for ( const FractureGroupRuntime& runtime : _listRuntime )
        {
            if ( runtime._body == body )
                return runtime._groupId;
        }
        for ( uint32 leaf = 0; leaf < static_cast<uint32>( _listLeafStaticBody.size() ); ++leaf )
        {
            if ( _listLeafStaticBody[leaf] == body )
                return _state.getGroupOfLeaf( leaf );
        }
        return 0;
    }

    FractureGroupRuntime* FractureComponentBase::findRuntime( uint32 groupId )
    {
        for ( FractureGroupRuntime& runtime : _listRuntime )
        {
            if ( runtime._groupId == groupId )
                return &runtime;
        }
        return nullptr;
    }

    const FractureGroupRuntime* FractureComponentBase::findRuntime( uint32 groupId ) const
    {
        for ( const FractureGroupRuntime& runtime : _listRuntime )
        {
            if ( runtime._groupId == groupId )
                return &runtime;
        }
        return nullptr;
    }

    uint32 FractureComponentBase::getDynamicBodyCount() const
    {
        uint32 count = 0;
        for ( const FractureGroupRuntime& runtime : _listRuntime )
            count += runtime._body.isValid() ? 1u : 0u;
        return count;
    }

    uint32 FractureComponentBase::getStaticBodyCount() const
    {
        uint32 count = 0;
        for ( const PhysicsBodyHandle& body : _listLeafStaticBody )
            count += body.isValid() ? 1u : 0u;
        return count;
    }

    uint32 FractureComponentBase::getDebrisGroupCount() const
    {
        uint32 count = 0;
        for ( const FractureGroupRuntime& runtime : _listRuntime )
            count += ( runtime._bAnchored == SW_FALSE && runtime._bGone == SW_FALSE ) ? 1u : 0u;
        return count;
    }

    Component* FractureComponentBase::resolveOwnedComponent( const ComponentHandle& handle ) const
    {
        const GameObject*  pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        return ( pManager != nullptr && handle.isValid() ) ? pManager->resolveComponent( handle ) : nullptr;
    }

    bool FractureComponentBase::isReachedBy( const float3& worldCenter, float32 radius ) const
    {
        float3     position{};
        quaternion rotation{};
        float32    scale = 1.0f;
        readObjectPose( position, rotation, scale );
        if ( _asset == nullptr )
            return float3::getDistance( position, worldCenter ) <= radius;
        const float3  center      = ( _asset->_boundsMin + _asset->_boundsMax ) * 0.5f;
        const float32 boundRadius = float3::getDistance( _asset->_boundsMax, center ) * scale;
        const float3  worldBounds = position + float3::transform( center * scale, rotation );
        return float3::getDistance( worldBounds, worldCenter ) <= radius + boundRadius;
    }
} // namespace sw
