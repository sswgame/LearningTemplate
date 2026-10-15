#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Text/FontService.h"
#include "Engine/UI/Base/UIEvents.h"
#include "Engine/UI/Base/UIFocusManager.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Input/UIClipboardService.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/TextInputWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// UITextInputTest — 글 입력 칸의 편집: 커서 이동 · 선택 · 지우기 · 복사 붙여넣기 · 조합 글 · 포인터로 커서 놓기 · 오른쪽에서 왼쪽 방향 · 입력 맵 경로(Shift 조합 가림) · 클립보드 창구.

namespace
{
    /** @brief 글 입력 칸 하나를 든 화면과 UI 시스템입니다. 글꼴 없이도 편집 논리는 돕니다. */
    struct UITextInputFixture
    {
        sw::InputManager     _input;
        sw::UISystem         _ui;
        sw::TextInputWidget* _pField;
        sw::UIScreenHandle   _screen;
        sw::UIViewport       _viewport;
        bool                 _bReady;

        explicit UITextInputFixture( sw::FontService* pFontService = nullptr )
            : _input{}
            , _ui{}
            , _pField{ nullptr }
            , _screen{ sw::kInvalidUIScreenHandle }
            , _viewport{}
            , _bReady{ false }
        {
            _bReady = _input.initialize() && _ui.initialize( _input, pFontService, "engine/input/ui.input.xml" );
            if ( _bReady == false )
                return;
            sw::unique_ptr<sw::BoxPanel> root = sw::make_unique<sw::BoxPanel>();
            _pField                           = static_cast<sw::TextInputWidget*>( root->addChild( sw::make_unique<sw::TextInputWidget>() ) );
            _screen                           = _ui.pushScreen( sw::make_unique<sw::UIScreen>( sw::UIScreenDesc{}, std::move( root ) ) );
            _viewport._size                   = sw::float2{ 800.0f, 600.0f };
            _viewport._physicalSize           = _viewport._size;
            runFrame();
            _bReady = _ui.getInputRouter().getFocusManager().setFocus( _ui.findScreen( _screen )->getTree(), _pField->getID() );
            runFrame();
        }

        ~UITextInputFixture()
        {
            _ui.shutdown();
            _input.shutdown();
        }

        void runFrame()
        {
            _input.beginFrame( 1.0f / 60.0f );
            _ui.processInput( 1.0f / 60.0f );
            _ui.update( 1.0f / 60.0f, _viewport );
            _input.endFrame();
        }

        /** @brief 편집 액션 하나를 칸에 직접 보냅니다(입력 맵을 거치지 않습니다). */
        void sendAction( const utf8* pAction ) const
        {
            sw::UIActionEvent event{};
            event._action = sw::hashed_string( pAction );
            (void)_pField->onActionEvent( event, sw::UIRoutePhase::Bubble );
        }

        void sendText( const utf8* pText ) const
        {
            sw::UITextEvent event{};
            event._text = pText;
            (void)_pField->onTextEvent( event );
        }

        void sendComposition( const utf8* pText ) const
        {
            sw::UITextEvent event{};
            event._text         = pText;
            event._bComposition = SW_TRUE;
            (void)_pField->onTextEvent( event );
        }
    };

    /** @brief 눌렀다 뗍니다(한 프레임씩). */
    void tapKey( UITextInputFixture& fixture, sw::Key key )
    {
        SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyDown( key ) ) );
        fixture.runFrame();
        SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyUp( key ) ) );
        fixture.runFrame();
    }

    /** @brief 가짜 글꼴(기본 가족 Latin, 글자 0.5 em)로 UI 시스템을 띄우는 장치입니다. */
    struct UITextInputFontFixture
    {
        sw::test::FakeFontServiceFixture _fonts;
        bool                             _bInitialized;

        UITextInputFontFixture()
            : _fonts{}
            , _bInitialized{ false }
        {
            sw::FontCatalogDesc catalog{};
            catalog._defaultFamily = "Latin";
            sw::test::FakeFontServiceFixture::addFamily( catalog, "Latin", "test/fonts/latin.ttf" );
            _bInitialized = _fonts.initialize( catalog );
        }
    };

    constexpr utf8 kHangulGa[] = "\xEA\xB0\x80"; // 가 (3 바이트)
    constexpr utf8 kHangulNa[] = "\xEB\x82\x98"; // 나 (3 바이트)
} // namespace

