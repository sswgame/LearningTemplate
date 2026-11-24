// GameFlowTest — 게임 흐름의 화면 없이 도는 규칙: 팁 파일 읽기, UI 시스템이 없을 때의 로딩 화면, 타이틀 씬이 없는 게임(nogpu). 화면이 뜨는 길은 시나리오 `loadingscreen` 이 맡는다.
#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestWait.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)와 엔진 서비스를 직접 쓴다
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"

#include "GameFramework/Client/Flow/GameInstanceClient.h"
#include "GameFramework/Client/Flow/LoadingScreenController.h"
#include "GameFramework/Runtime/Foundation/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/Flow/ScreenTransitionManager.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/Save/SaveLock.h"
#include "GameFramework/Runtime/Foundation/Service/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Service/GameEvents.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

using namespace sw;

/** @brief [GameFlowTest] 팁 파일: 적은 순서대로 읽고 먼저 비운다. 팁이 없어도 파일은 맞다 */
SW_TEST_CASE( GameFlowTest, TipsFileReads )
{
    vector<string> listTip{ "stale" };
    SW_REQUIRE_TRUE( LoadingScreenController::loadTipsFromXMLText( "<LoadingTips><Tip text=\"first\"/><Tip text=\"second\"/></LoadingTips>", "tips", listTip ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), listTip.size() );
    SW_EXPECT_EQUAL( string( "second" ), listTip[1] );

    SW_REQUIRE_TRUE( LoadingScreenController::loadTipsFromXMLText( "<LoadingTips/>", "tips", listTip ) );
    SW_EXPECT_TRUE( listTip.empty() );
}

/** @brief [GameFlowTest] 팁 파일: 글이 빈 팁이 하나라도 있으면 오류이고 목록은 비어 있다(깨진 파일과 팁 없음을 섞지 않는다) */
SW_TEST_CASE( GameFlowTest, TipsFileRejectsEmptyText )
{
    SW_EXPECT_LOG( "LoadingScreen.TipNeedsNonEmpty" );
    vector<string> listTip;
    SW_EXPECT_FALSE( LoadingScreenController::loadTipsFromXMLText( "<LoadingTips><Tip text=\"ok\"/><Tip text=\"\"/><Tip/></LoadingTips>", "tips", listTip ) );
    SW_EXPECT_TRUE( listTip.empty() );
}

/** @brief [GameFlowTest] UI 시스템이 없으면(전용 서버) 로딩 화면은 뜨지 않고, 넘겨받은 페이드를 건드리지 않으며 표시 시간도 흐르지 않는다 */
SW_TEST_CASE( GameFlowTest, LoadingScreenWithoutUISystem )
{
    LoadingScreenController controller;
    SW_EXPECT_NULL( controller.getUISystem() );
    SW_EXPECT_FALSE( controller.beginLoading() );
    SW_EXPECT_FALSE( controller.isShowing() );
    SW_EXPECT_NULL( controller.findLoadingScreen() );

    ScreenTransitionManager transition;
    controller.update( 0.5f, false, sw::optional<float32>{ 0.5f }, transition );
    SW_EXPECT_NEAR( 0.0f, controller.getShownSeconds(), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, transition.fade().getOverlayAlpha(), 1.0e-6f );
    SW_EXPECT_NULL( controller.findFadeScreen() );

    SW_REQUIRE_TRUE( transition.beginTransition( Delegate<void()>{}, 0.1f, 0.1f ).has_value() );
    transition.update( 0.05f ); // 어두워지는 중이어도 패널을 열 UI 가 없다
    SW_EXPECT_TRUE( transition.fade().getOverlayAlpha() > 0.0f );
    controller.update( 0.5f, false, sw::nullopt, transition );
    SW_EXPECT_NULL( controller.findFadeScreen() );
    SW_EXPECT_TRUE( controller.getCurrentTip().empty() );
}

/** @brief [GameFlowTest] 씬 로드 실패 이벤트는 UI 가 없는 호스트(전용 서버 · 헤드리스)에서 아무 화면도 열지 않고, 컨트롤러가 내려간 뒤에는 받지 않는다 */
SW_TEST_CASE( GameFlowTest, FailedSceneLoadWithoutUISystem )
{
    EventDispatcher dispatcher;
    SubsystemScope  scope;
    SW_REQUIRE_TRUE( scope.bind<EventDispatcher>( &dispatcher ) );
    game::ScopedGameScope   scopedScope( scope );
    LoadingScreenController controller;
    SW_REQUIRE_TRUE( controller.initializeSubsystem() == SubsystemInitResult::Succeeded );
    SceneLoadCompletedEvent failed;
    failed._bSuccess = false;
    GameEventUtil::send( failed );
    SW_EXPECT_FALSE( controller.isShowingLoadFailure() );
    controller.shutdownSubsystem();
    GameEventUtil::send( failed );
    SW_EXPECT_FALSE( controller.isShowingLoadFailure() );
}

