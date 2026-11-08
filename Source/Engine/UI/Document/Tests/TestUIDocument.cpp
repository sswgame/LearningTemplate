// UIDocumentTest — UI 문서: 읽기 오류(경로 · 줄을 담는다), 위젯 만들기 · 이름 · 바인딩 분리, 조각(이름 감싸기 · 자기 참조), 쓰기와 다시 읽기(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Document/UIDocumentLoader.h"
#include "Engine/UI/Document/UIDocumentWriter.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/TextWidget.h"

using namespace sw;

namespace
{
    constexpr const utf8* kDocumentXML = R"(<UIDocument _schemaVersion="1">
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
        return result.has_value() ? string() : string( result.error().message().data(), result.error().message().size() ) + ": " + string( result.error().detail().data(), result.error().detail().size() );
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
        return string( "<UIDocument _schemaVersion=\"1\">" ) + pBody + "</UIDocument>";
    }
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

/** @brief [UIDocumentTest] 거절: 문법 · 루트 · 버전 · 모르는 요소와 속성 · 패널 아닌 부모 · 루트 위젯 수 · 빈 목록 항목 · 겹친 애니메이션 이름 */
SW_TEST_CASE( UIDocumentTest, RefusesBadDocuments )
{
    expectRefused( "<Other/>", "root element" );
    expectRefused( "<UIDocument><BoxPanel/></UIDocument>", "needs _schemaVersion" );
    expectRefused( "<UIDocument _schemaVersion=\"99\"><BoxPanel/></UIDocument>", "not supported" );
    expectRefused( "<UIDocument _schemaVersion=\"1\" odd=\"1\"><BoxPanel/></UIDocument>", "unknown attribute" );
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
    error = describeFailure( UIDocumentLoader::parse( "<UIDocument _schemaVersion=\"1\">\n<BoxPanel>\n<odd/>\n</BoxPanel>\n</UIDocument>", "engine/ui/lines.ui.xml", ResourceContext::forDocument(), asset ) );
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

    const uint32                                      parseCount = cache.getParseCount();
    const expected<shared_ptr<const UIDocumentAsset>> engineDoc  = cache.findOrLoad( kPath );
    const expected<shared_ptr<const UIDocumentAsset>> variantDoc = cache.findOrLoad( kPath, tagOverride );
    SW_REQUIRE_TRUE( engineDoc.has_value() );
    SW_REQUIRE_TRUE( variantDoc.has_value() );
    SW_EXPECT_FALSE( engineDoc->get() == variantDoc->get() );
    SW_EXPECT_TRUE( cache.findOrLoad( kPath, tagOverride )->get() == variantDoc->get() );
    SW_EXPECT_EQUAL( parseCount + 2u, cache.getParseCount() );
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

    const string text = UIDocumentWriter::write( asset._screenDesc, asset._listStyleSheet, *root, listBinding, asset._listAnimation );
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
    SW_EXPECT_EQUAL( string( text ), UIDocumentWriter::write( reread._screenDesc, reread._listStyleSheet, *secondRoot, listSecond, reread._listAnimation ) );
}
