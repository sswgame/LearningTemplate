#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Core/UiFocusManager.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/Style/UiStylePass.h"
#include "Engine/UI/Style/UiStyleSet.h"
#include "Engine/UI/Style/UiStyleSheet.h"
#include "Engine/UI/Style/UiTheme.h"
#include "Engine/UI/Style/WidgetStyle.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "TestFramework/TestFramework.h"

// UiStyleTest — 스타일 시트(*.uistyle.xml): 선택자 · 특정도 · 상태 · 자손 결합자 · 상속 · 변수 · 무효화 종류 · 나눠 쓰기 · 테마.
// 시트와 문서는 메모리로 넣는다(UiStyleSheetCache::registerMemorySheet · UiDocumentCache::registerMemoryDocument). 디바이스 없음(nogpu).

namespace
{
    struct UiStyleTestUtil
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        static void runFrame( sw::InputManager& input, sw::UiSystem& ui )
        {
            input.beginFrame( kFrameSeconds );
            ui.processInput( kFrameSeconds );
            ui.update( kFrameSeconds, sw::UiViewport{
                                          sw::float2{ 1920.0f, 1080.0f }
            } );
            input.endFrame();
        }

        /** @brief 계산된 스타일이 그 칸을 정했으면 그 값, 아니면 (-1, -1, -1, -1) 입니다. */
        static sw::float4 findTextColor( const sw::Widget* pWidget )
        {
            const sw::UiComputedStyle* pStyle = pWidget != nullptr ? pWidget->getComputedStyle() : nullptr;
            if ( pStyle == nullptr || pStyle->has( sw::UiStyleField::TextColor ) == false )
                return sw::float4{ -1.0f, -1.0f, -1.0f, -1.0f };
            return pStyle->_value._textColor;
        }

        static sw::float4 findBackground( const sw::Widget* pWidget )
        {
            const sw::UiComputedStyle* pStyle = pWidget != nullptr ? pWidget->getComputedStyle() : nullptr;
            if ( pStyle == nullptr || pStyle->has( sw::UiStyleField::BackgroundColor ) == false )
                return sw::float4{ -1.0f, -1.0f, -1.0f, -1.0f };
            return pStyle->_value._backgroundColor;
        }

        static const sw::float4 kRed;
        static const sw::float4 kGreen;
        static const sw::float4 kBlue;
        static const sw::float4 kYellow;
    };
    const sw::float4 UiStyleTestUtil::kRed{ 1.0f, 0.0f, 0.0f, 1.0f };
    const sw::float4 UiStyleTestUtil::kGreen{ 0.0f, 1.0f, 0.0f, 1.0f };
    const sw::float4 UiStyleTestUtil::kBlue{ 0.0f, 0.0f, 1.0f, 1.0f };
    const sw::float4 UiStyleTestUtil::kYellow{ 1.0f, 1.0f, 0.0f, 1.0f };

    /** @brief 입력 관리자와 UI 시스템 — 시트 하나를 건 문서를 연다. */
    struct UiStyleFixture
    {
        sw::InputManager _input;
        sw::UiSystem     _ui;

        UiStyleFixture()
            : _input{}
            , _ui{}
        {
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr ) );
        }

        ~UiStyleFixture()
        {
            _ui.shutdown();
            _input.shutdown();
        }

        UiStyleFixture( const UiStyleFixture& )            = delete;
        UiStyleFixture& operator=( const UiStyleFixture& ) = delete;

        /** @brief 시트 @p pSheetText 를 건 문서(루트 아래 @p pBody)를 열고 한 프레임 돌립니다. */
        sw::UiScreen* open( const utf8* pSheetText, const utf8* pBody )
        {
            _ui.getStyleSheetCache().registerMemorySheet( "test/style.uistyle.xml", pSheetText );
            const sw::string document = sw::string( "<UiDocument _schemaVersion=\"1\">\n"
                                                    "\t<_listStyleSheet><item>test/style.uistyle.xml</item></_listStyleSheet>\n" ) +
                                        pBody + "</UiDocument>\n";
            _ui.getDocumentCache().registerMemoryDocument( "test/style.ui.xml", document );
            sw::UiScreen* pScreen = _ui.findScreen( _ui.openScreen( "test/style.ui.xml" ) );
            UiStyleTestUtil::runFrame( _input, _ui );
            return pScreen;
        }
    };
} // namespace

