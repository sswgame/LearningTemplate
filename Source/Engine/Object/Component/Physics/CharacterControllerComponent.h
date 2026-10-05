/**
 * @file CharacterControllerComponent.h
 * @brief 캐릭터 컨트롤러(3D) — 캡슐 하나를 질의로 움직여 벽에 미끄러지고 · 턱을 오르고 · 가파른 비탈에 막힙니다(Jolt `CharacterVirtual`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class CharacterControllerComponent
     * @brief 3D 캐릭터 컨트롤러입니다(유니티 CharacterController · 언리얼 CharacterMovement 의 충돌 부분). 이 컴포넌트의 월드 자리가 발입니다.
     * @details 시뮬레이션 바디가 아닙니다 — 고정 스텝마다 원하는 속도(`setMoveVelocity`) + 중력으로 캡슐을 쓸어 옮깁니다. 서 있으면 수직 속도를 지우고,
     *          `jump` 가 수직 속도를 줍니다. 자리는 마지막 두 스텝 사이를 보간해 씁니다. 코드가 트랜스폼을 옮기면 순간이동입니다.
     *          이동 요청은 어느 틱에서든 부를 수 있습니다(다음 물리 프레임에 든다).
     */
    REFLECT( Category = "Physics", DisplayName = "Character Controller", Tooltip = "Capsule character mover: slides on walls, climbs steps, blocked by steep slopes" )
    class SW_API CharacterControllerComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        CharacterControllerComponent();
        ~CharacterControllerComponent() override = default;

        /** @brief 원하는 수평 이동 속도(월드, 미터/초)입니다. 다시 부를 때까지 유지합니다. Y 는 보지 않습니다(중력 · 점프가 정한다). */
        void setMoveVelocity( const float3& velocity );
        /** @brief 위로 @p speed(미터/초)를 줍니다. 다음 스텝에 듭니다. */
        void jump( float32 speed );
        /**
         * @brief 애니메이션 루트 모션의 이번 프레임 이동(월드, 미터)을 더합니다. 다음 물리 프레임의 스텝들이 나눠 움직입니다(벽에 막히고 턱을 오른다).
         * @details 수평(XZ)만 씁니다 — 수직은 중력 · 점프가 정합니다. 스텝이 없는 프레임이면 다음 프레임으로 넘깁니다. 아무 스레드에서 불러도 됩니다.
         */
        void addRootMotionDisplacement( const float3& worldDisplacement );
        /**
         * @brief 발사대 · 스프링이 쏩니다 — 수직은 그 속도로 덮고, 수평은 땅에 다시 닿을 때까지 이동 속도에 더합니다. 다음 스텝에 듭니다. 아무 스레드에서 불러도 됩니다.
         */
        void launch( const float3& velocity );
        /**
         * @brief 딛고 선 면의 속도(컨베이어 · 흐르는 물, 월드 m/s)를 이번 물리 프레임에 더합니다. 프레임마다 다시 불러야 이어집니다(여럿이면 합).
         * @details 아무 스레드에서 불러도 됩니다. 트랜스폼을 직접 옮기면 순간이동이라 벽을 지나므로, 면이 나르는 것은 이것으로 넘깁니다.
         */
        void addSurfaceVelocity( const float3& velocity );
        /** @brief 마지막 스텝에 바닥을 딛고 있었는지입니다. */
        bool isGrounded() const { return _state._bGrounded; }
        /** @brief 마지막 스텝의 바닥 법선입니다. */
        float3 getGroundNormal() const { return _state._groundNormal; }
        /** @brief 마지막 스텝에서 실제로 난 속도입니다. */
        float3 getVelocity() const { return _state._velocity; }
        /** @brief 마지막 스텝 뒤의 발 자리입니다(보간 전). */
        float3 getSimulatedPosition() const { return _state._position; }
        /** @brief 캐릭터 핸들입니다(시작 전이면 무효). */
        PhysicsCharacterHandle getCharacterHandle() const { return _character; }

        float32 getRadius() const { return _radius; }
        void    setRadius( float32 radius );
        float32 getHalfHeight() const { return _halfHeight; }
        void    setHalfHeight( float32 halfHeight );
        float32 getStepHeight() const { return _stepHeight; }
        void    setStepHeight( float32 stepHeight );
        float32 getMaxSlopeAngle() const { return _maxSlopeAngle; }
        void    setMaxSlopeAngle( float32 angle );

    protected:
        void beginPhysicsFrame( ScenePhysics& physics ) override;
        void prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount ) override;
        void endPhysicsFrame( ScenePhysics& physics, float32 alpha ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        PROPERTY( Category = "Character", Min = 0.01, Tooltip = "Capsule radius", Units = m )
        float32 _radius;
        PROPERTY( Category = "Character", Min = 0.0, Tooltip = "Half the cylinder part; height = 2 * (half height + radius)", Units = m )
        float32 _halfHeight;
        PROPERTY( Category = "Character", Min = 0.0, Tooltip = "Highest step the character walks up", Units = m )
        float32 _stepHeight;
        PROPERTY( Category = "Character", Min = 0.0, Max = 1.57, Tooltip = "Steepest walkable floor", Units = rad )
        float32 _maxSlopeAngle;
        PROPERTY( Category = "Character", Tooltip = "Multiplier on scene gravity (0 floats)" )
        float32 _gravityScale;
        PROPERTY( Category = "Character", Min = 0.0, Tooltip = "Mass used to push dynamic bodies", Units = kg )
        float32 _mass;
        PROPERTY( Category = "Character", Tooltip = "Collision layer name from the physics settings" )
        hashed_string _layer;

        PhysicsCharacterHandle  _character;
        PhysicsCharacterState3D _state;
        mutable SpinLock        _commandLock;
        float3                  _moveVelocity;            ///< 요청된 수평 속도(`_commandLock`)
        float3                  _pendingRootMotion;       ///< 아직 스텝에 넘기지 않은 루트 모션 이동(`_commandLock`)
        float3                  _frameRootMotionVelocity; ///< 이번 물리 프레임의 스텝마다 더할 루트 모션 속도
        float3                  _pendingSurfaceVelocity;  ///< 이번 프레임에 쌓인 면 속도(`_commandLock`)
        float3                  _frameSurfaceVelocity;    ///< 이번 물리 프레임의 면 속도
        float3                  _pendingLaunch;           ///< 0 이 아니면 다음 스텝에 쏜다(`_commandLock`)
        float3                  _airVelocity;             ///< 발사의 수평 속도 — 땅에 다시 닿으면 0
        float3                  _previousPosition;
        float3                  _writtenPosition;
        quaternion              _writtenRotation;
        float32                 _verticalSpeed;
        float32                 _pendingJumpSpeed; ///< 0 이 아니면 다음 스텝에 준다(`_commandLock`)
        bool                    _bHasWritten;
    };
} // namespace sw
