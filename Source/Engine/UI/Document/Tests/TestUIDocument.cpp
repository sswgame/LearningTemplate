// UIDocumentTest — UI 문서: 읽기 오류(경로 · 줄을 담는다), 위젯 만들기 · 이름 · 바인딩 분리, 조각(이름 감싸기 · 자기 참조), 쓰기와 다시 읽기(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"
#include "Engine/UI/Animation/UIAnimatedProperty.h"
#include "Engine/UI/Binding/UIBindingValue.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Document/UIDocumentLoader.h"
#include "Engine/UI/Document/UIDocumentWriter.h"
#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Style/UIStylePass.h"
#include "Engine/UI/Style/UIStyleSet.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/CheckBoxWidget.h"
#include "Engine/UI/Widgets/ComboBoxWidget.h"
#include "Engine/UI/Widgets/ImageWidget.h"
#include "Engine/UI/Widgets/ListViewWidget.h"
#include "Engine/UI/Widgets/RotatorWidget.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextInputWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

using namespace sw;

namespace
{
    constexpr const utf8* kDocumentXML = R"(<UIDocument _schemaVersion="0">
  <UIScreenDef _bPausesGame="true"/>
  <_listStyleSheet><item>engine/ui/a.uss.xml</item></_listStyleSheet>
  <_listAnimation>
    <UIAnimation _name="fade"/>
  </_listAnimation>
  <BoxPanel _name="Root" _spacing="8">
    <TextWidget _name="Title" _text="Hello"/>
    <TextWidget _name="Value" _text="{bind:_health}"/>
  </BoxPanel>
</UIDocument>)";

    /** @brief 실패한 결과의 글(`문장: 대상`)입니다. 성공이면 빈 글입니다. */
    template <typename T>
    string describeFailure( const expected<T>& result )
    {
        if ( result.has_value() )
            return string();
        StringBuilder<constant::buffer::kMax256> text;
        text.appendFormat( "%#: %#", result.error().message(), result.error().detail() );
        return string( text.view() );
    }

    /** @brief 문서 @p pXML 이 @p pExpected 를 담은 오류로 거절되는지 봅니다. */
    void expectRefused( const utf8* pXML, const utf8* pExpected )
    {
        UIDocumentAsset asset;
        const string    error = describeFailure( UIDocumentLoader::parse( pXML, "engine/ui/test.ui.xml", ResourceContext::forDocument(), asset ) );
        SW_EXPECT_FALSE( error.empty() );
        SW_EXPECT_TRUE( error.find( pExpected ) != string::npos );
    }

    string makeDocument( const utf8* pBody )
    {
        StringBuilder<constant::buffer::kMax1024> document;
        document.appendFormat( "<UIDocument _schemaVersion=\"0\">%#</UIDocument>", pBody );
        return string( document.view() );
    }

    /** @brief 세로 상자 안의 위젯들 @p pWidgets 를 문서로 읽어 트리에 붙인 것입니다. */
    struct UIDocumentWidgets
    {
        WidgetTree _tree;

        explicit UIDocumentWidgets( const utf8* pWidgets )
            : _tree{}
        {
            StringBuilder<constant::buffer::kMax1024> body;
            body.appendFormat( "<BoxPanel>%#</BoxPanel>", pWidgets );
            UIDocumentAsset asset;
            SW_EXPECT_TRUE( UIDocumentLoader::parse( makeDocument( body.c_str() ), "engine/ui/test.ui.xml", ResourceContext::forDocument(), asset ).has_value() );
            UIDocumentCache      cache;
            vector<UIBindingDef> listBinding;
            unique_ptr<Widget>   root = UIDocumentLoader::instantiate( asset, cache, ResourceTagSet{}, listBinding ).value_or( nullptr );
            SW_EXPECT_NOT_NULL( root.get() );
            if ( root != nullptr )
                _tree.setRoot( std::move( root ) );
        }

        template <typename T>
        T* find( const utf8* pName )
        {
            return _tree.findWidget<T>( hashed_string( pName ) );
        }
    };
} // namespace

