#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/UiFocusManager.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/Utility/GameTimeScale.h"

#include "EngineTest/UI/UiTestWidgets.h"

#include "TestFramework/TestFramework.h"

// UiScreenStackTest — 화면 스택: 층 · 모달(아래 화면 · 게임 입력 막기) · 덮였다 돌아올 때 포커스 복원 · 기본 포커스 · 지연 닫기 · 게임 정지 · 모듈 내리기.
// 입력은 자기 InputManager 에 원시 사건을 넣는다. 디바이스 없음(nogpu).

namespace
{
    struct UiScreenTestUtil
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        /** @brief 루트 패널(@p x, @p y, @p width × @p height) 하나와 그 안의 포커스 받는 버튼들(가로 한 줄, 100 × 40, 틈 10)로 된 화면입니다. 버튼 이름은 @p pPrefix + 번호. */
        static sw::unique_ptr<sw::UiScreen> makeScreen( const sw::UiScreenDesc& desc, float32 x, float32 y, float32 width, float32 height, const utf8* pPrefix,
                                                        uint32 buttonCount, sw::uitest::UiEventRecord* pRecord = nullptr )
        {
            auto                         root  = sw::make_unique<sw::uitest::TestPanelWidget>( sw::hashed_string( sw::string( pPrefix ) + "root" ) );
            sw::uitest::TestPanelWidget* pRoot = root.get();
            pRoot->_pRecord                    = pRecord;
            sw::uitest::UiTestUtil::placeWidget( *pRoot, x, y, width, height );
            for ( uint32 index = 0; index < buttonCount; ++index )
            {
                auto* pButton           = static_cast<sw::uitest::TestBoxWidget*>( pRoot->addChild(
                    sw::make_unique<sw::uitest::TestBoxWidget>( sw::hashed_string( sw::string( pPrefix ) + sw::to_string( index ) ), true ) ) );
                pButton->_pRecord       = pRecord;
                pButton->_bHandleBubble = true;
                sw::uitest::UiTestUtil::placeWidget( *pButton, x + 10.0f + static_cast<float32>( index ) * 110.0f, y + 10.0f, 100.0f, 40.0f );
            }
            return sw::make_unique<sw::UiScreen>( desc, std::move( root ) );
        }

        static sw::UiScreenDesc makeDesc( sw::UiLayer layer, bool bModal = false )
        {
            sw::UiScreenDesc desc{};
            desc._layer  = layer;
            desc._bModal = bModal;
            return desc;
        }

        /** @brief 입력 한 프레임 → UI 입력 → UI 갱신입니다. */
        static void runFrame( sw::InputManager& input, sw::UiSystem& ui )
        {
            input.beginFrame( kFrameSeconds );
            ui.processInput( kFrameSeconds );
            ui.update( kFrameSeconds, sw::UiViewport{
                                          sw::float2{ 1920.0f, 1080.0f }
            } );
            input.endFrame();
        }

        /** @brief (@p x, @p y) 를 왼쪽 버튼으로 누르고 뗍니다(두 프레임). */
        static void click( sw::InputManager& input, sw::UiSystem& ui, int32 x, int32 y )
        {
            SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeMouseMove( x, y ) ) );
            SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left, x, y ) ) );
            runFrame( input, ui );
            SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeMouseButtonUp( sw::MouseButton::Left, x, y ) ) );
            runFrame( input, ui );
        }
    };

    /** @brief 입력 관리자와 그것을 읽는 UI 시스템입니다. */
    struct UiScreenFixture
    {
        sw::InputManager _input;
        sw::UiSystem     _ui;

        UiScreenFixture()
            : _input{}
            , _ui{}
        {
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr ) );
        }

        ~UiScreenFixture()
        {
            _ui.shutdown();
            _input.shutdown();
        }

        UiScreenFixture( const UiScreenFixture& )            = delete;
        UiScreenFixture& operator=( const UiScreenFixture& ) = delete;

        sw::Widget* find( sw::UiScreenHandle screen, const sw::hashed_string& name ) const
        {
            sw::UiScreen* pScreen = _ui.findScreen( screen );
            return pScreen != nullptr ? pScreen->getTree().findWidgetByName( name ) : nullptr;
        }
    };
} // namespace

