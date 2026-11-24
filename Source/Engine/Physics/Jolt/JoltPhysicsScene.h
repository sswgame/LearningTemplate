/**
 * @file JoltPhysicsScene.h
 * @brief `IPhysicsScene3D` 의 Jolt 구현입니다. Jolt 헤더를 include 하므로 `Physics/Jolt` 밖에서는 보지 않습니다(`JoltPhysicsBackend::createScene` 으로만 만듭니다).
 * @details
 *  - **레이어.** 엔진 레이어 n 은 Jolt 오브젝트 레이어 2n(움직이지 않는 바디)과 2n+1(움직이는 바디)이고, 넓은 단계 레이어는 정적과 움직임 둘이다.
 *    레이어끼리의 충돌은 `CollisionLayers` 행렬 하나가 정한다. 바디 쌍의 예외는 `PhysicsPairFilter` 를 접촉 검증(`JoltContactListener::OnContactValidate`)에서 확인한다.
 *  - **재질.** 셰이프마다 Jolt 재질(마찰, 반발)을 붙이고, 접촉이 생길 때 서브 셰이프의 재질로 섞는다(마찰은 기하 평균, 반발은 큰 쪽). 밀도는 셰이프에 베이크한다.
 *  - **이벤트.** Jolt 의 접촉 콜백(`JoltContactListener`)은 잡 스레드에서 온다. 잠근 목록에 모았다가 스텝 뒤 정렬해 `PhysicsContactTracker` 에 넣는다(결정적 순서).
 *    충격량은 접촉이 생길 때 `EstimateCollisionResponse` 로 어림하고, 접촉이 유지되는 동안에는 그 스텝의 어림값을 쓴다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/SlotHandleTable.h"
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Physics/Collision/PhysicsContact.h"
#include "Engine/Physics/Collision/PhysicsPairFilter.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Jolt/JoltSoftBodySet.h"
#include "Engine/Physics/Jolt/JoltUtil.h"
#include "Engine/Physics/Jolt/JoltVehicleSet.h"
#include "Engine/Physics/PhysicsSettings.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace sw
{
    /** @brief 오브젝트 레이어를 넓은 단계 레이어(정적 0, 움직임 1)로 바꿉니다. */
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
    /** @brief 움직이지 않는 바디는 정적 넓은 단계 레이어와 비교하지 않습니다. */
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
    /** @brief 엔진 레이어 행렬로 오브젝트 레이어 쌍을 거릅니다. 둘 다 움직이지 않으면 비교하지 않습니다. */
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

        void                   setLayers( const CollisionLayers& layers ) { _layers = layers; }
        const CollisionLayers& getLayers() const { return _layers; }

    private:
        CollisionLayers _layers;
    };
} // namespace sw

namespace sw
{
    /** @brief 엔진 재질(마찰, 반발)을 가진 Jolt 재질입니다. 셰이프가 보관하고, 접촉이 생길 때 섞습니다. */
    class JoltPhysicsMaterial final : public JPH::PhysicsMaterial
    {
    public:
        JoltPhysicsMaterial( const hashed_string& name, float32 friction, float32 restitution )
            : _name{ name }
            , _friction{ friction }
            , _restitution{ restitution }
        {
        }

        const hashed_string& getName() const { return _name; }
        float32              getFriction() const { return _friction; }
        float32              getRestitution() const { return _restitution; }

    private:
        hashed_string _name;
        float32       _friction;
        float32       _restitution;
    };
} // namespace sw

namespace sw
{
    /**
     * @class JoltTempAllocator
     * @brief 고정 블록에서 쌓아 받고, 넘치면 힙으로 가는 Jolt 임시 할당자입니다 — Jolt `TempAllocatorImplWithMallocFallback` 과 같은 길에 최고 사용량 · 폴백 수를 더했다.
     * @details 블록만 쓰는 `TempAllocatorImpl` 은 넘치면 `std::abort()` 라 크래시 처리기도 타지 않았다(감사 S-2).
     *          Jolt 처럼 한 번에 한 스레드만 부른다(받고 놓는 차례가 스택이다) — 카운터에 잠금이 없다.
     */
    class JoltTempAllocator final : public JPH::TempAllocator
    {
    public:
        explicit JoltTempAllocator( uint32 byteSize )
            : _block{ byteSize }
            , _fallback{}
            , _peakBytes{ 0 }
            , _fallbackCount{ 0 }
        {
        }

