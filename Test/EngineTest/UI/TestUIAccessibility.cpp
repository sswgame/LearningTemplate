#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Text/FontSystem.h"
#include "Engine/Text/TextLayout.h"
#include "Engine/UI/Layout/ScrollPanel.h"
#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/Layout/UIScale.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Screen/UISubtitleService.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "EngineTest/Text/FakeFontRasterizer.h"

#include "GameFramework/Base/UI/Dialogue/DialogueRunnerComponent.h"

#include "TestFramework/TestFramework.h"

// UIAccessibilityTest — 글자 크기 하한(gv_uiTextScale) · 글자 배율 2 에서 메뉴가 넘치지 않음(견본 문서) · 자막(UISubtitleService: 설정 · 읽기 시간 · 둘까지 쌓기 · 대화 러너).
// 가짜 래스터라이저(글자 0.5 em) · 디바이스 없음(nogpu). 자막 화면은 저장소 문서 engine/ui/subtitles.ui.xml 을 읽는다.

namespace
{
    /** @brief 전역 변수 하나를 이름으로 바꾸고 스코프 끝에 되돌립니다(시험 DLL 은 Engine 의 gv_* 를 extern 으로 못 읽는다). */
    class ScopedVariable
    {
    public:
        ScopedVariable( const utf8* pName, sw::string_view value )
            : _pInfo{ sw::engine::getGlobalVariableManager().findVariable( pName ) }
            , _previous{}
        {
            SW_EXPECT_TRUE( _pInfo != nullptr );
            if ( _pInfo == nullptr )
                return;
            _previous = _pInfo->getValueAsString();
            SW_EXPECT_TRUE( _pInfo->setValueFromString( value ) );
        }
        ~ScopedVariable()
        {
            if ( _pInfo != nullptr )
                (void)_pInfo->setValueFromString( _previous );
        }
        ScopedVariable( const ScopedVariable& )            = delete;
        ScopedVariable& operator=( const ScopedVariable& ) = delete;

        /** @brief 지금 스코프 안에서 값을 다시 바꿉니다(되돌릴 값은 처음 것). */
        void set( sw::string_view value ) const
        {
            if ( _pInfo != nullptr )
                SW_EXPECT_TRUE( _pInfo->setValueFromString( value ) );
        }

    private:
        sw::GlobalVariableInfo* _pInfo;
        sw::string              _previous;
    };

    /** @brief 입력 · 가짜 글꼴 · UI 시스템 — 1280×720 UI 단위 뷰포트(배율 1)로 프레임을 돌립니다. */
    struct UIAccessibilityFixture
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        sw::InputManager                _input;
        sw::test::FakeFontSystemFixture _fonts;
        sw::UISystem                    _ui;
        bool                            _bReady;

        UIAccessibilityFixture()
            : _input{}
            , _fonts{}
            , _ui{}
            , _bReady{ false }
        {
            sw::FontCatalogDesc catalog{};
            catalog._defaultFamily = "Latin";
            sw::test::FakeFontSystemFixture::addFamily( catalog, "Latin", "test/fonts/latin.ttf" );
            _bReady = _input.initialize() && _fonts.initialize( catalog ) && _ui.initialize( _input, _fonts._fontSystem.get() );
        }

        ~UIAccessibilityFixture()
        {
            _ui.shutdown();
            _input.shutdown();
        }

        UIAccessibilityFixture( const UIAccessibilityFixture& )            = delete;
        UIAccessibilityFixture& operator=( const UIAccessibilityFixture& ) = delete;

        void runFrame( float32 deltaSeconds = kFrameSeconds )
        {
            sw::UIViewport viewport{};
            viewport._size         = sw::float2{ 1280.0f, 720.0f };
            viewport._physicalSize = viewport._size;
            _input.beginFrame( deltaSeconds );
            _ui.processInput( deltaSeconds );
            _ui.update( deltaSeconds, viewport );
            _input.endFrame();
        }

