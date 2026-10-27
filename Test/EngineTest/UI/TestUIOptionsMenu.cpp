#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/UI/Base/UIEvents.h"
#include "Engine/UI/Binding/UIBindingSet.h"
#include "Engine/UI/Screen/KeyRebindScreen.h"
#include "Engine/UI/Screen/OptionsMenuScreen.h"
#include "Engine/UI/Screen/PauseMenuScreen.h"
#include "Engine/UI/Screen/SettingsConfirmScreen.h"
#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/CheckBoxWidget.h"
#include "Engine/UI/Widgets/ComboBoxWidget.h"
#include "Engine/UI/Widgets/RotatorWidget.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "TestFramework/Harness/TestHostRuntime.h"
#include "TestFramework/TestFramework.h"

// UIOptionsMenuTest — 옵션 메뉴(8-2): 설정 스키마 → 탭 · 행(엔진 문서 engine/ui/options.ui.xml + 행 견본 조각), 확인 카운트다운, 닫을 때 보류 값 확인,
// 키 바인딩 창(원시 입력 · 겹침 바꾸기 · 받는 동안 UI 행동 끔), enabledWhen, 패드 탐색, 일시정지 메뉴(UI.Pause). 테스트 스키마(XML 글) · 디바이스 없음(nogpu).