        void* Allocate( JPH::uint size ) override
        {
            if ( _block.CanAllocate( size ) )
            {
                void* const pAddress = _block.Allocate( size );
                _peakBytes           = _block.GetUsage() > _peakBytes ? _block.GetUsage() : _peakBytes;
                return pAddress;
            }
            ++_fallbackCount;
            return _fallback.Allocate( size );
        }

        void Free( void* pAddress, JPH::uint size ) override
        {
            if ( pAddress == nullptr )
                return;
            if ( _block.OwnsMemory( pAddress ) )
                _block.Free( pAddress, size );
            else
                _fallback.Free( pAddress, size );
        }

        /** @brief 블록 크기(바이트, `PhysicsSettings::_tempAllocatorBytes`)입니다. */
        size_t getByteSize() const { return _block.GetSize(); }
        /** @brief 지난 `consumePeakBytes` 뒤 블록의 최고 사용량(바이트)을 돌려주고 비웁니다. 폴백 몫은 들지 않는다. */
        size_t consumePeakBytes()
        {
            const size_t peakBytes = _peakBytes;
            _peakBytes             = 0;
            return peakBytes;
        }
        /** @brief 지난 `consumeFallbackCount` 뒤 힙으로 간 할당 수를 돌려주고 비웁니다. */
        uint32 consumeFallbackCount()
        {
            const uint32 count = _fallbackCount;
            _fallbackCount     = 0;
            return count;
        }

    private:
        JPH::TempAllocatorImpl   _block;
        JPH::TempAllocatorMalloc _fallback;
        size_t                   _peakBytes;
        uint32                   _fallbackCount;
    };
} // namespace sw

namespace sw
{
    class JoltPhysicsScene;

    /**
     * @class JoltContactListener
     * @brief Jolt 의 접촉 콜백을 받아 씬에 넘기는 어댑터입니다. 씬이 하나 보관하고 자기 물리 시스템에 등록합니다(`SetContactListener`).
     * @details 잡 스레드에서 호출된다. 씬의 쌍 필터는 읽기만 하고, 알림 목록은 씬이 `_reportMutex` 를 쥐고 붙인다.
     */
    class JoltContactListener final : public JPH::ContactListener
    {
    public:
        explicit JoltContactListener( JoltPhysicsScene& owner );

        JPH::ValidateResult OnContactValidate( const JPH::Body& bodyA, const JPH::Body& bodyB, JPH::RVec3Arg baseOffset,
                                               const JPH::CollideShapeResult& collisionResult ) override;
        void                OnContactAdded( const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold, JPH::ContactSettings& ioSettings ) override;
        void                OnContactPersisted( const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold, JPH::ContactSettings& ioSettings ) override;
        void                OnContactRemoved( const JPH::SubShapeIDPair& subShapePair ) override;

    private:
        JoltPhysicsScene& _owner;
    };
} // namespace sw

namespace sw
{
    /**
     * @class JoltCharacterSnapshotCollision
     * @brief 캐릭터끼리 막기를 캐릭터마다 둔 **사본**(속 바디 없는 `CharacterVirtual`)으로 봅니다 — 일괄 이동이 캐릭터를 청크로 나눠 돌게 하는 자리입니다(감사 S-8).
     * @details Jolt 의 `CharacterVsCharacterCollisionSimple` 은 다른 캐릭터의 자리 · 속도를 살아 있는 객체에서 읽어 한 번에 한 캐릭터만 돌 수 있다(그쪽 주석).
     *          사본은 씬이 캐릭터의 이동을 확정할 때(`JoltPhysicsScene::commitCharacterMove`)만 옮겨, 이동 중에는 얼어 있다 — 여러 청크가 함께 읽어도 된다.
     *          일괄 이동은 모든 캐릭터가 단계 앞 사본을 보고(유니티 DOTS Character Controller 가 앞 스텝 충돌 월드를 읽는 것과 같다), 하나씩 옮기면 앞에 옮긴 캐릭터의 새 자리를 본다.
     *          셈은 Jolt 5.6.0 의 것을 그대로 옮겼다(경계 상자로 거른 뒤 좁은 단계). 두 캐릭터의 레이어가 씬의 레이어 표에서 막지 않으면 서로 통과한다
     *          (바디와 같은 표 — Jolt 의 `ObjectLayerPairFilter`, 언리얼 Pawn 채널 응답과 같다).
     *          shortcut: 쌍을 모두 훑는다(O(n²)), 캐릭터가 수천 명이 되면 격자로 거른다.
     */
    class JoltCharacterSnapshotCollision final : public JPH::CharacterVsCharacterCollision
    {
    public:
        /** @brief @p layerFilter 는 씬의 레이어 표다(씬이 쥐고 이것보다 오래 산다 — 표를 바꾸면 다음 이동부터 본다). */
        explicit JoltCharacterSnapshotCollision( const JoltObjectLayerPairFilter& layerFilter );