/**
 * @brief [UITextInputTest] 커서는 코드 포인트 단위로 움직이고(좌우, Home, End), 글은 커서 자리에 들어가며, 처음과 끝에서 멈춘다
 * @details 변이: `previousBoundary` 의 이어지는 바이트 건너뛰기를 빼면 "가" 한가운데에 커서가 서서 글이 깨진다.
 */
SW_TEST_CASE( UITextInputTest, CaretMovesByCodepointAndInsertsAtCaret )
{
    UITextInputFixture fixture;
    SW_ASSERT_TRUE( fixture._bReady );
    sw::TextInputWidget& field = *fixture._pField;
    field.setText( "a\xEA\xB0\x80"
                   "b" ); // a 가 b = 5 바이트
    SW_EXPECT_EQUAL( 5u, field.getCaretByte() );
    SW_EXPECT_FALSE( field.hasSelection() );

    fixture.sendAction( sw::UIActionName::kTextCaretLeft );
    SW_EXPECT_EQUAL( 4u, field.getCaretByte() );
    fixture.sendAction( sw::UIActionName::kTextCaretLeft );
    SW_EXPECT_EQUAL( 1u, field.getCaretByte() ); // "가" 를 통째로 넘는다
    fixture.sendText( "X" );
    SW_EXPECT_STREQ( "aX\xEA\xB0\x80"
                     "b",
                     field.getText().c_str() );
    SW_EXPECT_EQUAL( 2u, field.getCaretByte() );

    fixture.sendAction( sw::UIActionName::kTextHome );
    SW_EXPECT_EQUAL( 0u, field.getCaretByte() );
    fixture.sendAction( sw::UIActionName::kTextCaretLeft );
    SW_EXPECT_EQUAL( 0u, field.getCaretByte() ); // 처음에서 멈춘다
    fixture.sendAction( sw::UIActionName::kTextEnd );
    SW_EXPECT_EQUAL( 6u, field.getCaretByte() );
    fixture.sendAction( sw::UIActionName::kTextCaretRight );
    SW_EXPECT_EQUAL( 6u, field.getCaretByte() ); // 끝에서 멈춘다
}

/**
 * @brief [UITextInputTest] 선택: Shift 이동으로 넓히고, 입력은 선택을 대신하며, Backspace · Delete 는 선택을 지우고, 선택 없이는 앞뒤 한 글자를 지운다
 * @details 변이: `replaceSelection` 이 선택 길이를 빼지 않으면 "Hello" 에 "J" 를 넣을 때 "JHello" 처럼 선택이 남아 진다.
 */
