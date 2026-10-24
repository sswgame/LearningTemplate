#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Base/Foundation/Data/GameSettings.h"
#include "GameFramework/Base/Foundation/Framework/Flow/LoadingScreenController.h"
#include "GameFramework/Base/Foundation/Framework/Flow/ScreenTransitionManager.h"

#include "TestFramework/TestFramework.h"

// LoadingScreenTest — 로딩 화면(비동기 씬 전환 동안 Loading 층 · 최소 표시 시간 · 닫으면 페이드 인)과 화면 페이드 패널. 엔진 견본 문서(engine/ui/loading.ui.xml)를 읽는다. 디바이스 없음(nogpu).

namespace
{
    /** @brief 입력 관리자와 그것을 읽는 UI 시스템, 그 위의 로딩 화면 제어입니다. */
    struct LoadingScreenFixture
    {
        static constexpr float32 kFrameSeconds = 0.1f;

        sw::InputManager            _input;
        sw::UISystem                _ui;
        sw::ScreenFade              _fade;
        sw::LoadingScreenController _loading;

        LoadingScreenFixture()
            : _input{}
            , _ui{}
            , _fade{}
            , _loading{}
        {
            SW_EXPECT_TRUE( sw::ResourcePaths::initialize() );
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr ) );
            _loading.bindUISystem( &_ui );
        }

        ~LoadingScreenFixture()
        {
            _loading.bindUISystem( nullptr );
            _ui.shutdown();
            _input.shutdown();
        }

        /** @brief 한 프레임 — 페이드 → 로딩 제어 → UI 입력 · 갱신(닫기 요청이 적용된다). 게임 인스턴스와 같은 순서다. */
        void runFrame( bool bLoading, std::optional<float32> loadProgress = std::nullopt )
        {
            _fade.update( kFrameSeconds );
            _loading.update( kFrameSeconds, bLoading, loadProgress, _fade );
            _input.beginFrame( kFrameSeconds );
            _ui.processInput( kFrameSeconds );
            _ui.update( kFrameSeconds, sw::UIViewport{
                                           sw::float2{ 1920.0f, 1080.0f }
            } );
            _input.endFrame();
        }
    };
} // namespace

/**
 * @brief [LoadingScreenTest] 로딩 화면 문서를 적지 않은 게임(gamesettings 에 loadingScreen 없음)은 로딩 화면을 띄우지 않는다 — 엔진 문서로 대체하지 않는다
 */
SW_TEST_CASE( LoadingScreenTest, NoDocumentMeansNoLoadingScreen )
{
    LoadingScreenFixture fixture;
    SW_EXPECT_FALSE( fixture._loading.beginLoading() );
    fixture.runFrame( true );
    SW_EXPECT_TRUE( fixture._loading.findLoadingScreen() == nullptr );
    SW_EXPECT_FALSE( fixture._ui.isLoadingScreenShown() );
}

/**
 * @brief [LoadingScreenTest] 로딩 화면은 로드가 0.1 초에 끝나도 최소 표시 시간(0.5 초)까지 떠 있고, 그 뒤 닫히며 페이드 인을 건다 · 떠 있는 동안 게임 입력을 막는다
 * @details 빠른 로드에서 로딩 화면이 한 프레임만 깜박이지 않게(Lyra `ULoadingScreenManager` 의 최소 표시 시간). 변이: `update` 의 최소 시간 조건을 빼면 로드가
 *          끝난 첫 프레임에 닫혀 진다.
 */
