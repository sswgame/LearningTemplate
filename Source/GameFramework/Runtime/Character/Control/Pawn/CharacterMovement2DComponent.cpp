#include "pch.h"

#include "GameFramework/Runtime/Character/Control/Pawn/CharacterMovement2DComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Physics/Component/CharacterController2DComponent.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/MovementConstraint.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Traversal/ClimbZoneComponent.h"

namespace sw
{
    CharacterMovement2DComponent::CharacterMovement2DComponent()
        : _jumpButton{}
        , _walkSpeed{ 5.0f }
        , _acceleration{ 40.0f }
        , _deceleration{ 50.0f }
        , _jumpSpeed{ 7.0f }
        , _climbReleaseSeconds{ 0.3f }
        , _velocity{ 0.0f }
        , _climbReleaseLeft{ 0.0f }
        , _bClimbing{ false }
        , _jumpIndex{}
    {
        setCanEverTick( true );
    }

    void CharacterMovement2DComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _jumpIndex                  = pPawn != nullptr && _jumpButton.empty() == false ? pPawn->findButton( _jumpButton ) : nullopt;
    }

    float32 CharacterMovement2DComponent::computeVelocity( float32 current, float32 moveRight, float32 walkSpeed, float32 acceleration, float32 deceleration, float32 deltaTime )
    {
        const float32 target   = MathUtil::clamp( moveRight, -1.0f, 1.0f ) * walkSpeed;
        const float32 maxDelta = ( target != 0.0f ? acceleration : deceleration ) * deltaTime;
        return current + MathUtil::clamp( target - current, -maxDelta, maxDelta );
    }

    void CharacterMovement2DComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*                     pOwner      = getOwner();
        const PawnComponent*            pPawn       = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        CharacterController2DComponent* pController = pOwner != nullptr ? pOwner->getComponent<CharacterController2DComponent>() : nullptr;
        if ( pPawn == nullptr || pController == nullptr )
            return;
        const ControlIntent& intent = pPawn->getIntent();
        // 이펙트의 이동 제약 — 3D 이동기와 같은 계약(`MovementConstraint`).
        const MovementConstraint constraint = MovementConstraint::read( *pOwner );
        if ( updateClimb( context, intent, constraint, *pController ) )
            return;
        const float32 moveInput = constraint._bMoveBlocked == SW_TRUE ? 0.0f : intent._move._x;
        _velocity               = computeVelocity( _velocity, moveInput, _walkSpeed * constraint._speedScale, _acceleration, _deceleration, context.getDeltaTime() );
        pController->setMoveVelocity( _velocity );
        if ( intent.wasTriggered( _jumpIndex ) && pController->isGrounded() && constraint._bJumpBlocked == SW_FALSE )
            pController->jump( _jumpSpeed );
    }

    bool CharacterMovement2DComponent::updateClimb( const TickContext& context, const ControlIntent& intent, const MovementConstraint& constraint, CharacterController2DComponent& controller )
    {
        const bool bWasClimbing = _bClimbing;
        _bClimbing              = false;
        _climbReleaseLeft       = MathUtil::max( 0.0f, _climbReleaseLeft - context.getDeltaTime() );
        if ( _climbReleaseLeft > 0.0f )
            return false;
        const ClimbZoneComponent* const pZone = ClimbZoneComponent::findClimbZone( context.getManager(), context.getSelf().getHandle() );
        if ( pZone == nullptr )
            return false;
        const bool    bBlocked = constraint._bMoveBlocked == SW_TRUE;
        const float32 climb    = bBlocked ? 0.0f : MathUtil::clamp( intent._move._y + intent._moveUp, -1.0f, 1.0f );
        // 땅에서는 올라가는 쪽으로만 잡는다. 공중에서는 위아래를 누르면 잡고, 매달린 뒤에는 놓아도 머문다(3D 이동기와 같다).
        const bool bGrab = controller.isGrounded() ? climb > 0.0f : ( climb != 0.0f || bWasClimbing );
        if ( bGrab == false )
            return false;
        _velocity = 0.0f;
        controller.setMoveVelocity( _velocity );
        if ( bWasClimbing && intent.wasTriggered( _jumpIndex ) && constraint._bJumpBlocked == SW_FALSE )
        {
            controller.jump( _jumpSpeed );
            _climbReleaseLeft = _climbReleaseSeconds;
            return true;
        }
        const float3  axis    = pZone->computeAxis();
        const float32 lateral = bBlocked ? 0.0f : MathUtil::clamp( intent._move._x, -1.0f, 1.0f ) * _walkSpeed;
        const float2  velocity{ axis._x * climb * pZone->getClimbSpeed() + lateral, axis._y * climb * pZone->getClimbSpeed() };
        controller.applyClimbVelocity( velocity * constraint._speedScale );
        _bClimbing = true;
        return true;
    }
} // namespace sw