SW_TEST_CASE( UITextInputTest, SelectionIsReplacedAndDeleted )
{
    UITextInputFixture fixture;
    SW_ASSERT_TRUE( fixture._bReady );
    sw::TextInputWidget& field = *fixture._pField;
    field.setText( "Hello" );
    fixture.sendAction( sw::UIActionName::kTextHome );
    fixture.sendAction( sw::UIActionName::kTextSelectRight );
    fixture.sendAction( sw::UIActionName::kTextSelectRight );
    SW_EXPECT_TRUE( field.hasSelection() );
    SW_EXPECT_STREQ( "He", field.getSelectedText().c_str() );

    fixture.sendText( "J" );
    SW_EXPECT_STREQ( "Jllo", field.getText().c_str() );
    SW_EXPECT_FALSE( field.hasSelection() );
    SW_EXPECT_EQUAL( 1u, field.getCaretByte() );

    fixture.sendAction( sw::UIActionName::kTextSelectEnd );
    SW_EXPECT_STREQ( "llo", field.getSelectedText().c_str() );
    fixture.sendAction( sw::UIActionName::kTextBackspace );
    SW_EXPECT_STREQ( "J", field.getText().c_str() );

    field.setText( "abc" );
    fixture.sendAction( sw::UIActionName::kTextHome );
    fixture.sendAction( sw::UIActionName::kTextDelete );
    SW_EXPECT_STREQ( "bc", field.getText().c_str() ); // 선택 없이 Delete 는 뒤 한 글자
    fixture.sendAction( sw::UIActionName::kTextEnd );
    fixture.sendAction( sw::UIActionName::kTextBackspace );
    SW_EXPECT_STREQ( "b", field.getText().c_str() );

    fixture.sendAction( sw::UIActionName::kTextSelectAll );
    SW_EXPECT_STREQ( "b", field.getSelectedText().c_str() );
    fixture.sendAction( sw::UIActionName::kTextCaretLeft );
    SW_EXPECT_FALSE( field.hasSelection() );
    SW_EXPECT_EQUAL( 0u, field.getCaretByte() ); // 왼쪽 키는 선택의 왼쪽 끝으로 푼다
}

/**
 * @brief [UITextInputTest] 복사 · 잘라내기 · 붙여넣기: 클립보드로 한 바퀴 돌고, 붙이는 글의 줄 바꿈은 빠지며, 최대 길이를 넘는 만큼은 버린다
 * @details 변이: 붙이는 글에서 제어 문자를 빼지 않으면 "ab\ncd" 가 그대로 들어와 진다.
 */
SW_TEST_CASE( UITextInputTest, ClipboardRoundTripAndSingleLinePaste )
{
    UITextInputFixture fixture;
    SW_ASSERT_TRUE( fixture._bReady );
    sw::TextInputWidget& field = *fixture._pField;
    field.setText( "abcdef" );
    fixture.sendAction( sw::UIActionName::kTextHome );
    fixture.sendAction( sw::UIActionName::kTextSelectRight );
    fixture.sendAction( sw::UIActionName::kTextSelectRight );
    fixture.sendAction( sw::UIActionName::kTextCopy );
    sw::string clip{};
    SW_ASSERT_TRUE( fixture._ui.getClipboard().tryGetText( clip ) );
    SW_EXPECT_STREQ( "ab", clip.c_str() );
    SW_EXPECT_STREQ( "abcdef", field.getText().c_str() ); // 복사는 글을 바꾸지 않는다

    fixture.sendAction( sw::UIActionName::kTextCut );
    SW_EXPECT_STREQ( "cdef", field.getText().c_str() );
    fixture.sendAction( sw::UIActionName::kTextEnd );
    fixture.sendAction( sw::UIActionName::kTextPaste );
    SW_EXPECT_STREQ( "cdefab", field.getText().c_str() );

    fixture._ui.getClipboard().setText( "x\ny\r\nz\t" );
    field.setText( "" );
    fixture.sendAction( sw::UIActionName::kTextPaste );
    SW_EXPECT_STREQ( "xyz", field.getText().c_str() );

    sw::string changed{};
    (void)field.getOnTextChanged().add( [&changed]( const sw::string& text )
    { changed = text; } );
    fixture._ui.getClipboard().setText( "123456" );
    field.setText( "" );
    field.setMaxLength( 4 );
    fixture.sendAction( sw::UIActionName::kTextPaste );
    SW_EXPECT_STREQ( "1234", field.getText().c_str() ); // 최대 길이를 넘는 만큼은 버린다
    SW_EXPECT_STREQ( "1234", changed.c_str() );
    field.setText( "12" );
    fixture.sendAction( sw::UIActionName::kTextHome );
    fixture.sendAction( sw::UIActionName::kTextSelectRight );               // "1" 선택
    fixture.sendText( "\xEA\xB0\x80\xEB\x82\x98\xEA\xB0\x80\xEB\x82\x98" ); // 가나가나 — 선택을 뺀 1 글자 + 3 글자만 들어간다
    SW_EXPECT_STREQ( "\xEA\xB0\x80\xEB\x82\x98\xEA\xB0\x80"
                     "2",
                     field.getText().c_str() );
}