/** @brief [UiStyleTest] 칸 표(UiStyleField)가 WidgetStyle 의 PROPERTY 와 순서 · 이름이 같다 — 칸을 더하면 둘을 같이 고친다 */
SW_TEST_CASE( UiStyleTest, FieldTableMatchesReflection )
{
    const sw::vector<sw::PropertyInfo>& listProperty = sw::WidgetStyle::StaticType()->getPropertiesWithBase();
    SW_ASSERT_EQUAL( static_cast<uint32>( sw::UiStyleField::Count ), static_cast<uint32>( listProperty.size() ) );
    for ( uint32 index = 0; index < static_cast<uint32>( listProperty.size() ); ++index )
        SW_EXPECT_STREQ( sw::UiStyleFieldTable::getEntry( static_cast<sw::UiStyleField>( index ) )._pName, listProperty[index]._name.c_str() );
}

/** @brief [UiStyleTest] 겹치면 #이름 > .클래스 > 타입, 특정도가 같으면 뒤 규칙이 이긴다 */
SW_TEST_CASE( UiStyleTest, TypeClassNameSpecificity )
{
    using Util = UiStyleTestUtil;
    UiStyleFixture fixture;
    sw::UiScreen*  pScreen = fixture.open( "<UiStyleSheet _schemaVersion=\"1\">\n"
                                            "\t<Rule _selector=\"#hero\" _textColor=\"0,0,1,1\" />\n"
                                            "\t<Rule _selector=\".big\" _textColor=\"0,1,0,1\" />\n"
                                            "\t<Rule _selector=\"TextWidget\" _textColor=\"1,0,0,1\" />\n"
                                            "\t<Rule _selector=\".late\" _textColor=\"0,1,0,1\" />\n"
                                            "\t<Rule _selector=\".late\" _textColor=\"1,1,0,1\" />\n"
                                            "</UiStyleSheet>\n",
                                           "\t<BoxPanel>\n"
                                            "\t\t<TextWidget _name=\"hero\" _styleClass=\"big\" />\n"
                                            "\t\t<TextWidget _name=\"Big\" _styleClass=\"big\" />\n"
                                            "\t\t<TextWidget _name=\"Plain\" />\n"
                                            "\t\t<TextWidget _name=\"Late\" _styleClass=\"late\" />\n"
                                            "\t</BoxPanel>\n" );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::WidgetTree& tree = pScreen->getTree();
    SW_EXPECT_TRUE( Util::findTextColor( tree.findWidgetByName( "hero" ) ) == Util::kBlue );
    SW_EXPECT_TRUE( Util::findTextColor( tree.findWidgetByName( "Big" ) ) == Util::kGreen );
    SW_EXPECT_TRUE( Util::findTextColor( tree.findWidgetByName( "Plain" ) ) == Util::kRed );
    SW_EXPECT_TRUE( Util::findTextColor( tree.findWidgetByName( "Late" ) ) == Util::kYellow );
}