/** @brief [GameFlowTest] 저장 · 불러오기 실패 이벤트는 UI 가 없는 호스트(전용 서버 · 헤드리스)에서 아무 화면도 열지 않고, 컨트롤러가 내려간 뒤에는 받지 않는다 */
SW_TEST_CASE( GameFlowTest, FailedSaveWithoutUISystem )
{
    EventDispatcher dispatcher;
    SubsystemScope  scope;
    SW_REQUIRE_TRUE( scope.bind<EventDispatcher>( &dispatcher ) );
    game::ScopedGameScope   scopedScope( scope );
    LoadingScreenController controller;
    SW_REQUIRE_TRUE( controller.initializeSubsystem() == SubsystemInitResult::Succeeded );
    const size_t       subscriberCount = dispatcher.getSubscriberCount();
    SaveGameSavedEvent saved;
    saved._bSuccess = false;
    GameEventUtil::send( saved );
    SaveGameLoadedEvent loaded;
    loaded._bSuccess = false;
    GameEventUtil::send( loaded );
    SW_EXPECT_FALSE( controller.isShowingSaveFailure() );
    controller.shutdownSubsystem();
    SW_EXPECT_EQUAL( subscriberCount - 3u, dispatcher.getSubscriberCount() ); // 씬 로드 · 저장 · 불러오기
}

/** @brief [GameFlowTest] 타이틀 씬이 활성인 동안 세이브가 막힌다 — 타이틀을 저장하면 이어 하기가 타이틀을 연다. 다른 씬이 서면 풀린다 */
SW_TEST_CASE( GameFlowTest, SavingIsBlockedOnTheTitleScene )
{
    const test::ScopedScratchDomain domain;
    SceneManager                    sceneManager;
    SW_REQUIRE_NOT_NULL( sceneManager.createEmptyActiveScene( "Title" ) );
    SW_REQUIRE_TRUE( sceneManager.saveActiveSceneCopy( domain.makeDiskPath( "title.scene.xml" ) ) );
    SW_REQUIRE_TRUE( sceneManager.saveActiveSceneCopy( domain.makeDiskPath( "map.scene.xml" ) ) );

    SubsystemScope              scope;
    const game::ScopedGameScope scopedGame{ scope };
    SW_REQUIRE_TRUE( scope.bind( &sceneManager ) );
    SW_REQUIRE_TRUE( scope.bind( &engine::getSubsystem<GameTimeScale>() ) );
    SW_REQUIRE_TRUE( sceneManager.initializeSubsystem() == SubsystemInitResult::Succeeded );
    const string                             titlePath = test::ScopedScratchDomain::makeID( "title.scene.xml" ).str();
    const string                             mapPath   = test::ScopedScratchDomain::makeID( "map.scene.xml" ).str();
    GameSettings                             settings;
    StringBuilder<constant::buffer::kMax512> settingsText;
    settingsText.appendFormat( "<GameSettings><startMap>%#</startMap><titleScene>%#</titleScene><defaultLanguage>ko_kr</defaultLanguage></GameSettings>", mapPath, titlePath );
    SW_REQUIRE_TRUE( settings.loadFromXMLText( settingsText.view(), "GameFlowTest" ) );
    ScreenTransitionManager transition;
    SaveLock                saveLock;
    GameSceneFlow           flow( settings, transition, nullptr, saveLock );
    SW_REQUIRE_TRUE( flow.initializeSubsystem() == SubsystemInitResult::Succeeded );

    const auto travel = [&]()
    {
        return test::waitUntil( [&]()
        {
            sceneManager.updateTransitions();
            flow.update();
            return flow.hasPendingSceneLoad() == false;
        } );
    };
    SW_REQUIRE_TRUE( flow.requestTitleScene() );
    SW_EXPECT_TRUE( flow.hasPendingSceneLoad() );
    SW_REQUIRE_TRUE( travel() );
    SW_EXPECT_TRUE( saveLock.isBlocked() ); // 로드는 끝났다 — 타이틀이라 막혀 있다
    SW_REQUIRE_TRUE( flow.requestEntranceScene() );
    SW_REQUIRE_TRUE( travel() );
    SW_EXPECT_FALSE( saveLock.isBlocked() );
    flow.shutdownSubsystem();
    sceneManager.shutdownSubsystem();
}