/** @brief [UiScreenStackTest] 메뉴 위 모달: 메뉴 버튼은 클릭 대상이 아니고 활성 화면은 모달이며 게임 입력은 막힌다 — 모달을 닫으면 풀린다 */
SW_TEST_CASE( UiScreenStackTest, ModalBlocksLowerScreensAndGame )
{
    using Util = UiScreenTestUtil;
    UiScreenFixture           fixture;
    sw::uitest::UiEventRecord record;
    const sw::UiScreenHandle  menu = fixture._ui.pushScreen( Util::makeScreen( Util::makeDesc( sw::UiLayer::Menu ), 0.0f, 0.0f, 800.0f, 600.0f, "menu", 2, &record ) );
    SW_EXPECT_FALSE( fixture._ui.isGameInputBlocked() );
    Util::click( fixture._input, fixture._ui, 20, 20 );
    SW_EXPECT_TRUE( record.joined().find( "menu0 B" ) != sw::string::npos );

    const sw::UiScreenHandle modal =
        fixture._ui.pushScreen( Util::makeScreen( Util::makeDesc( sw::UiLayer::Modal, true ), 300.0f, 300.0f, 300.0f, 100.0f, "modal", 1, &record ) );
    SW_EXPECT_TRUE( fixture._ui.isGameInputBlocked() );
    SW_ASSERT_NOT_NULL( fixture._ui.getActiveScreen() );
    SW_EXPECT_EQUAL( modal, fixture._ui.getActiveScreen()->getHandle() );

    record._listLine.clear();
    Util::click( fixture._input, fixture._ui, 20, 20 );                                                                                                  // 메뉴 버튼 자리 — 모달 아래라 받지 않는다
    SW_EXPECT_TRUE_MSG( record.joined().find( " T" ) == sw::string::npos && record.joined().find( " B" ) == sw::string::npos, record.joined().c_str() ); // 호버 Leave 만 남는다
    Util::click( fixture._input, fixture._ui, 320, 320 );                                                                                                // 모달 버튼은 받는다
    SW_EXPECT_TRUE( record.joined().find( "modal0 B" ) != sw::string::npos );

    fixture._ui.closeScreen( modal );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_TRUE( fixture._ui.findScreen( modal ) == nullptr );
    SW_EXPECT_FALSE( fixture._ui.isGameInputBlocked() );
    SW_EXPECT_EQUAL( menu, fixture._ui.getActiveScreen()->getHandle() );
}

/** @brief [UiScreenStackTest] 메뉴에서 menu1 포커스 → 모달을 열면 모달로 → 닫으면 menu1 이 돌아온다 */
SW_TEST_CASE( UiScreenStackTest, ClosingRestoresLastFocus )
{
    using Util = UiScreenTestUtil;
    UiScreenFixture          fixture;
    const sw::UiScreenHandle menu   = fixture._ui.pushScreen( Util::makeScreen( Util::makeDesc( sw::UiLayer::Menu ), 0.0f, 0.0f, 800.0f, 600.0f, "menu", 3 ) );
    sw::Widget*              pMenu1 = fixture.find( menu, "menu1" );
    SW_ASSERT_TRUE( fixture._ui.getFocusManager().setFocus( fixture._ui.findScreen( menu )->getTree(), pMenu1->getId() ) );

    fixture._ui.setInputMode( sw::UiInputMode::Navigation );
    const sw::UiScreenHandle modal =
        fixture._ui.pushScreen( Util::makeScreen( Util::makeDesc( sw::UiLayer::Modal, true ), 300.0f, 300.0f, 300.0f, 100.0f, "modal", 1 ) );
    SW_EXPECT_FALSE( pMenu1->hasFocus() );
    SW_EXPECT_EQUAL( fixture.find( modal, "modal0" )->getId(), fixture._ui.getFocusManager().getFocusedWidget() );

    fixture._ui.closeScreen( modal );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_TRUE( pMenu1->hasFocus() );
}

/** @brief [UiScreenStackTest] 탐색 방식이면 열 때 기본 포커스(`_defaultFocus`, 없으면 첫 위젯), 포인터 방식이면 포커스 없음 */
SW_TEST_CASE( UiScreenStackTest, DefaultFocusOnOpenInNavigationMode )
{
    using Util = UiScreenTestUtil;
    {
        UiScreenFixture fixture;
        fixture._ui.setInputMode( sw::UiInputMode::Navigation );
        sw::UiScreenDesc desc         = Util::makeDesc( sw::UiLayer::Menu );
        desc._defaultFocus            = "menu2";
        const sw::UiScreenHandle menu = fixture._ui.pushScreen( Util::makeScreen( desc, 0.0f, 0.0f, 800.0f, 600.0f, "menu", 3 ) );
        SW_EXPECT_EQUAL( fixture.find( menu, "menu2" )->getId(), fixture._ui.getFocusManager().getFocusedWidget() );
    }
    {
        UiScreenFixture fixture;
        fixture._ui.setInputMode( sw::UiInputMode::Navigation );
        const sw::UiScreenHandle menu = fixture._ui.pushScreen( Util::makeScreen( Util::makeDesc( sw::UiLayer::Menu ), 0.0f, 0.0f, 800.0f, 600.0f, "menu", 3 ) );
        SW_EXPECT_EQUAL( fixture.find( menu, "menu0" )->getId(), fixture._ui.getFocusManager().getFocusedWidget() );
    }
    {
        UiScreenFixture          fixture;
        const sw::UiScreenHandle menu = fixture._ui.pushScreen( Util::makeScreen( Util::makeDesc( sw::UiLayer::Menu ), 0.0f, 0.0f, 800.0f, 600.0f, "menu", 3 ) );
        SW_EXPECT_EQUAL( sw::kInvalidWidgetId, fixture._ui.getFocusManager().getFocusedWidget() );
        // 포인터에서 탐색으로 바뀌는 순간 기본 포커스로.
        fixture._ui.setInputMode( sw::UiInputMode::Navigation );
        SW_EXPECT_EQUAL( fixture.find( menu, "menu0" )->getId(), fixture._ui.getFocusManager().getFocusedWidget() );
    }
}

