#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Catalog/Model/CatalogCurveModel.h"
#include "Editor/Panels/Catalog/Model/CatalogDocument.h"
#include "Editor/Panels/Catalog/Model/CatalogXMLText.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct CatalogTestUtil
    {
        static constexpr const utf8* kItems = "<?xml version=\"1.0\"?>\n"
                                              "<!-- 머리 <주석> -->\n"
                                              "<Items version=\"2\">\n"
                                              "\t<Item id=\"a\" cost=\"1\"/>\n"
                                              "\t<Item id=\"b\" name=\"x &amp; y\">\n"
                                              "\t\t<Tag value='q'/>\n"
                                              "\t</Item>\n"
                                              "\t<Other id=\"c\"></Other>\n"
                                              "</Items>\n";

        static constexpr const utf8* kCurves = "<VFX>\n"
                                               "\t<Def _name=\"Smoke\">\n"
                                               "\t\t<_size>\n"
                                               "\t\t\t<_listKey>\n"
                                               "\t\t\t\t<FloatCurveKey _time=\"0\" _value=\"1\" _interpolation=\"Linear\"/>\n"
                                               "\t\t\t\t<FloatCurveKey _time=\"2\" _value=\"3\" _interpolation=\"Linear\"/>\n"
                                               "\t\t\t\t<FloatCurveKey _time=\"oops\" _value=\"9\"/>\n"
                                               "\t\t\t</_listKey>\n"
                                               "\t\t</_size>\n"
                                               "\t\t<_colorOverLife>\n"
                                               "\t\t\t<_red><_listKey><FloatCurveKey _time=\"0\" _value=\"1\"/></_listKey></_red>\n"
                                               "\t\t\t<_green/>\n"
                                               "\t\t\t<_blue/>\n"
                                               "\t\t\t<_alpha/>\n"
                                               "\t\t</_colorOverLife>\n"
                                               "\t</Def>\n"
                                               "</VFX>\n";

        /** @brief 파싱에 실패하는지 봅니다. 실패하면 표가 비어야 합니다("깨진 문서는 테이블이 없다"). */
        static void expectParseFails( string_view text, const utf8* pExpectedError )
        {
            CatalogXMLText parsed;
            string         error;
            SW_EXPECT_FALSE( parsed.parse( text, error ) );
            SW_EXPECT_FALSE( parsed.isParsed() );
            SW_EXPECT_TRUE( parsed.getElements().empty() );
            SW_EXPECT_TRUE_MSG( error.find( pExpectedError ) != string::npos, error.c_str() );
        }

        /** @brief 첫 @p name 원소의 번호입니다. */
        static int32 findElement( const CatalogXMLText& parsed, string_view name )
        {
            for ( size_t index = 0; index < parsed.getElements().size(); ++index )
            {
                if ( string_view( parsed.getElements()[index]._name ) == name )
                    return static_cast<int32>( index );
            }
            return -1;
        }
    };
    using Util = CatalogTestUtil;
} // namespace

// ---------------------------------------------------------------------- 파싱

SW_TEST_CASE( CatalogXMLTextTest, ParsesElementsAttributesAndNesting )
{
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( Util::kItems, error ) );
    SW_EXPECT_TRUE( error.empty() );
    SW_REQUIRE_EQUAL( size_t{ 5 }, parsed.getElements().size() );
    const CatalogXMLElement& root = parsed.getElement( parsed.getRootIndex() );
    SW_EXPECT_TRUE( root._name == "Items" );
    SW_EXPECT_EQUAL( size_t{ 3 }, parsed.getRowIndices().size() );

    const CatalogXMLElement& second = parsed.getElement( parsed.getRowIndices()[1] );
    SW_EXPECT_TRUE( second._listAttribute[1]._value == "x & y" ); // 엔티티를 풀었다
    SW_REQUIRE_EQUAL( size_t{ 1 }, second._listChild.size() );
    const CatalogXMLElement& tag = parsed.getElement( second._listChild[0] );
    SW_EXPECT_TRUE( tag._bSelfClosing );
    SW_EXPECT_TRUE( tag._listAttribute[0]._value == "q" ); // 작은따옴표
    SW_EXPECT_EQUAL( -1, second.findAttribute( "nope" ) );
}