/** @brief [UiStyleTest] :hover 가 걸리면 배경이 바뀌고(그리기만 — 레이아웃 아님) 떠나면 되돌아간다 */
SW_TEST_CASE( UiStyleTest, StatePseudoClassesApplyAndRevert )
{
    using Util = UiStyleTestUtil;
    UiStyleFixture fixture;
    sw::UiScreen*  pScreen = fixture.open( "<UiStyleSheet _schemaVersion=\"1\">\n"
                                            "\t<Rule _selector=\"ButtonWidget\" _backgroundColor=\"1,0,0,1\" />\n"
                                            "\t<Rule _selector=\"ButtonWidget:hover\" _backgroundColor=\"0,1,0,1\" />\n"
                                            "</UiStyleSheet>\n",
                                           "\t<CanvasPanel>\n"
                                            "\t\t<ButtonWidget _name=\"Go\">\n"
                                            "\t\t\t<_slot _offsetMin=\"100,100\" _offsetMax=\"300,160\" />\n"
                                            "\t\t</ButtonWidget>\n"
                                            "\t</CanvasPanel>\n" );
    SW_ASSERT_NOT_NULL( pScreen );
    sw::ButtonWidget* pGo = pScreen->getTree().findWidget<sw::ButtonWidget>( "Go" );
    SW_ASSERT_NOT_NULL( pGo );
    SW_EXPECT_TRUE( Util::findBackground( pGo ) == Util::kRed );

    // 호버 — 입력만 돌리고 스타일 걷기를 손으로 돌려 무효화 종류를 본다.
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseMove( 200, 130 ) ) );
    fixture._input.beginFrame( Util::kFrameSeconds );
    fixture._ui.processInput( Util::kFrameSeconds );
    fixture._input.endFrame();
    SW_EXPECT_TRUE( pGo->isHovered() );
    SW_EXPECT_TRUE( ( pGo->getDirtyFlags() & sw::WidgetDirty::kStyle ) != 0 );
    (void)sw::UiStylePass::update( pScreen->getTree(), *pScreen->getStyleSet(), false );
    SW_EXPECT_TRUE( Util::findBackground( pGo ) == Util::kGreen );
    SW_EXPECT_TRUE( ( pGo->getDirtyFlags() & sw::WidgetDirty::kPaint ) != 0 );
    SW_EXPECT_TRUE( ( pGo->getDirtyFlags() & sw::WidgetDirty::kLayout ) == 0 );
    Util::runFrame( fixture._input, fixture._ui );

    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseMove( 800, 800 ) ) );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_FALSE( pGo->isHovered() );
    SW_EXPECT_TRUE( Util::findBackground( pGo ) == Util::kRed );
}

/** @brief [UiStyleTest] :focus-visible 은 탐색 입력 방식에서만 — 포인터 방식이면 포커스가 있어도 맞지 않는다 */
SW_TEST_CASE( UiStyleTest, FocusVisibleOnlyInNavigationMode )
{
    using Util = UiStyleTestUtil;
    UiStyleFixture fixture;
    sw::UiScreen*  pScreen = fixture.open( "<UiStyleSheet _schemaVersion=\"1\">\n"
                                            "\t<Rule _selector=\"ButtonWidget:focus\" _borderWidth=\"1\" />\n"
                                            "\t<Rule _selector=\"ButtonWidget:focus-visible\" _borderWidth=\"3\" />\n"
                                            "</UiStyleSheet>\n",
                                           "\t<BoxPanel>\n"
                                            "\t\t<ButtonWidget _name=\"Go\" />\n"
                                            "\t</BoxPanel>\n" );
    SW_ASSERT_NOT_NULL( pScreen );
    sw::ButtonWidget* pGo = pScreen->getTree().findWidget<sw::ButtonWidget>( "Go" );
    SW_ASSERT_NOT_NULL( pGo );
    fixture._ui.setInputMode( sw::UiInputMode::Pointer );
    SW_ASSERT_TRUE( fixture._ui.getFocusManager().setFocus( pScreen->getTree(), pGo->getId() ) );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_EQUAL( 1.0f, pGo->getComputedStyle()->_value._borderWidth );

    fixture._ui.setInputMode( sw::UiInputMode::Navigation );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_EQUAL( 3.0f, pGo->getComputedStyle()->_value._borderWidth );
}

/** @brief [UiStyleTest] 자손 결합자는 아무 조상이나 본다(부모가 아니어도) — 그 조상 밖의 같은 위젯은 맞지 않는다 */
SW_TEST_CASE( UiStyleTest, DescendantCombinatorMatchesAncestors )
{
    using Util = UiStyleTestUtil;
    UiStyleFixture fixture;
    sw::UiScreen*  pScreen = fixture.open( "<UiStyleSheet _schemaVersion=\"1\">\n"
                                            "\t<Rule _selector=\"BorderPanel.window TextWidget\" _textColor=\"0,0,1,1\" />\n"
                                            "</UiStyleSheet>\n",
                                           "\t<BoxPanel>\n"
                                            "\t\t<BorderPanel _styleClass=\"window\">\n"
                                            "\t\t\t<BoxPanel>\n"
                                            "\t\t\t\t<TextWidget _name=\"Inside\" />\n"
                                            "\t\t\t</BoxPanel>\n"
                                            "\t\t</BorderPanel>\n"
                                            "\t\t<TextWidget _name=\"Outside\" />\n"
                                            "\t</BoxPanel>\n" );
    SW_ASSERT_NOT_NULL( pScreen );
    SW_EXPECT_TRUE( Util::findTextColor( pScreen->getTree().findWidgetByName( "Inside" ) ) == Util::kBlue );
    SW_EXPECT_FALSE( pScreen->getTree().findWidgetByName( "Outside" )->getComputedStyle()->has( sw::UiStyleField::TextColor ) );
}