/** @brief [UiScreenStackTest] 버튼이 사건 처리 중에 자기 화면을 닫아도 경로가 끝까지 돈다(루트까지 버블링) — 화면은 그 입력 처리가 끝난 뒤 지워진다 */
SW_TEST_CASE( UiScreenStackTest, CloseDuringEventIsDeferred )
{
    using Util = UiScreenTestUtil;
    UiScreenFixture           fixture;
    sw::uitest::UiEventRecord record;
    auto                      screen  = Util::makeScreen( Util::makeDesc( sw::UiLayer::Menu ), 0.0f, 0.0f, 800.0f, 600.0f, "menu", 1, &record );
    auto*                     pButton = static_cast<sw::uitest::TestBoxWidget*>( screen->getTree().findWidgetByName( "menu0" ) );
    pButton->_bHandleBubble           = false;
    pButton->_pScreenToClose          = screen.get();
    const sw::UiScreenHandle menu     = fixture._ui.pushScreen( std::move( screen ) );

    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseMove( 20, 20 ) ) );
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left, 20, 20 ) ) );
    fixture._input.beginFrame( Util::kFrameSeconds );
    record._listLine.clear();
    fixture._ui.processInput( Util::kFrameSeconds );
    SW_EXPECT_TRUE_MSG( record.joined().find( "menuroot T, menu0 T, menu0 B, menuroot B" ) != sw::string::npos, record.joined().c_str() );
    SW_EXPECT_TRUE( fixture._ui.findScreen( menu ) == nullptr ); // processInput 끝에서 지웠다
    fixture._input.endFrame();
}

/** @brief [UiScreenStackTest] HUD 층 화면만 있으면 활성 화면이 없다 — 포커스 · 커서 · 게임 입력 막기 모두 없다 */
SW_TEST_CASE( UiScreenStackTest, HudLayerNeverTakesFocus )
{
    using Util = UiScreenTestUtil;
    UiScreenFixture fixture;
    fixture._ui.setInputMode( sw::UiInputMode::Navigation );
    (void)fixture._ui.pushScreen( Util::makeScreen( Util::makeDesc( sw::UiLayer::Hud ), 0.0f, 0.0f, 800.0f, 600.0f, "hud", 2 ) );
    SW_EXPECT_TRUE( fixture._ui.getActiveScreen() == nullptr );
    SW_EXPECT_EQUAL( sw::kInvalidWidgetId, fixture._ui.getFocusManager().getFocusedWidget() );
    SW_EXPECT_FALSE( fixture._ui.wantsCursor() );
    SW_EXPECT_FALSE( fixture._ui.isGameInputBlocked() );

    // 메뉴가 열리면 메뉴가 활성 — HUD 는 그 아래 층이다.
    const sw::UiScreenHandle menu = fixture._ui.pushScreen( Util::makeScreen( Util::makeDesc( sw::UiLayer::Menu ), 0.0f, 0.0f, 800.0f, 600.0f, "menu", 1 ) );
    SW_EXPECT_EQUAL( menu, fixture._ui.getActiveScreen()->getHandle() );
    SW_EXPECT_TRUE( fixture._ui.wantsCursor() );
}

