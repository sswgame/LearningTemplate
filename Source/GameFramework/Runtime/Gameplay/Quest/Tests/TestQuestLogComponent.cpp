// QuestLogComponentTest — 일지 주인이 "game" 채널의 쓰러짐 · 상호작용 완료 · 연출 신호를 구독해, 자기가 조종하는 폰이 한 것만 Defeat(대상 태그) · Interact(이름) · Cue(신호 이름)로 세는지 본다(nogpu, 헤드리스).
#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/File/FileUtil.h"
#include "Core/Name/TagID.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityEvents.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickEvents.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionAuthority.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

using namespace sw;

namespace
{
    constexpr const utf8* kQuestXML = R"(<QuestCatalog>
  <Quest id="hunt"><Stage id="kill" next="end"><Objective kind="Defeat" target="Enemy.Wolf" count="2"/></Stage><Stage id="end" complete="true"/></Quest>
  <Quest id="scare"><Stage id="enter" next="end"><Objective kind="Cue" target="Cue.Scare"/></Stage><Stage id="end" complete="true"/></Quest>
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
    SW_REQUIRE_TRUE( pQuests->initialize( catalog ) == GameStateInitResult::Fresh );
    manager.beginPlay();
    pPlayer->possess( *pPawn );
    QuestLog& log = pQuests->getQuestLog();
    SW_REQUIRE_TRUE( log.start( hashed_string( "hunt" ), 1 ) == QuestStartResult::Ok );
    SW_REQUIRE_TRUE( log.start( hashed_string( "shrine" ), 1 ) == QuestStartResult::Ok );
    SW_REQUIRE_TRUE( log.start( hashed_string( "scare" ), 1 ) == QuestStartResult::Ok );

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

    GimmickCueEvent cue;
    cue._cue    = hashed_string( "Cue.Scare" );
    cue._target = pStranger->getHandle();
    GameEventUtil::send( cue ); // 남이 트리거에 들어갔다 — 세지 않는다
    cue._target = GameObjectHandle{};
    GameEventUtil::send( cue ); // 부른 쪽이 없는 신호(폭발 · 파괴) — 세지 않는다
    SW_EXPECT_TRUE( log.getStatus( hashed_string( "scare" ) ) == QuestStatus::Active );
    cue._target = pPawnObject->getHandle();
    GameEventUtil::send( cue );
    SW_EXPECT_TRUE( log.getStatus( hashed_string( "scare" ) ) == QuestStatus::Completed );
}

/** @brief [QuestLogComponentTest] 카탈로그가 다시 읽히면 일지가 진행을 지킨 채 새 판에 묶인다 — 더한 퀘스트를 바로 받을 수 있다 */
SW_TEST_CASE( QuestLogComponentTest, ReloadedCatalogKeepsTheProgress )
{
    const test::ScopedScratchDomain domain;
    const string                    id           = test::ScopedScratchDomain::makeID( "reload.quest.xml" ).str();
    const auto                      writeCatalog = [&domain]( string_view xml )
    { return FileUtil::writeTextFile( domain.makeDiskPath( "reload.quest.xml" ), xml ); };
    SW_REQUIRE_TRUE( writeCatalog( kQuestXML ) );
    const SharedAssetRef<QuestCatalog> catalog = SharedAssetCache<QuestCatalog>::acquire( id );
    SW_REQUIRE_TRUE( catalog != nullptr );

    EventDispatcher dispatcher;
    SubsystemScope  scope;
    SW_REQUIRE_TRUE( scope.bind<EventDispatcher>( &dispatcher ) );
    game::ScopedGameScope scopedScope( scope );
    GameObjectManager     manager;
    QuestLogComponent*    pQuests = manager.createGameObject( hashed_string( "Controller" ) )->addComponent<QuestLogComponent>();
    SW_REQUIRE_NOT_NULL( pQuests );
    SW_REQUIRE_TRUE( pQuests->initialize( *catalog ) == GameStateInitResult::Fresh );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pQuests->getQuestLog().start( hashed_string( "hunt" ), 1 ) == QuestStartResult::Ok );
    SW_EXPECT_FALSE( pQuests->getQuestLog().start( hashed_string( "extra" ), 1 ) == QuestStartResult::Ok );

    SW_REQUIRE_TRUE( writeCatalog( R"(<QuestCatalog>
  <Quest id="hunt"><Stage id="kill" next="end"><Objective kind="Defeat" target="Enemy.Wolf" count="3"/></Stage><Stage id="end" complete="true"/></Quest>
  <Quest id="extra"><Stage id="enter" next="end"><Objective kind="Cue" target="Cue.Scare"/></Stage><Stage id="end" complete="true"/></Quest>
</QuestCatalog>)" ) );
    SW_REQUIRE_TRUE( SharedAssetCache<QuestCatalog>::reloadShared( id ) );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_TRUE( pQuests->getQuestLog().getStatus( hashed_string( "hunt" ) ) == QuestStatus::Active );
    SW_EXPECT_TRUE( pQuests->getQuestLog().start( hashed_string( "extra" ), 1 ) == QuestStartResult::Ok );
    manager.endPlay();
}