namespace
{
    struct UIOptionsMenuTestUtil
    {
        static constexpr float32     kFrameSeconds = 1.0f / 60.0f;
        static constexpr const utf8* kSchemaXML    = R"(
<UserSettingsSchema version="1">
    <Category id="audio" text="t.audio"/>
    <Category id="video" text="t.video"/>
    <Category id="controls" text="t.controls"/>
    <Category id="empty" text="t.empty"/>
    <Setting id="audio.master" category="audio" type="float" default="0.5" min="0" max="1" step="0.25" text="t.master"/>
    <Setting id="audio.music" category="audio" type="float" default="1" min="0" max="1" step="0.25" text="t.music"/>
    <Setting id="audio.voices" category="audio" type="int" default="8" min="1" max="16" step="1" text="t.voices"/>
    <Setting id="video.mode" category="video" type="enum" default="windowed" confirmSeconds="10" text="t.mode">
        <Option value="windowed"/>
        <Option value="fullscreen"/>
    </Setting>
    <Setting id="video.vsync" category="video" type="bool" default="true" enabledWhen="video.mode!=windowed" text="t.vsync"/>
    <Setting id="controls.jump" category="controls" type="keyBinding" action="Jump" default="" text="t.jump"/>
    <Setting id="controls.fire" category="controls" type="keyBinding" action="Fire" default="" text="t.fire"/>
</UserSettingsSchema>)";
    };

    /** @brief 테스트 스키마의 설정(키 바인딩 대상은 자기 입력 맵) · 입력 · UI 행동 맵을 든 UI 시스템, 그리고 엔진 옵션 메뉴입니다. */
    struct UIOptionsMenuFixture
    {
        sw::InputMap            _bindingMap; ///< 키 바인딩 설정의 대상(Jump = Space, Fire = F)
        sw::UserSettingsManager _settings;
        sw::InputManager        _input;
        sw::UISystem            _ui;
        sw::UIViewport          _viewport;
        sw::UIScreenHandle      _menu;

        UIOptionsMenuFixture()
            : _bindingMap{}
            , _settings{}
            , _input{}
            , _ui{}
            , _viewport{}
            , _menu{ sw::kInvalidUIScreenHandle }
        {
            _bindingMap.registerLayer( "Gameplay", 0, true, false, false );
            _bindingMap.bind( "Jump", sw::Key::Space, sw::ActionTrigger::Pressed, "Gameplay" );
            _bindingMap.bind( "Fire", sw::Key::F, sw::ActionTrigger::Pressed, "Gameplay" );
            sw::UserSettingsTargets targets{};
            targets._pInputMap = &_bindingMap;
            _settings.initialize( targets );
            SW_EXPECT_TRUE( _settings.loadSchemaFromXMLText( UIOptionsMenuTestUtil::kSchemaXML, "test.settings.xml" ) );
            _settings.reapplyAll();
            _viewport._size         = sw::float2{ 1920.0f, 1080.0f };
            _viewport._physicalSize = _viewport._size;
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr, sw::ResourceID{ ::test::getEngineDefaultAssets()._uiInputMap.c_str() } ) );
            _ui.setEngineDocuments( sw::UIEngineDocuments::makeFromDefaultAssets( ::test::getEngineDefaultAssets() ) );
            _ui.setUserSettings( &_settings );
            _menu = sw::OptionsMenuScreen::open( _ui );
            runFrame();
        }

        ~UIOptionsMenuFixture()
        {
            _ui.shutdown();
            _input.shutdown();
            _settings.shutdown();
        }

        UIOptionsMenuFixture( const UIOptionsMenuFixture& )            = delete;
        UIOptionsMenuFixture& operator=( const UIOptionsMenuFixture& ) = delete;

        void runFrame()
        {
            _input.beginFrame( UIOptionsMenuTestUtil::kFrameSeconds );
            _ui.processInput( UIOptionsMenuTestUtil::kFrameSeconds );
            _ui.update( UIOptionsMenuTestUtil::kFrameSeconds, _viewport );
            _input.endFrame();
        }

        /** @brief 키 하나를 누르고(한 프레임) 뗍니다(한 프레임). */
        void tapKey( sw::Key key )
        {
            SW_EXPECT_TRUE( _input.postRawEvent( sw::RawInputEvent::makeKeyDown( key ) ) );
            runFrame();
            SW_EXPECT_TRUE( _input.postRawEvent( sw::RawInputEvent::makeKeyUp( key ) ) );
            runFrame();
        }

        sw::OptionsMenuScreen* getMenu() const { return static_cast<sw::OptionsMenuScreen*>( _ui.findScreen( _menu ) ); }

        template <typename ScreenType>
        ScreenType* getPrompt() const
        {
            const sw::OptionsMenuScreen* pMenu = getMenu();
            return pMenu != nullptr ? static_cast<ScreenType*>( _ui.findScreen( pMenu->getPromptScreen() ) ) : nullptr;
        }

        /**
         * @brief 지금 포커스 위젯 이름과 지난 프레임에 발화한 UI 탐색 행동 · 스틱 값을 한 줄로 만듭니다(실패 메시지용).
         * @details 포커스가 엉뚱한 곳으로 가면 어느 행동이 옮겼는지가 원인이다. 리눅스 CI 에서만 지는 경로를 로그 없이 좁히려고 둔다.
         */
        sw::string describeFocusAndActions() const
        {
            const sw::WidgetID    focused     = _ui.getInputRouter().getFocusManager().getFocusedWidget();
            const sw::WidgetTree* pTree       = _ui.getInputRouter().getFocusManager().getFocusedTree();
            const sw::Widget*     pFocused    = pTree != nullptr ? pTree->findWidgetByID( focused ) : nullptr;
            sw::string            detail      = sw::string( "focus '" ) + ( pFocused != nullptr ? pFocused->getName().c_str() : "(none)" ) + "' actions [";
            const sw::InputMap*   pUIMap      = _ui.getInputRouter().getUIInputMap();
            const utf8*           arrAction[] = { sw::UIActionName::kNavigateUp, sw::UIActionName::kNavigateDown, sw::UIActionName::kNavigateLeft,
                                                  sw::UIActionName::kNavigateRight, sw::UIActionName::kFocusNext, sw::UIActionName::kFocusPrevious,
                                                  sw::UIActionName::kTabNext, sw::UIActionName::kTabPrevious };
            for ( const utf8* pAction : arrAction )
            {
                if ( pUIMap != nullptr && pUIMap->wasActionTriggered( sw::hashed_string( pAction ) ) )
                    detail += sw::string( pAction ) + " ";
            }
            const sw::float2 stick = pUIMap != nullptr ? pUIMap->getVector2D( sw::hashed_string( sw::UIActionName::kNavigateStick ) ) : sw::float2{};
            detail += "] stick (" + sw::to_string( stick._x ) + ", " + sw::to_string( stick._y ) + ")";
            return detail;
        }

        /** @brief 탭을 설정 카테고리 @p categoryIndex(스키마 순서 — 빈 카테고리 빼고)로 옮깁니다. */
        void selectTab( uint32 index )
        {
            getMenu()->selectTab( index );
            runFrame();
        }
    };
} // namespace

