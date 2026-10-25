// 카탈로그 표 편집기의 원문 편집 — 훑기(주석 · 처리 명령 · CDATA · 엔티티), 속성 값 바꾸기 · 더하기 · 빼기, 원소 더하기 · 빼기가 주석과 들여쓰기를 건드리지 않는지.
#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogXMLText.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    constexpr const utf8* kSample =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<!-- items for the farm -->\n"
        "<ItemCatalog>\n"
        "\t<!-- first -->\n"
        "\t<Item id=\"sword\" name=\"Iron &amp; Steel\" value='40'>\n"
        "\t\t<Stats attack=\"12\"/>\n"
        "\t</Item>\n"
        "\t<Item id=\"bow\" value=\"25\" />\n"
        "</ItemCatalog>\n";

    void parseOrFail( string_view text, CatalogXMLText& outParsed )
    {
        string error;
        SW_ASSERT_TRUE_MSG( outParsed.parse( text, error ), error.c_str() );
    }
} // namespace

SW_TEST_CASE( CatalogXMLTextTest, ParseFindsRowsAttributesAndDecodesEntities )
{
    CatalogXMLText parsed;
    parseOrFail( kSample, parsed );
    SW_ASSERT_TRUE( parsed.isParsed() );
    SW_EXPECT_EQUAL( parsed.getElement( parsed.getRootIndex() )._name, string( "ItemCatalog" ) );
    const vector<int32>& listRow = parsed.getRowIndices();
    SW_ASSERT_EQUAL( listRow.size(), size_t( 2 ) );
    const CatalogXMLElement& sword = parsed.getElement( listRow[0] );
    SW_EXPECT_EQUAL( sword._name, string( "Item" ) );
    SW_EXPECT_EQUAL( sword._listAttribute.size(), size_t( 3 ) );
    SW_EXPECT_EQUAL( sword._listAttribute[1]._value, string( "Iron & Steel" ) ); // 엔티티를 푼다
    SW_EXPECT_EQUAL( sword._listAttribute[2]._value, string( "40" ) );           // 작은따옴표
    SW_EXPECT_EQUAL( sword._listChild.size(), size_t( 1 ) );
    SW_EXPECT_FALSE( sword._bSelfClosing );
    const CatalogXMLElement& bow = parsed.getElement( listRow[1] );
    SW_EXPECT_TRUE( bow._bSelfClosing );
    SW_EXPECT_EQUAL( bow.findAttribute( "value" ), 1 );
    SW_EXPECT_EQUAL( bow.findAttribute( "nothing" ), -1 );
}

SW_TEST_CASE( CatalogXMLTextTest, ParseReportsBrokenDocumentsWithALineNumber )
{
    CatalogXMLText parsed;
    string         error;
    SW_EXPECT_FALSE( parsed.parse( "<A>\n<B></C>\n</A>", error ) );
    SW_EXPECT_TRUE( error.find( "line 2" ) != string::npos );
    SW_EXPECT_FALSE( parsed.parse( "<A><B></A>", error ) );
    SW_EXPECT_FALSE( parsed.parse( "<A/><B/>", error ) ); // 루트가 둘
    SW_EXPECT_FALSE( parsed.parse( "<A x=1/>", error ) ); // 따옴표 없는 값
    SW_EXPECT_FALSE( parsed.parse( "<A", error ) );
    SW_EXPECT_FALSE( parsed.parse( "<!-- only a comment -->", error ) ); // 루트 없음
    SW_EXPECT_TRUE( parsed.parse( "<A>text with a > sign</A>", error ) );
}

SW_TEST_CASE( CatalogXMLTextTest, SetAttributeValueKeepsCommentsAndOtherBytes )
{
    CatalogXMLText parsed;
    parseOrFail( kSample, parsed );
    const CatalogXMLElement& sword = parsed.getElement( parsed.getRowIndices()[0] );
    string                   edited;
    string                   error;
    SW_ASSERT_TRUE( CatalogXMLText::setAttributeValue( kSample, sword, "value", "55", edited, error ) );
    string expected = kSample;
    expected.replace( expected.find( "value='40'" ) + 7, 2, "55" );
    SW_EXPECT_EQUAL( edited, expected ); // 바뀐 것은 값 두 글자뿐이다 — 주석과 따옴표 종류가 그대로다

    SW_ASSERT_TRUE( CatalogXMLText::setAttributeValue( kSample, sword, "name", "A \"quoted\" <name>", edited, error ) );
    CatalogXMLText reparsed;
    parseOrFail( edited, reparsed );
    SW_EXPECT_EQUAL( reparsed.getElement( reparsed.getRowIndices()[0] )._listAttribute[1]._value, string( "A \"quoted\" <name>" ) ); // 쓸 때 입고 읽을 때 푼다
}

