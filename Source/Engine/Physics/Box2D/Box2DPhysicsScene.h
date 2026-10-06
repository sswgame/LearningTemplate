/**
 * @file Box2DPhysicsScene.h
 * @brief `IPhysicsScene2D` 의 Box2D v3 구현입니다. Box2D 헤더를 include 하므로 `Physics/Box2D` 밖에서는 보지 않습니다.
 * @details
 *  - **레이어.** 엔진 레이어 n 은 셰이프 거름의 범주 비트 1 << n 이고, 마스크는 레이어 표의 그 행입니다. 표를 바꾸면 모든 셰이프의 거름을 다시 씁니다.
 *  - **쌍 예외.** 두 바디의 충돌을 끄는 것은 Box2D 의 필터 관절(`b2CreateFilterJoint`)입니다. 관절로 이은 두 바디는 관절의 `collideConnected` 가 막습니다.
 *  - **이벤트.** 접촉 시작 · 끝(`b2World_GetContactEvents`)과 센서 시작 · 끝(`b2World_GetSensorEvents`)을 셰이프 → 바디 표로 풀어 추적기에 넣습니다.
 *    충격량 — 시작은 부딪힘 이벤트(`b2ContactHitEvent`, 다가온 속도가 1 m/s 를 넘을 때)의 속도 × 유효 질량 × (1 + 반발)입니다. Box2D 의 시작은
 *    투기적 접촉(아직 떨어져 있다)에서도 나고 그 매니폴드는 풀기 전이라 매니폴드로는 부딪힘을 잴 수 없다. 유지는 그 스텝에 푼 접촉 충격량
 *    (마지막 서브 스텝의 `normalImpulse` × 서브 스텝 수 — `totalNormalImpulse` 는 이완 반복까지 더해 약 두 배다)입니다.
 *  - **월드에 붙는 관절.** 바디 B 가 없으면 씬이 드는 정적 바디(원점) 하나에 붙입니다.
 *  - **위치 모터.** Box2D 의 관절 스프링(목표 각 · 거리)으로 합니다. 속도 모터는 Box2D 모터 그대로입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandleTable.h"
#include "Core/Container/map.h"
#include "Core/Container/pair.h"
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsContact.h"
#include "Engine/Physics/PhysicsSettings.h"

#include <box2d/box2d.h>

namespace sw
{
    /** @class Box2DPhysicsScene @brief Box2D 로 도는 2D 물리 씬입니다. 파일 머리말 참고. */
    class Box2DPhysicsScene final : public IPhysicsScene2D
    {
    public:
        explicit Box2DPhysicsScene( const PhysicsSettings& settings );
        ~Box2DPhysicsScene() override;

        Box2DPhysicsScene( const Box2DPhysicsScene& )            = delete;
        Box2DPhysicsScene& operator=( const Box2DPhysicsScene& ) = delete;

        void                                 step( float32 fixedDeltaTime ) override;
        const vector<PhysicsContactEvent2D>& getContactEvents() const override { return _listContactEvent; }
        void                                 setGravity( const float2& gravity ) override;
        float2                               getGravity() const override;
        void                                 setLayerCollision( const CollisionLayers& layers ) override;
        void                                 drawDebug( IPhysicsDebugRenderer& renderer ) const override;

        PhysicsShapeHandle createShape( span<const PhysicsShapeDesc2D> listShape, const hashed_string& material ) override;
        PhysicsShapeHandle createCompoundShape( span<const PhysicsShapeHandle> listChild ) override;
        void               destroyShape( PhysicsShapeHandle shape ) override;

        PhysicsBodyHandle createBody( const PhysicsBodyDesc2D& desc ) override;
        void              createBodies( span<const PhysicsBodyDesc2D> listDesc, vector<PhysicsBodyHandle>& outListBody ) override;
        void              destroyBody( PhysicsBodyHandle body ) override;
        void              destroyBodies( span<const PhysicsBodyHandle> listBody ) override;
        bool              isBodyValid( PhysicsBodyHandle body ) const override;
        uint32            getBodyCount() const override;
        void              setBodyEnabled( PhysicsBodyHandle body, bool bEnabled ) override;
        bool              isBodyEnabled( PhysicsBodyHandle body ) const override;

        bool getBodyTransform( PhysicsBodyHandle body, float2& outPosition, float32& outRotation ) const override;
        void setBodyTransform( PhysicsBodyHandle body, const float2& position, const float32& rotation ) override;
        void moveKinematic( PhysicsBodyHandle body, const float2& targetPosition, const float32& targetRotation, float32 deltaTime ) override;

        float2  getLinearVelocity( PhysicsBodyHandle body ) const override;
        void    setLinearVelocity( PhysicsBodyHandle body, const float2& velocity ) override;
        float32 getAngularVelocity( PhysicsBodyHandle body ) const override;
        void    setAngularVelocity( PhysicsBodyHandle body, const float32& velocity ) override;
        void    addForce( PhysicsBodyHandle body, const float2& force ) override;
        void    addImpulse( PhysicsBodyHandle body, const float2& impulse ) override;
        void    addImpulseAtPoint( PhysicsBodyHandle body, const float2& impulse, const float2& point ) override;
        void    addTorque( PhysicsBodyHandle body, const float32& torque ) override;

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

        PhysicsJointHandle createJoint( const PhysicsJointDesc2D& desc ) override;
        void               destroyJoint( PhysicsJointHandle joint ) override;
        bool               isJointValid( PhysicsJointHandle joint ) const override;
        void               setJointMotor( PhysicsJointHandle joint, const PhysicsJointMotor& motor ) override;
        float32            getJointPosition( PhysicsJointHandle joint ) const override;

        PhysicsCharacterHandle  createCharacter( const PhysicsCharacterDesc2D& desc ) override;
        void                    destroyCharacter( PhysicsCharacterHandle character ) override;
        PhysicsCharacterState2D moveCharacter( PhysicsCharacterHandle character, const float2& velocity, float32 deltaTime ) override;
        void                    setCharacterPosition( PhysicsCharacterHandle character, const float2& position ) override;
        bool                    getCharacterState( PhysicsCharacterHandle character, PhysicsCharacterState2D& outState ) const override;

        /** @brief 2D 물리에는 바퀴 차가 없습니다 — 무효 핸들 · false 입니다. */
        PhysicsVehicleHandle createWheeledVehicle( PhysicsBodyHandle chassis, const PhysicsWheeledVehicleDesc& desc ) override;
        void                 destroyVehicle( PhysicsVehicleHandle vehicle ) override { (void)vehicle; }
        bool                 isVehicleValid( PhysicsVehicleHandle vehicle ) const override
        {
            (void)vehicle;
            return false;
        }
        void setVehicleInput( PhysicsVehicleHandle vehicle, float32 forward, float32 right, float32 brake, float32 handBrake ) override;
        bool getVehicleState( PhysicsVehicleHandle vehicle, PhysicsVehicleState& outState ) const override;

        bool   raycast( const float2& origin, const float2& direction, float32 maxDistance, const PhysicsQueryFilter& filter, PhysicsCastHit2D& outHit ) const override;
        bool   shapeCast( const PhysicsShapeDesc2D& shape, const float2& position, const float32& rotation, const float2& direction, float32 maxDistance,
                          const PhysicsQueryFilter& filter, PhysicsCastHit2D& outHit ) const override;
        uint32 overlapShape( const PhysicsShapeDesc2D& shape, const float2& position, const float32& rotation, const PhysicsQueryFilter& filter,
                             vector<PhysicsBodyHandle>& outListBody ) const override;

    private:
        using ShapeDescList = vector<PhysicsShapeDesc2D>;

        struct BodyRecord
        {
            shared_ptr<const ShapeDescList> _pListShape;
            vector<b2ShapeId>               _listShapeId;
            vector<b2ChainId>               _listChainId;
            uint64                          _userData{ 0 };
            b2BodyId                        _bodyId{};
            PhysicsBodyType                 _type{ PhysicsBodyType::Dynamic };
            uint8                           _layer{ 0 };
            bool                            _bTrigger{ false };
            bool                            _bEnabled{ true };
        };

        struct JointRecord
        {
            b2JointId         _jointId{};
            PhysicsBodyHandle _bodyA{};
            PhysicsBodyHandle _bodyB{};
            PhysicsJointType  _type{ PhysicsJointType::Fixed };
        };

        struct CharacterRecord
        {
            PhysicsCharacterDesc2D  _desc{};
            PhysicsCharacterState2D _state{};
        };

        struct ShapeRecord
        {
            shared_ptr<const ShapeDescList> _pListShape;
            hashed_string                   _material{};
        };

        /** @brief 셰이프 하나를 바디에 붙입니다. 셰이프 id(체인이면 선분들)를 기록에 더합니다. */
        [[nodiscard]] bool        attachShape( BodyRecord& record, const PhysicsShapeDesc2D& shape, const hashed_string& defaultMaterial, PhysicsBodyHandle handle );
        const PhysicsMaterialDef& resolveMaterial( const hashed_string& name, const hashed_string& fallback ) const;
        /** @brief 재질 이름의 설정 표 번호입니다(셰이프의 `userMaterialId` — 질의가 이름으로 되돌린다). 없으면 0 입니다. */
        int32 findMaterialIndex( const hashed_string& name ) const;
        /** @brief 설정 표 번호의 재질 이름입니다. 범위 밖이면 빈 이름입니다. */
        hashed_string     findMaterialName( int32 materialIndex ) const;
        b2Filter          makeFilter( uint8 layer ) const;
        void              applyFilter( const BodyRecord& record );
        const BodyRecord* findBody( PhysicsBodyHandle body ) const;
        BodyRecord*       findBody( PhysicsBodyHandle body );
        PhysicsBodyHandle findHandleOfShape( b2ShapeId shapeId ) const;
        /** @brief 쌍 예외 필터 관절 표에서 @p body 가 낀 것을 지웁니다(바디를 지우면 Box2D 가 관절도 지운다). */
        void forgetPairJointsOf( PhysicsBodyHandle body );
        /** @brief 스텝이 낸 Box2D 이벤트를 추적기에 넣고 충격량을 채웁니다. */
        void flushEvents();
        /** @brief 두 바디 사이 접촉이 이번 스텝에 받은 법선 충격량입니다(마지막 서브 스텝 × 서브 스텝 수). */
        float32 computePairImpulse( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB ) const;
        /** @brief 무버 캡슐(월드)입니다. */
        b2Capsule makeMoverCapsule( const CharacterRecord& record, const float2& position ) const;

        PhysicsSettings                           _settings;
        CollisionLayers                           _layers;
        b2WorldId                                 _worldId;
        b2BodyId                                  _groundBodyId; ///< 월드에 붙는 관절의 상대(정적, 원점)
        SlotHandleTable<BodyRecord>               _bodies;
        SlotHandleTable<JointRecord>              _joints;
        SlotHandleTable<CharacterRecord>          _characters;
        SlotHandleTable<ShapeRecord>              _shapes;
        unordered_map<uint64, PhysicsBodyHandle>  _mapShapeToBody;     ///< 셰이프 id(색인 | 세대 << 32) → 바디. 지워진 셰이프의 끝 이벤트도 푼다
        map<pair<uint64, uint64>, b2JointId>      _mapPairToJoint;     ///< 충돌을 끈 바디 쌍(작은 핸들 먼저) → 필터 관절
        mutable vector<b2ContactData>             _listContactScratch; ///< 충격량을 읽을 때 쓰는 자리(할당 재사용)
        PhysicsContactTracker<PhysicsDimension2D> _contactTracker;
        vector<PhysicsContactEvent2D>             _listContactEvent;
        uint32                                    _bodyCount;
    };
} // namespace sw