/** @brief [UIOptionsMenuTest] 탭 = 보일 설정이 있는 카테고리(빈 카테고리 빼고), 행 = 그 카테고리의 설정 · 형식별 견본(슬라이더 · 콤보 · 체크 · 단추) */
SW_TEST_CASE( UIOptionsMenuTest, BuildsTabsAndRowsFromSchema )
{
    UIOptionsMenuFixture   fixture;
    sw::OptionsMenuScreen* pMenu = fixture.getMenu();
    SW_ASSERT_NOT_NULL( pMenu );
    SW_EXPECT_EQUAL( 3u, pMenu->getTabCount() ); // audio · video · controls (empty 는 뺀다)
    SW_ASSERT_EQUAL( 3u, pMenu->getRowCount() );
    SW_EXPECT_STREQ( "audio.master", pMenu->getRowSetting( 0 ).c_str() );
    sw::SliderWidget* pMaster = sw::castTo<sw::SliderWidget>( pMenu->findRowValueWidget( "audio.master" ) );
    SW_ASSERT_NOT_NULL( pMaster );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pMaster->getValue(), 0.0001f );
    sw::UIActionEvent right{};
    right._action = sw::hashed_string( sw::UIActionName::kNavigateRight );
    (void)pMaster->onActionEvent( right, sw::UIRoutePhase::Bubble ); // 한 칸 = 설정 정의의 눈금 0.25(범위 · 눈금은 설정에서)
    SW_EXPECT_STREQ( "0.75", sw::string( fixture._settings.getValue( "audio.master" ) ).c_str() );
    SW_EXPECT_NOT_NULL( pMenu->getTree().findWidgetByName( "audio.master.Label" ) ); // 견본 안 이름은 설정 id 로 감싼다

    fixture.selectTab( 1 );
    SW_ASSERT_EQUAL( 2u, pMenu->getRowCount() );
    SW_EXPECT_NOT_NULL( sw::castTo<sw::ComboBoxWidget>( pMenu->findRowValueWidget( "video.mode" ) ) );
    SW_EXPECT_NOT_NULL( sw::castTo<sw::CheckBoxWidget>( pMenu->findRowValueWidget( "video.vsync" ) ) );
    SW_EXPECT_TRUE( pMenu->findRowValueWidget( "audio.master" ) == nullptr ); // 옛 탭의 행은 지웠다

    fixture.selectTab( 2 );
    SW_ASSERT_EQUAL( 2u, pMenu->getRowCount() );
    SW_EXPECT_NOT_NULL( sw::castTo<sw::ButtonWidget>( pMenu->findRowValueWidget( "controls.jump" ) ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( pMenu->getBindingSet().getErrors().size() ) );
}

/** @brief [UIOptionsMenuTest] 확인 대기가 있는 설정을 적용하면 카운트다운 창이 뜨고, 시간이 다 되면 매니저가 되돌리며 창도 닫힌다 */
SW_TEST_CASE( UIOptionsMenuTest, ApplyWithConfirmShowsCountdownAndRevertsOnTimeout )
{
    UIOptionsMenuFixture fixture;
    fixture.selectTab( 1 );
    SW_EXPECT_TRUE( fixture._settings.setPendingValue( "video.mode", "fullscreen" ) == sw::UserSettingSetResult::Accepted );
    fixture.getMenu()->apply();
    sw::SettingsConfirmScreen* pConfirm = fixture.getPrompt<sw::SettingsConfirmScreen>();
    SW_ASSERT_NOT_NULL( pConfirm );
    SW_EXPECT_TRUE( fixture._ui.getActiveScreen() == pConfirm );
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture._settings.isAwaitingConfirm() );
    SW_EXPECT_STREQ( "fullscreen", sw::string( fixture._settings.getAppliedValue( "video.mode" ) ).c_str() );

    fixture._settings.update( 11.0f ); // 호스트(UserSettingsHost)의 프레임 갱신 — 시간이 다 됐다
    fixture.runFrame();
    fixture.runFrame();
    SW_EXPECT_STREQ( "windowed", sw::string( fixture._settings.getAppliedValue( "video.mode" ) ).c_str() );
    SW_EXPECT_TRUE( fixture.getPrompt<sw::SettingsConfirmScreen>() == nullptr );
    SW_EXPECT_TRUE( fixture._ui.getActiveScreen() == fixture.getMenu() );
}