        /** @brief 열린 자막 화면(없으면 nullptr)입니다. */
        sw::UIScreen* findSubtitleScreen() const { return _ui.findScreen( _ui.getSubtitles().getScreen() ); }
    };

    struct UIAccessibilityTestUtil
    {
        /** @brief 견본 옵션 메뉴 — 640×400 창 안 세로 스크롤에 "이름 · 슬라이더" 줄 여덟. 이름은 줄 바꿈 없이 줄임표로 자른다. */
        static constexpr utf8   kOptionsDocument[] = "<UiDocument _schemaVersion=\"1\">\n"
                                                     "\t<UIScreenDesc _bPausesGame=\"false\" />\n"
                                                     "\t<CanvasPanel>\n"
                                                     "\t\t<BorderPanel _name=\"Window\" _contentPadding=\"24,24,24,24\">\n"
                                                     "\t\t\t<_slot _anchorMin=\"0.5,0.5\" _anchorMax=\"0.5,0.5\" _offsetMin=\"-320,-200\" _offsetMax=\"320,200\" />\n"
                                                     "\t\t\t<ScrollPanel _name=\"Scroll\">\n"
                                                     "\t\t\t\t<BoxPanel _name=\"Rows\" _orientation=\"Vertical\" _spacing=\"8\">\n"
                                                     "%ROWS%"
                                                     "\t\t\t\t</BoxPanel>\n"
                                                     "\t\t\t</ScrollPanel>\n"
                                                     "\t\t</BorderPanel>\n"
                                                     "\t</CanvasPanel>\n"
                                                     "</UiDocument>\n";
        static constexpr uint32 kOptionRowCount    = 8;

        static sw::string makeOptionsDocument()
        {
            sw::string rows;
            for ( uint32 index = 0; index < kOptionRowCount; ++index )
            {
                const sw::string number = sw::to_string( index );
                rows += "\t\t\t\t\t<BoxPanel _name=\"Row" + number + "\" _orientation=\"Horizontal\" _spacing=\"12\">\n";
                rows += "\t\t\t\t\t\t<TextWidget _name=\"Label" + number + "\" _text=\"Subtitle background opacity\">\n";
                rows += "\t\t\t\t\t\t\t<_slot _sizeRule=\"Fill\" _verticalAlignment=\"Center\" />\n";
                rows += "\t\t\t\t\t\t\t<_style _fontSize=\"20\" _bWrap=\"false\" _overflow=\"Ellipsis\" _maxLines=\"1\" />\n";
                rows += "\t\t\t\t\t\t</TextWidget>\n";
                rows += "\t\t\t\t\t\t<SliderWidget _name=\"Slider" + number + "\">\n";
                rows += "\t\t\t\t\t\t\t<_slot _widthOverride=\"200\" _heightOverride=\"24\" _verticalAlignment=\"Center\" />\n";
                rows += "\t\t\t\t\t\t</SliderWidget>\n";
                rows += "\t\t\t\t\t</BoxPanel>\n";
            }
            sw::string       document = kOptionsDocument;
            const size_t     at       = document.find( "%ROWS%" );
            constexpr size_t kTagSize = 6;
            document.replace( at, kTagSize, rows );
            return document;
        }

        /** @brief @p inner 가 @p outer 안에 드는지(오차 0.5 UI 단위) 봅니다. */
        static bool isInside( const sw::UIRect& inner, const sw::UIRect& outer )
        {
            constexpr float32 kEpsilon = 0.5f;
            return inner.getLeft() >= outer.getLeft() - kEpsilon && inner.getTop() >= outer.getTop() - kEpsilon && inner.getRight() <= outer.getRight() + kEpsilon &&
                   inner.getBottom() <= outer.getBottom() + kEpsilon;
        }

        static const sw::TextWidget* findText( const sw::UIScreen& screen, const sw::string& name )
        {
            return screen.getTree().findWidget<sw::TextWidget>( sw::hashed_string( name ) );
        }
    };
} // namespace

/**
 * @brief [UIAccessibilityTest] 글자 배율이 줄여도 글은 12 UI 단위 밑으로 가지 않는다 — 12 보다 작게 적은 글은 그 크기가 하한이고, 키우는 쪽은 그대로 곱한다
 * @details 글 위젯의 측정까지: 글자 크기 16 · 배율 0.5 는 크기 12 · 배율 1 과 같은 크기로 잰다. 변이: `computeScaledFontSize` 의 하한을 빼면 8 로 잰다.
 */
