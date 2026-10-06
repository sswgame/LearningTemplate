#include "pch.h"

#include "Engine/Object/Component/Physics/CharacterController2DComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/ScenePhysics.h"

namespace sw
{
    CharacterController2DComponent::CharacterController2DComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Character }
        , _radius{ 0.3f }
        , _halfHeight{ 0.4f }
        , _maxSlopeAngle{ MathUtil::kHalfPi * 0.5f }
        , _gravityScale{ 1.0f }
        , _layer{ "Character" }
        , _character{}
        , _state{}
        , _commandLock{}
        , _previousPosition{}
        , _moveVelocity{ 0.0f }
        , _writtenPosition{}
        , _writtenRotation{}
        , _verticalSpeed{ 0.0f }
        , _pendingJumpSpeed{ 0.0f }
        , _bHasWritten{ false }
    {
    }

    void CharacterController2DComponent::setMoveVelocity( float32 velocity )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _moveVelocity = velocity;
    }

    void CharacterController2DComponent::jump( float32 speed )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _pendingJumpSpeed = speed;
    }

    void CharacterController2DComponent::setRadius( float32 radius )
    {
        _radius = radius;
        requestRebuild();
    }

    void CharacterController2DComponent::setHalfHeight( float32 halfHeight )
    {
        _halfHeight = halfHeight;
        requestRebuild();
    }

    void CharacterController2DComponent::setMaxSlopeAngle( float32 angle )
    {
        _maxSlopeAngle = angle;
        requestRebuild();
    }

    void CharacterController2DComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsScene2D* pScene = physics.getScene2D();
        if ( pScene == nullptr )
            return;
        if ( consumeRebuild() )
            releasePhysics( physics );

        float3     position3{};
        quaternion rotation{};
        float3     scale{};
        PhysicsComponentUtil::readWorldPose( *this, position3, rotation, scale );
        const float2 position{ position3._x, position3._y };
        if ( _character.isValid() == false )
        {
            PhysicsCharacterDesc2D desc;
            desc._position      = position;
            desc._userData      = getOwner()->getObjectId();
            desc._radius        = _radius;
            desc._halfHeight    = _halfHeight;
            desc._maxSlopeAngle = _maxSlopeAngle;
            desc._layer         = physics.resolveLayer( _layer );
            _character          = pScene->createCharacter( desc );
            _state              = PhysicsCharacterState2D{};
            _state._position    = position;
            _previousPosition   = position;
            _writtenPosition    = position3;
            _writtenRotation    = rotation;
            _verticalSpeed      = 0.0f;
            _bHasWritten        = true;
            (void)consumeTeleport();
            return;
        }
        const bool bTeleported = consumeTeleport();
        if ( bTeleported || PhysicsComponentUtil::hasMoved( position3, rotation, _writtenPosition, _writtenRotation ) )
        {
            pScene->setCharacterPosition( _character, position );
            _state._position  = position;
            _previousPosition = position;
            if ( bTeleported )
                _verticalSpeed = 0.0f;
        }
        _writtenPosition = position3;
        _writtenRotation = rotation;
    }

    void CharacterController2DComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        (void)stepIndex;
        (void)stepCount;
        IPhysicsScene2D* pScene = physics.findScene2D();
        if ( pScene == nullptr || _character.isValid() == false )
            return;
        float32 moveVelocity = 0.0f;
        float32 jumpSpeed    = 0.0f;
        {
            std::scoped_lock<SpinLock> lock{ _commandLock };
            moveVelocity      = _moveVelocity;
            jumpSpeed         = _pendingJumpSpeed;
            _pendingJumpSpeed = 0.0f;
        }
        if ( _state._bGrounded && _verticalSpeed < 0.0f )
            _verticalSpeed = 0.0f;
        _verticalSpeed += pScene->getGravity()._y * _gravityScale * fixedDeltaTime;
        if ( jumpSpeed > 0.0f )
            _verticalSpeed = jumpSpeed;

        _previousPosition = _state._position;
        _state            = pScene->moveCharacter( _character, float2{ moveVelocity, _verticalSpeed }, fixedDeltaTime );
        if ( _state._bGrounded && _verticalSpeed <= 0.0f )
            _verticalSpeed = 0.0f;
        else if ( _verticalSpeed > 0.0f && _state._velocity._y < _verticalSpeed )
            _verticalSpeed = _state._velocity._y;
    }

    void CharacterController2DComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        (void)physics;
        if ( _character.isValid() == false )
            return;
        const float2 position = float2::lerp( _previousPosition, _state._position, alpha );
        float3       current{};
        quaternion   rotation{};
        float3       scale{};
        PhysicsComponentUtil::readWorldPose( *this, current, rotation, scale );
        PhysicsComponentUtil::writeWorldPose( *this, float3{ position._x, position._y, current._z }, rotation, _writtenPosition, _writtenRotation );
        _bHasWritten = true;
    }

    void CharacterController2DComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _character.isValid() == false )
            return;
        IPhysicsScene2D* pScene = physics.findScene2D();
        if ( pScene != nullptr )
            pScene->destroyCharacter( _character );
        _character   = PhysicsCharacterHandle{};
        _bHasWritten = false;
    }
} // namespace sw