/** @brief [UIDocumentTest] 읽기: 화면 서술 · 스타일 시트 · 애니메이션 · 노드 트리가 담기고 바인딩 식은 값이 아니라 따로 분리된다 */
SW_TEST_CASE( UIDocumentTest, ParsesEverything )
{
    UIDocumentAsset asset;
    string          error;
    SW_REQUIRE_TRUE( UIDocumentLoader::parse( kDocumentXML, "engine/ui/test.ui.xml", ResourceContext::forDocument(), asset ).has_value() );
    SW_EXPECT_TRUE( asset._screenDesc._bPausesGame );
    SW_REQUIRE_EQUAL( size_t( 1 ), asset._listStyleSheet.size() );
    SW_EXPECT_EQUAL( string( "engine/ui/a.uss.xml" ), asset._listStyleSheet[0] );
    SW_REQUIRE_EQUAL( size_t( 1 ), asset._listAnimation.size() );
    SW_EXPECT_TRUE( asset._listAnimation[0]._name == hashed_string( "fade" ) );
    SW_REQUIRE_EQUAL( size_t( 3 ), asset._listNode.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), asset._listNode[0]._listChildIndex.size() );
    SW_REQUIRE_EQUAL( size_t( 1 ), asset._listNode[2]._listBinding.size() );
    SW_EXPECT_EQUAL( string( "_text" ), asset._listNode[2]._listBinding[0]._propertyPath );
    SW_EXPECT_EQUAL( uint32( 9 ), asset._listNode[2]._sourceLine );
}

/** @brief [UIDocumentTest] 문서에 적은 항목 · 고른 인덱스가 콤보 · 넘김 상자의 보이는 글이 되고, 항목 밖의 인덱스는 세터처럼 없음이 된다 */
SW_TEST_CASE( UIDocumentTest, SelectedOptionShowsItsText )
{
    UIDocumentWidgets     document( "<ComboBoxWidget _name=\"Combo\" _selectedIndex=\"1\"><_listOption><item>Low</item><item>High</item></_listOption></ComboBoxWidget>"
                                        "<RotatorWidget _name=\"Rotator\" _selectedIndex=\"0\"><_listOption><item>Near</item><item>Far</item></_listOption></RotatorWidget>"
                                        "<ComboBoxWidget _name=\"OutOfRange\" _selectedIndex=\"5\"><_listOption><item>Low</item></_listOption></ComboBoxWidget>" );
    const ComboBoxWidget* pCombo      = document.find<ComboBoxWidget>( "Combo" );
    const RotatorWidget*  pRotator    = document.find<RotatorWidget>( "Rotator" );
    const ComboBoxWidget* pOutOfRange = document.find<ComboBoxWidget>( "OutOfRange" );
    SW_REQUIRE_TRUE( pCombo != nullptr && pRotator != nullptr && pOutOfRange != nullptr );
    const TextWidget* pComboLabel   = castTo<TextWidget>( pCombo->getChild( 0 ) );
    const TextWidget* pRotatorLabel = castTo<TextWidget>( pRotator->getChild( 0 ) );
    SW_REQUIRE_TRUE( pComboLabel != nullptr && pRotatorLabel != nullptr );
    SW_EXPECT_EQUAL( string( "High" ), pComboLabel->getText() );
    SW_EXPECT_EQUAL( string( "Near" ), pRotatorLabel->getText() );
    SW_EXPECT_EQUAL( constant::invalid_index::kUint32, pOutOfRange->getSelectedIndex() );
}