SW_TEST_CASE( CatalogXMLTextTest, BrokenDocumentsAreRefusedWithAReason )
{
    Util::expectParseFails( "", "no root element" );
    Util::expectParseFails( "<!-- only a comment -->", "no root element" );
    Util::expectParseFails( "<a><b></a>", "does not match" );
    Util::expectParseFails( "<a></a><b/>", "more than one root" );
    Util::expectParseFails( "<a>", "never closed" );
    Util::expectParseFails( "</a>", "without an opening tag" );
    Util::expectParseFails( "<a x=1/>", "must be quoted" );
    Util::expectParseFails( "<a x/>", "without a value" );
    Util::expectParseFails( "<a x=\"1/>", "unterminated attribute" );
    Util::expectParseFails( "<a / >", "stray" );
    Util::expectParseFails( "<>", "without a name" );
    Util::expectParseFails( "<a", "unterminated start tag" );
    Util::expectParseFails( "<!-- open", "unterminated comment" );
    Util::expectParseFails( "<a>\n\n</b>", "line 3" ); // 줄 번호를 알려 준다
}

SW_TEST_CASE( CatalogXMLTextTest, MarkupInsideCommentsAndValuesIsNotAnElement )
{
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( "<a v=\"&lt;x&gt; > /\"><!-- <b> --><![CDATA[<c>]]><?pi <d>?></a>", error ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, parsed.getElements().size() );
    SW_EXPECT_TRUE( parsed.getElement( 0 )._listAttribute[0]._value == "<x> > /" );
}

// ---------------------------------------------------------------------- 원문 편집: 건드린 곳 말고는 한 글자도 바뀌지 않는다

SW_TEST_CASE( CatalogXMLTextTest, SetAttributeKeepsEverythingElse )
{
    const string   text = "<a  x = \"1\"   y='2'/>";
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( text, error ) );
    string edited;
    SW_REQUIRE_TRUE( CatalogXMLText::setAttributeValue( text, parsed.getElement( 0 ), "x", "5", edited, error ) );
    SW_EXPECT_TRUE( edited == "<a  x = \"5\"   y='2'/>" );

    SW_REQUIRE_TRUE( CatalogXMLText::setAttributeValue( text, parsed.getElement( 0 ), "z", "<&>", edited, error ) );
    SW_REQUIRE_TRUE( parsed.parse( edited, error ) ); // 새 속성은 끝에 붙고 값은 이스케이프한다
    SW_EXPECT_TRUE( parsed.getElement( 0 )._listAttribute[2]._value == "<&>" );
    SW_EXPECT_TRUE( edited.find( "y='2'" ) != string::npos );

    SW_REQUIRE_TRUE( parsed.parse( text, error ) );
    SW_EXPECT_FALSE( CatalogXMLText::setAttributeValue( text, parsed.getElement( 0 ), "", "1", edited, error ) );
}

SW_TEST_CASE( CatalogXMLTextTest, FirstAttributeOfABareElementGoesAfterTheName )
{
    const string   text = "<a/>";
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( text, error ) );
    string edited;
    SW_REQUIRE_TRUE( CatalogXMLText::setAttributeValue( text, parsed.getElement( 0 ), "x", "1", edited, error ) );
    SW_EXPECT_TRUE( edited == "<a x=\"1\"/>" );
}

SW_TEST_CASE( CatalogXMLTextTest, RemoveAttributeTakesItsLeadingSpace )
{
    const string   text = "<a x=\"1\" y=\"2\"/>";
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( text, error ) );
    string edited;
    SW_REQUIRE_TRUE( CatalogXMLText::removeAttribute( text, parsed.getElement( 0 ), "x", edited, error ) );
    SW_EXPECT_TRUE( edited == "<a y=\"2\"/>" );
    SW_EXPECT_FALSE( CatalogXMLText::removeAttribute( text, parsed.getElement( 0 ), "z", edited, error ) );
    SW_EXPECT_FALSE( error.empty() );
}

SW_TEST_CASE( CatalogXMLTextTest, AppendingOpensASelfClosedParent )
{
    const string   text = "<r>\n\t<a/>\n</r>\n";
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( text, error ) );
    string edited;
    SW_REQUIRE_TRUE( CatalogXMLText::appendChildElement( text, parsed.getElement( 1 ), parsed.getElements(), "c", {
                                                                                                                      { "k", "v" }
    },
                                                         edited, error ) );
    SW_EXPECT_TRUE( edited == "<r>\n\t<a>\n\t\t<c k=\"v\" />\n\t</a>\n</r>\n" );
}

