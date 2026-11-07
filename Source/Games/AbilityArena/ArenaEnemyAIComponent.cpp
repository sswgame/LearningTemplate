#include "pch.h"

#include "Games/AbilityArena/ArenaEnemyAIComponent.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

#include "Games/AbilityArena/ArenaGameModeComponent.h"

namespace sw
{
    ArenaEnemyAIComponent::ArenaEnemyAIComponent()
    {
        requireBehaviorTree();
    }

    void ArenaEnemyAIComponent::updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        (void)context;
        const ArenaGameModeComponent* pGameMode = ArenaGameModeComponent::findForPawn( pawn );
        const GameObject*             pOwner    = pawn.getOwner();
        float3                        selfPosition{};
        float3                        targetPosition{};
        const bool                    bHasTarget = pGameMode != nullptr && pOwner != nullptr && pGameMode->findUnitPosition( pOwner->getHandle(), selfPosition ) &&
                                pGameMode->findNearestHostilePosition( pOwner->getHandle(), targetPosition );
        Blackboard& blackboard = getBlackboard();
        if ( bHasTarget == false )
        {
            blackboard.clearValue( "SelfPosition" );
            blackboard.clearValue( "TargetPosition" );
            blackboard.clearValue( "TargetDistance" );
            return;
        }
        blackboard.setVector( "SelfPosition", selfPosition );
        blackboard.setVector( "TargetPosition", targetPosition );
        blackboard.setFloat( "TargetDistance", ( targetPosition - selfPosition ).getLength() );
    }
} // namespace sw