/** @brief [UIOptionsMenuTest] 보류 값이 있으면 닫기가 "적용 · 버리기 · 취소" 를 묻는다 — 취소는 메뉴를 두고, 버리기는 되돌리고 닫는다 */
SW_TEST_CASE( UIOptionsMenuTest, CloseWithPendingAsks )
{
    UIOptionsMenuFixture fixture;
    (void)fixture._settings.setPendingFloatValue( "audio.master", 0.75f );
    fixture.getMenu()->requestClose();
    sw::UIScreen* pPrompt = fixture.getPrompt<sw::UIScreen>();
    SW_ASSERT_NOT_NULL( pPrompt );
    sw::Widget* pCancel = pPrompt->getTree().findWidgetByName( "Cancel" );
    SW_ASSERT_NOT_NULL( pCancel );
    pPrompt->dispatchCommand( "Cancel", *pCancel );
    fixture.runFrame();
    SW_EXPECT_NOT_NULL( fixture.getMenu() );
    SW_EXPECT_TRUE( fixture._settings.hasPendingChanges() );

    fixture.getMenu()->requestClose();
    pPrompt = fixture.getPrompt<sw::UIScreen>();
    SW_ASSERT_NOT_NULL( pPrompt );
    pPrompt->dispatchCommand( "Discard", *pPrompt->getTree().findWidgetByName( "Discard" ) );
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture.getMenu() == nullptr );
    SW_EXPECT_FALSE( fixture._settings.hasPendingChanges() );
    SW_EXPECT_EQUAL( 0u, fixture._ui.getScreenCount() );
}

/** @brief [UIOptionsMenuTest] 키 바인딩 창이 다음 키(F)를 받는다 — Fire 와 겹치면 묻고, 바꾸기는 Fire 에 Jump 의 지금 키(Space)를 준다 */
SW_TEST_CASE( UIOptionsMenuTest, RebindCapturesNextKeyAndSwapsOnConflict )
{
    UIOptionsMenuFixture fixture;
    fixture.selectTab( 2 );
    sw::OptionsMenuScreen* pMenu  = fixture.getMenu();
    sw::Widget*            pValue = pMenu->findRowValueWidget( "controls.jump" );
    SW_ASSERT_NOT_NULL( pValue );
    SW_EXPECT_STREQ( "[ Space ]", pMenu->getTree().findWidget<sw::TextWidget>( "controls.jump.Glyph" )->getText().c_str() );
    pMenu->dispatchCommand( "Rebind", *pValue );
    sw::KeyRebindScreen* pRebind = fixture.getPrompt<sw::KeyRebindScreen>();
    SW_ASSERT_NOT_NULL( pRebind );
    SW_EXPECT_TRUE( pRebind->isListening() );
    fixture.runFrame();

    fixture.tapKey( sw::Key::F );
    SW_ASSERT_TRUE( fixture.getPrompt<sw::KeyRebindScreen>() == pRebind );
    SW_EXPECT_FALSE( pRebind->isListening() ); // 겹침을 묻는 중
    SW_EXPECT_TRUE( pRebind->getTree().findWidgetByName( "ConflictRow" )->isVisible() );
    pRebind->dispatchCommand( "Swap", *pRebind->getTree().findWidgetByName( "Swap" ) );
    fixture.runFrame();
    SW_EXPECT_STREQ( "Key.F", sw::string( fixture._settings.getValue( "controls.jump" ) ).c_str() );
    SW_EXPECT_STREQ( "Key.Space", sw::string( fixture._settings.getValue( "controls.fire" ) ).c_str() );
    SW_EXPECT_TRUE( fixture.getPrompt<sw::KeyRebindScreen>() == nullptr );
    SW_EXPECT_STREQ( "[ F ]", pMenu->getTree().findWidget<sw::TextWidget>( "controls.jump.Glyph" )->getText().c_str() );
}

/**
 * @brief [UIOptionsMenuTest] 키를 기다리다 아무것도 받지 못한 채 시간(10 초)이 지나면 취소하고 닫는다 — 패드 사용자도 빠져나올 수 있다. 보류 값은 그대로다
 * @details 변이: `onTick` 의 `_listenSeconds` 누적을 빼면 창이 남아 진다.
 */
SW_TEST_CASE( UIOptionsMenuTest, RebindTimesOutWithoutInput )
{
    UIOptionsMenuFixture fixture;
    fixture.selectTab( 2 );
    sw::OptionsMenuScreen* pMenu = fixture.getMenu();
    pMenu->dispatchCommand( "Rebind", *pMenu->findRowValueWidget( "controls.jump" ) );
    SW_ASSERT_NOT_NULL( fixture.getPrompt<sw::KeyRebindScreen>() );
    constexpr uint32 kFramesPerSecond = 60;
    for ( uint32 frame = 0; frame < 9 * kFramesPerSecond; ++frame )
    {
        fixture.runFrame();
    }
    SW_EXPECT_NOT_NULL( fixture.getPrompt<sw::KeyRebindScreen>() ); // 9 초 — 아직 기다린다
    for ( uint32 frame = 0; frame < 2 * kFramesPerSecond; ++frame )
    {
        fixture.runFrame();
    }
    SW_EXPECT_TRUE( fixture.getPrompt<sw::KeyRebindScreen>() == nullptr ); // 11 초 — 취소했다
    SW_EXPECT_FALSE( fixture._settings.isPending( "controls.jump" ) );
}

