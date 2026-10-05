/**
 * @file RigidBody2DComponent.h
 * @brief 2D 강체 하나(XY 평면, Z 축 둘레 회전) — 바디 종류 · 셰이프(여럿이면 컴파운드) · 레이어 · 재질 · 질량을 데이터로 들고, 바디를 이 컴포넌트의 월드 자세와 맞춥니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsAsset.h"
#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsShape.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class RigidBody2DComponent
     * @brief 2D 강체입니다(유니티 Rigidbody2D + Collider2D). 셰이프는 이 컴포넌트의 속성입니다 — 둘 이상이면 컴파운드. 3D 판(`RigidBodyComponent`)과 규칙이 같고
     *        자세만 2D 입니다 — 월드 XY 와 Z 축 둘레 각을 바디로 옮기고, 쓸 때는 Z 자리와 다른 축 회전을 그대로 둡니다.
     * @details 바디는 이 컴포넌트의 월드 자세를 따릅니다(붙은 자식도 함께 움직인다 — 보통 오브젝트의 루트에 둔다).
     *          - **Dynamic**: 물리가 움직이고 보간한 자세를 트랜스폼에 씁니다. 코드가 트랜스폼을 옮기면(`setWorldPosition` · `teleportTo`) 순간이동입니다.
     *          - **Kinematic**: 트랜스폼이 원본입니다 — 프레임의 새 자세로 스텝마다 나눠 움직이고(`moveKinematic`), 밀린 동적 바디가 그 속도를 받습니다.
     *          - **Static**: 움직이지 않습니다. 트랜스폼을 옮기면 순간이동합니다.
     *          셰이프 크기는 바디를 만들 때의 월드 배율을 받습니다(그 뒤의 배율 변화는 속성을 바꿔야 다시 짓는다).
     *
     *          힘 · 충격량 · 속도 설정은 어느 틱에서든 부를 수 있습니다 — 다음 물리 프레임에 게임 스레드가 바디에 줍니다(`PhysicsBodyCommand`).
     */
    REFLECT( Category = "Physics 2D", DisplayName = "Rigid Body 2D", Tooltip = "2D rigid body with its collision shapes" )
    class SW_API RigidBody2DComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        RigidBody2DComponent();
        ~RigidBody2DComponent() override = default;

        /** @brief 바디 핸들입니다(시작 전 · 꺼짐이면 무효). 질의 결과 · 이벤트의 바디와 견줍니다. */
        PhysicsBodyHandle getBodyHandle() const { return _body; }
        /** @brief 바디가 사는 2D 씬입니다(없으면 nullptr). */
        IPhysicsScene2D* findScene() const;

        PhysicsBodyType getBodyType() const { return _bodyType; }
        /** @brief 바디 종류를 바꿉니다. 시작했으면 다음 물리 프레임에 바디에도 듭니다. */
        void setBodyType( PhysicsBodyType type );

        const vector<PhysicsShapeDesc2D>& getShapes() const { return _listShape; }
        /** @brief 셰이프를 바꿉니다(바디를 다시 짓는다). */
        void setShapes( const vector<PhysicsShapeDesc2D>& listShape );
        /** @brief 셰이프 하나로 바꿉니다. */
        void setShape( const PhysicsShapeDesc2D& shape );

        const hashed_string& getLayer() const { return _layer; }
        /** @brief 충돌 레이어 이름을 바꿉니다(물리 설정 표의 이름). */
        void                 setLayer( const hashed_string& layer );
        const hashed_string& getMaterial() const { return _material; }
        void                 setMaterial( const hashed_string& material );
        /** @brief 무기 판정이 이 바디를 맞혔을 때의 히트 존(이름 · 피해 배율)입니다(`CharacterHitUtil::resolveHitZone`). */
        const PhysicsHitZoneDef& getHitZone() const { return _hitZone; }
        void                     setHitZone( const PhysicsHitZoneDef& hitZone ) { _hitZone = hitZone; }
        float32                  getMass() const { return _mass; }
        /** @brief 질량(kg)입니다. 0 이면 셰이프 부피 × 재질 밀도입니다. */
        void    setMass( float32 mass );
        bool    isTrigger() const { return _bTrigger; }
        void    setTrigger( bool bTrigger );
        bool    isContinuous() const { return _bContinuous; }
        void    setContinuous( bool bContinuous );
        float32 getGravityFactor() const { return _gravityFactor; }
        void    setGravityFactor( float32 factor );

        /** @brief 이번 프레임 동안 힘을 줍니다(무게 중심, 뉴턴). 프레임의 스텝마다 듭니다. */
        void addForce( const float2& force );
        /** @brief 충격량을 줍니다(무게 중심, 뉴턴초). 다음 스텝에 한 번 듭니다. */
        void addImpulse( const float2& impulse );
        /** @brief 이번 프레임 동안 토크를 줍니다. */
        void addTorque( float32 torque );
        /** @brief 선속도를 정합니다(다음 물리 프레임에). */
        void setLinearVelocity( const float2& velocity );
        /** @brief 각속도를 정합니다. */
        void setAngularVelocity( float32 velocity );
        /** @brief 잠든 바디를 깨웁니다. */
        void wake();
        /** @brief 마지막 스텝 뒤의 선속도입니다. */
        float2 getLinearVelocity() const { return _lastLinearVelocity; }
        /** @brief 마지막 스텝 뒤의 각속도입니다. */
        float32 getAngularVelocity() const { return _lastAngularVelocity; }
        /** @brief 바디의 질량(kg)입니다(바디가 없으면 0). */
        float32 getBodyMass() const;
        /** @brief 바디가 잠들었는지입니다. */
        bool isSleeping() const;

    protected:
        void beginPhysicsFrame( ScenePhysics& physics ) override;
        void prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount ) override;
        void postPhysicsStep( ScenePhysics& physics ) override;
        void endPhysicsFrame( ScenePhysics& physics, float32 alpha ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        /** @brief 지금 속성 · 자세로 바디를 만듭니다. */
        void createBody( ScenePhysics& physics, IPhysicsScene2D& scene, const float2& position, float32 angle, const float3& scale );

        PROPERTY( Category = "Body", Tooltip = "Static never moves, Kinematic follows the transform, Dynamic is simulated" )
        PhysicsBodyType _bodyType;
        PROPERTY( Category = "Body", Tooltip = "Collision shapes; more than one makes a compound body" )
        vector<PhysicsShapeDesc2D> _listShape;
        PROPERTY( Category = "Body", Tooltip = "Collision layer name from the physics settings" )
        hashed_string _layer;
        PROPERTY( Category = "Body", Tooltip = "Physics material name (empty: the first material)" )
        hashed_string _material;
        PROPERTY( Category = "Body", Tooltip = "Hit zone of this body for weapon traces (name and damage multiplier); empty name is no zone" )
        PhysicsHitZoneDef _hitZone;
        PROPERTY( Category = "Body", Min = 0.0, Tooltip = "Mass in kg; 0 computes it from the shapes and material density", Units = kg )
        float32 _mass;
        PROPERTY( Category = "Body", Min = 0.0, Tooltip = "Linear velocity damping" )
        float32 _linearDamping;
        PROPERTY( Category = "Body", Min = 0.0, Tooltip = "Angular velocity damping" )
        float32 _angularDamping;
        PROPERTY( Category = "Body", Tooltip = "Multiplier on gravity (0 floats)" )
        float32 _gravityFactor;
        PROPERTY( Category = "Body", Tooltip = "Detects overlaps without blocking" )
        bool _bTrigger;
        PROPERTY( Category = "Body", Tooltip = "Continuous collision for fast bodies" )
        bool _bContinuous;
        PROPERTY( Category = "Body", Tooltip = "Never rotates (platformer characters)" )
        bool _bLockRotation;
        PROPERTY( Category = "Body", Tooltip = "Draw the pose interpolated between the last two physics steps" )
        bool _bInterpolate;

        PhysicsBodyHandle                      _body;
        PhysicsBodyCommand<PhysicsDimension2D> _command;
        mutable SpinLock                       _commandLock;
        float2                                 _previousPosition;
        float2                                 _currentPosition;
        float2                                 _kinematicStartPosition;
        float2                                 _kinematicTargetPosition;
        float2                                 _lastLinearVelocity;
        float3                                 _writtenPosition; ///< 쓴 월드 자리(Z 포함) — 바깥 변화 비교용
        quaternion                             _writtenRotation;
        float32                                _previousAngle;
        float32                                _currentAngle;
        float32                                _kinematicStartAngle;
        float32                                _kinematicTargetAngle;
        float32                                _lastAngularVelocity;
        PhysicsBodyType                        _appliedBodyType; ///< 바디에 든 종류(`_bodyType` 이 바뀌면 다음 프레임에 맞춘다)
        bool                                   _bHasWritten;     ///< `_written*` 이 유효한지
    };
} // namespace sw