SW_TEST_CASE( CatalogXMLTextTest, AppendingFollowsTheSiblingIndentAndNewlineStyle )
{
    const string   text = "<r>\r\n    <a/>\r\n</r>\r\n";
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( text, error ) );
    string edited;
    SW_REQUIRE_TRUE( CatalogXMLText::appendChildElement( text, parsed.getElement( 0 ), parsed.getElements(), "b", {}, edited, error ) );
    SW_EXPECT_TRUE( edited == "<r>\r\n    <a/>\r\n    <b />\r\n</r>\r\n" );
    SW_EXPECT_FALSE( CatalogXMLText::appendChildElement( text, parsed.getElement( 0 ), parsed.getElements(), "", {}, edited, error ) );
}

SW_TEST_CASE( CatalogXMLTextTest, AppendingIntoAnOneLineElementBreaksTheLine )
{
    const string   text = "<r></r>";
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( text, error ) );
    string edited;
    SW_REQUIRE_TRUE( CatalogXMLText::appendChildElement( text, parsed.getElement( 0 ), parsed.getElements(), "b", {}, edited, error ) );
    SW_EXPECT_TRUE( edited == "<r>\n\t<b />\n</r>" );
}

SW_TEST_CASE( CatalogXMLTextTest, RemovingAnElementTakesItsLineOnlyWhenItOwnsIt )
{
    const string   text = "<r>\n\t<a/>\n\t<b/><c/>\n</r>";
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( text, error ) );
    string edited;
    SW_REQUIRE_TRUE( CatalogXMLText::removeElement( text, parsed.getElement( 1 ), edited, error ) );
    SW_EXPECT_TRUE( edited == "<r>\n\t<b/><c/>\n</r>" );
    SW_REQUIRE_TRUE( CatalogXMLText::removeElement( text, parsed.getElement( 2 ), edited, error ) );
    SW_EXPECT_TRUE( edited == "<r>\n\t<a/>\n\t<c/>\n</r>" );
    SW_EXPECT_FALSE( CatalogXMLText::removeElement( text, parsed.getElement( 0 ), edited, error ) ); // 루트
}

// ---------------------------------------------------------------------- 문서

SW_TEST_CASE( CatalogDocumentTest, GroupsCollectRowsAndColumnsInOrder )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( Util::kItems ) );
    SW_EXPECT_TRUE( document.getRootName() == "Items" );
    SW_EXPECT_FALSE( document.isDirty() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, document.getGroups().size() );
    const CatalogDocument::Group& items = document.getGroups()[0];
    SW_EXPECT_TRUE( items._name == "Item" );
    SW_EXPECT_EQUAL( size_t{ 2 }, items._listRow.size() );
    SW_REQUIRE_EQUAL( size_t{ 3 }, items._listColumn.size() );
    SW_EXPECT_TRUE( items._listColumn[0] == "id" );
    SW_EXPECT_TRUE( items._listColumn[1] == "cost" );
    SW_EXPECT_TRUE( items._listColumn[2] == "name" );

    const string* pCost = document.findCellValue( items._listRow[0], "cost" );
    SW_REQUIRE_NOT_NULL( pCost );
    SW_EXPECT_TRUE( *pCost == "1" );
    SW_EXPECT_TRUE( document.findCellValue( items._listRow[1], "cost" ) == nullptr );
    SW_EXPECT_TRUE( document.findCellValue( 999, "cost" ) == nullptr );
}

SW_TEST_CASE( CatalogDocumentTest, EditsUndoRedoAndDirtyTrackTheText )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( Util::kItems ) );
    const int32 row = document.getGroups()[0]._listRow[0];
    SW_EXPECT_FALSE( document.canUndo() );

    SW_REQUIRE_TRUE( document.setCell( row, "cost", "7" ) );
    SW_EXPECT_TRUE( document.isDirty() );
    SW_EXPECT_TRUE( *document.findCellValue( row, "cost" ) == "7" );

    document.undo();
    SW_EXPECT_FALSE( document.isDirty() );
    SW_EXPECT_TRUE( *document.findCellValue( row, "cost" ) == "1" );
    SW_EXPECT_TRUE( document.canRedo() );

    document.redo();
    SW_EXPECT_TRUE( *document.findCellValue( row, "cost" ) == "7" );
    document.markSaved();
    SW_EXPECT_FALSE( document.isDirty() );
    SW_EXPECT_TRUE( document.canUndo() ); // 저장해도 되돌릴 수 있다

    SW_REQUIRE_TRUE( document.unsetCell( row, "cost" ) );
    SW_EXPECT_TRUE( document.findCellValue( row, "cost" ) == nullptr );
    SW_EXPECT_FALSE( document.canRedo() ); // 새 편집이 다시 하기를 지운다
    document.revert();
    SW_EXPECT_TRUE( *document.findCellValue( row, "cost" ) == "7" );
    SW_EXPECT_FALSE( document.canUndo() );
}

