/**
 * @file IPhysicsBodyScene.h
 * @brief 물리 씬의 셰이프와 바디 쪽 인터페이스입니다(`IPhysicsBodyScene3D`, `IPhysicsBodyScene2D`).
 * @details 바디를 만들고, 옮기고, 힘을 주는 코드(강체 컴포넌트, 파괴, 래그돌)는 씬 전체가 아니라 이 인터페이스만 받는다.
 *          언리얼 Chaos 의 `FPhysicsInterface`(액터 · 바디 인스턴스), 유니티의 `Rigidbody`, Jolt 의 `BodyInterface` 에 해당한다.
 *          핸들 · 스레드 · 단위 규칙은 `IPhysicsScene.h` 머리말과 같다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    class IPhysicsDebugRenderer;

    /** @brief 셰이프와 바디를 다룹니다. 차원 트레이트(`PhysicsDimension3D`, `PhysicsDimension2D`)만 다르고 함수는 같습니다. */
    template <typename TDimension>
    class IPhysicsBodyScene
    {
    public:
        using Vector    = typename TDimension::Vector;
        using Rotation  = typename TDimension::Rotation;
        using Angular   = typename TDimension::Angular;
        using ShapeDesc = typename TDimension::ShapeDesc;
        using BodyDesc  = PhysicsBodyDesc<TDimension>;

        virtual ~IPhysicsBodyScene() = default;

        // --- 셰이프 -----------------------------------------------------------------------------------------------------

        /**
         * @brief 셰이프 여럿을 미리 만들어 둡니다(둘 이상이면 컴파운드). 바디 서술자의 `_sharedShape` 로 여러 바디가 나눠 씁니다.
         * @details 같은 형태의 바디를 대량으로 만들 때(파편, 탄피) 볼록 껍질과 메시를 바디마다 다시 만들지 않게 한다. 셰이프를 지운 뒤에도 그것을 쓰는
         *          바디는 그대로 살아 있다(백엔드가 참조를 센다). @p material 은 셰이프가 재질을 지정하지 않았을 때의 재질 이름이다.
         */
        virtual PhysicsShapeHandle createShape( span<const ShapeDesc> listShape, const hashed_string& material ) = 0;
        /**
         * @brief 이미 만든 셰이프 여럿을 바디 원점에 그대로 묶은 컴파운드를 만듭니다(자식의 로컬 위치와 재질은 자식의 것을 씁니다).
         * @details 자식의 볼록 껍질을 다시 만들지 않는다. 파괴 조각처럼 잎 셰이프를 미리 만들어 두고, 갈라지는 덩어리마다 묶을 때 쓴다.
         *          무효이거나 지운 자식이 있으면 오류를 남기고 무효 핸들을 반환한다. 자식을 지워도 컴파운드는 그대로 살아 있다.
         */
        virtual PhysicsShapeHandle createCompoundShape( span<const PhysicsShapeHandle> listChild ) = 0;
        virtual void               destroyShape( PhysicsShapeHandle shape )                        = 0;

        // --- 바디 -------------------------------------------------------------------------------------------------------

        /** @brief 바디 하나를 만들어 시뮬레이션에 넣습니다. 셰이프가 없거나 만들 수 없으면 오류를 남기고 무효 핸들입니다. */
        virtual PhysicsBodyHandle createBody( const BodyDesc& desc ) = 0;
        /**
         * @brief 바디 여럿을 한 번에 만듭니다. @p outListBody 에 서술자 순서대로 핸들을 붙입니다(실패한 항목은 무효 핸들).
         * @details 백엔드가 묶어서 넣는다(Jolt `AddBodiesPrepare/Finalize`, 넓은 단계 트리를 한 번만 고친다). 파괴나 래그돌처럼 수십에서 수천 개를 한
         *          프레임에 만들 때 쓴다.
         */
        virtual void createBodies( span<const BodyDesc> listDesc, vector<PhysicsBodyHandle>& outListBody ) = 0;
        virtual void destroyBody( PhysicsBodyHandle body )                                                 = 0;
        /** @brief 바디 여럿을 한 번에 지웁니다. 그 바디들의 접촉은 다음 이벤트 배치에서 끝납니다. 붙은 관절도 함께 지웁니다. */
        virtual void   destroyBodies( span<const PhysicsBodyHandle> listBody ) = 0;
        virtual bool   isBodyValid( PhysicsBodyHandle body ) const             = 0;
        virtual uint32 getBodyCount() const                                    = 0;
        /** @brief 바디를 시뮬레이션에서 빼거나 다시 넣습니다. 지우지는 않으며, 풀에 넣어 둔 파편을 다시 쓸 때 씁니다. 뺀 바디는 질의와 접촉에서 제외됩니다. */
        virtual void setBodyEnabled( PhysicsBodyHandle body, bool bEnabled ) = 0;
        virtual bool isBodyEnabled( PhysicsBodyHandle body ) const           = 0;

        /**
         * @brief 마지막 `step` 동안 깨어 있던(움직였을 수 있는) 바디입니다. 핸들 순서이고 다음 `step` 까지 그대로입니다.
         * @details 스텝 앞이나 뒤에 깨어 있던 바디를 모두 든다 — 이 스텝에 잠든 바디(마지막으로 조금 움직였다)와 이 스텝에 깨어난 바디가 빠지지 않는다.
         *          정적 바디와 내내 잠든 바디는 들지 않는다. 바디 자세를 읽는 쪽(`ScenePhysics`)은 이 목록만 돌아 잠든 · 정적 바디마다 스텝마다
         *          백엔드를 부르지 않는다(Jolt `GetActiveBodies`, Box2D 바디 이동 이벤트, 유니티 · 언리얼의 활성 바디 목록과 같은 자리).
         */
        virtual const vector<PhysicsBodyHandle>& getMovedBodies() const                                                                       = 0;
        virtual bool                             getBodyTransform( PhysicsBodyHandle body, Vector& outPosition, Rotation& outRotation ) const = 0;
        /** @brief 바디를 그 위치로 옮깁니다(순간이동). 그 사이를 쓸지 않고 바디를 깨웁니다. */
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
        /** @brief 바디가 트리거(`PhysicsBodyDesc::_bTrigger`)로 만들어졌으면 true 입니다. 겹침 전용 바디(`_bOverlapOnly`)는 트리거가 아닙니다. 없는 바디는 false 입니다. */
        virtual bool isBodyTrigger( PhysicsBodyHandle body ) const = 0;
        /** @brief 두 바디의 충돌을 켜거나 끕니다(`PhysicsPairFilter`). 레이어 테이블보다 좁은 예외입니다. */
        virtual void setPairCollision( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, bool bCollide ) = 0;

        /**
         * @brief 바디 하나의 셰이프를 지금 자세로 선으로 냅니다(색은 씬 전체 `drawDebug` 와 같다). 무효 핸들이면 아무것도 내지 않고 false 입니다.
         * @details 선을 목록으로 받으려면 `PhysicsDebugLineCollector` 를 넘긴다. 시뮬레이션 전(에디터 편집 중)에는 바디가 없으므로, 컴포넌트의 셰이프 서술자와
         *          트랜스폼을 `PhysicsDebugDrawUtil::drawShape3D` · `drawShape2D` 에 바로 넘긴다 — 둘 다 같은 선을 낸다.
         */
        virtual bool drawBodyDebug( PhysicsBodyHandle body, IPhysicsDebugRenderer& renderer ) const = 0;
    };

    using IPhysicsBodyScene3D = IPhysicsBodyScene<PhysicsDimension3D>;
    using IPhysicsBodyScene2D = IPhysicsBodyScene<PhysicsDimension2D>;
} // namespace sw
