// GameFlowTest — 게임 흐름의 화면 없이 도는 규칙: 팁 파일 읽기, UI 시스템이 없을 때의 로딩 화면, 타이틀 씬이 없는 게임(nogpu). 화면이 뜨는 길은 시나리오 `loadingscreen` 이 맡는다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"

#include "GameFramework/Client/Flow/GameInstanceClient.h"
#include "GameFramework/Client/Flow/LoadingScreenController.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/ScreenTransitionManager.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/SaveLock.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

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

    ScreenFade fade;
    controller.update( 0.5f, false, sw::optional<float32>{ 0.5f }, fade );
    SW_EXPECT_NEAR( 0.0f, controller.getShownSeconds(), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, fade.getOverlayAlpha(), 1.0e-6f );
    SW_EXPECT_NULL( controller.findFadeScreen() );

    fade.beginFadeOut( 0.1f );
    fade.update( 0.05f ); // 어두워지는 중이어도 패널을 열 UI 가 없다
    SW_EXPECT_TRUE( fade.getOverlayAlpha() > 0.0f );
    controller.update( 0.5f, false, sw::nullopt, fade );
    SW_EXPECT_NULL( controller.findFadeScreen() );
    SW_EXPECT_TRUE( controller.getCurrentTip().empty() );
}

/** @brief [GameFlowTest] 타이틀 씬이 없는 게임은 타이틀 요청이 false 이고 "타이틀로" 명령을 걸지 않는다 */
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
    flow.unregisterSessionCommands();
}
