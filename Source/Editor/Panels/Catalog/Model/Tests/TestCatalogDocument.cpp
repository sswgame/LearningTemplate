#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Catalog/Model/CatalogCurveModel.h"
#include "Editor/Panels/Catalog/Model/CatalogDocument.h"
#include "Editor/Panels/Catalog/Model/CatalogFileList.h"
#include "Editor/Panels/Catalog/Model/CatalogFileSession.h"
#include "Editor/Panels/Catalog/Model/CatalogTableEdit.h"
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
    SW_EXPECT_FALSE( document.isDirty() );

    document.undo(); // 되돌리기(Revert)로 버린 편집도 돌아온다
    SW_EXPECT_TRUE( document.isDirty() );
    SW_EXPECT_TRUE( document.findCellValue( row, "cost" ) == nullptr );
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

// 빈 값을 적으면 로더가 읽지 못하고, 같은 id 를 적으면 로더가 거절한다 — 새 행은 바로 검증을 통과하는 모양이어야 한다.
SW_TEST_CASE( CatalogDocumentTest, AddedRowFollowsTheLastRowWithUniqueNames )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( "<r>\n\t<Item id=\"a\" cost=\"1\"/>\n\t<Item id=\"b\" cost=\"5\"/>\n\t<Note id=\"n\" text=\"hi\"/>\n\t<Note id=\"m\"/>\n</r>\n" ) );
    SW_REQUIRE_TRUE( document.addRow( "Item" ) );
    const CatalogDocument::Group* pItems = &document.getGroups()[0];
    SW_REQUIRE_EQUAL( size_t{ 3 }, pItems->_listRow.size() );
    SW_EXPECT_TRUE( *document.findCellValue( pItems->_listRow[2], "id" ) == "new" );
    SW_EXPECT_TRUE( *document.findCellValue( pItems->_listRow[2], "cost" ) == "5" );

    SW_REQUIRE_TRUE( document.addRow( "Item" ) );
    pItems = &document.getGroups()[0];
    SW_EXPECT_TRUE( *document.findCellValue( pItems->_listRow[3], "id" ) == "new_2" );
    SW_EXPECT_TRUE( pItems->_duplicateID.empty() );

    SW_REQUIRE_TRUE( document.addRow( "Note" ) ); // 마지막 행에 없는 칸은 빈 값으로 적지 않는다
    const CatalogDocument::Group& notes = document.getGroups()[1];
    SW_EXPECT_TRUE( document.findCellValue( notes._listRow.back(), "text" ) == nullptr );

    SW_REQUIRE_TRUE( document.addRow( "Brand" ) ); // 없는 묶음은 id 하나로 시작한다
    SW_REQUIRE_EQUAL( size_t{ 3 }, document.getGroups().size() );
    SW_EXPECT_TRUE( *document.findCellValue( document.getGroups()[2]._listRow[0], "id" ) == "new" );
}

SW_TEST_CASE( CatalogDocumentTest, DuplicatedRowKeepsChildrenAndGetsANewName )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( Util::kItems ) );
    const int32 row = document.getGroups()[0]._listRow[1]; // 자식 Tag 가 있는 b
    SW_REQUIRE_TRUE( document.duplicateRow( row ) );
    const CatalogDocument::Group& items = document.getGroups()[0];
    SW_REQUIRE_EQUAL( size_t{ 3 }, items._listRow.size() );
    const int32 copy = items._listRow[2];
    SW_EXPECT_TRUE( *document.findCellValue( copy, "id" ) == "b_copy" );
    SW_EXPECT_TRUE( *document.findCellValue( copy, "name" ) == "x & y_copy" );
    SW_EXPECT_EQUAL( size_t{ 1 }, document.getParsed().getElement( copy )._listChild.size() );
    SW_EXPECT_TRUE( document.getText().find( "<!-- 머리 <주석> -->" ) != string::npos );

    SW_REQUIRE_TRUE( document.duplicateRow( items._listRow[1] ) ); // 또 복사해도 겹치지 않는다
    SW_EXPECT_TRUE( *document.findCellValue( document.getGroups()[0]._listRow[2], "id" ) == "b_copy_2" );

    document.undo();
    document.undo(); // 복사 한 번이 되돌리기 한 번이다
    SW_EXPECT_FALSE( document.isDirty() );

    SW_EXPECT_FALSE( document.duplicateRow( document.getParsed().getRootIndex() ) ); // 루트는 행이 아니다
    SW_EXPECT_FALSE( document.getError().empty() );
    SW_EXPECT_FALSE( document.duplicateRow( 999 ) );
    SW_EXPECT_FALSE( document.isDirty() );
}

