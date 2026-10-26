#include "pch.h"

#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/StateHashBuilder.h"
#include "Engine/Object/Component/ComponentNameValidation.h"
#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    CharacterControllerComponent::CharacterControllerComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Character }
        , _radius{ 0.3f }
        , _halfHeight{ 0.6f }
        , _stepHeight{ 0.3f }
        , _maxSlopeAngle{ MathUtil::kHalfPi * 0.5f }
        , _gravityScale{ 1.0f }
        , _mass{ 70.0f }
        , _layer{ "Character" }
        , _bInnerBody{ false }
        , _character{}
        , _state{}
        , _commandLock{}
        , _moveVelocity{}
        , _pendingRootMotion{}
        , _frameRootMotionVelocity{}
        , _pendingSurfaceVelocity{}
        , _frameSurfaceVelocity{}
        , _pendingLaunch{}
        , _airVelocity{}
        , _previousPosition{}
        , _writtenPosition{}
        , _writtenRotation{}
        , _verticalSpeed{ 0.0f }
        , _pendingJumpSpeed{ 0.0f }
        , _pendingGroundYaw{ 0.0f }
        , _bHasWritten{ false }
    {
    }

    void CharacterControllerComponent::setMoveVelocity( const float3& velocity )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _moveVelocity = float3{ velocity._x, 0.0f, velocity._z };
    }

    void CharacterControllerComponent::jump( float32 speed )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _pendingJumpSpeed = speed;
    }

    void CharacterControllerComponent::addRootMotionDisplacement( const float3& worldDisplacement )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _pendingRootMotion += float3{ worldDisplacement._x, 0.0f, worldDisplacement._z };
    }

    void CharacterControllerComponent::launch( const float3& velocity )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _pendingLaunch = velocity;
    }

    void CharacterControllerComponent::addSurfaceVelocity( const float3& velocity )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _pendingSurfaceVelocity += float3{ velocity._x, 0.0f, velocity._z };
    }

    void CharacterControllerComponent::setRadius( float32 radius )
    {
        _radius = radius;
        requestRebuild();
    }

    void CharacterControllerComponent::setHalfHeight( float32 halfHeight )
    {
        _halfHeight = halfHeight;
        requestRebuild();
    }

    void CharacterControllerComponent::setStepHeight( float32 stepHeight )
    {
        _stepHeight = stepHeight;
        requestRebuild();
    }

    void CharacterControllerComponent::setMaxSlopeAngle( float32 angle )
    {
        _maxSlopeAngle = angle;
        requestRebuild();
    }

    void CharacterControllerComponent::foldStateHash( StateHashBuilder& inoutBuilder ) const
    {
        PhysicsComponent::foldStateHash( inoutBuilder );
        inoutBuilder.addFloat3( getVelocity() );
        inoutBuilder.addUint8( isGrounded() ? 1 : 0 );
    }

    void CharacterControllerComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsScene3D* pScene = physics.getScene3D();
        if ( pScene == nullptr )
            return;
        if ( consumeRebuild() )
            releasePhysics( physics );

        float3     position{};
        quaternion rotation{};
        float3     scale{};
        PhysicsComponentUtil::readWorldPose( *this, position, rotation, scale );
        if ( _character.isValid() == false )
        {
            PhysicsCharacterDesc3D desc;
            desc._position      = position;
            desc._userData      = getOwner()->getObjectID();
            desc._radius        = _radius;
            desc._halfHeight    = _halfHeight;
            desc._maxSlopeAngle = _maxSlopeAngle;
            desc._stepHeight    = _stepHeight;
            desc._mass          = _mass;
            desc._layer         = physics.resolveLayer( _layer );
            desc._bInnerBody    = _bInnerBody;
            _character          = pScene->createCharacter( desc );
            _state              = PhysicsCharacterState3D{};
            _state._position    = position;
            _previousPosition   = position;
            _writtenPosition    = position;
            _writtenRotation    = rotation;
            _verticalSpeed      = 0.0f;
            _bHasWritten        = true;
            (void)consumeTeleport();
            return;
        }
        const bool bTeleported = consumeTeleport();
        if ( bTeleported || PhysicsComponentUtil::hasMoved( position, rotation, _writtenPosition, _writtenRotation ) )
        {
            pScene->setCharacterPosition( _character, position );
            _state._position  = position;
            _previousPosition = position;
            if ( bTeleported )
                _verticalSpeed = 0.0f;
        }
        _writtenPosition = position;
        _writtenRotation = rotation;
    }

    void CharacterControllerComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene == nullptr || _character.isValid() == false )
            return;
        float3  moveVelocity{};
        float32 jumpSpeed = 0.0f;
        {
            std::scoped_lock<SpinLock> lock{ _commandLock };
            moveVelocity      = _moveVelocity;
            jumpSpeed         = _pendingJumpSpeed;
            _pendingJumpSpeed = 0.0f;
            // 루트 모션은 프레임의 첫 스텝에 꺼내 이 프레임의 스텝 수로 나눈다 — 프레임이 끝날 때 다 간다. 면 속도도 프레임마다 꺼낸다.
            if ( stepIndex == 0 )
            {
                const float32 frameSeconds = fixedDeltaTime * static_cast<float32>( stepCount > 0 ? stepCount : 1 );
                _frameRootMotionVelocity   = _pendingRootMotion * ( 1.0f / frameSeconds );
                _pendingRootMotion         = float3{};
                _frameSurfaceVelocity      = _pendingSurfaceVelocity;
                _pendingSurfaceVelocity    = float3{};
            }
            if ( _pendingLaunch.getLengthSquared() > 0.0f )
            {
                jumpSpeed      = _pendingLaunch._y;
                _airVelocity   = float3{ _pendingLaunch._x, 0.0f, _pendingLaunch._z };
                _pendingLaunch = float3{};
            }
        }
        moveVelocity = moveVelocity + _frameRootMotionVelocity + _frameSurfaceVelocity + _airVelocity;
        // 서 있으면 떨어지는 속도를 지우고, 떠 있으면 중력을 쌓는다. 점프는 그 위에 덮는다.
        if ( _state._bGrounded && _verticalSpeed < 0.0f )
            _verticalSpeed = 0.0f;
        _verticalSpeed += pScene->getGravity()._y * _gravityScale * fixedDeltaTime;
        if ( jumpSpeed > 0.0f )
            _verticalSpeed = jumpSpeed;

        _previousPosition = _state._position;
        _state            = pScene->moveCharacter( _character, float3{ moveVelocity._x, _verticalSpeed, moveVelocity._z }, fixedDeltaTime );
        // 결정: 바닥이 돌면 위치만이 아니라 방향(yaw)도 같이 돈다 — 이번 스텝에 바닥이 돈 각을 모아 두고 트랜스폼을 쓸 때 얹는다.
        _pendingGroundYaw += _state._groundYawRate * fixedDeltaTime;
        // 천장에 막혔으면 오르던 속도가 줄었다 — 실제로 난 수직 속도를 잇는다. 디디면 수직 속도는 0 이다.
        if ( _state._bGrounded && _verticalSpeed <= 0.0f )
        {
            _verticalSpeed = 0.0f;
            _airVelocity   = float3{}; // 발사가 끝났다 — 다시 디뎠다
        }
        else if ( _verticalSpeed > 0.0f && _state._velocity._y < _verticalSpeed )
            _verticalSpeed = _state._velocity._y;
        // 결정: 바닥을 떠나는 스텝(점프 · 걸어 내려섬)에 바닥이 실어 나르던 속도를 관성으로 잇는다 — 수평은 `_airVelocity` 로, 오르는 바닥이면 수직 속도에도.
        // 바닥 속도는 딛고 있는 스텝에만 얹히므로(`moveCharacter`), 이을 곳은 떠나는 순간을 아는 여기뿐이다. 위의 천장 판정 뒤에 둔다
        // (`_velocity` 는 바닥 몫을 뺀 값이라 앞에 두면 더한 수직 속도를 도로 깎는다).
        const bool bLeftGround = _state._bGrounded == false && _state._carriedVelocity.getLengthSquared() > 0.0f;
        if ( bLeftGround )
        {
            _airVelocity += float3{ _state._carriedVelocity._x, 0.0f, _state._carriedVelocity._z };
            if ( _state._carriedVelocity._y > 0.0f )
                _verticalSpeed += _state._carriedVelocity._y;
        }
    }

    void CharacterControllerComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        (void)physics;
        if ( _character.isValid() == false )
            return;
        const float3 position = float3::lerp( _previousPosition, _state._position, alpha );
        float3       current{};
        quaternion   rotation{};
        float3       scale{};
        PhysicsComponentUtil::readWorldPose( *this, current, rotation, scale );
        if ( _pendingGroundYaw != 0.0f )
        {
            rotation          = quaternion::concatenate( rotation, quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, _pendingGroundYaw ) ).normalize();
            _pendingGroundYaw = 0.0f;
        }
        PhysicsComponentUtil::writeWorldPose( *this, position, rotation, _writtenPosition, _writtenRotation );
        _bHasWritten = true;
    }

    void CharacterControllerComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _character.isValid() == false )
            return;
        IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene != nullptr )
            pScene->destroyCharacter( _character );
        _character   = PhysicsCharacterHandle{};
        _bHasWritten = false;
    }
} // namespace sw

namespace sw
{
    void CharacterControllerComponent::validateLayer( ValidationContext& context ) const
    {
        ComponentNameValidation::validatePhysicsLayer( context, "_layer", _layer );
    }
} // namespace sw
