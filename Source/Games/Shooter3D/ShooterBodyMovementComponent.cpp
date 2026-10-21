#include "pch.h"

#include "Games/Shooter3D/ShooterBodyMovementComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"

namespace sw
{
    ShooterBodyMovementComponent::ShooterBodyMovementComponent()
        : _sprintButton{}
        , _jumpButton{}
        , _walkSpeed{ 5.5f }
        , _sprintSpeed{ 8.5f }
        , _jumpSpeed{ 6.0f }
        , _gravity{ 18.0f }
        , _radius{ 0.35f }
        , _position{ 0.0f, 0.0f, 0.0f }
        , _moveVelocity{ 0.0f, 0.0f, 0.0f }
        , _displayOffset{ 0.0f, 0.0f, 0.0f }
        , _verticalSpeed{ 0.0f }
        , _bOnGround{ SW_TRUE }
        , _bSuspended{ SW_FALSE }
        , _bNetDriven{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool ShooterBodyMovementComponent::stepMovement( const vector<ShooterArenaBox>& listBox, float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _bNetDriven == SW_TRUE )
            return false;
        const float3           before  = _position;
        bool                   bLanded = false;
        const ShooterMoveState next    = simulate( listBox, getState(), makeInput(), deltaTime, bLanded );
        setState( next, float3{ next._position._x - before._x, 0.0f, next._position._z - before._z } * ( 1.0f / deltaTime ) );
        return bLanded;
    }

    ShooterMoveInput ShooterBodyMovementComponent::makeInput() const
    {
        ShooterMoveInput     input;
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn == nullptr || _bSuspended == SW_TRUE )
            return input;
        // 이동 축은 조종 요 기준이다 — 월드 XZ 로 돌리고 길이는 1 에서 자른다(아날로그 스틱 · AI 경로 속도는 그 비율로 느리다).
        const ControlIntent& intent = pPawn->getIntent();
        const float3         world  = intent.computeWorldMove();
        float3               move{ world._x, 0.0f, world._z };
        const float32        length = move.getLength();
        if ( length > 1.0f )
            move = move * ( 1.0f / length );
        input._move    = move;
        input._bSprint = _sprintButton.empty() == false && pPawn->isButtonDown( pPawn->findButton( _sprintButton ) ) ? SW_TRUE : SW_FALSE;
        input._bJump   = _jumpButton.empty() == false && pPawn->wasButtonTriggered( pPawn->findButton( _jumpButton ) ) ? SW_TRUE : SW_FALSE;
        return input;
    }

    ShooterMoveState ShooterBodyMovementComponent::simulate( const vector<ShooterArenaBox>& listBox, const ShooterMoveState& state, const ShooterMoveInput& input, float32 deltaTime,
                                                             bool& outbLanded ) const
    {
        outbLanded             = false;
        ShooterMoveState next  = state;
        const float32    speed = input._bSprint == SW_TRUE ? _sprintSpeed : _walkSpeed;
        float3           feet  = state._position + input._move * ( speed * deltaTime );
        if ( input._bJump == SW_TRUE && next._bOnGround == SW_TRUE )
        {
            next._verticalSpeed = _jumpSpeed;
            next._bOnGround     = SW_FALSE;
        }
        next._verticalSpeed -= _gravity * deltaTime;
        feet._y += next._verticalSpeed * deltaTime;
        if ( feet._y <= 0.0f )
        {
            outbLanded          = next._bOnGround == SW_FALSE;
            feet._y             = 0.0f;
            next._verticalSpeed = 0.0f;
            next._bOnGround     = SW_TRUE;
        }
        next._position = ShooterArenaMath::resolveCircle( listBox, feet, _radius );
        return next;
    }

    ShooterMoveState ShooterBodyMovementComponent::getState() const
    {
        ShooterMoveState state;
        state._position      = _position;
        state._verticalSpeed = _verticalSpeed;
        state._bOnGround     = _bOnGround == SW_TRUE ? SW_TRUE : SW_FALSE;
        return state;
    }

    void ShooterBodyMovementComponent::setState( const ShooterMoveState& state, const float3& moveVelocity )
    {
        _position      = state._position;
        _verticalSpeed = state._verticalSpeed;
        _bOnGround     = state._bOnGround == SW_TRUE ? SW_TRUE : SW_FALSE;
        _moveVelocity  = moveVelocity;
    }

    void ShooterBodyMovementComponent::teleport( const float3& feet )
    {
        _position      = feet;
        _moveVelocity  = float3{ 0.0f, 0.0f, 0.0f };
        _verticalSpeed = 0.0f;
        _bOnGround     = SW_TRUE;
    }
} // namespace sw
