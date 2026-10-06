#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
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

// UiStyleTest — 스타일 시트(*.uistyle.xml): 선택자 · 특정도 · 상태 · 자손 결합자 · 상속 · 변수 · 무효화 종류 · 나눠 쓰기 · 테마(고대비 · 사용자 설정).
// 시트와 문서는 메모리로 넣는다(UiStyleSheetCache::registerMemorySheet · UiDocumentCache::registerMemoryDocument). 디바이스 없음(nogpu).

namespace
{
    struct UiStyleTestUtil
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        static void runFrame( sw::InputManager& input, sw::UiSystem& ui, float32 deltaSeconds = kFrameSeconds )
        {
            input.beginFrame( deltaSeconds );
            ui.processInput( deltaSeconds );
            ui.update( deltaSeconds, sw::UiViewport{
                                         sw::float2{ 1920.0f, 1080.0f }
            } );
            input.endFrame();
        }

        /** @brief 두 색이 성분마다 허용 오차 안이면 true 입니다. */
        static bool isNear( const sw::float4& lhs, const sw::float4& rhs )
        {
            const float32 kTolerance = 0.001f;
            return sw::MathUtil::abs( lhs._x - rhs._x ) < kTolerance && sw::MathUtil::abs( lhs._y - rhs._y ) < kTolerance &&
                   sw::MathUtil::abs( lhs._z - rhs._z ) < kTolerance && sw::MathUtil::abs( lhs._w - rhs._w ) < kTolerance;
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

    /** @brief 전역 bool 변수 하나를 이름으로 바꾸고 스코프 끝에 되돌립니다(시험 DLL 은 Engine 의 gv_* 를 extern 으로 못 읽는다). */
    class ScopedBoolVariable
    {
    public:
        ScopedBoolVariable( const utf8* pName, bool bValue )
            : _pInfo{ sw::engine::getGlobalVariableManager().findVariable( pName ) }
            , _bPrevious{ false }
        {
            SW_EXPECT_TRUE( _pInfo != nullptr );
            if ( _pInfo == nullptr )
                return;
            _bPrevious = _pInfo->getValueAsBool();
            (void)_pInfo->setValueAsBool( bValue );
        }
        ~ScopedBoolVariable()
        {
            if ( _pInfo != nullptr )
                (void)_pInfo->setValueAsBool( _bPrevious );
        }
        ScopedBoolVariable( const ScopedBoolVariable& )            = delete;
        ScopedBoolVariable& operator=( const ScopedBoolVariable& ) = delete;

    private:
        sw::GlobalVariableInfo* _pInfo;
        bool                    _bPrevious;
    };

    /** @brief 전환 시험의 시트 — 배경색만 1 초 Linear 로 옮기고, 글 색은 적지 않아 바로 바뀐다. */
    constexpr utf8 kTransitionSheet[] = "<UiStyleSheet _schemaVersion=\"1\">\n"
                                        "\t<Rule _selector=\"BorderPanel\" _transition=\"_backgroundColor 1 Linear\" />\n"
                                        "\t<Rule _selector=\".a\" _backgroundColor=\"1,0,0,1\" _textColor=\"0,1,0,1\" />\n"
                                        "\t<Rule _selector=\".b\" _backgroundColor=\"0,0,1,1\" _textColor=\"1,1,0,1\" />\n"
                                        "</UiStyleSheet>\n";
    constexpr utf8 kTransitionBody[]  = "\t<CanvasPanel>\n"
                                        "\t\t<BorderPanel _name=\"Box\" _styleClass=\"a\" />\n"
                                        "\t</CanvasPanel>\n";

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
        {              "<Rule _selector=\"TextWidget\" _textColour=\"1,0,0,1\" />",       "unknown style property '_textColour'"},
        {                   "<Rule _selector=\"TextWidget\" _textColor=\"red\" />",                             "cannot be read"},
        {    "<Rule _selector=\"BoxPanel > TextWidget\" _textColor=\"1,0,0,1\" />",                         "unsupported syntax"},
        {           "<Rule _selector=\"ButtonWidget:hovered\" _opacity=\"0.5\" />",                   "unknown state ':hovered'"},
        {       "<Rule _selector=\"TextWidget\"><_font _wieght=\"Bold\" /></Rule>",    "<_font> has unknown attribute '_wieght'"},
        {                                        "<Rule _textColor=\"1,0,0,1\" />",                       "Rule needs _selector"},
        {"<Rule _selector=\"TextWidget\" _transition=\"_backgroundColour 0.2\" />", "unknown style property '_backgroundColour'"},
        {            "<Rule _selector=\"TextWidget\" _transition=\"_font 0.2\" />",                          "cannot transition"},
        {        "<Rule _selector=\"TextWidget\" _transition=\"_opacity fast\" />",                        "unreadable duration"},
        {  "<Rule _selector=\"TextWidget\" _transition=\"_opacity 0.2 Bouncy\" />",                     "unknown curve 'Bouncy'"},
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

/** @brief [UiStyleTest] 전환을 적은 칸은 계산된 스타일이 바뀔 때 옛 값에서 새 값으로 옮겨 가고(반에서 반), 끝나면 나눠 쓰는 계산된 스타일로 돌아간다 */
SW_TEST_CASE( UiStyleTest, TransitionInterpolatesChangedProperty )
{
    using Util = UiStyleTestUtil;
    UiStyleFixture fixture;
    sw::UiScreen*  pScreen = fixture.open( kTransitionSheet, kTransitionBody );
    SW_ASSERT_NOT_NULL( pScreen );
    sw::Widget* pBox = pScreen->getTree().findWidgetByName( "Box" );
    SW_ASSERT_NOT_NULL( pBox );
    SW_EXPECT_TRUE( Util::findBackground( pBox ) == Util::kRed ); // 처음 맞춘 스타일은 전환 없이

    pBox->setStyleClass( "b" );
    Util::runFrame( fixture._input, fixture._ui, 0.0f ); // 스타일이 바뀐 프레임 — 아직 옛 값
    SW_EXPECT_TRUE( Util::findBackground( pBox ) == Util::kRed );
    Util::runFrame( fixture._input, fixture._ui, 0.5f );
    SW_EXPECT_TRUE( Util::isNear( sw::float4{ 0.5f, 0.0f, 0.5f, 1.0f }, Util::findBackground( pBox ) ) );
    Util::runFrame( fixture._input, fixture._ui, 0.6f );
    SW_EXPECT_TRUE( Util::findBackground( pBox ) == Util::kBlue );
}

/** @brief [UiStyleTest] 전환 중에 다시 바뀌면 지금 보이는 값에서 새 목표로(처음 값으로 튀지 않는다) */
SW_TEST_CASE( UiStyleTest, RetargetFromCurrentValue )
{
    using Util = UiStyleTestUtil;
    UiStyleFixture fixture;
    sw::UiScreen*  pScreen = fixture.open( kTransitionSheet, kTransitionBody );
    SW_ASSERT_NOT_NULL( pScreen );
    sw::Widget* pBox = pScreen->getTree().findWidgetByName( "Box" );
    SW_ASSERT_NOT_NULL( pBox );

    pBox->setStyleClass( "b" );
    Util::runFrame( fixture._input, fixture._ui, 0.0f );
    Util::runFrame( fixture._input, fixture._ui, 0.5f ); // (0.5, 0, 0.5)
    pBox->setStyleClass( "a" );
    Util::runFrame( fixture._input, fixture._ui, 0.0f );
    SW_EXPECT_TRUE( Util::isNear( sw::float4{ 0.5f, 0.0f, 0.5f, 1.0f }, Util::findBackground( pBox ) ) );
    Util::runFrame( fixture._input, fixture._ui, 0.5f ); // 보라 → 빨강의 반
    SW_EXPECT_TRUE( Util::isNear( sw::float4{ 0.75f, 0.0f, 0.25f, 1.0f }, Util::findBackground( pBox ) ) );
}

/** @brief [UiStyleTest] 전환에 적지 않은 칸(글 색)은 같은 변화에서도 바로 바뀐다 */
SW_TEST_CASE( UiStyleTest, UntransitionedPropertyJumps )
{
    using Util = UiStyleTestUtil;
    UiStyleFixture fixture;
    sw::UiScreen*  pScreen = fixture.open( kTransitionSheet, kTransitionBody );
    SW_ASSERT_NOT_NULL( pScreen );
    sw::Widget* pBox = pScreen->getTree().findWidgetByName( "Box" );
    SW_ASSERT_NOT_NULL( pBox );
    SW_EXPECT_TRUE( Util::findTextColor( pBox ) == Util::kGreen );

    pBox->setStyleClass( "b" );
    Util::runFrame( fixture._input, fixture._ui, 0.0f );
    SW_EXPECT_TRUE( Util::findTextColor( pBox ) == Util::kYellow );
    SW_EXPECT_TRUE( Util::findBackground( pBox ) == Util::kRed ); // 배경은 이제 막 옮겨 가기 시작
}

/** @brief [UiStyleTest] 움직임 줄이기(gv_uiReduceMotion)면 전환 없이 바로 새 값 */
SW_TEST_CASE( UiStyleTest, ReduceMotionDisablesTransitions )
{
    using Util = UiStyleTestUtil;
    const ScopedBoolVariable reduceMotion( "gv_uiReduceMotion", true );
    UiStyleFixture           fixture;
    sw::UiScreen*            pScreen = fixture.open( kTransitionSheet, kTransitionBody );
    SW_ASSERT_NOT_NULL( pScreen );
    sw::Widget* pBox = pScreen->getTree().findWidgetByName( "Box" );
    SW_ASSERT_NOT_NULL( pBox );

    pBox->setStyleClass( "b" );
    Util::runFrame( fixture._input, fixture._ui, 0.0f );
    SW_EXPECT_TRUE( Util::findBackground( pBox ) == Util::kBlue );
}

/**
 * @brief [UiStyleTest] 오프스크린 화면(에디터 미리보기)은 스타일 전환을 기다리지 않고 바뀐 값을 바로 보인다 — 진행 단계가 스택의 화면만 돌므로 시작한 전환이 옛 값에 멈춘다
 * @details 변이: `UiSystem::updateOffscreenScreens` 의 전환 끝내기 줄을 빼면 상자가 빨강에 남아 진다.
 */
SW_TEST_CASE( UiStyleTest, OffscreenScreenFinishesTransitionsAtOnce )
{
    using Util = UiStyleTestUtil;
    UiStyleFixture fixture;
    fixture._ui.getStyleSheetCache().registerMemorySheet( "test/style.uistyle.xml", kTransitionSheet );
    const sw::string document = sw::string( "<UiDocument _schemaVersion=\"1\">\n"
                                            "\t<_listStyleSheet><item>test/style.uistyle.xml</item></_listStyleSheet>\n" ) +
                                kTransitionBody + "</UiDocument>\n";
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/offscreen_style.ui.xml", document );
    const sw::UiScreenHandle handle  = fixture._ui.openOffscreenScreen( "test/offscreen_style.ui.xml", "rendertarget/test_preview" );
    sw::UiScreen*            pScreen = fixture._ui.findOffscreenScreen( handle );
    SW_ASSERT_NOT_NULL( pScreen );
    sw::UiViewport viewport{};
    viewport._size         = sw::float2{ 640.0f, 360.0f };
    viewport._physicalSize = viewport._size;
    fixture._ui.setOffscreenView( handle, viewport, 1.0f, sw::hashed_string{} );
    Util::runFrame( fixture._input, fixture._ui );
    sw::Widget* pBox = pScreen->getTree().findWidgetByName( "Box" );
    SW_ASSERT_NOT_NULL( pBox );
    SW_EXPECT_TRUE( Util::findBackground( pBox ) == Util::kRed );

    pBox->setStyleClass( "b" );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_TRUE( Util::findBackground( pBox ) == Util::kBlue );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_TRUE( Util::findBackground( pBox ) == Util::kBlue );
    fixture._ui.closeOffscreenScreen( handle );
}

/**
 * @brief [UiStyleTest] 엔진 고대비 테마(engine/ui/uithemes.xml 의 highcontrast)가 읽히고, 견본 pause.ui.xml 에서 창은 불투명 · 테두리 2, 글은 바탕과 7:1 이상 대비다
 * @details 고대비 테마 = 기본 시트 + 고대비 시트(색 · 테두리만 덮는다) — 기본 시트의 여백은 남는다. 대비는 WCAG 상대 휘도로 잰다.
 */
SW_TEST_CASE( UiStyleTest, HighContrastThemeLoads )
{
    using Util = UiStyleTestUtil;
    struct ContrastUtil
    {
        static float32 toLinear( float32 channel ) { return channel <= 0.03928f ? channel / 12.92f : sw::MathUtil::pow( ( channel + 0.055f ) / 1.055f, 2.4f ); }
        static float32 computeLuminance( const sw::float4& color )
        {
            return 0.2126f * toLinear( color._x ) + 0.7152f * toLinear( color._y ) + 0.0722f * toLinear( color._z );
        }
        static float32 computeContrast( const sw::float4& lhs, const sw::float4& rhs )
        {
            const float32 lhsLuminance = computeLuminance( lhs );
            const float32 rhsLuminance = computeLuminance( rhs );
            return ( sw::MathUtil::max( lhsLuminance, rhsLuminance ) + 0.05f ) / ( sw::MathUtil::min( lhsLuminance, rhsLuminance ) + 0.05f );
        }
    };
    UiStyleFixture     fixture;
    sw::UiThemeCatalog catalog{};
    SW_ASSERT_TRUE( catalog.loadFromResource( "engine/ui/uithemes.xml" ) );
    SW_ASSERT_NOT_NULL( catalog.findTheme( "highcontrast" ) );
    fixture._ui.setThemeCatalog( catalog );
    SW_EXPECT_TRUE( fixture._ui.setTheme( "highcontrast" ) );
    sw::UiScreen* pScreen = fixture._ui.findScreen( fixture._ui.openScreen( "engine/ui/pause.ui.xml" ) );
    SW_ASSERT_NOT_NULL( pScreen );
    Util::runFrame( fixture._input, fixture._ui );

    const sw::WidgetTree&      tree         = pScreen->getTree();
    const sw::Widget*          pWindow      = tree.findWidgetByName( "Window" );
    const sw::Widget*          pTitle       = tree.findWidgetByName( "Title" );
    const sw::UiComputedStyle* pWindowStyle = pWindow != nullptr ? pWindow->getComputedStyle() : nullptr;
    SW_ASSERT_NOT_NULL( pWindowStyle );
    const sw::float4 background = Util::findBackground( pWindow );
    SW_EXPECT_NEAR_EQUAL( 1.0f, background._w, 1e-4f ); // 불투명
    SW_EXPECT_TRUE( pWindowStyle->has( sw::UiStyleField::BorderWidth ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pWindowStyle->_value._borderWidth, 1e-4f );
    SW_EXPECT_TRUE( pWindowStyle->has( sw::UiStyleField::Padding ) ); // 기본 시트의 여백은 남는다
    const sw::float4 title = Util::findTextColor( pTitle );
    SW_EXPECT_TRUE( title._w > 0.0f );
    SW_EXPECT_TRUE( ContrastUtil::computeContrast( title, background ) >= 7.0f );
    // 버튼 안 글도 버튼 바탕과 7:1 이상.
    const sw::ButtonWidget* pResume = tree.findWidget<sw::ButtonWidget>( "Resume" );
    SW_ASSERT_NOT_NULL( pResume );
    SW_ASSERT_TRUE( pResume->getChildCount() > 0 );
    SW_EXPECT_TRUE( ContrastUtil::computeContrast( Util::findTextColor( pResume->getChild( 0 ) ), Util::findBackground( pResume ) ) >= 7.0f );
}

/**
 * @brief [UiStyleTest] 테마가 사용자 설정(gv_uiTheme — accessibility.uiTheme)을 따른다 — 테마 목록을 걸 때 고르고, 실행 중에 바꾸면 다음 update 가 바꾸며,
 *        목록에 없는 이름이면 목록의 기본(걸 때) · 지금 테마 그대로(실행 중)
 * @details 변이: `UiSystem::update` 의 `syncThemeSetting` 을 빼면 실행 중 바꾼 값이 테마에 닿지 않아 진다.
 */
SW_TEST_CASE( UiStyleTest, ThemeFollowsUserSetting )
{
    sw::GlobalVariableInfo* pSetting = sw::engine::getGlobalVariableManager().findVariable( "gv_uiTheme" );
    SW_ASSERT_NOT_NULL( pSetting );
    const sw::string   previous = pSetting->getValueAsString();
    sw::UiThemeCatalog catalog{};
    SW_ASSERT_TRUE( catalog.loadFromResource( "engine/ui/uithemes.xml" ) );
    {
        SW_EXPECT_TRUE( pSetting->setValueAsString( "highcontrast" ) );
        UiStyleFixture fixture;
        fixture._ui.setThemeCatalog( catalog );
        SW_EXPECT_STREQ( "highcontrast", fixture._ui.getThemeName().c_str() );

        SW_EXPECT_TRUE( pSetting->setValueAsString( "default" ) );
        UiStyleTestUtil::runFrame( fixture._input, fixture._ui );
        SW_EXPECT_STREQ( "default", fixture._ui.getThemeName().c_str() );

        SW_EXPECT_TRUE( pSetting->setValueAsString( "missing" ) ); // 모르는 이름 — 경고하고 지금 테마를 둔다
        UiStyleTestUtil::runFrame( fixture._input, fixture._ui );
        SW_EXPECT_STREQ( "default", fixture._ui.getThemeName().c_str() );
    }
    {
        // 그 이름을 두지 않은 게임 테마 목록 — 걸 때 목록의 기본으로 간다.
        SW_EXPECT_TRUE( pSetting->setValueAsString( "highcontrast" ) );
        UiStyleFixture     fixture;
        sw::UiThemeCatalog gameCatalog{};
        gameCatalog._defaultTheme = "shooter";
        sw::UiThemeDesc shooter{};
        shooter._name = "shooter";
        gameCatalog._listTheme.push_back( shooter );
        fixture._ui.setThemeCatalog( gameCatalog );
        SW_EXPECT_STREQ( "shooter", fixture._ui.getThemeName().c_str() );
    }
    SW_EXPECT_TRUE( pSetting->setValueAsString( previous ) );
}
