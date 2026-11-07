#include "pch.h"

#include "Games/AbilityArena/ArenaAutoBattleAIComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"

#include "Games/AbilityArena/ArenaGameModeComponent.h"

namespace sw
{
    ArenaAutoBattleAIComponent::ArenaAutoBattleAIComponent()
        : _crowdRadius{ 2.5f }
    {
        requireBehaviorTree();
    }

    void ArenaAutoBattleAIComponent::updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        (void)context;
        Blackboard&                   blackboard     = getBlackboard();
        const GameObject*             pOwner         = pawn.getOwner();
        const AbilityComponent*       pAbilitySystem = pOwner != nullptr ? pOwner->getComponent<AbilityComponent>() : nullptr;
        const ArenaGameModeComponent* pGameMode      = ArenaGameModeComponent::findForPawn( pawn );
        float3                        selfPosition{};
        const bool                    bMissingBattleContext = pAbilitySystem == nullptr || pGameMode == nullptr || pGameMode->findUnitPosition( pOwner->getHandle(), selfPosition ) == false;
        if ( bMissingBattleContext )
        {
            blackboard.clear();
            return;
        }
        blackboard.setVector( "SelfPosition", selfPosition );

        // 체력 어트리뷰트가 없는 유닛은 비율을 적지 않는다(전투 세트를 받지 않은 유닛) — 트리의 비교가 거짓이라 회복하지 않는다.
        const std::optional<float32> health    = pAbilitySystem->getAttributeValue( CombatAttributes::health() );
        const std::optional<float32> maxHealth = pAbilitySystem->getAttributeValue( CombatAttributes::maxHealth() );
        if ( health.has_value() && maxHealth.has_value() )
            blackboard.setFloat( "HealthRatio", *health / MathUtil::max( 1.0f, *maxHealth ) );
        else
            blackboard.clearValue( "HealthRatio" );

        float3 targetPosition{};
        if ( pGameMode->findNearestHostilePosition( pOwner->getHandle(), targetPosition ) )
        {
            blackboard.setVector( "TargetPosition", targetPosition );
            blackboard.setFloat( "TargetDistance", ( targetPosition - selfPosition ).getLength() );
        }
        else
        {
            blackboard.clearValue( "TargetPosition" );
            blackboard.clearValue( "TargetDistance" );
        }

        int32 nearbyEnemyCount = 0;
        for ( const ArenaUnitView& view : pGameMode->getUnitViews() )
        {
            const bool bNearby = view._kind != ArenaUnitKind::Player && view._bAlive == SW_TRUE && float3::getDistance( view._position, selfPosition ) < _crowdRadius;
            if ( bNearby )
                ++nearbyEnemyCount;
        }
        blackboard.setInt( "NearbyEnemyCount", nearbyEnemyCount );
        blackboard.setBool( "Dashing", pAbilitySystem->getOwnedTagCounts().hasMatchingTag( "State.Dashing"_tag ) );
    }
} // namespace sw