SW_TEST_CASE( CatalogXMLTextTest, SetAttributeValueAddsAMissingAttributeAfterTheLastOne )
{
    CatalogXMLText parsed;
    parseOrFail( kSample, parsed );
    const CatalogXMLElement& bow = parsed.getElement( parsed.getRowIndices()[1] );
    string                   edited;
    string                   error;
    SW_ASSERT_TRUE( CatalogXMLText::setAttributeValue( kSample, bow, "rarity", "2", edited, error ) );
    SW_EXPECT_TRUE( edited.find( "<Item id=\"bow\" value=\"25\" rarity=\"2\" />" ) != string::npos );
    CatalogXMLText reparsed;
    parseOrFail( edited, reparsed );
    SW_EXPECT_EQUAL( reparsed.getElement( reparsed.getRowIndices()[1] ).findAttribute( "rarity" ), 2 );

    // 속성이 하나도 없는 원소에도 더한다
    CatalogXMLText bare;
    parseOrFail( "<A><B/></A>", bare );
    SW_ASSERT_TRUE( CatalogXMLText::setAttributeValue( "<A><B/></A>", bare.getElement( bare.getRowIndices()[0] ), "x", "1", edited, error ) );
    SW_EXPECT_EQUAL( edited, string( "<A><B x=\"1\"/></A>" ) );
    SW_EXPECT_FALSE( CatalogXMLText::setAttributeValue( "<A><B/></A>", bare.getElement( bare.getRowIndices()[0] ), "", "1", edited, error ) );
}

SW_TEST_CASE( CatalogXMLTextTest, RemoveAttributeTakesItsLeadingSpace )
{
    CatalogXMLText parsed;
    parseOrFail( kSample, parsed );
    const CatalogXMLElement& sword = parsed.getElement( parsed.getRowIndices()[0] );
    string                   edited;
    string                   error;
    SW_ASSERT_TRUE( CatalogXMLText::removeAttribute( kSample, sword, "name", edited, error ) );
    SW_EXPECT_TRUE( edited.find( "<Item id=\"sword\" value='40'>" ) != string::npos );
    SW_EXPECT_FALSE( CatalogXMLText::removeAttribute( kSample, sword, "name2", edited, error ) );
    SW_EXPECT_FALSE( error.empty() );
}

SW_TEST_CASE( CatalogXMLTextTest, AppendChildElementFollowsTheDocumentIndent )
{
    CatalogXMLText parsed;
    parseOrFail( kSample, parsed );
    string                               edited;
    string                               error;
    const vector<CatalogXMLNewAttribute> listAttribute = {
        {   "id", "shield"},
        {"value",     "70"}
    };
    SW_ASSERT_TRUE( CatalogXMLText::appendChildElement( kSample, parsed.getElement( parsed.getRootIndex() ), parsed.getElements(), "Item", listAttribute, edited, error ) );
    SW_EXPECT_TRUE( edited.find( "\t<Item id=\"bow\" value=\"25\" />\n\t<Item id=\"shield\" value=\"70\" />\n</ItemCatalog>" ) != string::npos );
    CatalogXMLText reparsed;
    parseOrFail( edited, reparsed );
    SW_EXPECT_EQUAL( reparsed.getRowIndices().size(), size_t( 3 ) );
}

SW_TEST_CASE( CatalogXMLTextTest, AppendChildElementOpensASelfClosingRootAndKeepsCrLf )
{
    const string   source = "<Catalog version=\"1\"/>\r\n";
    CatalogXMLText parsed;
    parseOrFail( source, parsed );
    string edited;
    string error;
    SW_ASSERT_TRUE( CatalogXMLText::appendChildElement( source, parsed.getElement( parsed.getRootIndex() ), parsed.getElements(), "Row", {}, edited, error ) );
    SW_EXPECT_EQUAL( edited, string( "<Catalog version=\"1\">\r\n\t<Row />\r\n</Catalog>\r\n" ) );
    CatalogXMLText reparsed;
    parseOrFail( edited, reparsed );
    SW_EXPECT_EQUAL( reparsed.getRowIndices().size(), size_t( 1 ) );

    // 닫는 태그가 마지막 자식과 같은 줄에 있는 문서
    const string inlineSource = "<Catalog><Row/></Catalog>";
    parseOrFail( inlineSource, parsed );
    SW_ASSERT_TRUE( CatalogXMLText::appendChildElement( inlineSource, parsed.getElement( parsed.getRootIndex() ), parsed.getElements(), "Row", {}, edited, error ) );
    CatalogXMLText inlineReparsed;
    parseOrFail( edited, inlineReparsed );
    SW_EXPECT_EQUAL( inlineReparsed.getRowIndices().size(), size_t( 2 ) );
}

SW_TEST_CASE( CatalogXMLTextTest, RemoveElementTakesTheWholeLineOnlyWhenItOwnsTheLine )
{
    CatalogXMLText parsed;
    parseOrFail( kSample, parsed );
    string edited;
    string error;
    SW_ASSERT_TRUE( CatalogXMLText::removeElement( kSample, parsed.getElement( parsed.getRowIndices()[0] ), edited, error ) );
    SW_EXPECT_EQUAL( edited,
                     string( "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- items for the farm -->\n<ItemCatalog>\n\t<!-- first -->\n\t<Item id=\"bow\" value=\"25\" />\n</ItemCatalog>\n" ) );

    const string   inlineSource = "<C><A/><B/></C>";
    CatalogXMLText inlineParsed;
    parseOrFail( inlineSource, inlineParsed );
    SW_ASSERT_TRUE( CatalogXMLText::removeElement( inlineSource, inlineParsed.getElement( inlineParsed.getRowIndices()[0] ), edited, error ) );
    SW_EXPECT_EQUAL( edited, string( "<C><B/></C>" ) );
    SW_EXPECT_FALSE( CatalogXMLText::removeElement( inlineSource, inlineParsed.getElement( inlineParsed.getRootIndex() ), edited, error ) ); // 루트는 못 뺀다
}