/** @brief [UIDocumentTest] 문서에 적은 슬라이더 값은 세터와 같은 범위로 제한되고, 칸이 선언한 범위 밖의 값(음수 걸음 · 1 넘는 진행 비율 · 불투명도 · 0 줄 높이)은 읽기가 거절한다 */
SW_TEST_CASE( UIDocumentTest, WrittenValuesAreLimitedLikeTheSetters )
{
    // 칸의 `Min` · `Max` 가 막는 값(`_step` · `_percent` · `_opacity` · `_rowHeight`)은 읽기에서 거절한다(Serialization.ValueOutOfRange) — 세터처럼 제한되는 것은 범위가 칸 사이에 걸린 값뿐이다.
    UIDocumentWidgets   document( "<SliderWidget _name=\"Slider\" _minValue=\"0\" _maxValue=\"10\" _value=\"25\" />"
                                    "<SliderWidget _name=\"Inverted\" _minValue=\"5\" _maxValue=\"1\" _value=\"3\" />" );
    const SliderWidget* pSlider   = document.find<SliderWidget>( "Slider" );
    const SliderWidget* pInverted = document.find<SliderWidget>( "Inverted" );
    SW_REQUIRE_TRUE( pSlider != nullptr && pInverted != nullptr );
    SW_EXPECT_EQUAL( 10.0f, pSlider->getValue() );
    SW_EXPECT_EQUAL( 5.0f, pInverted->getValue() );

    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    for ( const char* pWidget : { "<BoxPanel><SliderWidget _name=\"Slider\" _step=\"-1\" /></BoxPanel>", "<BoxPanel><ProgressBarWidget _name=\"Progress\" _percent=\"1.5\" /></BoxPanel>", "<BoxPanel><ProgressBarWidget _name=\"Progress\" _opacity=\"2\" /></BoxPanel>", "<BoxPanel><ListViewWidget _name=\"List\" _rowHeight=\"0\" /></BoxPanel>" } )
    {
        UIDocumentAsset outOfRange;
        SW_REQUIRE_TRUE( UIDocumentLoader::parse( makeDocument( pWidget ), "engine/ui/test.ui.xml", ResourceContext::forDocument(), outOfRange ).has_value() );
        UIDocumentCache      cache;
        vector<UIBindingDef> listBinding;
        SW_EXPECT_TRUE( UIDocumentLoader::instantiate( outOfRange, cache, ResourceTagSet{}, listBinding ).value_or( nullptr ) == nullptr );
    }
}

/** @brief [UIDocumentTest] 문서에 적은 글 입력 칸은 글이 있으면 글(커서는 글 끝), 없으면 힌트를 보인다 */
SW_TEST_CASE( UIDocumentTest, TextInputShowsItsTextOrHint )
{
    UIDocumentWidgets      document( "<TextInputWidget _name=\"Empty\" _hintText=\"Player name\" />"
                                          "<TextInputWidget _name=\"Filled\" _hintText=\"Player name\" _text=\"Ann\" />" );
    const TextInputWidget* pEmpty  = document.find<TextInputWidget>( "Empty" );
    TextInputWidget*       pFilled = document.find<TextInputWidget>( "Filled" );
    SW_REQUIRE_TRUE( pEmpty != nullptr && pFilled != nullptr );
    const TextWidget* pEmptyText  = castTo<TextWidget>( pEmpty->getChild( 0 ) );
    const TextWidget* pFilledText = castTo<TextWidget>( pFilled->getChild( 0 ) );
    SW_REQUIRE_TRUE( pEmptyText != nullptr && pFilledText != nullptr );
    SW_EXPECT_EQUAL( string( "Player name" ), pEmptyText->getText() );
    SW_EXPECT_EQUAL( string( "Ann" ), pFilledText->getText() );
    SW_EXPECT_FALSE( pFilledText->isLocalized() );
    SW_EXPECT_EQUAL( 3u, pFilled->getCaretByte() );

    // 글이 있는 칸에 다른 글이 쓰이면 커서는 자리를 지키고 글 안으로만 물린다.
    const PropertyInfo* pTextProperty = TextInputWidget::StaticType()->findPropertyInHierarchy( hashed_string( "_text" ) );
    SW_REQUIRE_NOT_NULL( pTextProperty );
    pFilled->setCaretByte( 2, false );
    pFilled->notifyPropertyWritten( *pTextProperty );
    SW_EXPECT_EQUAL( 2u, pFilled->getCaretByte() );
}

