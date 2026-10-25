// 카탈로그 표 편집기의 문서 — 원소 이름별 표(행 · 열), 셀 · 행 편집과 되돌리기 · 다시 하기, 저장 표시, 깨진 원문.
#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogDocument.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    constexpr const utf8* kCatalog =
        "<ItemCatalog>\n"
        "\t<!-- weapons -->\n"
        "\t<Item id=\"sword\" value=\"40\" />\n"
        "\t<Item id=\"bow\" value=\"25\" rarity=\"1\" />\n"
        "\t<Set id=\"knight\" pieces=\"4\" />\n"
        "</ItemCatalog>\n";
} // namespace

SW_TEST_CASE( CatalogDocumentTest, LoadGroupsRowsByElementNameWithTheUnionOfColumns )
{
    CatalogDocument document;
    SW_ASSERT_TRUE( document.load( kCatalog ) );
    SW_EXPECT_EQUAL( document.getRootName(), string( "ItemCatalog" ) );
    SW_ASSERT_EQUAL( document.getGroups().size(), size_t( 2 ) );
    const CatalogDocument::Group& items = document.getGroups()[0];
    SW_EXPECT_EQUAL( items._name, string( "Item" ) );
    SW_EXPECT_EQUAL( items._listRow.size(), size_t( 2 ) );
    SW_ASSERT_EQUAL( items._listColumn.size(), size_t( 3 ) );
    SW_EXPECT_EQUAL( items._listColumn[2], string( "rarity" ) ); // 둘째 행에만 있는 속성도 열이 된다
    SW_EXPECT_EQUAL( document.getGroups()[1]._name, string( "Set" ) );
    SW_EXPECT_FALSE( document.isDirty() );
    const string* pValue = document.findCellValue( items._listRow[1], "rarity" );
    SW_ASSERT_TRUE( pValue != nullptr );
    SW_EXPECT_EQUAL( *pValue, string( "1" ) );
    SW_EXPECT_TRUE( document.findCellValue( items._listRow[0], "rarity" ) == nullptr ); // 없는 칸은 비어 있는 것과 다르다
}

SW_TEST_CASE( CatalogDocumentTest, SetCellEditsOnlyThatValueAndMarksDirty )
{
    CatalogDocument document;
    SW_ASSERT_TRUE( document.load( kCatalog ) );
    const int32 swordRow = document.getGroups()[0]._listRow[0];
    SW_ASSERT_TRUE( document.setCell( swordRow, "value", "55" ) );
    SW_EXPECT_TRUE( document.isDirty() );
    SW_EXPECT_TRUE( document.getText().find( "<!-- weapons -->" ) != string::npos ); // 주석이 남는다
    SW_EXPECT_TRUE( document.getText().find( "<Item id=\"sword\" value=\"55\" />" ) != string::npos );

    // 새 속성은 그 행에만 생긴다
    const int32 newSwordRow = document.getGroups()[0]._listRow[0];
    SW_ASSERT_TRUE( document.setCell( newSwordRow, "rarity", "3" ) );
    SW_EXPECT_TRUE( document.getText().find( "<Item id=\"sword\" value=\"55\" rarity=\"3\" />" ) != string::npos );
    SW_EXPECT_TRUE( document.unsetCell( document.getGroups()[0]._listRow[0], "rarity" ) );
    SW_EXPECT_TRUE( document.getText().find( "<Item id=\"sword\" value=\"55\" />" ) != string::npos );
    SW_EXPECT_FALSE( document.unsetCell( document.getGroups()[0]._listRow[0], "rarity" ) ); // 이미 없다
    SW_EXPECT_FALSE( document.getError().empty() );
}