SW_TEST_CASE( LoadingScreenTest, LoadingScreenStaysForMinimumTime )
{
    LoadingScreenFixture      fixture;
    sw::LoadingScreenSettings settings{};
    // 경로 칸(`DataPath`)은 데이터 읽기만 채운다 — 게임처럼 gamesettings 의 loadingScreen 을 읽어 옮긴다(시작 맵은 필수 칸이라 함께 적는다).
    sw::GameSettings gameSettings{};
    SW_ASSERT_TRUE( gameSettings.loadFromXMLText(
        "<GameSettings><startMap>engine/ui/loading.ui.xml</startMap><loadingScreen>engine/ui/loading.ui.xml</loadingScreen></GameSettings>", "test" ) );
    settings._documentPath = gameSettings._loadingScreen;
    settings._listTip.push_back( "Tip one" );
    fixture._loading.setSettings( settings );
    SW_ASSERT_TRUE( fixture._loading.beginLoading() );
    fixture.runFrame( true );
    const sw::UIScreen* pScreen = fixture._loading.findLoadingScreen();
    SW_ASSERT_NOT_NULL( pScreen );
    SW_EXPECT_TRUE( pScreen->getDesc()._layer == sw::UILayer::Loading );
    SW_EXPECT_TRUE( fixture._ui.isGameInputBlocked() );
    SW_EXPECT_TRUE( fixture._ui.isLoadingScreenShown() ); // 자동화의 "씬 플레이 중" 은 이것이 걷힌 뒤다
    // 팁은 목록에서 고른 것이 Tip 글 위젯에 들어간다(표에 없는 키는 원문 그대로).
    SW_EXPECT_STREQ( "Tip one", fixture._loading.getCurrentTip().c_str() );
    const sw::TextWidget* pTip = pScreen->getTree().findWidget<sw::TextWidget>( sw::hashed_string( sw::LoadingScreenController::kTipWidgetName ) );
    SW_ASSERT_NOT_NULL( pTip );
    SW_EXPECT_STREQ( "Tip one", pTip->getText().c_str() );

    // 로드는 끝났지만 아직 0.5 초가 안 됐다 — 그대로 떠 있다.
    for ( uint32 frame = 0; frame < 3; ++frame )
    {
        fixture.runFrame( false );
    }
    SW_EXPECT_TRUE( fixture._loading.isShowing() );
    SW_EXPECT_NEAR_EQUAL( 0.4f, fixture._loading.getShownSeconds(), 1e-4f );
    SW_EXPECT_TRUE( fixture._fade.getOverlayAlpha() == 0.0f );

    // 0.5 초를 넘긴 프레임에 닫고 페이드 인(검정 → 씬)을 건다 — 페이드 패널은 입력을 막지 않는다.
    fixture.runFrame( false );
    fixture.runFrame( false );
    SW_EXPECT_FALSE( fixture._loading.isShowing() );
    SW_EXPECT_TRUE( fixture._fade.getPhase() == sw::FadePhase::FadingIn );
    SW_EXPECT_NOT_NULL( fixture._loading.findFadeScreen() );
    SW_EXPECT_FALSE( fixture._ui.isLoadingScreenShown() );
    SW_EXPECT_FALSE( fixture._ui.isGameInputBlocked() );

    // 로드가 길면 그동안 계속 떠 있다.
    SW_ASSERT_TRUE( fixture._loading.beginLoading() );
    for ( uint32 frame = 0; frame < 20; ++frame )
    {
        fixture.runFrame( true );
    }
    SW_EXPECT_TRUE( fixture._loading.isShowing() );
}

/**
 * @brief [LoadingScreenTest] 화면 페이드의 알파가 전체 화면 검은 패널(Overlay 층)의 불투명도가 되고, 알파 0 이면 패널 화면이 닫힌다
 * @details `ScreenFade` 는 알파를 계산만 하고 그리는 곳이 없었다(`getOverlayAlpha` 호출 0). 변이: `syncFadeOverlay` 가 불투명도를 넣지 않으면 패널이 늘 불투명이라 진다.
 */