/** @brief [UIDocumentTest] 필드 하나를 쓴 뒤의 알림이 파생 상태와 무효화를 함께 한다 — 체크는 스타일(:checked)까지 다시 맞춘다 */
SW_TEST_CASE( UIDocumentTest, WritingOnePropertySynchronizesAndInvalidates )
{
    UIDocumentWidgets document( "<CheckBoxWidget _name=\"Check\" />" );
    CheckBoxWidget*   pCheck = document.find<CheckBoxWidget>( "Check" );
    SW_REQUIRE_NOT_NULL( pCheck );
    const PropertyInfo* pChecked = CheckBoxWidget::StaticType()->findPropertyInHierarchy( hashed_string( "_bChecked" ) );
    SW_REQUIRE_NOT_NULL( pChecked );
    UILayoutContext context{};
    context._viewportSize = float2{ 100.0f, 100.0f };
    UIStyleSet styleSet;
    (void)UIStylePass::update( document._tree, styleSet, false );
    (void)UILayoutPass::update( document._tree, context );
    SW_REQUIRE_TRUE( ( pCheck->getDirtyFlags() & WidgetDirty::kStyle ) == 0 );
    pCheck->notifyPropertyWritten( *pChecked );
    SW_EXPECT_TRUE( ( pCheck->getDirtyFlags() & WidgetDirty::kStyle ) != 0 );
}

/** @brief [UIDocumentTest] 편집기가 리플렉션으로 이름을 쓴 뒤의 알림은 이름 표를 옮긴다 — 옛 이름으로는 찾히지 않는다 */
SW_TEST_CASE( UIDocumentTest, WritingTheNameMovesItInTheTree )
{
    UIDocumentWidgets   document( "<TextWidget _name=\"Old\" />" );
    TextWidget*         pText = document.find<TextWidget>( "Old" );
    const PropertyInfo* pName = TextWidget::StaticType()->findPropertyInHierarchy( hashed_string( "_name" ) );
    SW_REQUIRE_TRUE( pText != nullptr && pName != nullptr );
    *pName->getValuePtr<hashed_string>( pText ) = hashed_string( "New" );
    pText->notifyPropertyWritten( *pName );
    SW_EXPECT_NULL( document.find<TextWidget>( "Old" ) );
    SW_EXPECT_TRUE( document.find<TextWidget>( "New" ) == pText );
}

/** @brief [UIDocumentTest] 위젯의 이름은 바인딩 · 애니메이션 대상이 아니다 — 다른 칸은 된다 */
SW_TEST_CASE( UIDocumentTest, NameIsNotABindingTarget )
{
    UIPropertyPath     path{};
    UIAnimatedProperty animated{};
    SW_EXPECT_TRUE( UIBindingValue::resolveWidgetTarget( *TextWidget::StaticType(), "_text", path ).has_value() );
    const expected<void> bound = UIBindingValue::resolveWidgetTarget( *TextWidget::StaticType(), "_name", path );
    SW_REQUIRE_TRUE( bound.has_value() == false );
    SW_EXPECT_TRUE( bound.error().kind() == ErrorKind::Unsupported );
    SW_EXPECT_FALSE( path.isValid() );
    SW_EXPECT_TRUE( UIAnimatedProperty::resolve( *TextWidget::StaticType(), "_opacity", animated ).has_value() );
    SW_EXPECT_FALSE( UIAnimatedProperty::resolve( *TextWidget::StaticType(), "_name", animated ).has_value() );
}

/** @brief [UIDocumentTest] 힌트는 기본이 현지화 키이고, 끄면 글 그대로다 */
SW_TEST_CASE( UIDocumentTest, HintFollowsItsLocalizedSwitch )
{
    UIDocumentWidgets      document( "<TextInputWidget _name=\"Key\" _hintText=\"Player name\" /><TextInputWidget _name=\"Plain\" _hintText=\"Player name\" _bLocalized=\"false\" />" );
    const TextInputWidget* pKey   = document.find<TextInputWidget>( "Key" );
    const TextInputWidget* pPlain = document.find<TextInputWidget>( "Plain" );
    SW_REQUIRE_TRUE( pKey != nullptr && pPlain != nullptr );
    SW_EXPECT_TRUE( castTo<TextWidget>( pKey->getChild( 0 ) )->isLocalized() );
    SW_EXPECT_FALSE( castTo<TextWidget>( pPlain->getChild( 0 ) )->isLocalized() );
}

