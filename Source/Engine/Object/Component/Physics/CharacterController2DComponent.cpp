#include "pch.h"

#include "Engine/Object/Component/Physics/CharacterController2DComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/StateHashBuilder.h"
#include "Engine/Object/Component/ComponentNameValidation.h"
#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionValidation.h"

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
        , _worldPosition{}
        , _worldRotation{}
        , _worldScale{ 1.0f, 1.0f, 1.0f }
        , _verticalSpeed{ 0.0f }
        , _pendingJumpSpeed{ 0.0f }
        , _airVelocity{ 0.0f }
    {
    }

    void CharacterController2DComponent::setMoveVelocity( float32 velocity )
    {
        ScopedLock<SpinLock> lock{ _commandLock };
        _moveVelocity = velocity;
    }

    void CharacterController2DComponent::jump( float32 speed )
    {
        ScopedLock<SpinLock> lock{ _commandLock };
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

    void CharacterController2DComponent::foldStateHash( StateHashBuilder& inoutBuilder ) const
    {
        PhysicsComponent::foldStateHash( inoutBuilder );
        inoutBuilder.addFloat2( getVelocity() );
        inoutBuilder.addUint8( isGrounded() ? 1 : 0 );
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

        // 트랜스폼의 바깥 변화는 칸의 변경 번호로 안다 — 같으면 월드 행렬을 풀지 않는다(3D 컨트롤러와 같은 규칙).
        const bool bTransformChanged = consumeTransformChange();
        const bool bCreate           = _character.isValid() == false;
        const bool bTeleported       = consumeTeleport();
        const bool bUnchanged        = bCreate == false && bTransformChanged == false && bTeleported == false;
        if ( bUnchanged )
            return;
        PhysicsComponentUtil::readWorldPose( *this, _worldPosition, _worldRotation, _worldScale );
        const float2 position{ _worldPosition._x, _worldPosition._y };
        if ( bCreate )
        {
            PhysicsCharacterDesc2D desc;
            desc._position      = position;
            desc._userData      = getOwner()->getObjectID();
            desc._radius        = _radius;
            desc._halfHeight    = _halfHeight;
            desc._maxSlopeAngle = _maxSlopeAngle;
            desc._layer         = physics.resolveLayer( _layer );
            _character          = pScene->createCharacter( desc );
            _state              = PhysicsCharacterState2D{};
            _state._position    = position;
            _previousPosition   = position;
            _verticalSpeed      = 0.0f;
            return;
        }
        pScene->setCharacterPosition( _character, position );
        _state._position  = position;
        _previousPosition = position;
        if ( bTeleported )
            _verticalSpeed = 0.0f;
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
            ScopedLock<SpinLock> lock{ _commandLock };
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
        _state            = pScene->moveCharacter( _character, float2{ moveVelocity + _airVelocity, _verticalSpeed }, fixedDeltaTime );
        if ( _state._bGrounded && _verticalSpeed <= 0.0f )
        {
            _verticalSpeed = 0.0f;
            _airVelocity   = 0.0f;
        }
        else if ( _verticalSpeed > 0.0f && _state._velocity._y < _verticalSpeed )
            _verticalSpeed = _state._velocity._y;
        // 결정: 바닥을 떠나는 스텝에 바닥이 실어 나르던 속도를 관성으로 잇는다(3D `CharacterControllerComponent` 와 같다 — 천장 판정 뒤에 둔다).
        const bool bLeftGround = _state._bGrounded == false && _state._carriedVelocity.getLengthSquared() > 0.0f;
        if ( bLeftGround )
        {
            _airVelocity += _state._carriedVelocity._x;
            if ( _state._carriedVelocity._y > 0.0f )
                _verticalSpeed += _state._carriedVelocity._y;
        }
    }

    void CharacterController2DComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        (void)physics;
        if ( _character.isValid() == false )
            return;
        const float2 position = float2::lerp( _previousPosition, _state._position, alpha );
        // Z · 회전 · 배율은 트랜스폼의 것이다. 이 프레임 시작 뒤에 누가 옮겼을 때만 다시 읽는다.
        if ( consumeTransformChange() )
            PhysicsComponentUtil::readWorldPose( *this, _worldPosition, _worldRotation, _worldScale );
        PhysicsComponentUtil::writeWorldPose( *this, float3{ position._x, position._y, _worldPosition._z }, _worldRotation, _worldScale );
        markTransformSynced();
    }

    void CharacterController2DComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _character.isValid() == false )
            return;
        IPhysicsScene2D* pScene = physics.findScene2D();
        if ( pScene != nullptr )
            pScene->destroyCharacter( _character );
        _character = PhysicsCharacterHandle{};
    }
} // namespace sw

namespace sw
{
    void CharacterController2DComponent::validateLayer( ValidationContext& context ) const
    {
        ComponentNameValidation::validatePhysicsLayer( context, "_layer", _layer );
    }
} // namespace sw
