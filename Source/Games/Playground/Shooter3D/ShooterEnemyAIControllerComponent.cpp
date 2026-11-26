#include "pch.h"

#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

#include "Games/Playground/Shooter3D/ShooterEnemyComponent.h"
#include "Games/Playground/Shooter3D/ShooterGameModeComponent.h"

namespace sw
{
    /**
     * @class ShooterEnemyAIControllerComponent
     * @brief 적 폰(`prefabs/quadshell.prefab.xml` · `trilobite.prefab.xml` — `_autoPossess AI`)이 플레이를 시작하면 조종 시스템이 `prefabs/enemy_ai.prefab.xml` 로 세워 빙의시킵니다.
     * @details 쫓는 동안 트리가 플레이어 발로 `ai.moveTo` 합니다 — 같은 오브젝트의 내비메시 에이전트(`SteerOnly`)가 경로 · 이웃 비키기로 속도를 내고,
     *          그것이 이동 의도가 되어 몸 이동(`ShooterBodyMovementComponent`)이 움직입니다. 휘두르기 간격 · 맞는 시각은 폰의 규칙입니다.
     *          블랙보드: `PlayerFeet`(위치) · `PlayerDistance`(수평 m) · `Reach`(적의 손 닿는 거리) · `StopDistance`(손 닿는 거리 x 멈춤 비율) · `PlayerAlive`,
     *          몸의 단계(`ShooterEnemyPhase`)는 참거짓 `Rising` · `Chasing` · `Attacking` · `Staggered` · `Dying` 중 하나만 참이다. 디렉터가 없거나 적이 죽었으면 `PlayerFeet` 를 지운다.
     *          폰 상태 · 플레이어 자리는 디렉터가 앞 프레임에 적은 것을 읽습니다(조종 단계 — 틱 전, 게임 스레드).
     */
    REFLECT( Category = "Shooter", DisplayName = "Shooter Enemy AI Controller", Tooltip = "Skeleton AI: senses the player and its body phase; its behavior tree chases and holds Attack inside reach" )
    class ShooterEnemyAIControllerComponent : public AIControllerComponent
    {
    public:
        REFLECT_BODY();

        ShooterEnemyAIControllerComponent();
        ~ShooterEnemyAIControllerComponent() override = default;

    protected:
        void updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn ) override;
    };

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

#include "ShooterEnemyAIControllerComponent.cpp.gen.inl"
