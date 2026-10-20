#include "pch.h"

#include "Games/AbilityArena/ArenaAutoBattleAIComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Base/Gameplay/Ability/CombatAttributeSet.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"
#include "Games/AbilityArena/ArenaUnitComponent.h"

namespace sw
{
    ArenaAutoBattleAIComponent::ArenaAutoBattleAIComponent()
        : _crowdRadius{ 2.5f }
        , _crowdCount{ 3 }
        , _meleeRange{ 2.0f }
        , _healBelow{ 0.5f }
    {
    }

    void ArenaAutoBattleAIComponent::think( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        (void)context;
        const GameObject*             pOwner         = pawn.getOwner();
        const AbilitySystemComponent* pAbilitySystem = pOwner != nullptr ? pOwner->getComponent<AbilitySystemComponent>() : nullptr;
        const ArenaDirectorComponent* pDirector      = ArenaDirectorComponent::findForPawn( pawn );
        float3                        selfPosition{};
        const bool                    bMissingBattleContext = pAbilitySystem == nullptr || pDirector == nullptr || pDirector->findUnitPosition( pOwner->getHandle(), selfPosition ) == false;
        if ( bMissingBattleContext )
        {
            stopMoving();
            return;
        }
        // 체력 어트리뷰트가 없는 유닛은 회복을 판단하지 않는다(전투 세트를 받지 않은 유닛).
        const std::optional<float32> health     = pAbilitySystem->getAttributeValue( CombatAttributes::health() );
        const std::optional<float32> maxHealth  = pAbilitySystem->getAttributeValue( CombatAttributes::maxHealth() );
        const bool                   bHasHealth = health.has_value() && maxHealth.has_value();
        if ( bHasHealth && *health / MathUtil::max( 1.0f, *maxHealth ) < _healBelow )
            pressButton( hashed_string( "Arena.Heal" ) );

        float3 targetPosition{};
        if ( pDirector->findNearestHostilePosition( pOwner->getHandle(), targetPosition ) == false )
        {
            stopMoving();
            return;
        }
        const float3  toTarget = targetPosition - selfPosition;
        const float32 distance = toTarget.getLength();
        const float3  forward  = ArenaUnitComponent::flattenDirection( toTarget, float3{ 0.0f, 0.0f, 1.0f } );

        // 둘러싸이면 반대쪽으로 대시 — 같은 틱에 물러나는 쪽으로 걸어 유닛이 그쪽을 바라본 뒤 대시가 그 쪽으로 미끄러진다.
        uint32 nearbyEnemyCount = 0;
        for ( const ArenaUnitView& view : pDirector->getUnitViews() )
        {
            const bool bNearby = view._kind != ArenaUnitKind::Player && view._bAlive == SW_TRUE && float3::getDistance( view._position, selfPosition ) < _crowdRadius;
            if ( bNearby )
                ++nearbyEnemyCount;
        }
        if ( nearbyEnemyCount >= _crowdCount )
        {
            moveTo( selfPosition - forward * 4.0f );
            pressButton( hashed_string( "Arena.Dash" ) );
            return;
        }
        if ( pAbilitySystem->getOwnedTagCounts().hasMatchingTag( "State.Dashing"_tag ) )
            return; // 대시는 바라보는 쪽으로 저절로 간다 — 걸어 둔 목적지를 그대로 둔다
        if ( distance > _meleeRange )
        {
            moveTo( targetPosition );
            pressButton( hashed_string( "Arena.Fireball" ) );
            return;
        }
        stopMoving();
        pressButton( hashed_string( "Arena.Melee" ) );
    }
} // namespace sw