SW_TEST_CASE( CatalogDocumentTest, UndoHistoryIsBounded )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( "<r x=\"0\"/>" ) );
    for ( size_t index = 0; index < CatalogDocument::kMaxUndoCount + 5; ++index )
    {
        SW_REQUIRE_TRUE( document.setCell( 0, "x", to_string( static_cast<uint64>( index + 1 ) ) ) );
    }
    uint32 undoCount = 0;
    while ( document.canUndo() )
    {
        document.undo();
        ++undoCount;
    }
    SW_EXPECT_EQUAL( static_cast<uint32>( CatalogDocument::kMaxUndoCount ), undoCount );
}

SW_TEST_CASE( CatalogDocumentTest, AddedRowCopiesTheGroupColumns )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( Util::kItems ) );
    SW_REQUIRE_TRUE( document.addRow( "Item" ) );
    const CatalogDocument::Group& items = document.getGroups()[0];
    SW_REQUIRE_EQUAL( size_t{ 3 }, items._listRow.size() );
    const int32 row = items._listRow[2];
    SW_EXPECT_TRUE( *document.findCellValue( row, "id" ) == "new" );
    SW_EXPECT_TRUE( *document.findCellValue( row, "name" ) == "new" );
    SW_EXPECT_TRUE( document.findCellValue( row, "cost" ) != nullptr );

    SW_REQUIRE_TRUE( document.addRow( "Brand" ) ); // 없는 묶음은 id 하나로 시작한다
    SW_REQUIRE_EQUAL( size_t{ 3 }, document.getGroups().size() );
    SW_EXPECT_TRUE( *document.findCellValue( document.getGroups()[2]._listRow[0], "id" ) == "new" );
}

SW_TEST_CASE( CatalogDocumentTest, RemovedRowAndRefusedRoot )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( Util::kItems ) );
    SW_REQUIRE_TRUE( document.removeRow( document.getGroups()[1]._listRow[0] ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, document.getGroups().size() );
    SW_EXPECT_FALSE( document.removeRow( document.getParsed().getRootIndex() ) );
    SW_EXPECT_FALSE( document.getError().empty() );
}

// 깨진 원문은 글 상자로 고치게 열리지만 표 편집은 거절한다.
SW_TEST_CASE( CatalogDocumentTest, BrokenTextOpensButRefusesTableEdits )
{
    CatalogDocument document;
    SW_EXPECT_FALSE( document.load( "<a><b></a>" ) );
    SW_EXPECT_FALSE( document.isParsed() );
    SW_EXPECT_FALSE( document.getError().empty() );
    SW_EXPECT_TRUE( document.getRootName().empty() );
    SW_EXPECT_TRUE( document.getText() == "<a><b></a>" );
    SW_EXPECT_FALSE( document.setCell( 0, "x", "1" ) );
    SW_EXPECT_FALSE( document.unsetCell( 0, "x" ) );
    SW_EXPECT_FALSE( document.addRow( "Item" ) );
    SW_EXPECT_FALSE( document.removeRow( 0 ) );
    SW_EXPECT_FALSE( document.addChild( 0, "c", {} ) );

    document.replaceText( "<a/>", true );
    SW_EXPECT_TRUE( document.isParsed() );
    SW_EXPECT_TRUE( document.getError().empty() );
    document.undo();
    SW_EXPECT_FALSE( document.isParsed() );
}

// ---------------------------------------------------------------------- 곡선

SW_TEST_CASE( CatalogCurveModelTest, FindsCurvesWithLabelsAndKeys )
{
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( Util::kCurves, error ) );
    vector<CatalogCurve> listCurve;
    CatalogCurveModel::collectCurves( parsed, parsed.getRootIndex(), listCurve );
    SW_REQUIRE_EQUAL( size_t{ 5 }, listCurve.size() );
    SW_EXPECT_TRUE( listCurve[0]._label == "Def(Smoke)/_size" );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listCurve[0]._listKey.size() );
    SW_EXPECT_TRUE( listCurve[0]._listKey[0]._bValid );
    SW_EXPECT_NEAR( 2.0f, listCurve[0]._listKey[1]._time, 1e-5f );
    SW_EXPECT_FALSE( listCurve[0]._listKey[2]._bValid ); // 숫자가 아닌 시각
    SW_EXPECT_TRUE( listCurve[1]._label == "Def(Smoke)/_colorOverLife/_red" );
    SW_EXPECT_TRUE( listCurve[2]._listKey.empty() ); // 키 없는 채널도 곡선이다

    vector<CatalogCurve> listNone;
    CatalogCurveModel::collectCurves( parsed, 999, listNone );
    SW_EXPECT_TRUE( listNone.empty() );
}