        /** @brief 캐릭터와 그 사본, 캐릭터의 엔진 레이어를 올립니다. 사본의 수명은 부르는 쪽(캐릭터 기록)이 쥔다. */
        void add( const JPH::CharacterVirtual* pCharacter, const JPH::CharacterVirtual* pMirror, uint8 layer );
        /** @brief 캐릭터를 내립니다. 남은 것의 차례는 그대로다(접촉 수집 차례가 바뀌지 않게). */
        void remove( const JPH::CharacterVirtual* pCharacter );
        void clear() { _listEntry.clear(); }

        void CollideCharacter( const JPH::CharacterVirtual* pCharacter, JPH::RMat44Arg centerOfMassTransform, const JPH::CollideShapeSettings& collideShapeSettings,
                               JPH::RVec3Arg baseOffset, JPH::CollideShapeCollector& ioCollector ) const override;
        void CastCharacter( const JPH::CharacterVirtual* pCharacter, JPH::RMat44Arg centerOfMassTransform, JPH::Vec3Arg direction, const JPH::ShapeCastSettings& shapeCastSettings,
                            JPH::RVec3Arg baseOffset, JPH::CastShapeCollector& ioCollector ) const override;

    private:
        struct Entry
        {
            const JPH::CharacterVirtual* _pCharacter{ nullptr }; ///< 살아 있는 캐릭터(자기 자신을 거르는 열쇠)
            const JPH::CharacterVirtual* _pMirror{ nullptr };    ///< 다른 캐릭터가 보는 사본 — 접촉의 상대 캐릭터로 넘긴다
            uint8                        _layer{ 0 };            ///< 캐릭터의 엔진 레이어(`PhysicsCharacterDesc::_layer`)
        };

        /** @brief @p pCharacter 의 레이어입니다. 올리지 않은 캐릭터면 0 번 레이어로 본다(씬이 만든 캐릭터는 늘 올라 있다). */
        uint8 findLayer( const JPH::CharacterVirtual* pCharacter ) const;

    private:
        const JoltObjectLayerPairFilter* _pLayerFilter;
        vector<Entry>                    _listEntry;
    };

    /**
     * @class JoltCharacterContactListener
     * @brief 캐릭터가 이동 중에 바디에 충격량을 주지 않게 합니다 — 미는 힘은 씬이 이동을 확정할 때 목록 차례로 준다(감사 S-8, 유니티 DOTS 의 지연 충격량).
     * @details Jolt 는 이동 중 닿은 동적 바디에 바로 `AddImpulse` 한다 — 청크 여럿이 같은 바디에 쓰면 더하는 차례가 청크마다 달라 결과가 흔들린다. 상태가 없어 청크가 함께 불러도 된다.
     */
    class JoltCharacterContactListener final : public JPH::CharacterContactListener
    {
    public:
        void OnContactAdded( const JPH::CharacterVirtual* pCharacter, const JPH::CharacterContact& contact, JPH::CharacterContactSettings& ioSettings ) override;
        void OnContactPersisted( const JPH::CharacterVirtual* pCharacter, const JPH::CharacterContact& contact, JPH::CharacterContactSettings& ioSettings ) override;
    };

    class JoltJobSystem;

    /** @class JoltPhysicsScene @brief Jolt 로 도는 3D 물리 씬입니다. 파일 머리말 참고. */
    class JoltPhysicsScene final : public IPhysicsScene3D
    {
        friend class JoltContactListener;