/** @brief [UiStyleTest] 글 칸은 부모의 계산된 값을 물려받고, 글 위젯 자기 규칙이 이긴다 — 글이 아닌 칸은 물려받지 않는다 */
SW_TEST_CASE( UiStyleTest, TextPropertiesInherit )
{
    using Util = UiStyleTestUtil;
    UiStyleFixture fixture;
    sw::UiScreen*  pScreen = fixture.open( "<UiStyleSheet _schemaVersion=\"1\">\n"
                                            "\t<Rule _selector=\".window\" _textColor=\"1,0,0,1\" _backgroundColor=\"0,0,1,1\" />\n"
                                            "\t<Rule _selector=\"TextWidget.own\" _textColor=\"0,1,0,1\" />\n"
                                            "</UiStyleSheet>\n",
                                           "\t<BorderPanel _styleClass=\"window\">\n"
                                            "\t\t<BoxPanel>\n"
                                            "\t\t\t<TextWidget _name=\"Inherited\" />\n"
                                            "\t\t\t<TextWidget _name=\"Own\" _styleClass=\"own\" />\n"
                                            "\t\t</BoxPanel>\n"
                                            "\t</BorderPanel>\n" );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::Widget* pInherited = pScreen->getTree().findWidgetByName( "Inherited" );
    SW_EXPECT_TRUE( Util::findTextColor( pInherited ) == Util::kRed );
    SW_EXPECT_TRUE( Util::findTextColor( pScreen->getTree().findWidgetByName( "Own" ) ) == Util::kGreen );
    SW_EXPECT_FALSE( pInherited->getComputedStyle()->has( sw::UiStyleField::BackgroundColor ) );
}

