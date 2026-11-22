// QuestLogComponentTest — 일지 주인이 "game" 채널의 쓰러짐 · 상호작용 완료를 구독해, 자기가 조종하는 폰이 한 것만 Defeat(대상 태그) · Interact(이름)로 세는지 본다(nogpu, 헤드리스).
#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/Name/TagID.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityEvents.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionAuthority.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

using namespace sw;

namespace
{
    constexpr const utf8* kQuestXML = R"(<QuestCatalog>
  <Quest id="hunt"><Stage id="kill" next="end"><Objective kind="Defeat" target="Enemy.Wolf" count="2"/></Stage><Stage id="end" complete="true"/></Quest>
  <Quest id="shrine"><Stage id="pray" next="end"><Objective kind="Interact" target="pray"/></Stage><Stage id="end" complete="true"/></Quest>
</QuestCatalog>)";
} // namespace

/**
 * @brief [QuestLogComponentTest] 자기 폰이 쓰러뜨린 태그 맞는 대상 · 자기 폰의 상호작용만 센다 — 다른 폰이 한 일과 태그가 다른 대상은 세지 않는다
 */
SW_TEST_CASE( QuestLogComponentTest, CountsOnlyWhatItsOwnPawnDid )
{
    SW_EXPECT_LOG( "Control.AxisActionNotInInputMap" ); // 기본 폰의 Look 축이 빈 입력 맵에 없다 — 이 테스트는 퀘스트 세기만 본다
    EventDispatcher dispatcher;
    SubsystemScope  scope;
    SW_REQUIRE_TRUE( scope.bind<EventDispatcher>( &dispatcher ) );
    game::ScopedGameScope scopedScope( scope );

    QuestCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kQuestXML, "QuestLogComponentTest" ) );

    GameObjectManager          manager;
    GameObject*                pController = manager.createGameObject( hashed_string( "Controller" ) );
    QuestLogComponent*         pQuests     = pController->addComponent<QuestLogComponent>();
    PlayerControllerComponent* pPlayer     = pController->addComponent<PlayerControllerComponent>();
    GameObject*                pPawnObject = manager.createGameObject( hashed_string( "Pawn" ) );
    PawnComponent*             pPawn       = pPawnObject->addComponent<PawnComponent>();
    GameObject*                pStranger   = manager.createGameObject( hashed_string( "Stranger" ) ); // 조종자 없는 몸(다른 플레이어 · AI 자리)
    GameObject*                pEnemy      = manager.createGameObject( hashed_string( "Enemy" ) );
    GameObject*                pAnimal     = manager.createGameObject( hashed_string( "Animal" ) );
    pEnemy->addTag( TagID::request( "Enemy.Wolf" ) );
    pAnimal->addTag( TagID::request( "Animal.Deer" ) );
    SW_REQUIRE_TRUE( pQuests->initialize( &catalog ) == GameStateInitResult::Fresh );
    manager.beginPlay();
    pPlayer->possess( *pPawn );
    QuestLog& log = pQuests->getQuestLog();
    SW_REQUIRE_TRUE( log.start( hashed_string( "hunt" ), 1 ) == QuestStartResult::Ok );
    SW_REQUIRE_TRUE( log.start( hashed_string( "shrine" ), 1 ) == QuestStartResult::Ok );

    AbilityOwnerDiedEvent died;
    died._target     = pEnemy->getHandle();
    died._instigator = pStranger->getHandle();
    GameEventUtil::send( died ); // 남이 쓰러뜨렸다 — 세지 않는다
    died._target     = pAnimal->getHandle();
    died._instigator = pPawnObject->getHandle();
    GameEventUtil::send( died ); // 태그가 다른 대상 — 세지 않는다
    died._target = pEnemy->getHandle();
    GameEventUtil::send( died );
    SW_EXPECT_TRUE( log.getStatus( hashed_string( "hunt" ) ) == QuestStatus::Active ); // 2 마리 중 1
    GameEventUtil::send( died );
    SW_EXPECT_TRUE( log.getStatus( hashed_string( "hunt" ) ) == QuestStatus::Completed );

    InteractionCompletedEvent completed;
    completed._request._interactor  = pStranger->getHandle();
    completed._request._interaction = hashed_string( "pray" );
    GameEventUtil::send( completed ); // 남이 했다 — 세지 않는다
    SW_EXPECT_TRUE( log.getStatus( hashed_string( "shrine" ) ) == QuestStatus::Active );
    completed._request._interactor = pPawnObject->getHandle();
    GameEventUtil::send( completed );
    SW_EXPECT_TRUE( log.getStatus( hashed_string( "shrine" ) ) == QuestStatus::Completed );
}
