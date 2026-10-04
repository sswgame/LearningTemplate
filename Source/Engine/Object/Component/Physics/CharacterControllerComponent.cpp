#include "pch.h"

#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/ScenePhysics.h"

namespace sw
{
    CharacterControllerComponent::CharacterControllerComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Character }
        , _radius{ 0.3f }
        , _halfHeight{ 0.6f }
        , _stepHeight{ 0.3f }
        , _maxSlopeAngle{ 0.785398f }
        , _gravityScale{ 1.0f }
        , _mass{ 70.0f }
        , _layer{ "Character" }
        , _character{}
        , _state{}
        , _commandLock{}
        , _moveVelocity{}
        , _pendingRootMotion{}
        , _frameRootMotionVelocity{}
        , _previousPosition{}
        , _writtenPosition{}
        , _writtenRotation{}
        , _verticalSpeed{ 0.0f }
        , _pendingJumpSpeed{ 0.0f }
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
            desc._userData      = getOwner()->getObjectId();
            desc._radius        = _radius;
            desc._halfHeight    = _halfHeight;
            desc._maxSlopeAngle = _maxSlopeAngle;
            desc._stepHeight    = _stepHeight;
            desc._mass          = _mass;
            desc._layer         = physics.resolveLayer( _layer );
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
            // 루트 모션은 프레임의 첫 스텝에 꺼내 이 프레임의 스텝 수로 나눈다 — 프레임이 끝날 때 다 간다.
            if ( stepIndex == 0 )
            {
                const float32 frameSeconds = fixedDeltaTime * static_cast<float32>( stepCount > 0 ? stepCount : 1 );
                _frameRootMotionVelocity   = _pendingRootMotion * ( 1.0f / frameSeconds );
                _pendingRootMotion         = float3{};
            }
        }
        moveVelocity = moveVelocity + _frameRootMotionVelocity;
        // 서 있으면 떨어지는 속도를 지우고, 떠 있으면 중력을 쌓는다. 점프는 그 위에 덮는다.
        if ( _state._bGrounded && _verticalSpeed < 0.0f )
            _verticalSpeed = 0.0f;
        _verticalSpeed += pScene->getGravity()._y * _gravityScale * fixedDeltaTime;
        if ( jumpSpeed > 0.0f )
            _verticalSpeed = jumpSpeed;

        _previousPosition = _state._position;
        _state            = pScene->moveCharacter( _character, float3{ moveVelocity._x, _verticalSpeed, moveVelocity._z }, fixedDeltaTime );
        // 천장에 막혔으면 오르던 속도가 줄었다 — 실제로 난 수직 속도를 잇는다. 디디면 수직 속도는 0 이다.
        if ( _state._bGrounded && _verticalSpeed <= 0.0f )
            _verticalSpeed = 0.0f;
        else if ( _verticalSpeed > 0.0f && _state._velocity._y < _verticalSpeed )
            _verticalSpeed = _state._velocity._y;
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
