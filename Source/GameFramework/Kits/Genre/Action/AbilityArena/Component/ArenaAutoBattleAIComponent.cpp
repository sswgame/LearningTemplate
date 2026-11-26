#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaGameModeComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"

namespace sw
{
    /**
     * @class ArenaAutoBattleAIComponent
     * @brief 감각(체력 비율 · 대상 · 둘러싼 적 수 · 대시 중)만 냅니다. 판단(회복 · 근접 · 화염구 · 대시)은 행동 트리 데이터(`data/ai/autobattle.bt.xml`)이고, 플레이어와 같은 폰 버튼만 누릅니다.
     * @details 몸 안의 자동 플레이 분기가 아니라 조종자 바꾸기입니다(언리얼에서 자동 플레이 AIController 가 플레이어 폰을 Possess 하는 것과 같다) — 움직임 ·
     *          어빌리티는 키보드로 할 때와 같은 `ArenaUnitComponent` 를 지납니다. 플레이어 유닛은 움직이는 쪽을 바라봅니다(대시는 물러나는 쪽으로).
     *          블랙보드: `SelfPosition` · `TargetPosition`(위치), `TargetDistance`(m), `HealthRatio`(체력 어트리뷰트가 있을 때만), `NearbyEnemyCount`(`_crowdRadius` 안), `Dashing`.
     *          전투 문맥(어빌리티 · 디렉터 · 자기 자리)이 없으면 `SelfPosition` 을 지우고, 대상이 없으면 `TargetPosition` · `TargetDistance` 를 지웁니다.
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Auto Battle AI", Tooltip = "AI controller that plays the arena player while auto play is on; its behavior tree decides" )
    class ArenaAutoBattleAIComponent : public AIControllerComponent
    {
    public:
        REFLECT_BODY();

        ArenaAutoBattleAIComponent();
        virtual ~ArenaAutoBattleAIComponent() override = default;

    protected:
        void updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "Auto Play", DisplayName = "Crowd Radius", Tooltip = "Enemies inside this distance count into NearbyEnemyCount", Min = 0.0, Units = m )
        float32 _crowdRadius;
    };

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
        const AbilitySystemComponent* pAbilitySystem = pOwner != nullptr ? pOwner->getComponent<AbilitySystemComponent>() : nullptr;
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
        const optional<float32> health    = pAbilitySystem->getAttributeValue( CombatAttributes::health() );
        const optional<float32> maxHealth = pAbilitySystem->getAttributeValue( CombatAttributes::maxHealth() );
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

        int32                      nearbyEnemyCount = 0;
        const ArenaUnitView* const pSelfView        = pGameMode->findUnitView( pOwner->getHandle() ); // 스냅샷끼리 견준다(절대 자리 — 블랙보드 자리는 로컬이다)
        for ( const ArenaUnitView& view : pGameMode->getUnitViews() )
        {
            const bool bNearby = pSelfView != nullptr && view._kind != ArenaUnitKind::Player && view._bAlive == SW_TRUE && float3::getDistance( view._position._value, pSelfView->_position._value ) < _crowdRadius;
            if ( bNearby )
                ++nearbyEnemyCount;
        }
        blackboard.setInt( "NearbyEnemyCount", nearbyEnemyCount );
        blackboard.setBool( "Dashing", pAbilitySystem->getOwnedTagCounts().hasMatchingTag( "State.Dashing"_tag ) );
    }
} // namespace sw

#include "ArenaAutoBattleAIComponent.cpp.gen.inl"