    public:
        JoltPhysicsScene( const PhysicsSettings& settings, JoltJobSystem* pJobSystem );
        ~JoltPhysicsScene() override;

        JoltPhysicsScene( const JoltPhysicsScene& )            = delete;
        JoltPhysicsScene& operator=( const JoltPhysicsScene& ) = delete;

        // IPhysicsScene3D
        void                                 step( float32 fixedDeltaTime ) override;
        const vector<PhysicsContactEvent3D>& getContactEvents() const override { return _listContactEvent; }
        void                                 setContactStayListener( uint64 userData, bool bListen ) override { _contactTracker.setStayListener( userData, bListen ); }
        void                                 setGravity( const float3& gravity ) override;
        float3                               getGravity() const override;
        void                                 setLayerCollision( const CollisionLayers& layers ) override;
        const CollisionLayers&               getLayerCollision() const override { return _objectLayerPairFilter.getLayers(); }
        void                                 drawDebug( IPhysicsDebugRenderer& renderer ) const override;

        PhysicsShapeHandle createShape( span<const PhysicsShapeDesc3D> listShape, const hashed_string& material ) override;
        PhysicsShapeHandle createCompoundShape( span<const PhysicsShapeHandle> listChild ) override;
        void               destroyShape( PhysicsShapeHandle shape ) override;

        PhysicsBodyHandle createBody( const PhysicsBodyDesc3D& desc ) override;
        void              createBodies( span<const PhysicsBodyDesc3D> listDesc, vector<PhysicsBodyHandle>& outListBody ) override;
        void              destroyBody( PhysicsBodyHandle body ) override;
        void              destroyBodies( span<const PhysicsBodyHandle> listBody ) override;
        bool              setBodyShape( PhysicsBodyHandle body, span<const PhysicsShapeDesc3D> listShape, const hashed_string& material, float32 mass ) override;
        bool              isBodyValid( PhysicsBodyHandle body ) const override;
        uint32            getBodyCount() const override;
        void              setBodyEnabled( PhysicsBodyHandle body, bool bEnabled ) override;
        bool              isBodyEnabled( PhysicsBodyHandle body ) const override;

        const vector<PhysicsBodyHandle>& getMovedBodies() const override { return _listMovedBody; }

        bool getBodyTransform( PhysicsBodyHandle body, float3& outPosition, quaternion& outRotation ) const override;
        void readBodyStates( span<const PhysicsBodyHandle> listBody, span<PhysicsBodyState3D> outListState ) const override;
        void setBodyTransform( PhysicsBodyHandle body, const float3& position, const quaternion& rotation ) override;
        void shiftOrigin( const float3& delta ) override;
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
        bool            isBodyTrigger( PhysicsBodyHandle body ) const override;
        void            setPairCollision( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, bool bCollide ) override;
        bool            drawBodyDebug( PhysicsBodyHandle body, IPhysicsDebugRenderer& renderer ) const override;

        PhysicsJointHandle createJoint( const PhysicsJointDesc3D& desc ) override;
        void               destroyJoint( PhysicsJointHandle joint ) override;
        bool               isJointValid( PhysicsJointHandle joint ) const override;
        void               setJointMotor( PhysicsJointHandle joint, const PhysicsJointMotor& motor ) override;
        float32            getJointPosition( PhysicsJointHandle joint ) const override;
        void               driveJointToRotation( PhysicsJointHandle joint, const quaternion& targetRotationA, const quaternion& targetRotationB, float32 maxTorque ) override;

        PhysicsCharacterHandle  createCharacter( const PhysicsCharacterDesc3D& desc ) override;
        void                    destroyCharacter( PhysicsCharacterHandle character ) override;
        PhysicsCharacterState3D moveCharacter( PhysicsCharacterHandle character, const float3& velocity, float32 deltaTime ) override;
        void                    moveCharacters( span<const PhysicsCharacterMove3D> listMove, float32 deltaTime ) override;
        PhysicsCharacterState3D replayCharacterMove( PhysicsCharacterHandle character, const float3& velocity, float32 deltaTime ) override;
        void                    setCharacterPosition( PhysicsCharacterHandle character, const float3& position ) override;
        void                    refreshCharacterContacts( PhysicsCharacterHandle character ) override;
        bool                    setCharacterSize( PhysicsCharacterHandle character, float32 radius, float32 halfHeight ) override;
        bool                    getCharacterState( PhysicsCharacterHandle character, PhysicsCharacterState3D& outState ) const override;
        /** @brief @p body 가 캐릭터의 속 바디인지입니다(캐릭터 이동이 속 바디에 막히지 않게 거른다). */
        bool isCharacterInnerBody( PhysicsBodyHandle body ) const;