SW_TEST_CASE( CatalogXMLTextTest, DuplicatingAnElementCopiesItsTextOntoANewLine )
{
    const string   text = "<r>\r\n  <a x=\"1\"><!-- c --></a>\r\n</r>\r\n";
    CatalogXMLText parsed;
    string         error;
    SW_REQUIRE_TRUE( parsed.parse( text, error ) );
    string edited;
    SW_REQUIRE_TRUE( CatalogXMLText::duplicateElement( text, parsed.getElement( 1 ), edited, error ) );
    SW_EXPECT_TRUE( edited == "<r>\r\n  <a x=\"1\"><!-- c --></a>\r\n  <a x=\"1\"><!-- c --></a>\r\n</r>\r\n" );
    SW_EXPECT_FALSE( CatalogXMLText::duplicateElement( text, parsed.getElement( 0 ), edited, error ) );
}

// 로더가 겹친 id 를 거절하므로 저장하기 전에 표가 알린다. 이름은 겹쳐도 된다.
SW_TEST_CASE( CatalogDocumentTest, GroupsReportASharedID )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( "<r><A id=\"x\" name=\"same\"/><A id=\"y\" name=\"same\"/><B _id=\"k\"/><B _id=\"k\"/><C/></r>" ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, document.getGroups().size() );
    SW_EXPECT_TRUE( document.getGroups()[0]._duplicateID.empty() );
    SW_EXPECT_TRUE( document.getGroups()[1]._duplicateID == "k" );
    SW_EXPECT_EQUAL( 5u, document.countRows() );
    SW_REQUIRE_TRUE( document.setCell( document.getGroups()[1]._listRow[1], "_id", "z" ) );
    SW_EXPECT_TRUE( document.getGroups()[1]._duplicateID.empty() );
}

SW_TEST_CASE( CatalogDocumentTest, RowFilterMatchesCellValues )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( Util::kItems ) );
    const CatalogDocument::Group& items = document.getGroups()[0];
    vector<int32>                 listRow;
    document.collectVisibleRows( items, "", listRow );
    SW_EXPECT_EQUAL( size_t{ 2 }, listRow.size() );
    document.collectVisibleRows( items, "  X & ", listRow ); // 앞뒤 공백 · 대소문자 무시
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRow.size() );
    SW_EXPECT_EQUAL( items._listRow[1], listRow[0] );
    document.collectVisibleRows( items, "nothing", listRow );
    SW_EXPECT_TRUE( listRow.empty() );
}

// 글 상자 입력은 칸을 떠날 때까지 되돌리기 한 단계다 — 쌓지 않으면 친 글을 되돌릴 수 없었다.
SW_TEST_CASE( CatalogDocumentTest, TextBoxEditsAreOneUndoStepPerVisit )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( "<a/>" ) );
    document.replaceText( "<a/>" ); // 같은 글은 편집이 아니다
    SW_EXPECT_FALSE( document.canUndo() );
    document.replaceText( "<a x=\"1\"/>" );
    document.replaceText( "<a x=\"12\"/>" );
    document.endTextEdit();
    document.replaceText( "<a x=\"123\"/>" );
    document.undo();
    SW_EXPECT_TRUE( document.getText() == "<a x=\"12\"/>" );
    document.undo();
    SW_EXPECT_TRUE( document.getText() == "<a/>" );
    SW_EXPECT_FALSE( document.canUndo() );
    document.redo();
    SW_EXPECT_TRUE( document.getText() == "<a x=\"12\"/>" );
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

    document.replaceText( "<a/>" );
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
    SW_EXPECT_FALSE( document.getError().empty() );
}

// 빈 채널에 키를 더하면 `_listKey` 와 키 두 원소가 생기지만 되돌리기는 한 번이다 — 빈 목록이 남지 않는다.
SW_TEST_CASE( CatalogCurveModelTest, AddKeyIsOneUndoStep )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( Util::kCurves ) );
    SW_REQUIRE_TRUE( CatalogCurveModel::addKey( document, Util::findElement( document.getParsed(), "_green" ) ) );
    document.undo();
    SW_EXPECT_FALSE( document.isDirty() );
    SW_EXPECT_FALSE( document.canUndo() );
}