/**
 * @brief [UIOptionsMenuTest] 키를 받는 동안 UI 행동은 꺼진다 — Esc 를 누른 프레임에 창이 닫히지 않고(UI.Back 이 아니다) 뗄 때 취소된다.
 *        아래 화살표는 탐색이 아니라 바인딩이 되고, 창이 닫히면 포커스는 그 행 단추로 돌아온다
 * @details 변이: `UIInputRouter::processActions` 의 `wantsUIActions` 검사를 빼면 Esc 를 누른 프레임에 UI.Back 이 창을 닫는다.
 */
SW_TEST_CASE( UIOptionsMenuTest, RebindIgnoresUIActionsWhileListening )
{
    UIOptionsMenuFixture fixture;
    fixture.selectTab( 2 );
    sw::OptionsMenuScreen* pMenu = fixture.getMenu();
    pMenu->dispatchCommand( "Rebind", *pMenu->findRowValueWidget( "controls.jump" ) );
    sw::KeyRebindScreen* pRebind = fixture.getPrompt<sw::KeyRebindScreen>();
    SW_ASSERT_NOT_NULL( pRebind );
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Escape ) ) );
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture.getPrompt<sw::KeyRebindScreen>() == pRebind ); // 누르는 중 — 1 초를 채우면 Esc 를 바인딩한다
    SW_EXPECT_TRUE( pRebind->isListening() );
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::Escape ) ) );
    fixture.runFrame(); // 짧게 — 취소
    SW_EXPECT_TRUE( fixture.getPrompt<sw::KeyRebindScreen>() == nullptr );
    SW_EXPECT_FALSE( fixture._settings.isPending( "controls.jump" ) );
    SW_EXPECT_NOT_NULL( fixture.getMenu() ); // Esc 가 메뉴의 뒤로가 되지 않았다

    fixture._ui.setInputMode( sw::UIInputMode::Navigation );
    const sw::WidgetID jumpButton = pMenu->findRowValueWidget( "controls.jump" )->getID();
    SW_ASSERT_TRUE( fixture._ui.getInputRouter().getFocusManager().setFocus( pMenu->getTree(), jumpButton ) );
    pMenu->dispatchCommand( "Rebind", *pMenu->findRowValueWidget( "controls.jump" ) );
    pRebind = fixture.getPrompt<sw::KeyRebindScreen>();
    SW_ASSERT_NOT_NULL( pRebind );
    fixture.runFrame();
    // 누른 프레임과 뗀 프레임을 따로 돌려 프레임마다 포커스와 발화한 행동을 남긴다. 지면 어느 프레임의 어느 행동이 옮겼는지 보인다.
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Down ) ) );
    fixture.runFrame();
    const sw::string pressedDetail = "pressed frame: " + fixture.describeFocusAndActions();
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::Down ) ) );
    fixture.runFrame();
    const sw::string focusedDetail = pressedDetail + "; released frame: " + fixture.describeFocusAndActions();
    SW_EXPECT_TRUE_MSG( jumpButton == fixture._ui.getInputRouter().getFocusManager().getFocusedWidget(), focusedDetail.c_str() ); // 아래 행(Fire)으로 가지 않았다
    SW_EXPECT_STREQ( "Key.Down", sw::string( fixture._settings.getValue( "controls.jump" ) ).c_str() );
    SW_EXPECT_TRUE( fixture.getPrompt<sw::KeyRebindScreen>() == nullptr );
}

/** @brief [UIOptionsMenuTest] enabledWhen — VSync 는 창 모드에서 꺼져 있고, 방식을 전체 화면으로(보류) 바꾸면 다음 프레임에 켜진다 */
SW_TEST_CASE( UIOptionsMenuTest, DisabledRowFollowsEnabledWhen )
{
    UIOptionsMenuFixture fixture;
    fixture.selectTab( 1 );
    sw::Widget* pVsync = fixture.getMenu()->findRowValueWidget( "video.vsync" );
    SW_ASSERT_NOT_NULL( pVsync );
    SW_EXPECT_FALSE( pVsync->isEnabled() );
    (void)fixture._settings.setPendingValue( "video.mode", "fullscreen" );
    fixture.runFrame();
    SW_EXPECT_TRUE( pVsync->isEnabled() );
}

