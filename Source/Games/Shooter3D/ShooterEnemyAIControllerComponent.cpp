#include "pch.h"

#include "Games/Shooter3D/ShooterEnemyAIControllerComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

#include "Games/Shooter3D/ShooterEnemyComponent.h"
#include "Games/Shooter3D/ShooterGameModeComponent.h"

namespace sw
{
    ShooterEnemyAIControllerComponent::ShooterEnemyAIControllerComponent()
    {
        requireBehaviorTree();
    }

    void ShooterEnemyAIControllerComponent::updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        (void)context;
        Blackboard&                     blackboard = getBlackboard();
        const GameObject*               pPawnOwner = pawn.getOwner();
        const GameObjectManager*        pManager   = pPawnOwner != nullptr ? pPawnOwner->getManager() : nullptr;
        const ShooterEnemyComponent*    pEnemy     = pPawnOwner != nullptr ? pPawnOwner->getComponent<ShooterEnemyComponent>() : nullptr;
        const ShooterGameModeComponent* pGameMode =
            pManager != nullptr && pEnemy != nullptr ? GameModeComponent::resolve<ShooterGameModeComponent>( *pManager, pEnemy->getGameMode() ) : nullptr;
        if ( pGameMode == nullptr || pEnemy->isAlive() == false )
        {
            blackboard.clearValue( "PlayerFeet" );
            return;
        }
        const float3& target = pGameMode->getPlayerFeet();
        blackboard.setVector( "PlayerFeet", target );
        blackboard.setFloat( "PlayerDistance", float3::getHorizontalDistance( target, pEnemy->getPosition() ) );
        blackboard.setFloat( "Reach", pEnemy->getReach() );
        blackboard.setFloat( "StopDistance", pEnemy->getReach() * pEnemy->getStopFraction() );
        blackboard.setBool( "PlayerAlive", pGameMode->isPlayerAlive() );
        const ShooterEnemyPhase phase = pEnemy->getPhase();
        blackboard.setBool( "Rising", phase == ShooterEnemyPhase::Rising );
        blackboard.setBool( "Chasing", phase == ShooterEnemyPhase::Chasing );
        blackboard.setBool( "Attacking", phase == ShooterEnemyPhase::Attacking );
        blackboard.setBool( "Staggered", phase == ShooterEnemyPhase::Staggered );
        blackboard.setBool( "Dying", phase == ShooterEnemyPhase::Dying );
    }
} // namespace sw