SW_TEST_CASE( CatalogCurveModelTest, GradientSamplesNeedEveryChannelAndAreClamped )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( Util::kCurves ) );
    vector<CatalogCurve>    listCurve;
    vector<CatalogGradient> listGradient;
    vector<float4>          listColor;
    CatalogCurveModel::collectCurves( document.getParsed(), document.getParsed().getRootIndex(), listCurve );
    CatalogCurveModel::collectGradients( listCurve, listGradient );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listGradient.size() );
    SW_EXPECT_FALSE( CatalogCurveModel::sampleGradient( listCurve, listGradient[0], 3, listColor ) ); // 키 없는 채널이 있다

    for ( const utf8* pChannel : { "_green", "_blue", "_alpha" } )
    {
        SW_REQUIRE_TRUE( CatalogCurveModel::addKey( document, Util::findElement( document.getParsed(), pChannel ) ) );
    }
    CatalogCurveModel::collectCurves( document.getParsed(), document.getParsed().getRootIndex(), listCurve );
    SW_REQUIRE_TRUE( document.setCell( listCurve[static_cast<size_t>( listGradient[0]._red )]._listKey[0]._elementIndex, "_value", "5" ) );
    CatalogCurveModel::collectCurves( document.getParsed(), document.getParsed().getRootIndex(), listCurve );
    CatalogCurveModel::collectGradients( listCurve, listGradient );
    SW_REQUIRE_TRUE( CatalogCurveModel::sampleGradient( listCurve, listGradient[0], 3, listColor ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listColor.size() );
    SW_EXPECT_NEAR( 1.0f, listColor[0]._x, 1e-5f ); // 5 는 1 로 자른다
    SW_EXPECT_NEAR( 1.0f, listColor[2]._w, 1e-5f );
}

// ---------------------------------------------------------------------- 표에서 고른 편집

SW_TEST_CASE( CatalogTableEditTest, DescribesWhatChangedOrWhyItWasRefused )
{
    CatalogDocument document;
    SW_REQUIRE_TRUE( document.load( Util::kItems ) );
    const int32 row = document.getGroups()[0]._listRow[0];
    string      description;

    SW_EXPECT_FALSE( CatalogTableEdit{}.apply( document, description ) );
    SW_EXPECT_TRUE( description.empty() );

    SW_REQUIRE_TRUE( ( CatalogTableEdit{ CatalogTableEdit::Kind::SetCell, row, "cost", "7" }.apply( document, description ) ) );
    SW_EXPECT_TRUE( description == "Edited Item.cost" );
    SW_REQUIRE_TRUE( ( CatalogTableEdit{ CatalogTableEdit::Kind::UnsetCell, row, "cost", "" }.apply( document, description ) ) );
    SW_EXPECT_TRUE( description == "Unset Item.cost" );
    SW_REQUIRE_TRUE( ( CatalogTableEdit{ CatalogTableEdit::Kind::AddRow, -1, "", "Item" }.apply( document, description ) ) );
    SW_EXPECT_TRUE( description == "Added row to Item" );
    SW_REQUIRE_TRUE( ( CatalogTableEdit{ CatalogTableEdit::Kind::DuplicateRow, row, "", "" }.apply( document, description ) ) );
    SW_EXPECT_TRUE( description == "Duplicated row in Item" );
    SW_REQUIRE_TRUE( ( CatalogTableEdit{ CatalogTableEdit::Kind::RemoveRow, row, "", "" }.apply( document, description ) ) );
    SW_EXPECT_TRUE( description == "Removed row from Item" );

    SW_EXPECT_FALSE( ( CatalogTableEdit{ CatalogTableEdit::Kind::UnsetCell, document.getGroups()[0]._listRow[0], "nope", "" }.apply( document, description ) ) );
    SW_EXPECT_TRUE( description == "the element has no such attribute" ); // 거절의 이유가 그대로 나온다
}

// ---------------------------------------------------------------------- 파일 목록

