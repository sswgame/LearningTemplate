#include "pch.h"

#include "Games/Shooter3D/ShooterBodyMovementComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Control/PawnComponent.h"

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
        , _verticalSpeed{ 0.0f }
        , _bOnGround{ SW_TRUE }
        , _bSuspended{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool ShooterBodyMovementComponent::stepMovement( const vector<ShooterArenaBox>& listBox, float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return false;
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        float3               move{ 0.0f, 0.0f, 0.0f };
        bool                 bSprint = false;
        bool                 bJump   = false;
        if ( pPawn != nullptr && _bSuspended == SW_FALSE )
        {
            // 이동 축은 조종 요 기준이다 — 월드 XZ 로 돌리고 길이는 1 에서 자른다(아날로그 스틱 · AI 경로 속도는 그 비율로 느리다).
            const ControlIntent& intent = pPawn->getIntent();
            const float3         world  = intent.computeWorldMove();
            move                        = float3{ world._x, 0.0f, world._z };
            const float32 length        = move.getLength();
            if ( length > 1.0f )
                move = move * ( 1.0f / length );
            bSprint = _sprintButton.empty() == false && pPawn->isButtonDown( pPawn->findButton( _sprintButton ) );
            bJump   = _jumpButton.empty() == false && pPawn->wasButtonTriggered( pPawn->findButton( _jumpButton ) );
        }
        const float3  before = _position;
        const float32 speed  = bSprint ? _sprintSpeed : _walkSpeed;
        float3        next   = _position + move * ( speed * deltaTime );
        if ( bJump && _bOnGround == SW_TRUE )
        {
            _verticalSpeed = _jumpSpeed;
            _bOnGround     = SW_FALSE;
        }
        _verticalSpeed -= _gravity * deltaTime;
        next._y += _verticalSpeed * deltaTime;
        bool bLanded = false;
        if ( next._y <= 0.0f )
        {
            bLanded        = _bOnGround == SW_FALSE;
            next._y        = 0.0f;
            _verticalSpeed = 0.0f;
            _bOnGround     = SW_TRUE;
        }
        _position     = ShooterArenaMath::resolveCircle( listBox, next, _radius );
        _moveVelocity = float3{ _position._x - before._x, 0.0f, _position._z - before._z } * ( 1.0f / deltaTime );
        return bLanded;
    }

    void ShooterBodyMovementComponent::teleport( const float3& feet )
    {
        _position      = feet;
        _moveVelocity  = float3{ 0.0f, 0.0f, 0.0f };
        _verticalSpeed = 0.0f;
        _bOnGround     = SW_TRUE;
    }
} // namespace sw