SW_TEST_CASE( CatalogCurveModelTest, GradientNeedsAllFourChannels )
{
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( Util::kCurves, error ) );
    vector<CatalogCurve> listCurve;
    CatalogCurveModel::collectCurves( parsed, parsed.getRootIndex(), listCurve );
    vector<CatalogGradient> listGradient;
    CatalogCurveModel::collectGradients( listCurve, listGradient );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listGradient.size() );
    SW_EXPECT_TRUE( listGradient[0]._label == "Def(Smoke)/_colorOverLife" );
    SW_EXPECT_EQUAL( 4, listGradient[0]._alpha );

    listCurve.pop_back(); // 알파가 없다
    CatalogCurveModel::collectGradients( listCurve, listGradient );
    SW_EXPECT_TRUE( listGradient.empty() );
}

SW_TEST_CASE( CatalogCurveModelTest, SamplingUsesOnlyValidKeys )
{
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( Util::kCurves, error ) );
    vector<CatalogCurve> listCurve;
    CatalogCurveModel::collectCurves( parsed, parsed.getRootIndex(), listCurve );
    const CatalogCurve& size = listCurve[0];
    SW_EXPECT_EQUAL( size_t{ 2 }, CatalogCurveModel::makeFloatCurve( size )._listKey.size() );
    SW_EXPECT_NEAR( 2.0f, CatalogCurveModel::computeEndTime( size ), 1e-5f );

    vector<float32> listValue;
    SW_REQUIRE_TRUE( CatalogCurveModel::sample( size, 2.0f, 3, listValue ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listValue.size() );
    SW_EXPECT_NEAR( 1.0f, listValue[0], 1e-4f );
    SW_EXPECT_NEAR( 2.0f, listValue[1], 1e-4f );
    SW_EXPECT_NEAR( 3.0f, listValue[2], 1e-4f );

    SW_EXPECT_FALSE( CatalogCurveModel::sample( size, 2.0f, 1, listValue ) );
    SW_EXPECT_FALSE( CatalogCurveModel::sample( listCurve[2], 2.0f, 3, listValue ) ); // 키가 없다
    SW_EXPECT_NEAR( 1.0f, CatalogCurveModel::computeEndTime( listCurve[2] ), 1e-5f ); // 빈 곡선은 1 초 창이다
}

// 빈 채널에 처음 더하면 시각 0 값 1, 그다음은 마지막 키 뒤 한 걸음에 같은 값이다.
SW_TEST_CASE( CatalogCurveModelTest, AddKeyContinuesAfterTheLastKey )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( Util::kCurves ) );
    const int32 green = Util::findElement( document.getParsed(), "_green" );
    SW_REQUIRE_TRUE( green >= 0 );
    SW_REQUIRE_TRUE( CatalogCurveModel::addKey( document, green ) );
    SW_REQUIRE_TRUE( CatalogCurveModel::addKey( document, green ) );

    vector<CatalogCurve> listCurve;
    CatalogCurveModel::collectCurves( document.getParsed(), document.getParsed().getRootIndex(), listCurve );
    const CatalogCurve* pGreen = nullptr;
    for ( const CatalogCurve& curve : listCurve )
    {
        if ( curve._channel == "_green" )
            pGreen = &curve;
    }
    SW_REQUIRE_NOT_NULL( pGreen );
    SW_REQUIRE_EQUAL( size_t{ 2 }, pGreen->_listKey.size() );
    SW_EXPECT_NEAR( 0.0f, pGreen->_listKey[0]._time, 1e-5f );
    SW_EXPECT_NEAR( 1.0f, pGreen->_listKey[0]._value, 1e-5f );
    SW_EXPECT_NEAR( 0.1f, pGreen->_listKey[1]._time, 1e-5f );
    SW_EXPECT_NEAR( 1.0f, pGreen->_listKey[1]._value, 1e-5f );
    SW_EXPECT_FALSE( CatalogCurveModel::addKey( document, -1 ) );
}