SW_TEST_CASE( CatalogFileListTest, SortsByKindThenIDAndFiltersIntoGroups )
{
    CatalogFileList list;
    list.setRows( {
        CatalogFileRow{"game/z.items.xml", "Items"},
        CatalogFileRow{  "game/a.vfx.xml",   "VFX"},
        CatalogFileRow{"game/a.items.xml", "Items"}
    } );
    SW_REQUIRE_EQUAL( size_t{ 3 }, list.getRows().size() );
    SW_EXPECT_TRUE( list.getRows()[0]._assetID == "game/a.items.xml" );
    SW_EXPECT_TRUE( list.getRows()[2]._kindLabel == "VFX" );

    vector<CatalogFileGroup> listGroup;
    SW_EXPECT_EQUAL( 3u, list.collectGroups( "", listGroup ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listGroup.size() );
    SW_EXPECT_EQUAL( size_t{ 2 }, listGroup[0]._listRow.size() );

    SW_EXPECT_EQUAL( 1u, list.collectGroups( "Z.ITEMS", listGroup ) ); // 거르면 머리의 수도 맞는 파일 수다
    SW_REQUIRE_EQUAL( size_t{ 1 }, listGroup.size() );
    SW_EXPECT_EQUAL( 1u, listGroup[0]._listRow[0] );

    SW_EXPECT_EQUAL( 1u, list.collectGroups( "vfx", listGroup ) ); // 종류 이름으로도 찾는다
    SW_EXPECT_EQUAL( 0u, list.collectGroups( "nothing", listGroup ) );
    SW_EXPECT_TRUE( listGroup.empty() ); // 빈 머리를 남기지 않는다
}

// ---------------------------------------------------------------------- 열린 파일

// 방금 연 파일은 되쓸 "이전 글" 이 없다. 저장한 뒤에야 저장 직전 글로 돌아갈 수 있고, 고치던 글은 Undo 로 돌아온다.
SW_TEST_CASE( CatalogFileSessionTest, RestoreBringsBackTheTextBeforeTheSaveAndKeepsEdits )
{
    CatalogFileSession session;
    SW_EXPECT_FALSE( session.hasFile() );
    session.open( "game/a.xml", "Items", "<r x=\"1\"/>" );
    SW_EXPECT_TRUE( session.hasFile() );
    SW_EXPECT_FALSE( session.canRestorePrevious() );

    SW_REQUIRE_TRUE( session.editDocument().setCell( 0, "x", "2" ) );
    SW_REQUIRE_TRUE( session.checkDiskBeforeSave( "<r x=\"1\"/>" ) );
    session.onSaved();
    SW_EXPECT_FALSE( session.getDocument().isDirty() );
    SW_REQUIRE_TRUE( session.canRestorePrevious() );
    SW_EXPECT_TRUE( session.getPreviousText() == "<r x=\"1\"/>" );

    SW_REQUIRE_TRUE( session.editDocument().setCell( 0, "x", "3" ) ); // 아직 저장하지 않은 편집
    session.onRestored();
    SW_EXPECT_TRUE( session.getDocument().getText() == "<r x=\"1\"/>" );
    SW_EXPECT_FALSE( session.getDocument().isDirty() );
    SW_EXPECT_FALSE( session.canRestorePrevious() );
    session.editDocument().undo();
    SW_EXPECT_TRUE( session.getDocument().getText() == "<r x=\"3\"/>" ); // 잃지 않았다
}

// 밖에서 바뀐 파일은 첫 저장에서 멈추고 알린다. 다시 저장하면 덮어쓰되 밖의 글이 "이전 파일" 로 남는다.
SW_TEST_CASE( CatalogFileSessionTest, ASaveOverAnOutsideChangeStopsOnceAndKeepsThatText )
{
    CatalogFileSession session;
    session.open( "game/a.xml", "Items", "<r/>" );
    SW_EXPECT_TRUE( session.checkDiskBeforeSave( "<r/>" ) );
    SW_EXPECT_FALSE( session.checkDiskBeforeSave( "<outside/>" ) );
    SW_EXPECT_FALSE( session.getStatus().empty() );
    SW_EXPECT_FALSE( session.checkDiskBeforeSave( "<outside again/>" ) ); // 또 바뀌었으면 또 알린다
    SW_EXPECT_TRUE( session.checkDiskBeforeSave( "<outside again/>" ) );
    SW_REQUIRE_TRUE( session.editDocument().setCell( 0, "x", "1" ) );
    session.onSaved();
    SW_EXPECT_TRUE( session.getPreviousText() == "<outside again/>" );
    SW_EXPECT_TRUE( session.checkDiskBeforeSave( session.getDocument().getSavedText() ) );
    SW_EXPECT_FALSE( session.checkDiskBeforeSave( "<other/>" ) ); // 덮어쓴 뒤의 새 변경은 다시 알린다
}

SW_TEST_CASE( CatalogFileSessionTest, ValidationNeedsALoadWithoutWarningsAndSaysWhatItSaw )
{
    CatalogFileSession session;
    session.open( "game/a.xml", "Items", "<r/>" );
    SW_EXPECT_TRUE( session.getValidationState() == CatalogFileSession::ValidationState::None );

    session.onValidated( true, {} );
    SW_EXPECT_TRUE( session.getValidationState() == CatalogFileSession::ValidationState::Passed );
    SW_EXPECT_TRUE( session.getStatus().empty() );

    session.onValidated( true, { string( "unknown key" ) } );
    SW_EXPECT_TRUE( session.getValidationState() == CatalogFileSession::ValidationState::Failed );
    SW_EXPECT_EQUAL( size_t{ 1 }, session.getWarnings().size() );

    session.onValidated( false, {} );
    SW_EXPECT_TRUE( session.getValidationState() == CatalogFileSession::ValidationState::Failed );

    SW_REQUIRE_TRUE( session.editDocument().setCell( 0, "x", "1" ) );
    session.onValidated( true, {} );
    SW_EXPECT_TRUE( session.getStatus().find( "unsaved" ) != string::npos ); // 고치던 글은 검증에 들지 않았다

    session.open( "game/b.xml", "Items", "<r/>" ); // 다른 파일을 열면 앞 결과가 남지 않는다
    SW_EXPECT_TRUE( session.getValidationState() == CatalogFileSession::ValidationState::None );
    SW_EXPECT_TRUE( session.getWarnings().empty() );
}