/** @brief [UIOptionsMenuTest] 패드 · 키보드 아래 반복이 탭의 모든 행을 지나고, 탭 행동(LB · RB)이 탭을 돌린다 */
SW_TEST_CASE( UIOptionsMenuTest, NavigationVisitsEveryRow )
{
    UIOptionsMenuFixture   fixture;
    sw::OptionsMenuScreen* pMenu = fixture.getMenu();
    fixture._ui.setInputMode( sw::UIInputMode::Navigation );
    SW_ASSERT_TRUE( fixture._ui.getInputRouter().getFocusManager().setFocus( pMenu->getTree(), pMenu->findRowValueWidget( "audio.master" )->getID() ) );
    sw::vector<sw::WidgetID> listVisited{ fixture._ui.getInputRouter().getFocusManager().getFocusedWidget() };
    for ( uint32 step = 0; step < 2; ++step )
    {
        fixture.tapKey( sw::Key::Down );
        listVisited.push_back( fixture._ui.getInputRouter().getFocusManager().getFocusedWidget() );
    }
    for ( uint32 row = 0; row < pMenu->getRowCount(); ++row )
    {
        SW_EXPECT_EQUAL( pMenu->findRowValueWidget( pMenu->getRowSetting( row ) )->getID(), listVisited[row] );
    }

    SW_EXPECT_TRUE( pMenu->onUnhandledAction( sw::UIActionName::kTabNext ) );
    SW_EXPECT_EQUAL( 1u, pMenu->getSelectedTab() );
    SW_EXPECT_EQUAL( pMenu->findRowValueWidget( "video.mode" )->getID(), fixture._ui.getInputRouter().getFocusManager().getFocusedWidget() ); // 새 탭의 첫 행
    SW_EXPECT_TRUE( pMenu->onUnhandledAction( sw::UIActionName::kTabPrevious ) );
    SW_EXPECT_TRUE( pMenu->onUnhandledAction( sw::UIActionName::kTabPrevious ) );
    SW_EXPECT_EQUAL( 2u, pMenu->getSelectedTab() ); // 처음에서 뒤로 = 끝
}

/**
 * @brief [UIOptionsMenuTest] 일시정지 메뉴 — 켠 게임에서 화면이 없을 때 Esc 가 일시정지 메뉴를 열고(그 Esc 는 먹힌다), 옵션 명령이 옵션 메뉴를 위에 연다
 * @details 변이: `UIInputRouter::syncInputLayers` 의 `UIGlobal` 줄을 빼면 Esc 가 아무것도 열지 않는다.
 */
SW_TEST_CASE( UIOptionsMenuTest, PauseActionOpensPauseMenu )
{
    UIOptionsMenuFixture fixture;
    fixture.getMenu()->close();
    fixture.runFrame();
    SW_ASSERT_EQUAL( 0u, fixture._ui.getScreenCount() );
    fixture.tapKey( sw::Key::Escape ); // 일시정지 메뉴를 켜지 않은 게임 — 아무것도 없다
    SW_EXPECT_EQUAL( 0u, fixture._ui.getScreenCount() );

    fixture._ui.setPauseMenuDocument( ::test::getEngineDefaultAssets()._uiPauseMenu.c_str() );
    fixture.tapKey( sw::Key::Escape );
    SW_ASSERT_EQUAL( 1u, fixture._ui.getScreenCount() );
    sw::UIScreen* pPause = fixture._ui.getActiveScreen();
    SW_ASSERT_NOT_NULL( pPause );
    SW_EXPECT_TRUE( sw::FileUtil::normalizePath( ::test::getEngineDefaultAssets()._uiPauseMenu.c_str() ) == pPause->getDocumentPath() );

    pPause->dispatchCommand( "OpenOptions", *pPause->getTree().findWidgetByName( "Options" ) );
    fixture.runFrame();
    SW_EXPECT_EQUAL( 2u, fixture._ui.getScreenCount() );
    fixture.tapKey( sw::Key::Escape ); // 옵션 메뉴의 뒤로 — 보류 값이 없으니 닫힌다
    SW_EXPECT_EQUAL( 1u, fixture._ui.getScreenCount() );
    fixture.tapKey( sw::Key::Escape ); // 일시정지 메뉴의 뒤로 — 닫는다(다시 열지 않는다)
    // pause.ui.xml 의 Close 애니메이션(0.12 초)이 끝나야 화면이 지워진다 — 그동안 Esc 를 다시 받아 여는 일은 없다.
    for ( uint32 frame = 0; frame < 30 && fixture._ui.getScreenCount() != 0; ++frame )
    {
        fixture.runFrame();
    }
    SW_EXPECT_EQUAL( 0u, fixture._ui.getScreenCount() );
}