SW_TEST_CASE( UIAccessibilityTest, TextScaleHasMinimumSize )
{
    SW_EXPECT_NEAR_EQUAL( 12.0f, sw::UIScaleUtil::computeScaledFontSize( 16.0f, 0.5f ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, sw::UIScaleUtil::computeScaledFontSize( 20.0f, 0.75f ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, sw::UIScaleUtil::computeScaledFontSize( 10.0f, 0.75f ), 1e-4f ); // 작게 적은 글은 적은 크기가 하한
    SW_EXPECT_NEAR_EQUAL( 20.0f, sw::UIScaleUtil::computeScaledFontSize( 10.0f, 2.0f ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 36.0f, sw::UIScaleUtil::computeScaledFontSize( 18.0f, 2.0f ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 18.0f, sw::UIScaleUtil::computeScaledFontSize( 18.0f, 0.0f ), 1e-4f ); // 0 이하 배율은 1

    sw::test::FakeFontSystemFixture fonts;
    sw::FontCatalogDesc             catalog{};
    catalog._defaultFamily = "Latin";
    sw::test::FakeFontSystemFixture::addFamily( catalog, "Latin", "test/fonts/latin.ttf" );
    SW_ASSERT_TRUE( fonts.initialize( catalog ) );
    sw::TextLayoutEngine layout( *fonts._fontSystem );

    auto measure = [&]( float32 fontSize, float32 textScale )
    {
        sw::WidgetTree                 tree;
        sw::unique_ptr<sw::TextWidget> text  = sw::make_unique<sw::TextWidget>();
        sw::TextWidget* const          pText = text.get();
        sw::TextLayoutStyle            style{};
        style._fontSize = fontSize;
        style._bWrap    = false;
        pText->setTextStyle( style );
        pText->setText( "Subtitles" );
        tree.setRoot( std::move( text ) );
        sw::UILayoutContext context{};
        context._pTextLayout  = &layout;
        context._viewportSize = sw::float2{ 1280.0f, 720.0f };
        context._textScale    = textScale;
        (void)sw::UILayoutPass::update( tree, context );
        return pText->getDesiredSize();
    };
    const sw::float2 floored   = measure( 16.0f, 0.5f );
    const sw::float2 reference = measure( 12.0f, 1.0f );
    SW_EXPECT_NEAR_EQUAL( reference._x, floored._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( reference._y, floored._y, 1e-3f );
    SW_EXPECT_TRUE( measure( 16.0f, 1.0f )._x > floored._x );
}

/**
 * @brief [UIAccessibilityTest] 글자 배율 2 에서 메뉴가 화면 · 창을 넘지 않는다 — 긴 이름은 줄임표로 잘리고 늘어난 줄은 스크롤로 간다
 * @details 옵션 메뉴(8-2)가 아직 없어 견본 문서(창 640×400 · 줄 여덟 "이름 · 슬라이더")와 엔진 견본 pause.ui.xml 로 본다. 배율 1 에서는 자르지도 스크롤하지도 않는다(대조).
 *          배율 2: 모든 이름의 사각형이 그 줄 안 · 슬라이더 왼쪽이고 잘렸다(줄임표), 스크롤 최대값 > 0, 창은 뷰포트 안. pause.ui.xml 의 창도 뷰포트 안.
 */
SW_TEST_CASE( UIAccessibilityTest, OptionsMenuFitsAtDoubleTextScale )
{
    using Util                    = UIAccessibilityTestUtil;
    const sw::UIRect viewportRect = sw::UIRect::makeFromPositionSize( 0.0f, 0.0f, 1280.0f, 720.0f );
    for ( const float32 textScale : { 1.0f, 2.0f } )
    {
        const ScopedVariable   scale( "gv_uiTextScale", sw::to_string( textScale ) );
        UIAccessibilityFixture fixture;
        SW_ASSERT_TRUE( fixture._bReady );
        fixture._ui.getDocumentCache().registerMemoryDocument( "test/options.ui.xml", Util::makeOptionsDocument() );
        sw::UIScreen* pScreen = fixture._ui.findScreen( fixture._ui.openScreen( "test/options.ui.xml" ) );
        SW_ASSERT_NOT_NULL( pScreen );
        fixture.runFrame();
        fixture.runFrame();

        const bool             bDouble = textScale > 1.5f;
        const sw::WidgetTree&  tree    = pScreen->getTree();
        const sw::Widget*      pWindow = tree.findWidgetByName( "Window" );
        const sw::ScrollPanel* pScroll = tree.findWidget<sw::ScrollPanel>( "Scroll" );
        SW_ASSERT_NOT_NULL( pWindow );
        SW_ASSERT_NOT_NULL( pScroll );
        SW_EXPECT_TRUE( Util::isInside( pWindow->getGeometry().computeScreenBounds(), viewportRect ) );
        SW_EXPECT_TRUE( Util::isInside( pScroll->getGeometry().computeScreenBounds(), pWindow->getGeometry().computeScreenBounds() ) );
        SW_EXPECT_EQUAL( bDouble, pScroll->getMaxScrollOffset()._y > 0.0f );
        for ( uint32 index = 0; index < Util::kOptionRowCount; ++index )
        {
            const sw::string      number  = sw::to_string( index );
            const sw::TextWidget* pLabel  = Util::findText( *pScreen, "Label" + number );
            const sw::Widget*     pRow    = tree.findWidgetByName( sw::hashed_string( "Row" + number ) );
            const sw::Widget*     pSlider = tree.findWidgetByName( sw::hashed_string( "Slider" + number ) );
            SW_ASSERT_NOT_NULL( pLabel );
            SW_ASSERT_NOT_NULL( pRow );
            SW_ASSERT_NOT_NULL( pSlider );
            const sw::UIRect label = pLabel->getGeometry().computeScreenBounds();
            SW_EXPECT_TRUE( Util::isInside( label, pRow->getGeometry().computeScreenBounds() ) );
            SW_EXPECT_TRUE( label.getRight() <= pSlider->getGeometry().computeScreenBounds().getLeft() + 0.5f );
            SW_EXPECT_EQUAL( bDouble, pLabel->getLastLayout()._bTruncated == SW_TRUE );
            SW_EXPECT_TRUE( pLabel->getLastLayout()._size._x <= label.getRight() - label.getLeft() + 0.5f );
        }

        sw::UIScreen* pPause = fixture._ui.findScreen( fixture._ui.openScreen( "engine/ui/pause.ui.xml" ) );
        SW_ASSERT_NOT_NULL( pPause );
        fixture.runFrame();
        const sw::Widget* pPauseWindow = pPause->getTree().findWidgetByName( "Window" );
        SW_ASSERT_NOT_NULL( pPauseWindow );
        SW_EXPECT_TRUE( Util::isInside( pPauseWindow->getGeometry().computeScreenBounds(), viewportRect ) );
    }
}

/**
 * @brief [UIAccessibilityTest] 자막이 설정을 따른다 — 크기 0.85 · 1 · 1.3 배, 바탕 알파 = 불투명도, 끄면 화면을 닫되 줄은 계속 흐르고 켜면 지금 줄부터 보인다
 * @details 변이: `update` 의 크기 배를 빼면 크기 2 에서 글 크기가 30 그대로라 진다.
 */
SW_TEST_CASE( UIAccessibilityTest, SubtitlesFollowSettings )
{
    const ScopedVariable   enabled( "gv_subtitles", "true" );
    const ScopedVariable   size( "gv_subtitleSize", "1" );
    const ScopedVariable   opacity( "gv_subtitleBackgroundOpacity", "0.5" );
    UIAccessibilityFixture fixture;
    SW_ASSERT_TRUE( fixture._bReady );
    sw::UISubtitleService& subtitles = fixture._ui.getSubtitles();
    SW_EXPECT_TRUE( fixture.findSubtitleScreen() == nullptr ); // 줄이 없으면 화면도 없다
    (void)subtitles.post( "Guide", "Welcome to the harbor.", 10.0f );
    fixture.runFrame();

    sw::UIScreen* pScreen = fixture.findSubtitleScreen();
    SW_ASSERT_NOT_NULL( pScreen );
    SW_EXPECT_TRUE( pScreen->getDesc()._layer == sw::UILayer::Overlay );
    const sw::WidgetTree& tree     = pScreen->getTree();
    sw::BorderPanel*      pLine    = tree.findWidget<sw::BorderPanel>( "Line0" );
    sw::TextWidget*       pSpeaker = tree.findWidget<sw::TextWidget>( "Speaker0" );
    sw::TextWidget*       pText    = tree.findWidget<sw::TextWidget>( "Text0" );
    SW_ASSERT_NOT_NULL( pLine );
    SW_ASSERT_NOT_NULL( pSpeaker );
    SW_ASSERT_NOT_NULL( pText );
    SW_EXPECT_TRUE( pLine->isVisible() );
    SW_EXPECT_FALSE( tree.findWidgetByName( "Line1" )->isVisible() );
    SW_EXPECT_STREQ( "Guide", pSpeaker->getText().c_str() );
    SW_EXPECT_STREQ( "Welcome to the harbor.", pText->getText().c_str() );
    const float32 baseSize = pText->getTextStyle()._fontSize;
    SW_EXPECT_TRUE( baseSize > 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pLine->getBackground()._color._w, 1e-4f );
    // 줄은 화면 아래 가운데에 놓인다.
    const sw::UIRect lineRect = pLine->getGeometry().computeScreenBounds();
    SW_EXPECT_TRUE( lineRect.getBottom() > 720.0f * 0.75f );
    SW_EXPECT_NEAR_EQUAL( 640.0f, lineRect.getCenter()._x, 1.0f );

    size.set( "2" );
    opacity.set( "0.8" );
    fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( baseSize * 1.3f, pText->getTextStyle()._fontSize, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.8f, pLine->getBackground()._color._w, 1e-4f );
    size.set( "0" );
    fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( baseSize * 0.85f, pText->getTextStyle()._fontSize, 1e-3f );

    // 끄면 화면이 닫히지만 줄은 흐른다(1 초 지남).
    enabled.set( "false" );
    fixture.runFrame( 1.0f );
    SW_EXPECT_TRUE( fixture.findSubtitleScreen() == nullptr );
    SW_EXPECT_EQUAL( 1u, subtitles.getActiveLineCount() );
    SW_EXPECT_TRUE( subtitles.getActiveLine( 0 )._remainingSeconds < 9.5f );
    // 켜면 지금 줄부터 다시 보인다.
    enabled.set( "true" );
    fixture.runFrame();
    pScreen = fixture.findSubtitleScreen();
    SW_ASSERT_NOT_NULL( pScreen );
    SW_EXPECT_STREQ( "Welcome to the harbor.", pScreen->getTree().findWidget<sw::TextWidget>( "Text0" )->getText().c_str() );
}

/**
 * @brief [UIAccessibilityTest] 읽기 시간 = max( 2 초, 글자(코드 포인트) 수 × 0.06 초 ) — 길이 0 으로 올린 줄은 그 시간 뒤에 사라진다
 */
SW_TEST_CASE( UIAccessibilityTest, SubtitleReadingTime )
{
    SW_EXPECT_NEAR_EQUAL( 2.0f, sw::UISubtitleService::computeReadingSeconds( "Hi" ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 6.0f, sw::UISubtitleService::computeReadingSeconds( sw::string( 100, 'a' ) ), 1e-4f );
    // 한글은 바이트가 아니라 글자로 센다 — 40 글자(120 바이트) = 2.4 초.
    sw::string hangul;
    for ( uint32 index = 0; index < 40; ++index )
    {
        hangul += "\xEA\xB0\x80"; // U+AC00
    }
    SW_EXPECT_NEAR_EQUAL( 2.4f, sw::UISubtitleService::computeReadingSeconds( hangul ), 1e-4f );

    const ScopedVariable   enabled( "gv_subtitles", "true" );
    UIAccessibilityFixture fixture;
    SW_ASSERT_TRUE( fixture._bReady );
    sw::UISubtitleService& subtitles = fixture._ui.getSubtitles();
    (void)subtitles.post( "", sw::string( 50, 'b' ) ); // 3 초
    fixture.runFrame( 0.0f );
    SW_ASSERT_EQUAL( 1u, subtitles.getActiveLineCount() );
    SW_EXPECT_NEAR_EQUAL( 3.0f, subtitles.getActiveLine( 0 )._durationSeconds, 1e-4f );
    // 화자가 없으면 이름 줄을 접는다.
    SW_ASSERT_NOT_NULL( fixture.findSubtitleScreen() );
    SW_EXPECT_FALSE( fixture.findSubtitleScreen()->getTree().findWidgetByName( "Speaker0" )->isVisible() );
    fixture.runFrame( 2.9f );
    SW_EXPECT_EQUAL( 1u, subtitles.getActiveLineCount() );
    fixture.runFrame( 0.2f );
    SW_EXPECT_EQUAL( 0u, subtitles.getActiveLineCount() );
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture.findSubtitleScreen() == nullptr );
}

/**
 * @brief [UIAccessibilityTest] 자막은 둘까지 보이고 셋째는 기다린다 — 첫 줄이 사라지면 셋째가 그때부터 자기 시간을 센다
 * @details 변이: `kMaxVisibleLineCount` 를 3 으로 두면 대기열이 비어 진다.
 */
SW_TEST_CASE( UIAccessibilityTest, SubtitleQueueShowsTwo )
{
    const ScopedVariable   enabled( "gv_subtitles", "true" );
    UIAccessibilityFixture fixture;
    SW_ASSERT_TRUE( fixture._bReady );
    sw::UISubtitleService& subtitles = fixture._ui.getSubtitles();
    (void)subtitles.post( "A", "first", 1.0f );
    (void)subtitles.post( "B", "second", 5.0f );
    (void)subtitles.post( "C", "third", 2.0f );
    fixture.runFrame( 0.0f );
    SW_EXPECT_EQUAL( 2u, subtitles.getActiveLineCount() );
    SW_EXPECT_EQUAL( 1u, subtitles.getQueuedLineCount() );
    sw::UIScreen* pScreen = fixture.findSubtitleScreen();
    SW_ASSERT_NOT_NULL( pScreen );
    SW_EXPECT_STREQ( "first", pScreen->getTree().findWidget<sw::TextWidget>( "Text0" )->getText().c_str() );
    SW_EXPECT_STREQ( "second", pScreen->getTree().findWidget<sw::TextWidget>( "Text1" )->getText().c_str() );
    SW_EXPECT_TRUE( pScreen->getTree().findWidgetByName( "Line1" )->isVisible() );

    fixture.runFrame( 1.5f ); // 첫 줄이 사라지고 셋째가 들어온다(시간은 지금부터)
    SW_EXPECT_EQUAL( 2u, subtitles.getActiveLineCount() );
    SW_EXPECT_EQUAL( 0u, subtitles.getQueuedLineCount() );
    SW_EXPECT_STREQ( "second", pScreen->getTree().findWidget<sw::TextWidget>( "Text0" )->getText().c_str() );
    SW_EXPECT_STREQ( "third", pScreen->getTree().findWidget<sw::TextWidget>( "Text1" )->getText().c_str() );
    SW_EXPECT_NEAR_EQUAL( 2.0f, subtitles.getActiveLine( 1 )._remainingSeconds, 1e-4f );
}

/**
 * @brief [UIAccessibilityTest] 대화 러너는 `_bPostSubtitles` 일 때만 줄을 자막으로 보낸다(화자 · 글 그대로)
 */
SW_TEST_CASE( UIAccessibilityTest, DialogueRunnerPostsSubtitlesWhenEnabled )
{
    UIAccessibilityFixture fixture;
    SW_ASSERT_TRUE( fixture._bReady );
    const sw::string            graph = R"({
		"nodes": [
			{ "id": 1, "type": "Start" },
			{ "id": 2, "type": "Dialogue", "speaker": "NPC", "text": "Hello traveler!" },
			{ "id": 3, "type": "Dialogue", "speaker": "NPC", "text": "Safe travels." },
			{ "id": 4, "type": "End" }
		],
		"links": [
			{ "from": 102, "to": 201 },
			{ "from": 202, "to": 301 },
			{ "from": 302, "to": 401 }
		]
	})";
    sw::DialogueRunnerComponent runner;
    runner.setUISystem( &fixture._ui );
    SW_ASSERT_TRUE( runner.loadGraphJSON( graph ) );
    SW_EXPECT_TRUE( runner.startDialogue() );
    SW_EXPECT_EQUAL( 0u, fixture._ui.getSubtitles().getQueuedLineCount() ); // 기본은 끔

    runner._bPostSubtitles = true;
    SW_EXPECT_TRUE( runner.advance() );
    SW_ASSERT_EQUAL( 1u, fixture._ui.getSubtitles().getQueuedLineCount() );
    fixture.runFrame( 0.0f );
    SW_ASSERT_EQUAL( 1u, fixture._ui.getSubtitles().getActiveLineCount() );
    SW_EXPECT_STREQ( "NPC", fixture._ui.getSubtitles().getActiveLine( 0 )._speaker.c_str() );
    SW_EXPECT_STREQ( "Safe travels.", fixture._ui.getSubtitles().getActiveLine( 0 )._text.c_str() );
}
