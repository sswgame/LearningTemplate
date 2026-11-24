// ArenaUnitPoolResetTest — 아레나 적의 몸과 AI 조종자를 풀(`PrefabPool`)에 돌려주면 다시 꺼낸 것이 갓 스폰한 것과 같다:
// 몸은 쓰러짐 표시 · 어트리뷰트 세트가 걷히고(디렉터가 다시 세트를 준다), AI 조종자는 쥔 폰을 놓는다. 인스턴스는 프리팹 대신 같은 컴포넌트를 코드로 단 오브젝트다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaEnemyAIComponent.h"
#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaUnitComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/PrefabPool.h"
#include "GameFramework/Runtime/Foundation/GameMode/Tests/PrefabPoolComponentSpawner.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"

using namespace sw;

/** @brief [ArenaUnitPoolResetTest] 쓰러진 졸개를 돌려주고 다시 꺼내면 같은 몸에 쓰러짐 표시 · 어트리뷰트 세트가 없고, AI 조종자는 폰을 놓은 채 다시 나온다 */
SW_TEST_CASE( ArenaUnitPoolResetTest, FallenGruntAndItsAIComeBackFresh )
{
    GameObjectManager manager;
    PrefabPool        bodyPool = PrefabPoolComponentSpawner<MeshComponent, AbilitySystemComponent, PawnComponent, ArenaUnitComponent>::makePool( 4 ); // 졸개 몸
    PrefabPool        aiPool   = PrefabPoolComponentSpawner<ArenaEnemyAIComponent>::makePool( 4 );                                                    // 졸개 AI 조종자
    GameObject*       pBody    = bodyPool.acquire( manager, "ArenaGrunt" );
    GameObject*       pAIBody  = aiPool.acquire( manager, "ArenaGruntAI" );
    SW_REQUIRE_TRUE( pBody != nullptr && pAIBody != nullptr ); // 처음 만든 인스턴스로 풀 계약을 확인했다
    AbilitySystemComponent* pAbility = pBody->getComponent<AbilitySystemComponent>();
    PawnComponent*          pPawn    = pBody->getComponent<PawnComponent>();
    ArenaEnemyAIComponent*  pAI      = pAIBody->getComponent<ArenaEnemyAIComponent>();
    SW_REQUIRE_TRUE( pAbility != nullptr && pPawn != nullptr && pAI != nullptr );

    // 디렉터가 세트를 주고 AI 가 쥔 뒤 쓰러졌다.
    SW_REQUIRE_NOT_NULL( pAbility->addAttributeSet<CombatAttributeSet>() );
    pAI->possess( *pPawn );
    SW_REQUIRE_TRUE( pPawn->isPossessed() );
    pAbility->getOwnedTagCounts().updateCount( CombatAttributeSet::getDeadTag(), 1 );

    // 디렉터의 정리 순서 — 조종자를 먼저, 그다음 몸(`ArenaGameModeComponent::retireUnit`).
    aiPool.release( manager, pAIBody->getHandle() );
    SW_EXPECT_FALSE( pPawn->isPossessed() );
    bodyPool.release( manager, pBody->getHandle() );

    SW_REQUIRE_TRUE( bodyPool.acquire( manager, "ArenaGrunt" ) == pBody );
    SW_REQUIRE_TRUE( aiPool.acquire( manager, "ArenaGruntAI" ) == pAIBody );
    SW_EXPECT_FALSE( pAbility->getOwnedTagCounts().hasMatchingTag( CombatAttributeSet::getDeadTag() ) );
    SW_EXPECT_TRUE( pAbility->findAttributeSetFor( CombatAttributes::health() ) == nullptr );
    SW_EXPECT_FALSE( pPawn->isPossessed() );
    SW_EXPECT_EQUAL( uint64{ 1 }, bodyPool.getCreatedCount().get() );
    SW_EXPECT_EQUAL( uint64{ 1 }, aiPool.getCreatedCount().get() );
}