/**
 * @brief [UIOptionsMenuTest] 일시정지 메뉴를 연 Esc 를 계속 누르고 있어도 메뉴가 닫히지 않는다(사람의 누름은 여러 프레임이다)
 * @details 변이: `InputMap::update` 가 층이 켜지는 프레임의 `_bWasDown` 을 맞추지 않으면 UI 층이 켜진 첫 프레임에 같은 Esc 의 Back 이 새 누름으로 발화해 다음 프레임에 활성 화면이 없다.
 *          한 프레임짜리 누름(`tapKey`)은 층이 켜지기 전에 떼어져 이 결함을 가렸다(시나리오 `Tap hold="1"` 도 같았다).
 */
SW_TEST_CASE( UIOptionsMenuTest, HeldPauseKeyDoesNotCloseThePauseMenu )
{
    UIOptionsMenuFixture fixture;
    fixture.getMenu()->close();
    fixture.runFrame();
    SW_ASSERT_EQUAL( 0u, fixture._ui.getScreenCount() );
    fixture._ui.setPauseMenuDocument( ::test::getEngineDefaultAssets()._uiPauseMenu.c_str() );

    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Escape ) ) );
    for ( uint32 frame = 0; frame < 6; ++frame )
    {
        fixture.runFrame();
        SW_EXPECT_TRUE( fixture._ui.getActiveScreen() != nullptr );
    }
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::Escape ) ) );
    fixture.runFrame();
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture._ui.getActiveScreen() != nullptr ); // 뗀 뒤에도 열려 있다

    fixture.tapKey( sw::Key::Escape ); // 새 누름은 메뉴의 뒤로다 — 소비는 뗄 때까지만 막는다
    SW_EXPECT_TRUE( fixture._ui.getActiveScreen() == nullptr );
}

/**
 * @brief [UIOptionsMenuTest] 일시정지 메뉴의 "타이틀로" · "끝내기" 단추 — 전역 명령 표에 같은 이름이 등록돼 있는 동안만 보이고, 누르면 메뉴를 닫고 등록한 함수를 부르며,
 *        교체된 옛 구독을 놓아도 지금 등록은 남는다
 * @details 변이: `PauseMenuScreen::onTick` 이 표를 보지 않고 늘 보이면 처음 접힘 단언이 진다. 전역 표로 넘기지 않으면 호출 수가 0 이고, 메뉴를 닫지 않으면 화면이 남는다.
 *          구독 해제가 지금 등록의 구독인지 보지 않으면 옛 주인이 놓을 때 등록이 사라져 진다.
 */