SW_TEST_CASE( CatalogDocumentTest, AddRowFollowsTheGroupColumnsAndRemoveRowTakesTheLine )
{
    CatalogDocument document;
    SW_ASSERT_TRUE( document.load( kCatalog ) );
    SW_ASSERT_TRUE( document.addRow( "Item" ) );
    const CatalogDocument::Group& items = document.getGroups()[0];
    SW_ASSERT_EQUAL( items._listRow.size(), size_t( 3 ) );
    const int32 added = items._listRow[2];
    SW_ASSERT_TRUE( document.findCellValue( added, "id" ) != nullptr );
    SW_EXPECT_EQUAL( *document.findCellValue( added, "id" ), string( "new" ) );
    SW_ASSERT_TRUE( document.findCellValue( added, "value" ) != nullptr );
    SW_EXPECT_EQUAL( *document.findCellValue( added, "value" ), string( "" ) ); // 열은 따라가지만 값은 비어 있다
    SW_EXPECT_TRUE( document.findCellValue( added, "rarity" ) != nullptr );

    SW_ASSERT_TRUE( document.removeRow( added ) );
    SW_EXPECT_EQUAL( document.getText(), string( kCatalog ) ); // 더했다 뺐더니 원문 그대로다
    SW_EXPECT_FALSE( document.isDirty() );

    SW_ASSERT_TRUE( document.addRow( "NewKind" ) ); // 표가 없던 원소 이름은 id 하나만 가진다
    SW_EXPECT_EQUAL( document.getGroups().size(), size_t( 3 ) );
}

SW_TEST_CASE( CatalogDocumentTest, UndoAndRedoWalkTheEditHistory )
{
    CatalogDocument document;
    SW_ASSERT_TRUE( document.load( kCatalog ) );
    const int32 row = document.getGroups()[0]._listRow[0];
    SW_EXPECT_FALSE( document.canUndo() );
    SW_ASSERT_TRUE( document.setCell( row, "value", "1" ) );
    SW_ASSERT_TRUE( document.setCell( document.getGroups()[0]._listRow[0], "value", "2" ) );
    SW_EXPECT_TRUE( document.canUndo() );
    document.undo();
    SW_EXPECT_TRUE( document.getText().find( "value=\"1\"" ) != string::npos );
    document.undo();
    SW_EXPECT_EQUAL( document.getText(), string( kCatalog ) );
    SW_EXPECT_FALSE( document.isDirty() );
    SW_EXPECT_TRUE( document.canRedo() );
    document.redo();
    SW_EXPECT_TRUE( document.getText().find( "value=\"1\"" ) != string::npos );
    SW_ASSERT_TRUE( document.setCell( document.getGroups()[0]._listRow[0], "value", "9" ) );
    SW_EXPECT_FALSE( document.canRedo() ); // 새 편집은 다시 하기를 지운다
}

SW_TEST_CASE( CatalogDocumentTest, MarkSavedAndRevert )
{
    CatalogDocument document;
    SW_ASSERT_TRUE( document.load( kCatalog ) );
    SW_ASSERT_TRUE( document.setCell( document.getGroups()[0]._listRow[0], "value", "77" ) );
    SW_EXPECT_TRUE( document.isDirty() );
    document.markSaved();
    SW_EXPECT_FALSE( document.isDirty() );
    SW_ASSERT_TRUE( document.setCell( document.getGroups()[0]._listRow[0], "value", "78" ) );
    document.revert();
    SW_EXPECT_TRUE( document.getText().find( "value=\"77\"" ) != string::npos ); // 저장된 원문으로
    SW_EXPECT_FALSE( document.canUndo() );
}

SW_TEST_CASE( CatalogDocumentTest, BrokenTextKeepsTheTextButShowsNoTable )
{
    CatalogDocument document;
    SW_EXPECT_FALSE( document.load( "<ItemCatalog><Item id=\"a\"></ItemCatalog>" ) );
    SW_EXPECT_FALSE( document.isParsed() );
    SW_EXPECT_FALSE( document.getError().empty() );
    SW_EXPECT_TRUE( document.getGroups().empty() );
    SW_EXPECT_FALSE( document.setCell( 0, "id", "b" ) ); // 훑지 못한 문서는 셀을 못 고친다

    document.replaceText( kCatalog, true ); // 글 상자로 고치면 표가 돌아온다
    SW_EXPECT_TRUE( document.isParsed() );
    SW_EXPECT_EQUAL( document.getGroups().size(), size_t( 2 ) );
    SW_EXPECT_TRUE( document.isDirty() ); // 읽은 원문과 다르다
}