/** @brief [GameFlowTest] 타이틀 씬이 없는 게임은 타이틀 요청이 false 이고 "타이틀로" 명령을 걸지 않는다("시작" 은 건다) */
SW_TEST_CASE( GameFlowTest, GameWithoutTitleScene )
{
    GameSettings            settings;
    ScreenTransitionManager transition;
    LoadingScreenController loading;
    SaveLock                saveLock;
    GameSceneFlow           flow( settings, transition, &loading.getLoadingScreen(), saveLock );
    SW_EXPECT_FALSE( flow.requestTitleScene() );

    UIGlobalCommandRegistry commands;
    GameSessionCommands::bind( flow, commands );
    SW_EXPECT_FALSE( commands.hasCommand( hashed_string( UIGlobalCommandRegistry::kReturnToTitle ) ) );
    // "시작" 은 타이틀 씬과 무관하게 걸리고(메인 메뉴), 거두면 함께 빠진다.
    SW_EXPECT_TRUE( commands.hasCommand( hashed_string( UIGlobalCommandRegistry::kStartGame ) ) );
    flow.unregisterSessionCommands();
    SW_EXPECT_FALSE( commands.hasCommand( hashed_string( UIGlobalCommandRegistry::kStartGame ) ) );
}

/**
 * @brief [GameFlowTest] 타이틀 복귀는 하던 게임을 **먼저** 끝내고 타이틀 씬을 요청한다 — 요청 뒤에 시작한 것(로딩 화면 · 타이틀의 음악)은 남는다
 * @details 결함이면 타이틀 씬이 시작한 음악 · 화면이 게임을 끝내는 비우기에 함께 꺼진다. 엔진 이벤트 큐(게임 인스턴스 수명)를 표지로 본다.
 */
SW_TEST_CASE( GameFlowTest, TitleEndsTheGameBeforeItRequestsTheScene )
{
    const test::ScopedScratchDomain domain;
    SceneManager                    sceneManager;
    SW_REQUIRE_NOT_NULL( sceneManager.createEmptyActiveScene( "Title" ) );
    SW_REQUIRE_TRUE( sceneManager.saveActiveSceneCopy( domain.makeDiskPath( "title.scene.xml" ) ) );

    SubsystemScope              scope;
    const game::ScopedGameScope scopedGame{ scope };
    SW_REQUIRE_TRUE( scope.bind( &sceneManager ) );
    GameSettings                             settings;
    StringBuilder<constant::buffer::kMax512> settingsText;
    settingsText.appendFormat( "<GameSettings><startMap>%#</startMap><titleScene>%#</titleScene><defaultLanguage>ko_kr</defaultLanguage></GameSettings>",
                               test::ScopedScratchDomain::makeID( "title.scene.xml" ).str(), test::ScopedScratchDomain::makeID( "title.scene.xml" ).str() );
    SW_REQUIRE_TRUE( settings.loadFromXMLText( settingsText.view(), "GameFlowTest" ) );
    ScreenTransitionManager transition;
    LoadingScreenController loading;
    SaveLock                saveLock;
    GameSceneFlow           flow( settings, transition, &loading.getLoadingScreen(), saveLock );

    EventDispatcher& dispatcher = engine::getSubsystem<EventDispatcher>();
    dispatcher.push( SceneLoadCompletedEvent{} ); // 하던 게임이 남긴 것
    bool               bEndedBeforeRequest = false;
    const Subscription subscription        = sceneManager.onSceneLoadRequested().add( SW_DELEGATE_LAMBDA( SceneLoadRequestedHandler, [&]( const string& scenePath, SceneLoadKind kind, const TaskFuture<Scene*>& future )
           {
        (void)scenePath;
        (void)kind;
        (void)future;
        bEndedBeforeRequest = dispatcher.findGameInstanceState() == nullptr;
        dispatcher.push( SceneLoadCompletedEvent{} ); // 타이틀이 시작한 것
    } ) );
    SW_EXPECT_TRUE( flow.requestTitleScene() );
    SW_EXPECT_TRUE( bEndedBeforeRequest );
    SW_EXPECT_TRUE( dispatcher.findGameInstanceState() != nullptr );
    dispatcher.resetGameInstanceState( GameInstanceBoundary::Ended );
}