SW_TEST_CASE( UIOptionsMenuTest, PauseMenuSessionButtonsFollowGlobalCommands )
{
    UIOptionsMenuFixture fixture;
    fixture.getMenu()->close();
    fixture.runFrame();
    fixture._ui.setPauseMenuDocument( ::test::getEngineDefaultAssets()._uiPauseMenu.c_str() );
    fixture.tapKey( sw::Key::Escape );
    sw::UIScreen* pPause = fixture._ui.getActiveScreen();
    SW_ASSERT_NOT_NULL( pPause );
    sw::Widget* pTitle = pPause->getTree().findWidgetByName( sw::UIGlobalCommandRegistry::kReturnToTitle );
    sw::Widget* pQuit  = pPause->getTree().findWidgetByName( sw::UIGlobalCommandRegistry::kQuitGame );
    SW_ASSERT_NOT_NULL( pTitle );
    SW_ASSERT_NOT_NULL( pQuit );
    SW_EXPECT_TRUE( pTitle->getVisibility() == sw::WidgetVisibility::Collapsed ); // 등록한 쪽이 없으면 접혀 있다
    SW_EXPECT_TRUE( pQuit->getVisibility() == sw::WidgetVisibility::Collapsed );

    /** 등록한 쪽 — 옛 인스턴스와 새 인스턴스가 따로 등록한다. */
    struct QuitCounter
    {
        uint32 _count{ 0 };
        void   onQuit( const sw::hashed_string&, sw::Widget& ) { ++_count; }
    };
    QuitCounter quit;
    QuitCounter staleOwner;
    // 옛 인스턴스가 먼저 등록하고, 새 인스턴스가 같은 이름을 교체한다(핫 리로드 순서).
    sw::Subscription staleRegistration =
        fixture._ui.getGlobalCommands().registerCommand( sw::UIGlobalCommandRegistry::kQuitGame, SW_DELEGATE_METHOD( sw::UICommandDelegate, &QuitCounter::onQuit, &staleOwner ) );
    sw::Subscription quitRegistration =
        fixture._ui.getGlobalCommands().registerCommand( sw::UIGlobalCommandRegistry::kQuitGame, SW_DELEGATE_METHOD( sw::UICommandDelegate, &QuitCounter::onQuit, &quit ) );
    fixture.runFrame();
    SW_EXPECT_TRUE( pQuit->getVisibility() == sw::WidgetVisibility::Visible );
    SW_EXPECT_TRUE( pTitle->getVisibility() == sw::WidgetVisibility::Collapsed ); // 다른 명령은 그대로

    // 교체된 옛 구독을 놓아도(핫 리로드의 옛 인스턴스가 내려간다) 지금 등록은 남는다.
    staleRegistration.reset();
    SW_EXPECT_TRUE( fixture._ui.getGlobalCommands().hasCommand( sw::UIGlobalCommandRegistry::kQuitGame ) );

    // 세션을 떠나는 명령은 메뉴를 닫고(정지가 남지 않게) 등록한 함수를 부른다.
    const sw::UIScreenHandle pauseHandle = pPause->getHandle();
    pPause->dispatchCommand( sw::UIGlobalCommandRegistry::kQuitGame, *pQuit );
    SW_EXPECT_EQUAL( 1u, quit._count );
    const sw::UIScreen* pClosed = fixture._ui.findScreen( pauseHandle );
    SW_EXPECT_TRUE( pClosed == nullptr || pClosed->isClosing() ); // 닫는 애니메이션이 있으면 끝날 때까지 남는다

    quitRegistration.reset();
    SW_EXPECT_FALSE( fixture._ui.getGlobalCommands().hasCommand( sw::UIGlobalCommandRegistry::kQuitGame ) );
}

/**
 * @brief [UIOptionsMenuTest] 열거형 행: 포인터 방식이면 펼치는 콤보 상자, 탐색(패드 · 키보드) 방식이면 좌우로 고르는 로테이터이고, 로테이터의 오른쪽 키가 설정 값을 다음 항목으로 쓴다
 * @details 행은 탭을 고를 때 짓는다. 변이: `findRowDocument` 가 입력 방식을 보지 않으면 탐색 방식에서도 콤보 상자가 나와 진다.
 */
SW_TEST_CASE( UIOptionsMenuTest, EnumRowUsesRotatorInNavigationMode )
{
    UIOptionsMenuFixture   fixture;
    sw::OptionsMenuScreen* pMenu = fixture.getMenu();
    fixture.selectTab( 1 );
    SW_EXPECT_NOT_NULL( sw::castTo<sw::ComboBoxWidget>( pMenu->findRowValueWidget( "video.mode" ) ) ); // 포인터 방식(기본)

    fixture._ui.setInputMode( sw::UIInputMode::Navigation );
    fixture.selectTab( 0 );
    fixture.selectTab( 1 );
    sw::RotatorWidget* pRotator = sw::castTo<sw::RotatorWidget>( pMenu->findRowValueWidget( "video.mode" ) );
    SW_ASSERT_NOT_NULL( pRotator );
    SW_EXPECT_EQUAL( 0u, pRotator->getSelectedIndex() ); // windowed
    SW_EXPECT_EQUAL( size_t{ 2 }, pRotator->getOptions().size() );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( pMenu->getBindingSet().getErrors().size() ) );

    sw::UIActionEvent right{};
    right._action = sw::hashed_string( sw::UIActionName::kNavigateRight );
    SW_EXPECT_TRUE( pRotator->onActionEvent( right, sw::UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_STREQ( "fullscreen", sw::string( fixture._settings.getValue( "video.mode" ) ).c_str() ); // 보류 값으로 쓰였다
    fixture.runFrame();
    SW_EXPECT_EQUAL( 1u, pRotator->getSelectedIndex() );

    fixture._ui.setInputMode( sw::UIInputMode::Pointer );
    fixture.selectTab( 0 );
    fixture.selectTab( 1 );
    SW_EXPECT_NOT_NULL( sw::castTo<sw::ComboBoxWidget>( pMenu->findRowValueWidget( "video.mode" ) ) );
}