/** @brief [UIDocumentTest] 거절: 문법 · 루트 · 버전 · 모르는 요소와 속성 · 패널 아닌 부모 · 루트 위젯 수 · 빈 목록 항목 · 겹친 애니메이션 이름 */
SW_TEST_CASE( UIDocumentTest, RefusesBadDocuments )
{
    expectRefused( "<Other/>", "root element" );
    expectRefused( "<UIDocument><BoxPanel/></UIDocument>", "needs _schemaVersion" );
    expectRefused( "<UIDocument _schemaVersion=\"99\"><BoxPanel/></UIDocument>", "not supported" );
    expectRefused( "<UIDocument _schemaVersion=\"0\" odd=\"1\"><BoxPanel/></UIDocument>", "unknown attribute" );
    expectRefused( makeDocument( "" ).c_str(), "no root widget" );
    expectRefused( makeDocument( "<BoxPanel/><BoxPanel/>" ).c_str(), "more than one root widget" );
    expectRefused( makeDocument( "<UIScreenDef/><UIScreenDef/><BoxPanel/>" ).c_str(), "more than one UIScreenDef" );
    expectRefused( makeDocument( "<NotAWidget/>" ).c_str(), "unknown element" );
    expectRefused( makeDocument( "<BoxPanel odd=\"1\"/>" ).c_str(), "unknown attribute 'odd'" );
    expectRefused( makeDocument( "<BoxPanel><odd/></BoxPanel>" ).c_str(), "unknown element <odd>" );
    expectRefused( makeDocument( "<TextWidget><TextWidget/></TextWidget>" ).c_str(), "not a panel" );
    expectRefused( makeDocument( "<_listStyleSheet><odd/></_listStyleSheet><BoxPanel/>" ).c_str(), "_listStyleSheet has unknown element" );
    expectRefused( makeDocument( "<_listStyleSheet><item/></_listStyleSheet><BoxPanel/>" ).c_str(), "empty item" );
    expectRefused( makeDocument( "<_listAnimation><UIAnimation/></_listAnimation><BoxPanel/>" ).c_str(), "without _name" );
    expectRefused( makeDocument( "<_listAnimation><UIAnimation _name=\"a\"/><UIAnimation _name=\"a\"/></_listAnimation><BoxPanel/>" ).c_str(), "two animations" );
    expectRefused( makeDocument( "<UserWidget/>" ).c_str(), "needs a _document path" );
    expectRefused( makeDocument( "<UserWidget _document=\"x.ui.xml\"><TextWidget/></UserWidget>" ).c_str(), "cannot have child widgets" );
}