        PhysicsVehicleHandle createWheeledVehicle( PhysicsBodyHandle chassis, const PhysicsWheeledVehicleDesc& desc ) override;
        void                 destroyVehicle( PhysicsVehicleHandle vehicle ) override;
        bool                 isVehicleValid( PhysicsVehicleHandle vehicle ) const override;
        void                 setVehicleInput( PhysicsVehicleHandle vehicle, float32 forward, float32 right, float32 brake, float32 handBrake ) override;
        bool                 getVehicleState( PhysicsVehicleHandle vehicle, PhysicsVehicleState& outState ) const override;

        PhysicsSoftBodyHandle createSoftBody( const PhysicsSoftBodyDesc& desc ) override;
        void                  destroySoftBody( PhysicsSoftBodyHandle softBody ) override;
        bool                  isSoftBodyValid( PhysicsSoftBodyHandle softBody ) const override;
        bool                  getSoftBodyVertices( PhysicsSoftBodyHandle softBody, vector<float3>& outListPosition ) const override;
        void                  setSoftBodyPinnedTransform( PhysicsSoftBodyHandle softBody, const float3& position, const quaternion& rotation ) override;
        void                  setSoftBodySkinningPose( PhysicsSoftBodyHandle softBody, const vector<float4x4>& listJointWorld, bool bHardSkinAll ) override;
        void                  setSoftBodyIterationCount( PhysicsSoftBodyHandle softBody, uint32 iterationCount ) override;
        void                  applySoftBodyWind( PhysicsSoftBodyHandle softBody, const float3& windVelocity, float32 dragDensity, float32 seconds ) override;

        [[nodiscard]] bool applyBuoyancy( PhysicsBodyHandle body, const PhysicsBuoyancyDesc& desc, float32 deltaSeconds ) override;

        bool   raycast( const float3& origin, const float3& direction, float32 maxDistance, const PhysicsQueryFilter& filter, PhysicsCastHit3D& outHit ) const override;
        bool   shapeCast( const PhysicsShapeDesc3D& shape, const float3& position, const quaternion& rotation, const float3& direction, float32 maxDistance,
                          const PhysicsQueryFilter& filter, PhysicsCastHit3D& outHit ) const override;
        uint32 raycastAll( const float3& origin, const float3& direction, float32 maxDistance, const PhysicsQueryFilter& filter,
                           vector<PhysicsCastHit3D>& outListHit ) const override;
        uint32 shapeCastAll( const PhysicsShapeDesc3D& shape, const float3& position, const quaternion& rotation, const float3& direction, float32 maxDistance,
                             const PhysicsQueryFilter& filter, vector<PhysicsCastHit3D>& outListHit ) const override;
        uint32 overlapShape( const PhysicsShapeDesc3D& shape, const float3& position, const quaternion& rotation, const PhysicsQueryFilter& filter,
                             vector<PhysicsBodyHandle>& outListBody ) const override;

    private:
        using ShapeDescList = vector<PhysicsShapeDesc3D>;

