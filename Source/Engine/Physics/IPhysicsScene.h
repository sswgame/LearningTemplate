/**
 * @file IPhysicsScene.h
 * @brief 물리 씬 인터페이스 — 바디 · 셰이프 · 관절 · 캐릭터 · 질의 · 고정 스텝 · 접촉 이벤트를 핸들로 다룹니다(`IPhysicsScene3D` · `IPhysicsScene2D`).
 * @details 엔진 코드는 이 인터페이스만 봅니다. 구현(Jolt · Box2D)은 백엔드 폴더(`Physics/Jolt` · `Physics/Box2D`) 안에 있고 라이브러리 헤더는 거기서만
 *          include 합니다(`CheckThirdPartyIsolation.py`). 백엔드를 바꾸면 이 인터페이스를 구현하는 씬 하나와 `PhysicsSystem` 의 생성 한 줄을 바꿉니다
 *          (Godot PhysicsServer3D 와 같은 자리).
 *
 *          **스레드.** 씬 하나는 한 스레드(게임 스레드)에서 다룹니다. `step` 안에서만 백엔드가 잡을 나눠 돌립니다. step 중에 다른 스레드가 바디를
 *          만들거나 지우면 안 됩니다.
 *
 *          **핸들.** 무효 · 지워진 핸들을 받은 함수는 아무것도 하지 않고 false · 0 · 기본값을 돌려줍니다(세대가 다르면 지워진 것이다).
 *
 *          **단위.** 미터 · 킬로그램 · 초 · 라디안. 3D 는 +Y 가 위, 2D 는 XY 평면에서 각은 Z 축 둘레(반시계가 +)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Physics/CollisionLayers.h"
#include "Engine/Physics/PhysicsContact.h"
#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsQuery.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    class IPhysicsDebugRenderer;

    /** @brief 물리 씬 하나입니다. 차원 묶음(`PhysicsDimension3D` · `PhysicsDimension2D`)만 다르고 함수는 같습니다. */
    template <typename TDimension>
    class IPhysicsScene
    {
    public:
        using Vector         = typename TDimension::Vector;
        using Rotation       = typename TDimension::Rotation;
        using Angular        = typename TDimension::Angular;
        using ShapeDesc      = typename TDimension::ShapeDesc;
        using BodyDesc       = PhysicsBodyDesc<TDimension>;
        using JointDesc      = PhysicsJointDesc<TDimension>;
        using CharacterDesc  = PhysicsCharacterDesc<TDimension>;
        using CharacterState = PhysicsCharacterState<TDimension>;
        using CastHit        = PhysicsCastHit<TDimension>;
        using ContactEvent   = PhysicsContactEvent<TDimension>;

        virtual ~IPhysicsScene() = default;

        // --- 시뮬레이션 ---------------------------------------------------------------------------------------------------

        /**
         * @brief 고정 스텝 하나를 진행합니다. 접촉 이벤트는 이 스텝의 것으로 바뀝니다(`getContactEvents`).
         * @details 가변 프레임 시간은 부르는 쪽이 `FixedStepAccumulator` 로 고정 스텝 수로 바꿉니다 — 같은 스텝 크기 · 같은 입력이면 같은 결과입니다.
         */
        virtual void step( float32 fixedDeltaTime ) = 0;
        /** @brief 마지막 `step` 이 낸 접촉 · 트리거 이벤트입니다(결정적 순서, `PhysicsContactTracker`). 다음 `step` 까지 그대로입니다. */
        virtual const vector<ContactEvent>& getContactEvents() const            = 0;
        virtual void                        setGravity( const Vector& gravity ) = 0;
        virtual Vector                      getGravity() const                  = 0;
        /** @brief 레이어끼리의 충돌 행렬을 바꿉니다(`PhysicsSettings::makeCollisionLayers`). 이미 있는 바디에도 다음 스텝부터 듭니다. */
        virtual void setLayerCollision( const CollisionLayers& layers ) = 0;
        /** @brief 바디마다 셰이프를 선으로 냅니다(`PhysicsDebugDrawUtil`). */
        virtual void drawDebug( IPhysicsDebugRenderer& renderer ) const = 0;

        // --- 셰이프 -----------------------------------------------------------------------------------------------------

        /**
         * @brief 셰이프 묶음을 미리 지어 둡니다(둘 이상이면 컴파운드). 바디 서술자의 `_sharedShape` 로 여러 바디가 나눠 씁니다.
         * @details 같은 모양의 바디를 대량으로 만들 때(파편 · 탄피) 볼록 껍질 · 메시를 바디마다 다시 짓지 않게 합니다. 지운 뒤에도 그것을 쓰는
         *          바디는 그대로 삽니다(백엔드가 참조를 센다). @p material 은 셰이프가 재질을 적지 않았을 때의 재질 이름입니다.
         */
        virtual PhysicsShapeHandle createShape( span<const ShapeDesc> listShape, const hashed_string& material ) = 0;
        /**
         * @brief 이미 지은 셰이프 여럿을 바디 원점에 그대로 묶은 컴파운드를 만듭니다(자식의 로컬 자리 · 재질은 자식 것).
         * @details 자식의 볼록 껍질을 다시 짓지 않습니다 — 파괴 조각처럼 잎 셰이프를 미리 지어 두고 갈라지는 덩어리마다 묶을 때 씁니다.
         *          무효 · 지운 자식이 있으면 오류를 남기고 무효 핸들입니다. 자식을 지워도 컴파운드는 그대로 삽니다.
         */
        virtual PhysicsShapeHandle createCompoundShape( span<const PhysicsShapeHandle> listChild ) = 0;
        virtual void               destroyShape( PhysicsShapeHandle shape )                        = 0;

        // --- 바디 -------------------------------------------------------------------------------------------------------

        /** @brief 바디 하나를 만들어 시뮬레이션에 넣습니다. 셰이프가 없거나 만들 수 없으면 오류를 남기고 무효 핸들입니다. */
        virtual PhysicsBodyHandle createBody( const BodyDesc& desc ) = 0;
        /**
         * @brief 바디 여럿을 한 번에 만듭니다. @p outListBody 에 서술자 순서대로 핸들을 붙입니다(실패한 자리는 무효 핸들).
         * @details 백엔드가 묶어서 넣습니다(Jolt `AddBodiesPrepare/Finalize` — 넓은 단계 트리를 한 번만 고친다). 파괴 · 래그돌처럼 수십 ~ 수천 개를 한
         *          프레임에 만들 때 씁니다.
         */
        virtual void createBodies( span<const BodyDesc> listDesc, vector<PhysicsBodyHandle>& outListBody ) = 0;
        virtual void destroyBody( PhysicsBodyHandle body )                                                 = 0;
        /** @brief 바디 여럿을 한 번에 지웁니다. 그 바디들의 접촉은 다음 이벤트 묶음에서 끝납니다. 붙은 관절도 함께 지웁니다. */
        virtual void   destroyBodies( span<const PhysicsBodyHandle> listBody ) = 0;
        virtual bool   isBodyValid( PhysicsBodyHandle body ) const             = 0;
        virtual uint32 getBodyCount() const                                    = 0;
        /** @brief 바디를 시뮬레이션에서 빼거나 다시 넣습니다(지우지 않는다 — 풀에 넣어 둔 파편을 다시 쓸 때). 뺀 바디는 질의 · 접촉에 들지 않습니다. */
        virtual void setBodyEnabled( PhysicsBodyHandle body, bool bEnabled ) = 0;
        virtual bool isBodyEnabled( PhysicsBodyHandle body ) const           = 0;

        virtual bool getBodyTransform( PhysicsBodyHandle body, Vector& outPosition, Rotation& outRotation ) const = 0;
        /** @brief 바디를 그 자리로 옮깁니다(순간이동 — 그 사이를 쓸지 않고 깨운다). */
        virtual void setBodyTransform( PhysicsBodyHandle body, const Vector& position, const Rotation& rotation ) = 0;
        /** @brief 키네마틱 바디가 @p deltaTime 뒤에 목표 자세에 닿도록 속도를 줍니다(밀린 동적 바디가 그 속도를 받는다). */
        virtual void moveKinematic( PhysicsBodyHandle body, const Vector& targetPosition, const Rotation& targetRotation, float32 deltaTime ) = 0;

        virtual Vector  getLinearVelocity( PhysicsBodyHandle body ) const                     = 0;
        virtual void    setLinearVelocity( PhysicsBodyHandle body, const Vector& velocity )   = 0;
        virtual Angular getAngularVelocity( PhysicsBodyHandle body ) const                    = 0;
        virtual void    setAngularVelocity( PhysicsBodyHandle body, const Angular& velocity ) = 0;
        /** @brief 다음 스텝 동안 힘을 줍니다(무게 중심, 뉴턴). */
        virtual void addForce( PhysicsBodyHandle body, const Vector& force ) = 0;
        /** @brief 충격량을 바로 줍니다(무게 중심, 뉴턴초). */
        virtual void addImpulse( PhysicsBodyHandle body, const Vector& impulse ) = 0;
        /** @brief 충격량을 월드 점 @p point 에 줍니다(회전도 생긴다). */
        virtual void addImpulseAtPoint( PhysicsBodyHandle body, const Vector& impulse, const Vector& point ) = 0;
        virtual void addTorque( PhysicsBodyHandle body, const Angular& torque )                              = 0;

        virtual void            setBodyType( PhysicsBodyHandle body, PhysicsBodyType type ) = 0;
        virtual PhysicsBodyType getBodyType( PhysicsBodyHandle body ) const                 = 0;
        virtual void            setBodyLayer( PhysicsBodyHandle body, uint8 layer )         = 0;
        virtual uint8           getBodyLayer( PhysicsBodyHandle body ) const                = 0;
        virtual void            setGravityFactor( PhysicsBodyHandle body, float32 factor )  = 0;
        virtual float32         getBodyMass( PhysicsBodyHandle body ) const                 = 0;
        virtual bool            isBodySleeping( PhysicsBodyHandle body ) const              = 0;
        virtual void            wakeBody( PhysicsBodyHandle body )                          = 0;
        virtual uint64          getBodyUserData( PhysicsBodyHandle body ) const             = 0;
        /** @brief 두 바디의 충돌을 켜거나 끕니다(`PhysicsPairFilter`). 레이어 표보다 좁은 예외입니다. */
        virtual void setPairCollision( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, bool bCollide ) = 0;

        // --- 관절 -------------------------------------------------------------------------------------------------------

        /** @brief 관절을 만듭니다. 바디가 무효이거나 차원이 지원하지 않는 종류(2D Cone)면 오류를 남기고 무효 핸들입니다. */
        virtual PhysicsJointHandle createJoint( const JointDesc& desc )           = 0;
        virtual void               destroyJoint( PhysicsJointHandle joint )       = 0;
        virtual bool               isJointValid( PhysicsJointHandle joint ) const = 0;
        /** @brief 모터를 바꿉니다(Hinge · Slider · Distance). */
        virtual void setJointMotor( PhysicsJointHandle joint, const PhysicsJointMotor& motor ) = 0;
        /** @brief Hinge 면 지금 각(라디안), Slider 면 지금 거리, Distance 면 지금 길이입니다. 나머지는 0 입니다. */
        virtual float32 getJointPosition( PhysicsJointHandle joint ) const = 0;

        // --- 캐릭터 -----------------------------------------------------------------------------------------------------

        virtual PhysicsCharacterHandle createCharacter( const CharacterDesc& desc )         = 0;
        virtual void                   destroyCharacter( PhysicsCharacterHandle character ) = 0;
        /**
         * @brief 캐릭터를 @p velocity 로 @p deltaTime 만큼 움직입니다 — 벽에 미끄러지고, 턱(`_stepHeight`)을 오르고, 가파른 비탈(`_maxSlopeAngle`)은
         *        벽처럼 막습니다. 중력은 넣지 않습니다(부르는 쪽이 속도에 더한다). 바로 움직입니다(스텝을 기다리지 않는다).
         */
        virtual CharacterState moveCharacter( PhysicsCharacterHandle character, const Vector& velocity, float32 deltaTime ) = 0;
        virtual void           setCharacterPosition( PhysicsCharacterHandle character, const Vector& position )             = 0;
        virtual bool           getCharacterState( PhysicsCharacterHandle character, CharacterState& outState ) const        = 0;

        // --- 질의 -------------------------------------------------------------------------------------------------------

        /** @brief @p origin 에서 @p direction(정규화)으로 @p maxDistance 까지 처음 닿는 바디입니다. */
        virtual bool raycast( const Vector& origin, const Vector& direction, float32 maxDistance, const PhysicsQueryFilter& filter, CastHit& outHit ) const = 0;
        /** @brief 셰이프를 @p position · @p rotation 에서 @p direction 으로 @p maxDistance 까지 쓸어 처음 닿는 바디입니다. */
        virtual bool shapeCast( const ShapeDesc& shape, const Vector& position, const Rotation& rotation, const Vector& direction, float32 maxDistance,
                                const PhysicsQueryFilter& filter, CastHit& outHit ) const = 0;
        /** @brief 셰이프와 겹치는 바디들을 @p outListBody 에 붙이고 그 수를 돌려줍니다(같은 바디는 한 번). */
        virtual uint32 overlapShape( const ShapeDesc& shape, const Vector& position, const Rotation& rotation, const PhysicsQueryFilter& filter,
                                     vector<PhysicsBodyHandle>& outListBody ) const = 0;
    };

    using IPhysicsScene3D = IPhysicsScene<PhysicsDimension3D>;
    using IPhysicsScene2D = IPhysicsScene<PhysicsDimension2D>;
} // namespace sw
