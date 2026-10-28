#include "pch.h"

#include "Games/AbilityArena/ArenaEnemyAIComponent.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Character/Control/Pawn/PawnComponent.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"

namespace sw
{
    ArenaEnemyAIComponent::ArenaEnemyAIComponent()
        : _kind{ ArenaUnitKind::Grunt }
        , _meleeReach{ 1.4f }
        , _preferredMin{ 5.0f }
        , _preferredMax{ 9.0f }
        , _fireRange{ 12.0f }
    {
    }

    void ArenaEnemyAIComponent::think( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        (void)context;
        const ArenaDirectorComponent* pDirector = ArenaDirectorComponent::findForPawn( pawn );
        const GameObject*             pOwner    = pawn.getOwner();
        float3                        selfPosition{};
        float3                        targetPosition{};
        const bool                    bHasTarget = pDirector != nullptr && pOwner != nullptr && pDirector->findUnitPosition( pOwner->getHandle(), selfPosition ) &&
                                pDirector->findNearestHostilePosition( pOwner->getHandle(), targetPosition );
        if ( bHasTarget == false )
        {
            setStateName( "Idle" );
            stopMoving();
            clearFocus();
            return;
        }
        const float3  toTarget = targetPosition - selfPosition;
        const float32 distance = toTarget.getLength();
        setFocus( targetPosition );
        if ( _kind == ArenaUnitKind::Caster )
        {
            // 원거리 — 적당한 거리를 지키며 쏜다. 물러나도 대상을 본다(초점). 상태 이름은 움직임 쪽이다(쏘기는 거리마다 겹친다).
            if ( distance > _preferredMax )
            {
                setStateName( "Chase" );
                moveTo( targetPosition );
            }
            else if ( distance < _preferredMin )
            {
                setStateName( "Retreat" );
                moveTo( selfPosition - ArenaUnitComponent::flattenDirection( toTarget, float3{ 0.0f, 0.0f, 1.0f } ) * _preferredMin );
            }
            else
            {
                setStateName( "Hold" );
                stopMoving();
            }
            if ( distance <= _fireRange )
                pressButton( hashed_string( "Arena.Fireball" ) );
            return;
        }
        if ( distance > _meleeReach )
        {
            setStateName( "Chase" );
            moveTo( targetPosition );
            return;
        }
        setStateName( "Strike" );
        stopMoving();
        pressButton( hashed_string( "Arena.Melee" ) );
    }
} // namespace sw
