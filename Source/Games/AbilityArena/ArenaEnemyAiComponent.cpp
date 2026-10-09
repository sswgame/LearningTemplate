#include "pch.h"

#include "Games/AbilityArena/ArenaEnemyAiComponent.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"

namespace sw
{
    ArenaEnemyAiComponent::ArenaEnemyAiComponent()
        : _kind{ ArenaUnitKind::Grunt }
        , _meleeReach{ 1.4f }
        , _preferredMin{ 5.0f }
        , _preferredMax{ 9.0f }
        , _fireRange{ 12.0f }
    {
    }

    void ArenaEnemyAiComponent::think( const ControlFrameContext& context, const PawnComponent& pawn )
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
            stopMoving();
            clearFocus();
            return;
        }
        const float3  toTarget = targetPosition - selfPosition;
        const float32 distance = toTarget.getLength();
        setFocus( targetPosition );
        if ( _kind == ArenaUnitKind::Caster )
        {
            // 원거리 — 적당한 거리를 지키며 쏜다. 물러나도 대상을 본다(초점).
            if ( distance > _preferredMax )
                moveTo( targetPosition );
            else if ( distance < _preferredMin )
                moveTo( selfPosition - ArenaUnitComponent::flattenDirection( toTarget, float3{ 0.0f, 0.0f, 1.0f } ) * _preferredMin );
            else
                stopMoving();
            if ( distance <= _fireRange )
                pressButton( hashed_string( "Arena.Fireball" ) );
            return;
        }
        if ( distance > _meleeReach )
        {
            moveTo( targetPosition );
            return;
        }
        stopMoving();
        pressButton( hashed_string( "Arena.Melee" ) );
    }
} // namespace sw
