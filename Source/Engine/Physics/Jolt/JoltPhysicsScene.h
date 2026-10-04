/**
 * @file JoltPhysicsScene.h
 * @brief `IPhysicsScene3D` 의 Jolt 구현입니다. Jolt 헤더를 include 하므로 `Physics/Jolt` 밖에서는 보지 않습니다(`JoltPhysicsBackend::createScene` 으로만 만든다).
 * @details
 *  - **레이어.** 엔진 레이어 n 은 Jolt 오브젝트 레이어 2n(움직이지 않는 바디) · 2n+1(움직이는 바디)이고, 넓은 단계 레이어는 둘(정적 · 움직임)입니다.
 *    레이어끼리의 충돌은 `CollisionLayers` 행렬 하나가 정합니다. 바디 쌍의 예외는 `PhysicsPairFilter` 를 접촉 검증(`OnContactValidate`)에서 봅니다.
 *  - **재질.** 셰이프마다 Jolt 재질(마찰 · 반발)을 붙이고 접촉이 생길 때 서브 셰이프의 재질로 섞습니다(마찰 기하 평균 · 반발 큰 쪽). 밀도는 셰이프에 굽습니다.
 *  - **이벤트.** Jolt 의 접촉 콜백은 잡 스레드에서 옵니다 — 잠근 목록에 모았다가 스텝 뒤 정렬해 `PhysicsContactTracker` 에 넣습니다(결정적 순서).
 *    충격량은 접촉이 생길 때 `EstimateCollisionResponse` 로 어림합니다(유지 중에는 그 스텝의 어림값).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/SlotHandleTable.h"
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsContact.h"
#include "Engine/Physics/PhysicsPairFilter.h"
#include "Engine/Physics/PhysicsSettings.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace sw
{
    /** @brief 엔진 레이어 ↔ Jolt 레이어 변환입니다. */
    struct JoltLayerUtil
    {
        static constexpr JPH::uint kBroadPhaseLayerCount = 2;
        static constexpr JPH::uint kObjectLayerCount     = CollisionLayers::kLayerCount * 2;

        static JPH::ObjectLayer makeObjectLayer( uint8 layer, bool bMoving ) { return static_cast<JPH::ObjectLayer>( layer * 2u + ( bMoving ? 1u : 0u ) ); }
        static uint8            getLayer( JPH::ObjectLayer objectLayer ) { return static_cast<uint8>( objectLayer / 2u ); }
        static bool             isMoving( JPH::ObjectLayer objectLayer ) { return ( objectLayer & 1u ) != 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 오브젝트 레이어 → 넓은 단계 레이어(정적 0 · 움직임 1)입니다. */
    class JoltBroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
    {
    public:
        JPH::uint            GetNumBroadPhaseLayers() const override { return JoltLayerUtil::kBroadPhaseLayerCount; }
        JPH::BroadPhaseLayer GetBroadPhaseLayer( JPH::ObjectLayer layer ) const override
        {
            return JPH::BroadPhaseLayer{ static_cast<JPH::BroadPhaseLayer::Type>( JoltLayerUtil::isMoving( layer ) ? 1 : 0 ) };
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 움직이지 않는 바디는 정적 넓은 단계 레이어와 견주지 않습니다. */
    class JoltObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide( JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer ) const override
        {
            return JoltLayerUtil::isMoving( layer ) || broadPhaseLayer.GetValue() != 0;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 엔진 레이어 행렬로 오브젝트 레이어 쌍을 거릅니다. 둘 다 움직이지 않으면 견주지 않습니다. */
    class JoltObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
    {
    public:
        JoltObjectLayerPairFilter()
            : _layers{}
        {
        }

        bool ShouldCollide( JPH::ObjectLayer layerA, JPH::ObjectLayer layerB ) const override
        {
            if ( JoltLayerUtil::isMoving( layerA ) == false && JoltLayerUtil::isMoving( layerB ) == false )
                return false;
            return _layers.shouldCollide( JoltLayerUtil::getLayer( layerA ), JoltLayerUtil::getLayer( layerB ) );
        }

        void setLayers( const CollisionLayers& layers ) { _layers = layers; }

    private:
        CollisionLayers _layers;
    };
} // namespace sw

namespace sw
{
    /** @brief 엔진 재질(마찰 · 반발)을 든 Jolt 재질입니다. 셰이프가 들고, 접촉이 생길 때 섞습니다. */
    class JoltPhysicsMaterial final : public JPH::PhysicsMaterial
    {
    public:
        JoltPhysicsMaterial( float32 friction, float32 restitution )
            : _friction{ friction }
            , _restitution{ restitution }
        {
        }

        float32 getFriction() const { return _friction; }
        float32 getRestitution() const { return _restitution; }

    private:
        float32 _friction;
        float32 _restitution;
    };
} // namespace sw

namespace sw
{
    class JoltJobSystem;

    /** @class JoltPhysicsScene @brief Jolt 로 도는 3D 물리 씬입니다. 파일 머리말 참고. */
    class JoltPhysicsScene final : public IPhysicsScene3D, public JPH::ContactListener
    {
    public:
        JoltPhysicsScene( const PhysicsSettings& settings, JoltJobSystem* pJobSystem );
        ~JoltPhysicsScene() override;

        JoltPhysicsScene( const JoltPhysicsScene& )            = delete;
        JoltPhysicsScene& operator=( const JoltPhysicsScene& ) = delete;

        // IPhysicsScene3D
        void                                 step( float32 fixedDeltaTime ) override;
        const vector<PhysicsContactEvent3D>& getContactEvents() const override { return _listContactEvent; }
        void                                 setGravity( const float3& gravity ) override;
        float3                               getGravity() const override;
        void                                 setLayerCollision( const CollisionLayers& layers ) override;
        void                                 drawDebug( IPhysicsDebugRenderer& renderer ) const override;

        PhysicsShapeHandle createShape( span<const PhysicsShapeDesc3D> listShape, const hashed_string& material ) override;
        void               destroyShape( PhysicsShapeHandle shape ) override;

        PhysicsBodyHandle createBody( const PhysicsBodyDesc3D& desc ) override;
        void              createBodies( span<const PhysicsBodyDesc3D> listDesc, vector<PhysicsBodyHandle>& outListBody ) override;
        void              destroyBody( PhysicsBodyHandle body ) override;
        void              destroyBodies( span<const PhysicsBodyHandle> listBody ) override;
        bool              isBodyValid( PhysicsBodyHandle body ) const override;
        uint32            getBodyCount() const override;
        void              setBodyEnabled( PhysicsBodyHandle body, bool bEnabled ) override;
        bool              isBodyEnabled( PhysicsBodyHandle body ) const override;

        bool getBodyTransform( PhysicsBodyHandle body, float3& outPosition, quaternion& outRotation ) const override;
        void setBodyTransform( PhysicsBodyHandle body, const float3& position, const quaternion& rotation ) override;
        void moveKinematic( PhysicsBodyHandle body, const float3& targetPosition, const quaternion& targetRotation, float32 deltaTime ) override;

        float3 getLinearVelocity( PhysicsBodyHandle body ) const override;
        void   setLinearVelocity( PhysicsBodyHandle body, const float3& velocity ) override;
        float3 getAngularVelocity( PhysicsBodyHandle body ) const override;
        void   setAngularVelocity( PhysicsBodyHandle body, const float3& velocity ) override;
        void   addForce( PhysicsBodyHandle body, const float3& force ) override;
        void   addImpulse( PhysicsBodyHandle body, const float3& impulse ) override;
        void   addImpulseAtPoint( PhysicsBodyHandle body, const float3& impulse, const float3& point ) override;
        void   addTorque( PhysicsBodyHandle body, const float3& torque ) override;

        void            setBodyType( PhysicsBodyHandle body, PhysicsBodyType type ) override;
        PhysicsBodyType getBodyType( PhysicsBodyHandle body ) const override;
        void            setBodyLayer( PhysicsBodyHandle body, uint8 layer ) override;
        uint8           getBodyLayer( PhysicsBodyHandle body ) const override;
        void            setGravityFactor( PhysicsBodyHandle body, float32 factor ) override;
        float32         getBodyMass( PhysicsBodyHandle body ) const override;
        bool            isBodySleeping( PhysicsBodyHandle body ) const override;
        void            wakeBody( PhysicsBodyHandle body ) override;
        uint64          getBodyUserData( PhysicsBodyHandle body ) const override;
        void            setPairCollision( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, bool bCollide ) override;

        PhysicsJointHandle createJoint( const PhysicsJointDesc3D& desc ) override;
        void               destroyJoint( PhysicsJointHandle joint ) override;
        bool               isJointValid( PhysicsJointHandle joint ) const override;
        void               setJointMotor( PhysicsJointHandle joint, const PhysicsJointMotor& motor ) override;
        float32            getJointPosition( PhysicsJointHandle joint ) const override;

        PhysicsCharacterHandle  createCharacter( const PhysicsCharacterDesc3D& desc ) override;
        void                    destroyCharacter( PhysicsCharacterHandle character ) override;
        PhysicsCharacterState3D moveCharacter( PhysicsCharacterHandle character, const float3& velocity, float32 deltaTime ) override;
        void                    setCharacterPosition( PhysicsCharacterHandle character, const float3& position ) override;
        bool                    getCharacterState( PhysicsCharacterHandle character, PhysicsCharacterState3D& outState ) const override;

        bool   raycast( const float3& origin, const float3& direction, float32 maxDistance, const PhysicsQueryFilter& filter, PhysicsCastHit3D& outHit ) const override;
        bool   shapeCast( const PhysicsShapeDesc3D& shape, const float3& position, const quaternion& rotation, const float3& direction, float32 maxDistance,
                          const PhysicsQueryFilter& filter, PhysicsCastHit3D& outHit ) const override;
        uint32 overlapShape( const PhysicsShapeDesc3D& shape, const float3& position, const quaternion& rotation, const PhysicsQueryFilter& filter,
                             vector<PhysicsBodyHandle>& outListBody ) const override;

        // JPH::ContactListener — 잡 스레드에서 불린다
        JPH::ValidateResult OnContactValidate( const JPH::Body& bodyA, const JPH::Body& bodyB, JPH::RVec3Arg baseOffset,
                                               const JPH::CollideShapeResult& collisionResult ) override;
        void                OnContactAdded( const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold, JPH::ContactSettings& ioSettings ) override;
        void                OnContactPersisted( const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold, JPH::ContactSettings& ioSettings ) override;
        void                OnContactRemoved( const JPH::SubShapeIDPair& subShapePair ) override;

    private:
        using ShapeDescList = vector<PhysicsShapeDesc3D>;

        struct BodyRecord
        {
            shared_ptr<const ShapeDescList> _pListShape;
            uint64                          _userData{ 0 };
            JPH::BodyID                     _bodyId{};
            PhysicsBodyType                 _type{ PhysicsBodyType::Dynamic };
            uint8                           _layer{ 0 };
            bool                            _bTrigger{ false };
            bool                            _bEnabled{ true };
        };

        struct JointRecord
        {
            JPH::Ref<JPH::TwoBodyConstraint> _pConstraint;
            PhysicsBodyHandle                _bodyA{};
            PhysicsBodyHandle                _bodyB{};
            float3                           _anchorA{}; ///< Distance 의 현재 길이를 재는 A 쪽 로컬 점
            float3                           _anchorB{};
            PhysicsJointType                 _type{ PhysicsJointType::Fixed };
            bool                             _bDisableCollision{ false };
        };

        struct CharacterRecord
        {
            JPH::Ref<JPH::CharacterVirtual> _pCharacter;
            PhysicsCharacterDesc3D          _desc{};
            PhysicsCharacterState3D         _state{};
        };

        struct ShapeRecord
        {
            JPH::RefConst<JPH::Shape>       _pShape;
            shared_ptr<const ShapeDescList> _pListShape;
        };

        /** @brief BodyID 색인 → 엔진 핸들입니다. 콜백(`OnContactRemoved` 는 BodyID 만 준다)과 질의 결과가 씁니다. */
        struct BodyIdEntry
        {
            JPH::uint32       _bodyIdValue{ JPH::BodyID::cInvalidBodyID };
            PhysicsBodyHandle _body{};
        };

        /** @brief 잡 스레드가 모으는 접촉 알림 하나입니다. */
        struct ContactReport
        {
            PhysicsContactBody _bodyA{};
            PhysicsContactBody _bodyB{};
            float3             _point{};
            float3             _normal{};
            float32            _impulse{ 0.0f };
            JPH::uint32        _subShapeA{ 0 };
            JPH::uint32        _subShapeB{ 0 };
            uint8              _kind{ 0 }; ///< 0 추가 · 1 유지 · 2 제거
        };

        /** @brief 셰이프 서술자 묶음을 Jolt 셰이프 하나로 짓습니다(둘 이상이면 정적 컴파운드). 실패하면 오류를 남기고 nullptr 입니다. */
        JPH::RefConst<JPH::Shape> buildShape( span<const PhysicsShapeDesc3D> listShape, const hashed_string& defaultMaterial, bool bAllowMesh ) const;
        /** @brief 셰이프 하나를 로컬 자리 · 회전 없이 짓습니다. */
        JPH::RefConst<JPH::Shape> buildSingleShape( const PhysicsShapeDesc3D& shape, const PhysicsMaterialDef& material, const JPH::PhysicsMaterial* pMaterial ) const;
        /** @brief 재질 이름을 풉니다. 없으면 오류를 남기고 첫 재질입니다. */
        const PhysicsMaterialDef&   resolveMaterial( const hashed_string& name, const hashed_string& fallback ) const;
        const JPH::PhysicsMaterial* findJoltMaterial( const hashed_string& name ) const;
        /** @brief 바디 하나를 Jolt 에 만들기만 합니다(넣지 않는다). 실패하면 무효 핸들입니다. */
        PhysicsBodyHandle createBodyUnadded( const PhysicsBodyDesc3D& desc );
        const BodyRecord* findBody( PhysicsBodyHandle body ) const;
        BodyRecord*       findBody( PhysicsBodyHandle body );
        PhysicsBodyHandle findHandle( const JPH::BodyID& bodyId ) const;
        void              setHandle( const JPH::BodyID& bodyId, PhysicsBodyHandle body );
        /** @brief 바디들에 붙은 관절을 지웁니다(바디를 지우기 전에). */
        void destroyJointsOf( span<const PhysicsBodyHandle> listBody );
        void recordContact( const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold, JPH::ContactSettings& ioSettings, uint8 kind );
        /** @brief 알림의 결정적 순서입니다(바디 쌍 · 서브 셰이프 · 종류). */
        static bool isEarlierReport( const ContactReport& lhs, const ContactReport& rhs );
        /** @brief 스텝 동안 모은 알림을 정렬해 추적기에 넣고 이벤트를 냅니다. */
        void flushContactReports();

        PhysicsSettings                           _settings;
        JoltBroadPhaseLayers                      _broadPhaseLayers;
        JoltObjectVsBroadPhaseFilter              _objectVsBroadPhaseFilter;
        JoltObjectLayerPairFilter                 _objectLayerPairFilter;
        JPH::TempAllocatorImpl                    _tempAllocator;
        JPH::PhysicsSystem                        _system;
        JoltJobSystem*                            _pJobSystem;
        vector<JPH::Ref<JoltPhysicsMaterial>>     _listJoltMaterial; ///< `_settings._listMaterial` 과 같은 순서
        SlotHandleTable<BodyRecord>               _bodies;
        SlotHandleTable<JointRecord>              _joints;
        SlotHandleTable<CharacterRecord>          _characters;
        SlotHandleTable<ShapeRecord>              _shapes;
        vector<BodyIdEntry>                       _listBodyIdEntry;
        PhysicsPairFilter                         _pairFilter;
        PhysicsContactTracker<PhysicsDimension3D> _contactTracker;
        vector<PhysicsContactEvent3D>             _listContactEvent;
        vector<ContactReport>                     _listContactReport; ///< 스텝 중 잡 스레드가 `_reportMutex` 를 쥐고 붙인다
        mutex                                     _reportMutex;
        uint32                                    _bodyCount;
    };
} // namespace sw