/**
 * @brief [UITextInputTest] 조합 글은 커서 자리에 끼워 보이고(확정 글에는 들어가지 않는다), 조합을 시작하면 선택은 지워지며, 확정하면 커서 자리에 들어간다
 * @details 변이: `refreshDisplay` 가 조합을 끝에 붙이면 "a|b" 의 가운데 조합이 "ab나" 로 보여 진다.
 */
SW_TEST_CASE( UITextInputTest, CompositionSitsAtCaret )
{
    UITextInputFixture fixture;
    SW_ASSERT_TRUE( fixture._bReady );
    sw::TextInputWidget& field = *fixture._pField;
    field.setText( "ab" );
    fixture.sendAction( sw::UIActionName::kTextCaretLeft ); // a|b
    fixture.sendComposition( kHangulNa );
    SW_EXPECT_STREQ( kHangulNa, field.getComposition().c_str() );
    SW_EXPECT_STREQ( "ab", field.getText().c_str() );
    const sw::TextWidget* pDisplay = static_cast<const sw::TextWidget*>( field.getChild( 0 ) );
    SW_EXPECT_STREQ( "a\xEB\x82\x98"
                     "b",
                     pDisplay->getText().c_str() );

    fixture.sendAction( sw::UIActionName::kTextCaretLeft ); // 조합 중에는 커서 이동이 IME 의 것이다
    SW_EXPECT_EQUAL( 1u, field.getCaretByte() );

    fixture.sendText( kHangulGa ); // 확정
    SW_EXPECT_STREQ( "a\xEA\xB0\x80"
                     "b",
                     field.getText().c_str() );
    SW_EXPECT_TRUE( field.getComposition().empty() );
    SW_EXPECT_EQUAL( 4u, field.getCaretByte() );

    fixture.sendAction( sw::UIActionName::kTextSelectAll );
    fixture.sendComposition( kHangulNa ); // 선택은 조합이 시작될 때 지워진다
    SW_EXPECT_TRUE( field.getText().empty() );
}

/**
 * @brief [UITextInputTest] 오른쪽에서 왼쪽 칸에서는 화면 기준 왼쪽 키가 논리 순서로 뒤(+1)다
 * @details 변이: `resolveHorizontalStep` 이 방향을 보지 않으면 RTL 에서 왼쪽 키가 앞으로 가 진다.
 */
SW_TEST_CASE( UITextInputTest, ArrowKeysFollowScreenDirectionInRtl )
{
    UITextInputFixture fixture;
    SW_ASSERT_TRUE( fixture._bReady );
    sw::TextInputWidget& field = *fixture._pField;
    field.setFlowDirection( sw::UIFlowDirection::RightToLeft );
    fixture.runFrame(); // 배치가 방향을 기록한다
    SW_ASSERT_TRUE( field.isRightToLeft() );
    field.setText( "abc" );
    fixture.sendAction( sw::UIActionName::kTextHome );
    SW_EXPECT_EQUAL( 0u, field.getCaretByte() );
    fixture.sendAction( sw::UIActionName::kTextCaretLeft ); // 화면 왼쪽 = 논리 뒤
    SW_EXPECT_EQUAL( 1u, field.getCaretByte() );
    fixture.sendAction( sw::UIActionName::kTextCaretRight );
    SW_EXPECT_EQUAL( 0u, field.getCaretByte() );
}

/**
 * @brief [UITextInputTest] 입력 맵 경로: 키로 커서를 옮기고(Shift+Left 는 Left 를 가려 한 칸만), Ctrl+A · Ctrl+C · Ctrl+V 가 복사 붙여넣기를 한 바퀴 돌린다
 * @details 변이: 라우터의 `kArrTextAction` 이 가림 표(`_pShadowedBy`)를 빼면 Shift+Left 가 선택 뒤 커서 이동으로 다시 풀려 선택이 없어 진다.
 */