/** @brief [UiScreenStackTest] `_bPausesGame` 화면이 있는 동안 게임 시간 배율이 0 이고, 닫으면 원래 배율로 돌아온다 */
SW_TEST_CASE( UiScreenStackTest, PausingScreenStopsGameTime )
{
    using Util = UiScreenTestUtil;
    UiScreenFixture  fixture;
    const float32    before         = sw::GameTimeScale::get();
    sw::UiScreenDesc desc           = Util::makeDesc( sw::UiLayer::Menu );
    desc._bPausesGame               = true;
    const sw::UiScreenHandle pause  = fixture._ui.pushScreen( Util::makeScreen( desc, 0.0f, 0.0f, 800.0f, 600.0f, "pause", 1 ) );
    const sw::UiScreenHandle second = fixture._ui.pushScreen( Util::makeScreen( desc, 0.0f, 0.0f, 800.0f, 600.0f, "second", 1 ) );
    SW_EXPECT_EQUAL( 0.0f, sw::GameTimeScale::get() );
    SW_EXPECT_EQUAL( 1u, sw::GameTimeScale::getPauseRequestCount() ); // 화면 둘이어도 요청은 하나

    fixture._ui.closeScreen( second );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_EQUAL( 0.0f, sw::GameTimeScale::get() );
    fixture._ui.closeScreen( pause );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_EQUAL( before, sw::GameTimeScale::get() );
    SW_EXPECT_EQUAL( 0u, sw::GameTimeScale::getPauseRequestCount() );
}

/** @brief [UiScreenStackTest] 내려가는 모듈 이미지에 vtable 이 있는 위젯이 든 화면은 그 자리에서 닫힌다 — 다른 화면은 남는다 */
SW_TEST_CASE( UiScreenStackTest, ModuleUnloadClosesScreensWithItsWidgets )
{
    using Util = UiScreenTestUtil;
    UiScreenFixture          fixture;
    const sw::UiScreenHandle menu = fixture._ui.pushScreen( Util::makeScreen( Util::makeDesc( sw::UiLayer::Menu ), 0.0f, 0.0f, 800.0f, 600.0f, "menu", 1 ) );
    // 버튼 없는 화면(시험 패널만) — 시험 상자의 vtable 범위에 들지 않아 남는다.
    const sw::UiScreenHandle other   = fixture._ui.pushScreen( Util::makeScreen( Util::makeDesc( sw::UiLayer::Hud ), 0.0f, 0.0f, 800.0f, 600.0f, "other", 0 ) );
    const sw::Widget*        pButton = fixture.find( menu, "menu0" );
    const uint8*             pVtable = static_cast<const uint8*>( sw::IModuleUnloadListener::findVtableAddress( pButton ) );

    test::ScopedLogCollector logs;
    bool                     bKeepMapped = false;
    SW_EXPECT_EQUAL( 1u, fixture._ui.onModuleUnloading( pVtable, pVtable + 1, bKeepMapped ) );
    SW_EXPECT_TRUE( fixture._ui.findScreen( menu ) == nullptr );
    SW_EXPECT_EQUAL( 1u, logs.countContaining( "closed for module reload" ) );
    SW_EXPECT_FALSE( bKeepMapped );
    SW_EXPECT_TRUE( fixture._ui.findScreen( other ) != nullptr );
}

/** @brief [UiScreenStackTest] UI.Back(Esc)은 아무 위젯도 처리하지 않으면 맨 위 화면을 닫고(기본 onBack), 그 아래가 활성 화면이 된다 — Esc 는 먹힌 입력이다 */
SW_TEST_CASE( UiScreenStackTest, BackClosesTopScreen )
{
    using Util = UiScreenTestUtil;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getInputMap().bind( "Pause", sw::Key::Escape );
    {
        sw::UiSystem ui;
        SW_ASSERT_TRUE( ui.initialize( input, nullptr, "engine/input/ui.input.xml" ) );
        const sw::UiScreenHandle menu    = ui.pushScreen( Util::makeScreen( Util::makeDesc( sw::UiLayer::Menu ), 0.0f, 0.0f, 800.0f, 600.0f, "menu", 1 ) );
        const sw::UiScreenHandle options = ui.pushScreen( Util::makeScreen( Util::makeDesc( sw::UiLayer::Menu ), 0.0f, 0.0f, 800.0f, 600.0f, "options", 1 ) );
        SW_EXPECT_EQUAL( options, ui.getActiveScreen()->getHandle() );

        SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Escape ) ) );
        input.beginFrame( Util::kFrameSeconds );
        ui.processInput( Util::kFrameSeconds );
        SW_EXPECT_TRUE( ui.findScreen( options ) == nullptr );
        SW_ASSERT_NOT_NULL( ui.getActiveScreen() );
        SW_EXPECT_EQUAL( menu, ui.getActiveScreen()->getHandle() );
        SW_EXPECT_TRUE( input.getInputMap().wasActionTriggered( "Pause" ) );
        SW_EXPECT_TRUE( ui.isActionConsumed( input.getInputMap(), "Pause" ) ); // 게임의 일시정지는 같은 Esc 를 받지 않는다
        input.endFrame();
        ui.shutdown();
    }
    input.shutdown();
}
