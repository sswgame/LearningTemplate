/**
 * @file CharacterController2DComponent.h
 * @brief 캐릭터 컨트롤러(2D) — 선 캡슐 하나를 Box2D 무버(`b2World_CastMover` · `b2SolvePlanes`)로 움직입니다.
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
     * @class CharacterController2DComponent
     * @brief 2D 캐릭터 컨트롤러입니다(플랫포머의 키네마틱 무버). 이 컴포넌트의 월드 XY 가 발이고 Z 는 그대로 둡니다. 3D 판과 규칙이 같습니다 —
     *        둥근 바닥이 작은 턱을 타고 넘으므로 턱 높이 칸이 없습니다.
     * @details 시뮬레이션 바디가 아닙니다 — 고정 스텝마다 원하는 속도(`setMoveVelocity`) + 중력으로 캡슐을 쓸어 옮깁니다. 서 있으면 수직 속도를 지우고,
     *          `jump` 가 수직 속도를 줍니다. 자리는 마지막 두 스텝 사이를 보간해 씁니다. 코드가 트랜스폼을 옮기면 순간이동입니다.
     *          이동 요청은 어느 틱에서든 부를 수 있습니다(다음 물리 프레임에 든다).
     */
    REFLECT( Category = "Physics 2D", DisplayName = "Character Controller 2D", Tooltip = "2D capsule mover: slides on walls, blocked by steep slopes" )
    class SW_API CharacterController2DComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        CharacterController2DComponent();
        ~CharacterController2DComponent() override = default;

        /** @brief 원하는 수평 이동 속도(미터/초)입니다. 다시 부를 때까지 유지합니다. */
        void setMoveVelocity( float32 velocity );
        /** @brief 위로 @p speed(미터/초)를 줍니다. 다음 스텝에 듭니다. */
        void jump( float32 speed );
        /** @brief 마지막 스텝에 바닥을 딛고 있었는지입니다. */
        bool isGrounded() const { return _state._bGrounded; }
        /** @brief 마지막 스텝의 바닥 법선입니다. */
        float2 getGroundNormal() const { return _state._groundNormal; }
        /** @brief 마지막 스텝에서 실제로 난 속도입니다. */
        float2 getVelocity() const { return _state._velocity; }
        /** @brief 마지막 스텝 뒤의 발 자리입니다(보간 전). */
        float2 getSimulatedPosition() const { return _state._position; }
        /** @brief 캐릭터 핸들입니다(시작 전이면 무효). */
        PhysicsCharacterHandle getCharacterHandle() const { return _character; }

        float32 getRadius() const { return _radius; }
        void    setRadius( float32 radius );
        float32 getHalfHeight() const { return _halfHeight; }
        void    setHalfHeight( float32 halfHeight );
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
        PROPERTY( Category = "Character", Min = 0.0, Max = 1.57, Tooltip = "Steepest walkable floor", Units = rad )
        float32 _maxSlopeAngle;
        PROPERTY( Category = "Character", Tooltip = "Multiplier on scene gravity (0 floats)" )
        float32 _gravityScale;
        PROPERTY( Category = "Character", Tooltip = "Collision layer name from the physics settings" )
        hashed_string _layer;

        PhysicsCharacterHandle  _character;
        PhysicsCharacterState2D _state;
        mutable SpinLock        _commandLock;
        float2                  _previousPosition;
        float32                 _moveVelocity; ///< 요청된 수평 속도(`_commandLock`)
        float3                  _writtenPosition;
        quaternion              _writtenRotation;
        float32                 _verticalSpeed;
        float32                 _pendingJumpSpeed; ///< 0 이 아니면 다음 스텝에 준다(`_commandLock`)
        bool                    _bHasWritten;
    };
} // namespace sw