SW_TEST_CASE( UITextInputTest, InputMapRoutesEditKeys )
{
    UITextInputFixture fixture;
    SW_ASSERT_TRUE( fixture._bReady );
    sw::TextInputWidget& field = *fixture._pField;
    field.setText( "abc" );
    SW_EXPECT_TRUE( fixture._input.getKeyboardFocus() == sw::InputKeyboardFocus::UI );

    tapKey( fixture, sw::Key::Left );
    SW_EXPECT_EQUAL( 2u, field.getCaretByte() );

    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::LeftShift ) ) );
    fixture.runFrame();
    tapKey( fixture, sw::Key::Left );
    SW_EXPECT_TRUE( field.hasSelection() );
    SW_EXPECT_STREQ( "b", field.getSelectedText().c_str() ); // 한 칸만 넓어졌다
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::LeftShift ) ) );
    fixture.runFrame();

    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::LeftControl ) ) );
    fixture.runFrame();
    tapKey( fixture, sw::Key::A );
    SW_EXPECT_STREQ( "abc", field.getSelectedText().c_str() );
    tapKey( fixture, sw::Key::C );
    sw::string clip{};
    SW_ASSERT_TRUE( fixture._ui.getClipboard().tryGetText( clip ) );
    SW_EXPECT_STREQ( "abc", clip.c_str() );
    tapKey( fixture, sw::Key::V ); // 선택을 같은 글로 바꾼다
    SW_EXPECT_STREQ( "abc", field.getText().c_str() );
    SW_EXPECT_FALSE( field.hasSelection() );
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::LeftControl ) ) );
    fixture.runFrame();

    tapKey( fixture, sw::Key::Home );
    SW_EXPECT_EQUAL( 0u, field.getCaretByte() );
    tapKey( fixture, sw::Key::Delete );
    SW_EXPECT_STREQ( "bc", field.getText().c_str() );
}

/**
 * @brief [UITextInputTest] Ctrl 조합이 만든 제어 문자(Ctrl+A 의 0x01)와 탭은 글에 들어가지 않는다
 * @details 변이: `isControlCodepoint` 검사를 빼면 "\x01" 이 글에 붙어 진다.
 */
SW_TEST_CASE( UITextInputTest, ControlCharactersAreNotInserted )
{
    UITextInputFixture fixture;
    SW_ASSERT_TRUE( fixture._bReady );
    fixture.sendText( "a\x01\t\x16"
                      "b" );
    SW_EXPECT_STREQ( "ab", fixture._pField->getText().c_str() );
}

/**
 * @brief [UITextInputTest] 포인터를 누르면 가장 가까운 글자 사이로 커서가 가고(포커스가 오는 알림이 그 자리를 덮지 않는다), 두 번 누르면 전체를 선택한다.
 *        선택 사각형은 글자 폭만큼, 커서는 선택 끝에 그려진다
 * @details 변이: `onFocusChanged` 가 `_bKeepCaretOnFocus` 를 보지 않으면 눌러 포커스를 얻을 때 커서가 글 끝으로 간다.
 */
