#include "pch.h"

#include "GameFramework/Runtime/Character/Control/Pawn/CharacterPawnMovement2DComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/CharacterController2DComponent.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/MovementConstraint.h"

namespace sw
{
    CharacterPawnMovement2DComponent::CharacterPawnMovement2DComponent()
        : _jumpButton{}
        , _walkSpeed{ 5.0f }
        , _acceleration{ 40.0f }
        , _deceleration{ 50.0f }
        , _jumpSpeed{ 7.0f }
        , _velocity{ 0.0f }
        , _jumpIndex{}
    {
        setCanEverTick( true );
    }

    void CharacterPawnMovement2DComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _jumpIndex                  = pPawn != nullptr && _jumpButton.empty() == false ? pPawn->findButton( _jumpButton ) : nullopt;
    }

    float32 CharacterPawnMovement2DComponent::computeVelocity( float32 current, float32 moveRight, float32 walkSpeed, float32 acceleration, float32 deceleration, float32 deltaTime )
    {
        const float32 target   = MathUtil::clamp( moveRight, -1.0f, 1.0f ) * walkSpeed;
        const float32 maxDelta = ( target != 0.0f ? acceleration : deceleration ) * deltaTime;
        return current + MathUtil::clamp( target - current, -maxDelta, maxDelta );
    }

    void CharacterPawnMovement2DComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*                     pOwner      = getOwner();
        const PawnComponent*            pPawn       = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        CharacterController2DComponent* pController = pOwner != nullptr ? pOwner->getComponent<CharacterController2DComponent>() : nullptr;
        if ( pPawn == nullptr || pController == nullptr )
            return;
        const ControlIntent& intent = pPawn->getIntent();
        // 이펙트의 이동 제약 — 3D 이동기와 같은 계약(`MovementConstraint`).
        const MovementConstraint constraint = MovementConstraint::read( *pOwner );
        const float32            moveInput  = constraint._bMoveBlocked == SW_TRUE ? 0.0f : intent._move._x;
        _velocity                           = computeVelocity( _velocity, moveInput, _walkSpeed * constraint._speedScale, _acceleration, _deceleration, deltaTime );
        pController->setMoveVelocity( _velocity );
        if ( intent.wasTriggered( _jumpIndex ) && pController->isGrounded() && constraint._bJumpBlocked == SW_FALSE )
            pController->jump( _jumpSpeed );
    }
} // namespace sw
