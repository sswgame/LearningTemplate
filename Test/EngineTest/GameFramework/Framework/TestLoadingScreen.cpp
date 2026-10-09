#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Base/Framework/LoadingScreenController.h"
#include "GameFramework/Base/Framework/ScreenTransitionManager.h"

#include "TestFramework/TestFramework.h"

// LoadingScreenTest — 로딩 화면(비동기 씬 전환 동안 Loading 층 · 최소 표시 시간 · 닫으면 페이드 인)과 화면 페이드 패널. 엔진 견본 문서(engine/ui/loading.ui.xml)를 읽는다. 디바이스 없음(nogpu).

namespace
{
    /** @brief 입력 관리자와 그것을 읽는 UI 시스템, 그 위의 로딩 화면 제어입니다. */
    struct LoadingScreenFixture
    {
        static constexpr float32 kFrameSeconds = 0.1f;

        sw::InputManager            _input;
        sw::UiSystem                _ui;
        sw::ScreenFade              _fade;
        sw::LoadingScreenController _loading;

        LoadingScreenFixture()
            : _input{}
            , _ui{}
            , _fade{}
            , _loading{}
        {
            SW_EXPECT_TRUE( sw::ResourceUtil::initialize() );
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr ) );
            _loading.bindUiSystem( &_ui );
        }

        ~LoadingScreenFixture()
        {
            _loading.bindUiSystem( nullptr );
            _ui.shutdown();
            _input.shutdown();
        }

        /** @brief 한 프레임 — 페이드 → 로딩 제어 → UI 입력 · 갱신(닫기 요청이 적용된다). 게임 인스턴스와 같은 순서다. */
        void runFrame( bool bLoading )
        {
            _fade.update( kFrameSeconds );
            _loading.update( kFrameSeconds, bLoading, _fade );
            _input.beginFrame( kFrameSeconds );
            _ui.processInput( kFrameSeconds );
            _ui.update( kFrameSeconds, sw::UiViewport{
                                           sw::float2{ 1920.0f, 1080.0f }
            } );
            _input.endFrame();
        }
    };
} // namespace

/**
 * @brief [LoadingScreenTest] 로딩 화면은 로드가 0.1 초에 끝나도 최소 표시 시간(0.5 초)까지 떠 있고, 그 뒤 닫히며 페이드 인을 건다 · 떠 있는 동안 게임 입력을 막는다
 * @details 빠른 로드에서 로딩 화면이 한 프레임만 깜박이지 않게(Lyra `ULoadingScreenManager` 의 최소 표시 시간). 변이: `update` 의 최소 시간 조건을 빼면 로드가
 *          끝난 첫 프레임에 닫혀 진다.
 */
SW_TEST_CASE( LoadingScreenTest, LoadingScreenStaysForMinimumTime )
{
    LoadingScreenFixture      fixture;
    sw::LoadingScreenSettings settings{};
    settings._listTip.push_back( "Tip one" );
    fixture._loading.setSettings( settings );
    SW_ASSERT_TRUE( fixture._loading.beginLoading() );
    fixture.runFrame( true );
    const sw::UiScreen* pScreen = fixture._loading.findLoadingScreen();
    SW_ASSERT_NOT_NULL( pScreen );
    SW_EXPECT_TRUE( pScreen->getDesc()._layer == sw::UiLayer::Loading );
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
    const sw::UiScreen* pFade = fixture._loading.findFadeScreen();
    SW_ASSERT_NOT_NULL( pFade );
    SW_EXPECT_TRUE( pFade->getDesc()._layer == sw::UiLayer::Overlay );
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