SW_TEST_CASE( UITextInputTest, PointerPlacesCaretAndSelectionIsPainted )
{
    UITextInputFontFixture fonts;
    SW_ASSERT_TRUE( fonts._bInitialized );
    UITextInputFixture fixture( fonts._fonts._fontService.get() );
    SW_ASSERT_TRUE( fixture._bReady );
    sw::TextInputWidget& field = *fixture._pField;
    field.setText( "abcd" );
    fixture.runFrame();
    const sw::TextWidget* pDisplay    = static_cast<const sw::TextWidget*>( field.getChild( 0 ) );
    const float32         glyphWidth  = pDisplay->getDesiredSize()._x / 4.0f;
    const float32         textLeft    = pDisplay->getGeometry()._position._x;
    const float32         textMiddleY = pDisplay->getGeometry()._position._y + pDisplay->getGeometry()._size._y * 0.5f;
    SW_ASSERT_TRUE( glyphWidth > 0.0f );

    // 포커스가 없는 칸의 "ab" 와 "c" 사이 경계 가까이(오른쪽으로 조금 치우침)를 누른다 — 포커스를 얻는 알림이 커서를 글 끝으로 덮지 않아야 한다.
    sw::WidgetTree& tree = fixture._ui.findScreen( fixture._screen )->getTree();
    fixture._ui.getInputRouter().getFocusManager().clearFocus();
    SW_ASSERT_FALSE( field.hasFocus() );
    sw::UIPointerEvent down{};
    down._kind              = sw::UIPointerEventKind::Down;
    down._button            = sw::MouseButton::Left;
    down._position          = sw::float2{ textLeft + glyphWidth * 2.2f, textMiddleY };
    const sw::UIReply reply = field.onPointerEvent( down, sw::UIRoutePhase::Bubble );
    SW_EXPECT_TRUE( reply.isHandled() );
    SW_ASSERT_TRUE( reply._focusRequest == field.getID() );
    SW_ASSERT_TRUE( fixture._ui.getInputRouter().getFocusManager().setFocus( tree, reply._focusRequest ) );
    SW_EXPECT_EQUAL( 2u, field.getCaretByte() );

    field.selectAll();
    fixture.runFrame();
    bool bFoundSelection = false;
    for ( const sw::CanvasQuad& quad : fixture._ui.getCanvas()._listQuad )
    {
        const bool bSelectionQuad = quad._color._z > 0.9f && quad._color._x < 0.35f && quad._color._w > 0.3f && quad._color._w < 0.5f;
        if ( bSelectionQuad )
        {
            bFoundSelection = true;
            SW_EXPECT_NEAR_EQUAL( glyphWidth * 4.0f, quad._rect._z, 0.6f );
            SW_EXPECT_NEAR_EQUAL( textLeft, quad._rect._x, 0.6f );
        }
    }
    SW_EXPECT_TRUE( bFoundSelection );

    sw::UIPointerEvent doubleDown = down;
    doubleDown._clickCount        = 2;
    field.setCaretByte( 0 );
    (void)field.onPointerEvent( doubleDown, sw::UIRoutePhase::Bubble );
    SW_EXPECT_STREQ( "abcd", field.getSelectedText().c_str() );
}

/**
 * @brief [UITextInputTest] 클립보드 창구: 연결이 없으면 쓴 글이 그대로 돌아오고, 운영체제 연결이 있으면 쓸 때 거기에도 쓰고 읽을 때 운영체제 쪽 글을 먼저 본다
 * @details 변이: `tryGetText` 가 연결을 보지 않으면 다른 프로그램에서 복사한 글이 아니라 엔진 안 보관본이 나와 진다.
 */
SW_TEST_CASE( UITextInputTest, ClipboardServiceUsesSystemHooksWhenSet )
{
    static sw::string s_systemText;
    s_systemText.clear();
    sw::UIClipboardService clipboard{};
    sw::string             out{};
    SW_EXPECT_FALSE( clipboard.tryGetText( out ) ); // 비어 있다
    clipboard.setText( "inner" );
    SW_ASSERT_TRUE( clipboard.tryGetText( out ) );
    SW_EXPECT_STREQ( "inner", out.c_str() );

    sw::UIClipboardAPI api{};
    api._pRead = []( sw::string& outText )
    {
        outText = s_systemText;
        return s_systemText.empty() == false;
    };
    api._pWrite = []( sw::string_view text )
    { s_systemText = sw::string{ text }; };
    clipboard.setAPI( api );
    clipboard.setText( "sent" );
    SW_EXPECT_STREQ( "sent", s_systemText.c_str() ); // 운영체제에도 썼다
    s_systemText = "from another app";
    SW_ASSERT_TRUE( clipboard.tryGetText( out ) );
    SW_EXPECT_STREQ( "from another app", out.c_str() );
    s_systemText.clear();
    SW_ASSERT_TRUE( clipboard.tryGetText( out ) ); // 운영체제 쪽이 비면 보관본
    SW_EXPECT_STREQ( "sent", out.c_str() );
}