/** @brief [UIDocumentTest] 오류에는 경로와 줄이 있다 */
SW_TEST_CASE( UIDocumentTest, ErrorsCarryPathAndLine )
{
    UIDocumentAsset asset;
    string          error;
    error = describeFailure( UIDocumentLoader::parse( "<UIDocument _schemaVersion=\"0\">\n<BoxPanel>\n<odd/>\n</BoxPanel>\n</UIDocument>", "engine/ui/lines.ui.xml", ResourceContext::forDocument(), asset ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.find( "engine/ui/lines.ui.xml:3:" ) != string::npos );
}

/** @brief [UIDocumentTest] 만들기: 타입 · 이름 · 값이 들어가고, 바인딩은 만든 위젯 번호와 함께 나온다. 같은 문서로 몇 번이든 만든다 */
SW_TEST_CASE( UIDocumentTest, InstantiatesWidgets )
{
    UIDocumentAsset asset;
    string          error;
    SW_REQUIRE_TRUE( UIDocumentLoader::parse( kDocumentXML, "engine/ui/test.ui.xml", ResourceContext::forDocument(), asset ).has_value() );
    UIDocumentCache cache;

    vector<UIBindingDef> listBinding;
    unique_ptr<Widget>   root = UIDocumentLoader::instantiate( asset, cache, ResourceTagSet{}, listBinding ).value_or( nullptr );
    SW_REQUIRE_TRUE( root != nullptr );
    SW_EXPECT_TRUE( root->getName() == hashed_string( "Root" ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), root->getChildren().size() );
    const TextWidget* pTitle = castTo<const TextWidget>( root->getChildren()[0].get() );
    SW_REQUIRE_TRUE( pTitle != nullptr );
    SW_EXPECT_EQUAL( string( "Hello" ), pTitle->getText() );
    SW_REQUIRE_EQUAL( size_t( 1 ), listBinding.size() );
    SW_EXPECT_TRUE( root->getChildren()[1]->getID() == listBinding[0]._widget );

    unique_ptr<Widget> second = UIDocumentLoader::instantiate( asset, cache, ResourceTagSet{}, listBinding ).value_or( nullptr );
    SW_REQUIRE_TRUE( second != nullptr );
    SW_EXPECT_EQUAL( size_t( 2 ), listBinding.size() );
    SW_EXPECT_FALSE( listBinding[0]._widget == listBinding[1]._widget );
}

/** @brief [UIDocumentTest] 이미지 위젯은 렌더 텍스처(`rendertarget/…` — 캡처 카메라의 그림)를 가리킬 수 있다. 예전에는 텍스처 종류가 파일 접미사만 받아 문서가 서지 않았다 */
SW_TEST_CASE( UIDocumentTest, ImageTakesARenderTexture )
{
    UIDocumentAsset asset;
    SW_REQUIRE_TRUE( UIDocumentLoader::parse( makeDocument( "<ImageWidget _name=\"Picture\" _imagePath=\"rendertarget/minimap\"/>" ), "engine/ui/picture.ui.xml", ResourceContext::forDocument(), asset ).has_value() );
    UIDocumentCache          cache;
    vector<UIBindingDef>     listBinding;
    const unique_ptr<Widget> root = UIDocumentLoader::instantiate( asset, cache, ResourceTagSet{}, listBinding ).value_or( nullptr );
    SW_REQUIRE_TRUE( root != nullptr );
    const ImageWidget* pImage = castTo<const ImageWidget>( root.get() );
    SW_REQUIRE_TRUE( pImage != nullptr );
    SW_EXPECT_EQUAL( string( "rendertarget/minimap" ), pImage->getImagePath() );
}

/** @brief [UIDocumentTest] 값을 읽지 못하면 위젯을 만들지 않고 줄을 알린다. 그때 앞서 모은 바인딩은 되돌린다 */
SW_TEST_CASE( UIDocumentTest, BadValueFailsInstantiate )
{
    UIDocumentAsset asset;
    string          error;
    SW_REQUIRE_TRUE( UIDocumentLoader::parse( makeDocument( "<BoxPanel _name=\"Root\"><TextWidget _text=\"{bind:_a}\"/><BoxPanel _spacing=\"abc\"/></BoxPanel>" ), "engine/ui/bad.ui.xml", ResourceContext::forDocument(), asset ).has_value() );
    UIDocumentCache      cache;
    vector<UIBindingDef> listBinding;
    listBinding.push_back( UIBindingDef{} );
    error = describeFailure( UIDocumentLoader::instantiate( asset, cache, ResourceTagSet{}, listBinding ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.find( "engine/ui/bad.ui.xml" ) != string::npos );
    SW_EXPECT_EQUAL( size_t( 1 ), listBinding.size() );
}

/** @brief [UIDocumentTest] 변형: 문서의 `_when` 은 읽을 때 풀린다. 바꿔 끼운 태그(에디터 미리보기의 기기 견본)로 읽은 변형은 엔진 판과 따로 들고, 엔진 태그는 그대로다 */
SW_TEST_CASE( UIDocumentTest, TagOverrideVariantsAreCachedApart )
{
    UIDocumentCache       cache;
    constexpr const utf8* kPath = "engine/ui/adaptive.ui.xml";
    cache.registerMemoryDocument( kPath, makeDocument( "<TextWidget _name=\"Title\" _text=\"Wide\"><_when _if=\"screen.compact\" _text=\"Narrow\"/></TextWidget>" ) );
    ResourceTagSet tagOverride;
    tagOverride.add( "screen.compact" );
    ResourceTagSet engineTags = engine::getSubsystem<ResourceTags>().capture();
    engineTags.removeAxis( "screen" );
    const ResourceTagSet previousTags = engine::getSubsystem<ResourceTags>().exchange( engineTags ); // 엔진 판은 screen 축이 없다 — compact 가 아니다

    const uint64                                      parseCount = cache.getParseCount().get();
    const expected<shared_ptr<const UIDocumentAsset>> engineDoc  = cache.acquire( kPath );
    const expected<shared_ptr<const UIDocumentAsset>> variantDoc = cache.acquire( kPath, tagOverride );
    SW_REQUIRE_TRUE( engineDoc.has_value() );
    SW_REQUIRE_TRUE( variantDoc.has_value() );
    SW_EXPECT_FALSE( engineDoc->get() == variantDoc->get() );
    SW_EXPECT_TRUE( cache.acquire( kPath, tagOverride )->get() == variantDoc->get() );
    SW_EXPECT_EQUAL( parseCount + 2u, cache.getParseCount().get() );
    SW_EXPECT_TRUE( engine::getSubsystem<ResourceTags>().capture() == engineTags );

    vector<UIBindingDef>     listBinding;
    const unique_ptr<Widget> wide   = UIDocumentLoader::instantiate( **engineDoc, cache, ResourceTagSet{}, listBinding ).value_or( nullptr );
    const unique_ptr<Widget> narrow = UIDocumentLoader::instantiate( **variantDoc, cache, tagOverride, listBinding ).value_or( nullptr );
    SW_REQUIRE_TRUE( wide != nullptr && narrow != nullptr );
    SW_EXPECT_EQUAL( string( "Wide" ), castTo<const TextWidget>( wide.get() )->getText() );
    SW_EXPECT_EQUAL( string( "Narrow" ), castTo<const TextWidget>( narrow.get() )->getText() );
    (void)engine::getSubsystem<ResourceTags>().exchange( previousTags );
    vector<string> listPending;
    engine::getSubsystem<ResourceComposition>().takePendingReload( listPending ); // 태그를 되돌리며 오른 다시 읽기 목록을 다음 케이스에 남기지 않는다
}

/** @brief [UIDocumentTest] 조각: 안의 이름은 `조각.이름` 으로 감싸고, 이름 없는 조각은 바깥 접두를 넘긴다. 자기를 부르면 그 사슬을 알린다 */
SW_TEST_CASE( UIDocumentTest, FragmentsWrapNamesAndRefuseCycles )
{
    UIDocumentCache cache;
    cache.registerMemoryDocument( "engine/ui/row.ui.xml", makeDocument( "<BoxPanel _name=\"Box\"><TextWidget _name=\"Label\"/></BoxPanel>" ) );
    cache.registerMemoryDocument( "engine/ui/self.ui.xml", makeDocument( "<UserWidget _document=\"engine/ui/self.ui.xml\"/>" ) );

    UIDocumentAsset asset;
    string          error;
    SW_REQUIRE_TRUE( UIDocumentLoader::parse( makeDocument( "<BoxPanel><UserWidget _name=\"Row\" _document=\"engine/ui/row.ui.xml\"/></BoxPanel>" ), "engine/ui/main.ui.xml", ResourceContext::forDocument(), asset ).has_value() );
    SW_REQUIRE_EQUAL( size_t( 1 ), asset._listFragment.size() );

    vector<UIBindingDef> listBinding;
    unique_ptr<Widget>   root = UIDocumentLoader::instantiate( asset, cache, ResourceTagSet{}, listBinding ).value_or( nullptr );
    SW_REQUIRE_TRUE( root != nullptr );
    const Widget* pRow = root->getChildren()[0].get();
    SW_EXPECT_TRUE( pRow->getName() == hashed_string( "Row" ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), pRow->getChildren().size() );
    const Widget* pBox = pRow->getChildren()[0].get();
    SW_EXPECT_TRUE( pBox->getName() == hashed_string( "Row.Box" ) );
    SW_EXPECT_TRUE( pBox->getChildren()[0]->getName() == hashed_string( "Row.Label" ) );

    UIDocumentAsset selfAsset;
    SW_REQUIRE_TRUE( UIDocumentLoader::parse( makeDocument( "<UserWidget _document=\"engine/ui/self.ui.xml\"/>" ), "engine/ui/self.ui.xml", ResourceContext::forDocument(), selfAsset ).has_value() );
    error = describeFailure( UIDocumentLoader::instantiate( selfAsset, cache, ResourceTagSet{}, listBinding ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.find( "includes itself" ) != string::npos );

    UIDocumentAsset missingAsset;
    SW_REQUIRE_TRUE( UIDocumentLoader::parse( makeDocument( "<UserWidget _document=\"engine/ui/missing.ui.xml\"/>" ), "engine/ui/m.ui.xml", ResourceContext::forDocument(), missingAsset ).has_value() );
    error = describeFailure( UIDocumentLoader::instantiate( missingAsset, cache, ResourceTagSet{}, listBinding ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.find( "not loaded" ) != string::npos );
}

/** @brief [UIDocumentTest] 쓰기: 다시 읽으면 같다. 기본값은 적지 않고, 바인딩 식은 그 자리에 식 그대로 돌아간다 */
SW_TEST_CASE( UIDocumentTest, WriteThenReadIsTheSame )
{
    UIDocumentAsset asset;
    string          error;
    SW_REQUIRE_TRUE( UIDocumentLoader::parse( kDocumentXML, "engine/ui/test.ui.xml", ResourceContext::forDocument(), asset ).has_value() );
    UIDocumentCache      cache;
    vector<UIBindingDef> listBinding;
    unique_ptr<Widget>   root = UIDocumentLoader::instantiate( asset, cache, ResourceTagSet{}, listBinding ).value_or( nullptr );
    SW_REQUIRE_TRUE( root != nullptr );

    const string text = UIDocumentWriter::write( asset._screenDesc, asset._listStyleSheet, *root, listBinding, asset._listAnimation, ResourceFieldWrite::OmitDefaults );
    SW_EXPECT_TRUE( text.find( "{bind:_health}" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "_bShowCursor" ) == string::npos ); // 기본값이다

    UIDocumentAsset reread;
    SW_REQUIRE_TRUE( UIDocumentLoader::parse( text, "engine/ui/test.ui.xml", ResourceContext::forDocument(), reread ).has_value() );
    SW_EXPECT_TRUE( reread._screenDesc._bPausesGame );
    SW_REQUIRE_EQUAL( size_t( 1 ), reread._listStyleSheet.size() );
    SW_EXPECT_EQUAL( string( "engine/ui/a.uss.xml" ), reread._listStyleSheet[0] );
    SW_REQUIRE_EQUAL( size_t( 1 ), reread._listAnimation.size() );
    SW_REQUIRE_EQUAL( size_t( 3 ), reread._listNode.size() );
    SW_REQUIRE_EQUAL( size_t( 1 ), reread._listNode[2]._listBinding.size() );
    SW_EXPECT_EQUAL( string( "{bind:_health}" ), reread._listNode[2]._listBinding[0]._expression );

    vector<UIBindingDef> listSecond;
    unique_ptr<Widget>   secondRoot = UIDocumentLoader::instantiate( reread, cache, ResourceTagSet{}, listSecond ).value_or( nullptr );
    SW_REQUIRE_TRUE( secondRoot != nullptr );
    SW_EXPECT_EQUAL( string( text ), UIDocumentWriter::write( reread._screenDesc, reread._listStyleSheet, *secondRoot, listSecond, reread._listAnimation, ResourceFieldWrite::OmitDefaults ) );
}