/** @brief [UiStyleTest] $변수는 시트 안 어디에 적든 풀리고, 모르는 변수는 로드 오류(파일 · 줄)다 */
SW_TEST_CASE( UiStyleTest, VariablesResolveAndUnknownVariableIsError )
{
    sw::UiStyleSheetAsset sheet;
    sw::string            error;
    SW_ASSERT_TRUE( sw::UiStyleSheetLoader::parse( "<UiStyleSheet _schemaVersion=\"1\">\n"
                                                   "\t<Rule _selector=\"TextWidget\" _textColor=\"$accent\" />\n"
                                                   "\t<Variable _name=\"accent\" _value=\"0.25,0.5,1,1\" />\n"
                                                   "</UiStyleSheet>\n",
                                                   "test/variables.uistyle.xml", sheet, error ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( sheet._listRule.size() ) );
    sw::WidgetStyle style{};
    sw::UiStyleSheetLoader::applyAssignment( sheet._listRule[0]._listAssignment[0], style );
    SW_EXPECT_TRUE( style._textColor == ( sw::float4{ 0.25f, 0.5f, 1.0f, 1.0f } ) );

    SW_EXPECT_FALSE( sw::UiStyleSheetLoader::parse( "<UiStyleSheet _schemaVersion=\"1\">\n"
                                                    "\n"
                                                    "\t<Rule _selector=\"TextWidget\" _textColor=\"$missing\" />\n"
                                                    "</UiStyleSheet>\n",
                                                    "test/missing.uistyle.xml", sheet, error ) );
    SW_EXPECT_TRUE_MSG( error.find( "test/missing.uistyle.xml:3:" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_TRUE_MSG( error.find( "$missing" ) != sw::string::npos, error.c_str() );
}

/** @brief [UiStyleTest] 모르는 칸 · 읽지 못한 값 · 쓰지 않는 선택자 문법(`>` · 모르는 상태)은 로드 오류다 */
SW_TEST_CASE( UiStyleTest, UnknownPropertyOrSelectorSyntaxIsLoadError )
{
    struct BadCase
    {
        const utf8* _pRule;
        const utf8* _pExpected;
    };
    const BadCase kArrCase[] = {
        {          "<Rule _selector=\"TextWidget\" _textColour=\"1,0,0,1\" />",    "unknown style property '_textColour'"},
        {               "<Rule _selector=\"TextWidget\" _textColor=\"red\" />",                          "cannot be read"},
        {"<Rule _selector=\"BoxPanel > TextWidget\" _textColor=\"1,0,0,1\" />",                      "unsupported syntax"},
        {       "<Rule _selector=\"ButtonWidget:hovered\" _opacity=\"0.5\" />",                "unknown state ':hovered'"},
        {   "<Rule _selector=\"TextWidget\"><_font _wieght=\"Bold\" /></Rule>", "<_font> has unknown attribute '_wieght'"},
        {                                    "<Rule _textColor=\"1,0,0,1\" />",                    "Rule needs _selector"},
    };
    for ( const BadCase& badCase : kArrCase )
    {
        sw::UiStyleSheetAsset sheet;
        sw::string            error;
        const sw::string      text = sw::string( "<UiStyleSheet _schemaVersion=\"1\">\n\t" ) + badCase._pRule + "\n</UiStyleSheet>\n";
        SW_EXPECT_FALSE( sw::UiStyleSheetLoader::parse( text, "test/bad.uistyle.xml", sheet, error ) );
        SW_EXPECT_TRUE_MSG( error.find( badCase._pExpected ) != sw::string::npos, error.c_str() );
        SW_EXPECT_TRUE_MSG( error.find( "test/bad.uistyle.xml:2:" ) != sw::string::npos, error.c_str() );
    }
}

/** @brief [UiStyleTest] 여백(레이아웃 칸)이 바뀌면 kLayout, 색만 바뀌면 kPaint 만 */
SW_TEST_CASE( UiStyleTest, PaddingChangeIsLayoutDirtyColorChangeIsPaintDirty )
{
    UiStyleFixture fixture;
    sw::UiScreen*  pScreen = fixture.open( "<UiStyleSheet _schemaVersion=\"1\">\n"
                                            "\t<Rule _selector=\".padded\" _padding=\"8,8,8,8\" />\n"
                                            "\t<Rule _selector=\".tinted\" _backgroundColor=\"1,0,0,1\" />\n"
                                            "</UiStyleSheet>\n",
                                           "\t<BoxPanel>\n"
                                            "\t\t<BorderPanel _name=\"Box\" />\n"
                                            "\t</BoxPanel>\n" );
    SW_ASSERT_NOT_NULL( pScreen );
    sw::Widget* pBox = pScreen->getTree().findWidgetByName( "Box" );
    SW_ASSERT_NOT_NULL( pBox );

    pBox->setStyleClass( "padded" );
    (void)sw::UiStylePass::update( pScreen->getTree(), *pScreen->getStyleSet(), false );
    SW_EXPECT_TRUE( ( pBox->getDirtyFlags() & sw::WidgetDirty::kLayout ) != 0 );
    UiStyleTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_TRUE( static_cast<sw::BorderPanel*>( pBox )->computeEffectivePadding() == ( sw::float4{ 8.0f, 8.0f, 8.0f, 8.0f } ) );

    pBox->setStyleClass( "padded tinted" );
    (void)sw::UiStylePass::update( pScreen->getTree(), *pScreen->getStyleSet(), false );
    SW_EXPECT_TRUE( ( pBox->getDirtyFlags() & sw::WidgetDirty::kPaint ) != 0 );
    SW_EXPECT_TRUE( ( pBox->getDirtyFlags() & sw::WidgetDirty::kLayout ) == 0 );
}

/** @brief [UiStyleTest] 같은 조건의 버튼 100 개는 계산된 스타일 객체 하나를 나눠 쓴다 */
SW_TEST_CASE( UiStyleTest, ComputedStylesAreShared )
{
    UiStyleFixture fixture;
    sw::string     body = "\t<BoxPanel>\n";
    for ( uint32 index = 0; index < 100; ++index )
        body += "\t\t<ButtonWidget _styleClass=\"primary\" />\n";
    body += "\t</BoxPanel>\n";
    sw::UiScreen* pScreen = fixture.open( "<UiStyleSheet _schemaVersion=\"1\">\n"
                                          "\t<Rule _selector=\"ButtonWidget.primary\" _backgroundColor=\"0,0,1,1\" />\n"
                                          "</UiStyleSheet>\n",
                                          body.c_str() );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::PanelWidget* pRoot = static_cast<const sw::PanelWidget*>( pScreen->getTree().getRoot() );
    SW_ASSERT_EQUAL( 100u, pRoot->getChildCount() );
    const sw::UiComputedStyle* pFirst = pRoot->getChild( 0 )->getComputedStyle();
    SW_ASSERT_NOT_NULL( pFirst );
    for ( uint32 index = 1; index < pRoot->getChildCount(); ++index )
        SW_EXPECT_TRUE( pRoot->getChild( index )->getComputedStyle() == pFirst );
    SW_EXPECT_EQUAL( 2u, pScreen->getStyleSet()->getComputedStyleCount() ); // 루트 하나 + 버튼 하나
}

/** @brief [UiStyleTest] 테마를 바꾸면 열린 화면의 모든 위젯이 새 테마 시트로 다시 맞춰진다 */
SW_TEST_CASE( UiStyleTest, ThemeSwitchRestylesEverything )
{
    using Util = UiStyleTestUtil;
    UiStyleFixture fixture;
    fixture._ui.getStyleSheetCache().registerMemorySheet( "test/light.uistyle.xml", "<UiStyleSheet _schemaVersion=\"1\">\n"
                                                                                    "\t<Rule _selector=\"TextWidget\" _textColor=\"1,0,0,1\" />\n"
                                                                                    "</UiStyleSheet>\n" );
    fixture._ui.getStyleSheetCache().registerMemorySheet( "test/dark.uistyle.xml", "<UiStyleSheet _schemaVersion=\"1\">\n"
                                                                                   "\t<Rule _selector=\"TextWidget\" _textColor=\"0,0,1,1\" />\n"
                                                                                   "</UiStyleSheet>\n" );
    sw::UiThemeCatalog catalog{};
    catalog._defaultTheme = "light";
    sw::UiThemeDesc light{};
    light._name = "light";
    light._listStyleSheet.push_back( "test/light.uistyle.xml" );
    sw::UiThemeDesc dark{};
    dark._name = "dark";
    dark._listStyleSheet.push_back( "test/dark.uistyle.xml" );
    catalog._listTheme.push_back( light );
    catalog._listTheme.push_back( dark );
    fixture._ui.setThemeCatalog( catalog );
    SW_EXPECT_STREQ( "light", fixture._ui.getThemeName().c_str() );

    sw::UiScreen* pScreen = fixture.open( "<UiStyleSheet _schemaVersion=\"1\">\n</UiStyleSheet>\n", "\t<BoxPanel>\n"
                                                                                                    "\t\t<TextWidget _name=\"A\" />\n"
                                                                                                    "\t\t<BoxPanel>\n"
                                                                                                    "\t\t\t<TextWidget _name=\"B\" />\n"
                                                                                                    "\t\t</BoxPanel>\n"
                                                                                                    "\t</BoxPanel>\n" );
    SW_ASSERT_NOT_NULL( pScreen );
    SW_EXPECT_TRUE( Util::findTextColor( pScreen->getTree().findWidgetByName( "A" ) ) == Util::kRed );
    SW_EXPECT_TRUE( Util::findTextColor( pScreen->getTree().findWidgetByName( "B" ) ) == Util::kRed );

    SW_EXPECT_TRUE( fixture._ui.setTheme( "dark" ) );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_TRUE( Util::findTextColor( pScreen->getTree().findWidgetByName( "A" ) ) == Util::kBlue );
    SW_EXPECT_TRUE( Util::findTextColor( pScreen->getTree().findWidgetByName( "B" ) ) == Util::kBlue );
    SW_EXPECT_FALSE( fixture._ui.setTheme( "missing" ) );
}