        struct BodyRecord
        {
            shared_ptr<const ShapeDescList> _pListShape;
            uint64                          _userData{ 0 };
            JPH::BodyID                     _bodyID{};
            PhysicsBodyType                 _type{ PhysicsBodyType::Dynamic };
            uint8                           _layer{ 0 };
            bool                            _bTrigger{ false };   ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool                            _bEnabled{ SW_TRUE }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            /// 캐릭터의 속 바디다(`PhysicsCharacterDesc::_bInnerBody`). Jolt 가 캐릭터와 함께 만들고 지우므로 `destroyBody` · 소멸자는 지우지 않고 `destroyCharacter` 가 표에서만 뺀다.
            bool _bCharacterInner{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        struct JointRecord
        {
            JPH::Ref<JPH::TwoBodyConstraint> _pConstraint;
            PhysicsBodyHandle                _bodyA{};
            PhysicsBodyHandle                _bodyB{};
            float3                           _anchorA{}; ///< Distance 의 현재 길이를 측정하는 A 쪽 로컬 점이다.
            float3                           _anchorB{};
            PhysicsJointType                 _type{ PhysicsJointType::Fixed };
            bool                             _bPairFilterSet{ false }; ///< 만들 때 두 바디 쌍의 충돌을 껐다. 지울 때 되돌린다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        struct CharacterRecord
        {
            JPH::Ref<JPH::CharacterVirtual> _pCharacter;
            JPH::Ref<JPH::CharacterVirtual> _pMirror; ///< 다른 캐릭터가 보는 사본(`JoltCharacterSnapshotCollision`) — 이동을 확정할 때만 옮긴다
            PhysicsCharacterDesc3D          _desc{};
            PhysicsCharacterState3D         _state{};
            PhysicsBodyHandle               _innerBody{};       ///< 속 바디의 엔진 핸들이다(없으면 무효). 사용자 값은 캐릭터의 것이다.
            float3                          _movedVelocity{};   ///< 이번 이동에 Jolt 에 준 속도(요청 + 바닥) — 미는 충격량을 셈한다
            float3                          _carriedVelocity{}; ///< 이번 이동에 바닥이 실어 준 속도
            float3                          _moveStart{};       ///< 이번 이동을 시작한 발 자리 — 실제로 난 속도를 셈한다
            float32                         _groundYawRate{ 0.0f };
            uint32                          _moveStamp{ 0 }; ///< 마지막으로 든 일괄 이동의 번호(`_characterMoveStamp`) — 같은 캐릭터를 한 일괄에 두 번 들지 않는다
        };

        /** @brief 일괄 이동의 한 칸입니다(핸들을 게임 스레드에서 푼 기록). 이동 동안 `_characters` 에 넣고 빼지 않으므로 포인터가 산다. */
        struct CharacterMoveJob
        {
            CharacterRecord* _pRecord{ nullptr };
            float3           _velocity{};
        };

        struct ShapeRecord
        {
            JPH::RefConst<JPH::Shape>       _pShape;
            shared_ptr<const ShapeDescList> _pListShape;
        };

        /** @brief BodyID 색인 → 엔진 핸들입니다. 콜백(`JoltContactListener::OnContactRemoved` 는 BodyID 만 준다)과 질의 결과가 씁니다. */
        struct BodyIDEntry
        {
            JPH::uint32       _bodyIDValue{ JPH::BodyID::cInvalidBodyID };
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
            uint8              _kind{ 0 }; ///< 0 은 추가, 1 은 유지, 2 는 제거다.
        };

        /** @brief 셰이프 서술자 여럿을 Jolt 셰이프 하나로 만듭니다(둘 이상이면 정적 컴파운드). 실패하면 오류를 남기고 nullptr 를 반환합니다. */
        JPH::RefConst<JPH::Shape> createJoltShape( span<const PhysicsShapeDesc3D> listShape, const hashed_string& defaultMaterial, bool bAllowMesh ) const;
        /** @brief 캐릭터 캡슐 셰이프입니다(바닥이 원점 — 발). 지을 수 없으면 오류를 남기고 nullptr 입니다. */
        static JPH::RefConst<JPH::Shape> createCharacterShape( float32 radius, float32 halfHeight );
        /** @brief 캐릭터 캡슐의 엔진 서술자입니다(속 바디 기록 — `createCharacterShape` 와 같은 모양). */
        static PhysicsShapeDesc3D makeCharacterShapeDesc( float32 radius, float32 halfHeight );
        /** @brief 셰이프 하나를 로컬 위치와 회전 없이 만듭니다. */
        JPH::RefConst<JPH::Shape> createSingleJoltShape( const PhysicsShapeDesc3D& shape, const PhysicsMaterialDef& material, const JPH::PhysicsMaterial* pMaterial ) const;
        /** @brief 재질 이름을 풉니다. 이름을 적지 않았으면 첫 재질이고, 적은 이름이 설정에 없으면 오류를 남기고 nullptr 입니다(다른 재질로 만들지 않는다). */
        [[nodiscard]] const PhysicsMaterialDef* resolveMaterial( const hashed_string& name, const hashed_string& fallback ) const;
        const JPH::PhysicsMaterial*             findJoltMaterial( const hashed_string& name ) const;
        /** @brief 바디 하나를 Jolt 에 만들기만 합니다(넣지 않는다). 실패하면 무효 핸들입니다. */
        PhysicsBodyHandle createBodyUnadded( const PhysicsBodyDesc3D& desc );
        const BodyRecord* findBody( PhysicsBodyHandle body ) const;
        BodyRecord*       findBody( PhysicsBodyHandle body );
        /** @brief 켜진 바디 하나의 셰이프를 지금 자세로 선으로 냅니다(`drawDebug` 와 `drawBodyDebug` 가 나눠 쓴다). */
        void drawBodyRecord( const BodyRecord& record, IPhysicsDebugRenderer& renderer ) const;
        /** @brief 광선 닿음 하나를 엔진 결과로 옮깁니다(법선과 재질 이름은 바디를 잠그고 읽는다). */
        void makeRayHit( const JPH::RRayCast& ray, const JPH::RayCastResult& hit, float32 maxDistance, PhysicsCastHit3D& outHit ) const;
        /** @brief 셰이프 캐스트 닿음 하나를 엔진 결과로 옮깁니다. */
        void              makeShapeCastHit( const JPH::ShapeCastResult& hit, float32 maxDistance, PhysicsCastHit3D& outHit ) const;
        PhysicsBodyHandle findHandle( const JPH::BodyID& bodyID ) const;
        void              setHandle( const JPH::BodyID& bodyID, PhysicsBodyHandle body );
        /** @brief 바디들에 붙은 관절과 바퀴 차를 지웁니다(바디를 지우기 전에 호출합니다). */
        void destroyJoints( span<const PhysicsBodyHandle> listBody );
        /** @brief 바디 쌍의 접촉을 받을지 쌍 필터로 정합니다(잡 스레드, `JoltContactListener::OnContactValidate`). */
        JPH::ValidateResult validateContact( const JPH::Body& bodyA, const JPH::Body& bodyB ) const;
        /** @brief 접촉이 생기거나 유지될 때 재질을 섞고 알림을 붙입니다(잡 스레드, `_reportMutex`). */
        void recordContact( const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold, JPH::ContactSettings& ioSettings, uint8 kind );
        /** @brief 접촉이 끝난 알림을 붙입니다(잡 스레드, `_reportMutex`). */
        void recordContactRemoved( const JPH::SubShapeIDPair& subShapePair );
        /** @brief 알림의 결정적 순서입니다(바디 쌍, 서브 셰이프, 종류 순). */
        static bool isEarlierReport( const ContactReport& lhs, const ContactReport& rhs );
        /** @brief 스텝 동안 모은 알림을 정렬해 추적기에 넣고 이벤트를 냅니다. */
        void flushContactReports();
        /** @brief 두 바디가 모두 활성이 아닌지입니다(잠든 섬 — 정적은 늘 활성이 아니다). 없는 바디면 false 입니다. */
        bool isPairAsleep( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB ) const;
        /** @brief 지금 깨어 있는 강체를 `_listMovedBody` 에 붙입니다(스텝 앞 · 뒤에 한 번씩). */
        void appendActiveBodies();
        /** @brief 임시 블록(씬 · 캐릭터 청크)의 최고 사용량 · 폴백 수를 카운터로 내고, 처음 넘친 스텝에 한 번 경고합니다(스텝 끝). */
        void reportTempAllocator();
        /**
         * @brief 캐릭터 하나를 Jolt 로 옮기기만 합니다 — 청크에서 돈다. 이 캐릭터의 기록 · Jolt 객체만 쓰고, 월드 · 사본은 읽기만 한다.
         * @details 중력은 넘기지 않는다(Jolt 는 그것으로 디딘 바디를 누른다 — 바디 쓰기다). 누르기와 미는 충격량은 `commitCharacterMove` 가 준다.
         */
        void runCharacterMove( CharacterRecord& record, const float3& velocity, float32 deltaTime, JPH::TempAllocator& allocator ) const;
        /** @brief 옮긴 캐릭터를 확정합니다(게임 스레드, 목록 차례): 바디에 미는 · 누르는 충격량(@p bPushBodies 일 때만), 엔진 상태, 사본 옮기기. */
        void commitCharacterMove( CharacterRecord& record, float32 deltaTime, bool bPushBodies );
        /** @brief 일괄 이동의 청크 [chunkStart, chunkEnd) 를 돕니다(`engine::runParallel` 본문). 청크마다 제 임시 블록을 쓴다. */
        void runCharacterChunks( uint32 chunkStart, uint32 chunkEnd );

    private:
        PhysicsSettings                       _settings;
        JoltBroadPhaseLayers                  _broadPhaseLayers;
        JoltObjectVsBroadPhaseFilter          _objectVsBroadPhaseFilter;
        JoltObjectLayerPairFilter             _objectLayerPairFilter;
        JoltContactListener                   _contactListener; ///< `_system` 에 등록한 접촉 콜백이다(`_system` 보다 먼저 생기고 나중에 사라진다)
        JoltTempAllocator                     _tempAllocator;   ///< 스텝 · 캐릭터 · 소프트 바디가 나눠 쓴다(넘치면 힙 — `_bTempFallbackWarned`)
        JPH::PhysicsSystem                    _system;
        JoltJobSystem*                        _pJobSystem;
        vector<JPH::Ref<JoltPhysicsMaterial>> _listJoltMaterial; ///< `_settings._listMaterial` 과 같은 순서다.
        SlotHandleTable<BodyRecord>           _bodies;
        SlotHandleTable<JointRecord>          _joints;
        /// 캐릭터끼리 막는다(사본을 본다 — 청크가 함께 읽는다). 아래 둘은 `_characters` 보다 먼저 선언해 나중에 사라진다.
        JoltCharacterSnapshotCollision            _characterSnapshot;
        JoltCharacterContactListener              _characterContactListener;
        SlotHandleTable<CharacterRecord>          _characters;
        vector<unique_ptr<JoltTempAllocator>>     _listCharacterChunkAllocator; ///< 캐릭터 이동 청크마다 하나(`PhysicsSettings::_characterChunkTempBytes`, 처음 쓰는 청크 수까지 늘린다)
        vector<CharacterMoveJob>                  _listCharacterMoveJob;        ///< 지금 일괄 이동의 칸(`moveCharacters` 동안만 뜻이 있다 — 청크가 읽기만 한다)
        float32                                   _characterMoveDeltaTime;      ///< 지금 일괄 이동의 스텝 길이(청크가 읽는다)
        uint32                                    _characterMoveChunkCount;     ///< 지금 일괄 이동의 청크 수(청크가 읽는다)
        uint32                                    _characterMoveStamp;          ///< 일괄 이동마다 하나 오르는 번호(0 은 쓰지 않는다)
        JoltVehicleSet                            _vehicles;                    ///< 바퀴 차(`_system` 뒤에 선언 — 먼저 사라진다)
        JoltSoftBodySet                           _softBodies;                  ///< 소프트 바디(`_system` · `_tempAllocator` 뒤에 선언)
        SlotHandleTable<ShapeRecord>              _shapes;
        vector<BodyIDEntry>                       _listBodyIDEntry;
        PhysicsPairFilter                         _pairFilter;
        PhysicsContactTracker<PhysicsDimension3D> _contactTracker;
        vector<PhysicsContactEvent3D>             _listContactEvent;
        mutable mutex                             _reportMutex;
        vector<ContactReport> _listContactReport  SW_GUARDED_BY( _reportMutex ); ///< 스텝 중 잡 스레드가 붙이고 스텝 뒤 게임 스레드가 비운다
        vector<PhysicsBodyHandle>                 _listMovedBody;                ///< 마지막 스텝 앞 · 뒤에 깨어 있던 바디(`getMovedBodies`, 핸들 순)
        JPH::BodyIDVector                         _listActiveBodyID;             ///< `appendActiveBodies` 가 다시 쓰는 버퍼(할당을 재사용한다)
        uint32                                    _bodyCount;
        uint8                                     _bTempFallbackWarned : 1; ///< 임시 블록이 넘쳐 힙으로 간 것을 이 씬에서 이미 경고했다(씬마다 한 번)
    };
} // namespace sw
