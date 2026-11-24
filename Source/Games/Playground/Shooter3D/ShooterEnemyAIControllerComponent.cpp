#include "pch.h"

#include "Games/Playground/Shooter3D/ShooterEnemyAIControllerComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

#include "Games/Playground/Shooter3D/ShooterEnemyComponent.h"
#include "Games/Playground/Shooter3D/ShooterGameModeComponent.h"

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
        const ShooterPlayerView* pTarget = pGameMode != nullptr ? pGameMode->findNearestPlayer( pEnemy->getPosition() ) : nullptr;
        if ( pTarget == nullptr || pEnemy->isAlive() == false )
        {
            blackboard.clearValue( "PlayerFeet" );
            return;
        }
        const AbsolutePosition& target = pTarget->_feet;
        blackboard.setVector( "PlayerFeet", pManager->getWorldOrigin().toLocal( target ) ); // 이동 · 시선은 씬 자리다
        blackboard.setFloat( "PlayerDistance", float3::getHorizontalDistance( target._value, pEnemy->getPosition()._value ) );
        blackboard.setFloat( "Reach", pEnemy->getReach() );
        blackboard.setFloat( "StopDistance", pEnemy->getReach() * pEnemy->getStopFraction() );
        blackboard.setBool( "PlayerAlive", pTarget->_bAlive == SW_TRUE );
        const ShooterEnemyPhase phase = pEnemy->getPhase();
        blackboard.setBool( "Rising", phase == ShooterEnemyPhase::Rising );
        blackboard.setBool( "Chasing", phase == ShooterEnemyPhase::Chasing );
        blackboard.setBool( "Attacking", phase == ShooterEnemyPhase::Attacking );
        blackboard.setBool( "Staggered", phase == ShooterEnemyPhase::Staggered );
        blackboard.setBool( "Dying", phase == ShooterEnemyPhase::Dying );
    }
} // namespace sw