SW_TEST_CASE( LoadingScreenTest, FadeOverlayFollowsScreenFadeAlpha )
{
    LoadingScreenFixture fixture;
    fixture.runFrame( false );
    SW_EXPECT_NULL( fixture._loading.findFadeScreen() ); // 알파 0 — 패널이 없다

    fixture._fade.beginFadeOut( 1.0f );
    for ( uint32 frame = 0; frame < 5; ++frame )
    {
        fixture.runFrame( false );
    }
    const sw::UIScreen* pFade = fixture._loading.findFadeScreen();
    SW_ASSERT_NOT_NULL( pFade );
    SW_EXPECT_TRUE( pFade->getDesc()._layer == sw::UILayer::Overlay );
    SW_EXPECT_FALSE( fixture._ui.isGameInputBlocked() );
    const sw::Widget* pPanel = pFade->getTree().getRoot();
    SW_ASSERT_NOT_NULL( pPanel );
    SW_EXPECT_NEAR_EQUAL( 0.5f, fixture._fade.getOverlayAlpha(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pPanel->getOpacity(), 1e-4f );

    // 다 어두워지면 1, 페이드 인이 끝나면 0 — 패널 화면을 닫는다.
    for ( uint32 frame = 0; frame < 6; ++frame )
    {
        fixture.runFrame( false );
    }
    SW_EXPECT_NEAR_EQUAL( 1.0f, fixture._loading.findFadeScreen()->getTree().getRoot()->getOpacity(), 1e-4f );
    fixture._fade.beginFadeIn( 0.2f );
    for ( uint32 frame = 0; frame < 3; ++frame )
    {
        fixture.runFrame( false );
    }
    SW_EXPECT_TRUE( fixture._fade.getOverlayAlpha() == 0.0f );
    SW_EXPECT_NULL( fixture._loading.findFadeScreen() );
}

/**
 * @brief [LoadingScreenTest] 진행 슬라이더(`Progress`)는 로드 진행이 있을 때만 보이고 그 값을 따르며, 진행을 모르면 접는다 · 입력은 받지 않는다
 * @details 진행은 씬 매니저의 프리로드 세트가 안다(`SceneManager::tryGetLoadProgress`) — 쿠킹이 구운 목록이 없는 씬은 모른다.
 */
SW_TEST_CASE( LoadingScreenTest, ProgressBarFollowsTheLoadProgressWhenKnown )
{
    LoadingScreenFixture      fixture;
    sw::LoadingScreenSettings settings{};
    sw::GameSettings          gameSettings{};
    SW_ASSERT_TRUE( gameSettings.loadFromXMLText(
        "<GameSettings><startMap>engine/ui/loading.ui.xml</startMap><loadingScreen>engine/ui/loading.ui.xml</loadingScreen></GameSettings>", "test" ) );
    settings._documentPath = gameSettings._loadingScreen;
    fixture._loading.setSettings( settings );
    SW_ASSERT_TRUE( fixture._loading.beginLoading() );

    fixture.runFrame( true ); // 진행을 모른다 — 접혀 있다
    sw::UIScreen* pScreen = fixture._loading.findLoadingScreen();
    SW_ASSERT_NOT_NULL( pScreen );
    sw::SliderWidget* pProgress = pScreen->getTree().findWidget<sw::SliderWidget>( sw::hashed_string( sw::LoadingScreenController::kProgressWidgetName ) );
    SW_ASSERT_NOT_NULL( pProgress );
    SW_EXPECT_TRUE( pProgress->getVisibility() == sw::WidgetVisibility::Collapsed );

    fixture.runFrame( true, 0.25f );
    SW_EXPECT_TRUE( pProgress->getVisibility() == sw::WidgetVisibility::HitTestInvisible );
    SW_EXPECT_NEAR_EQUAL( 0.25f, pProgress->getValue(), 1e-4f );
    fixture.runFrame( true, 2.0f ); // 1 을 넘는 값은 1 로 자른다 — 그리는 값일 뿐이다
    SW_EXPECT_NEAR_EQUAL( 1.0f, pProgress->getValue(), 1e-4f );
    fixture.runFrame( true ); // 프리로드 세트가 놓였다 — 다시 접는다
    SW_EXPECT_TRUE( pProgress->getVisibility() == sw::WidgetVisibility::Collapsed );
}

/**
 * @brief [LoadingScreenTest] 팁 파일(`<LoadingTips><Tip text="원문"/>…`)을 읽어 목록을 채우고, 글이 빈 팁이 있으면 오류로 거절한다
 */
SW_TEST_CASE( LoadingScreenTest, TipFileFillsTheListAndRefusesAnEmptyTip )
{
    sw::vector<sw::string> listTip;
    SW_ASSERT_TRUE( sw::LoadingScreenController::loadTipsFromXMLText( R"(<LoadingTips><Tip text="Tip A"/><Tip text="Tip B"/></LoadingTips>)", "tips.xml", listTip ) );
    SW_ASSERT_EQUAL( size_t( 2 ), listTip.size() );
    SW_EXPECT_STREQ( "Tip B", listTip[1].c_str() );

    SW_TEST_DEFENSIVE_SCOPE( "a loading tip without text is refused" );
    SW_EXPECT_FALSE( sw::LoadingScreenController::loadTipsFromXMLText( R"(<LoadingTips><Tip text="Tip A"/><Tip text=""/></LoadingTips>)", "broken.xml", listTip ) );
    SW_EXPECT_TRUE( listTip.empty() );
}
