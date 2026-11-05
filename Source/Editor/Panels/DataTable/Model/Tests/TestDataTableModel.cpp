#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Panels/DataTable/Model/DataTableModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 임시 폴더의 표 파일 둘입니다. 끝나면 지웁니다. */
    struct DataTableTestFiles
    {
        GameDataFileEntry _first;
        GameDataFileEntry _second;

        DataTableTestFiles()
        {
            _first._fileName      = "SwDataTableFirst.xml";
            _first._absolutePath  = FileUtil::joinPath( FileUtil::getTempDirectory(), _first._fileName );
            _second._fileName     = "SwDataTableSecond.xml";
            _second._absolutePath = FileUtil::joinPath( FileUtil::getTempDirectory(), _second._fileName );
            SW_REQUIRE_TRUE( FileUtil::writeTextFile( _first._absolutePath, "<First a=\"1\"/>" ) );
            SW_REQUIRE_TRUE( FileUtil::writeTextFile( _second._absolutePath, "<Second b=\"2\"/>" ) );
        }

        ~DataTableTestFiles()
        {
            (void)FileUtil::removeFile( _first._absolutePath );
            (void)FileUtil::removeFile( _second._absolutePath );
        }

        static string read( const GameDataFileEntry& entry )
        {
            string text;
            SW_EXPECT_TRUE( FileUtil::readTextFile( entry._absolutePath, text ) );
            return text;
        }
    };
} // namespace

// ---------------------------------------------------------------------- 게임 데이터 파일

SW_TEST_CASE( DataTableModelTest, SelectingLoadsTheFile )
{
    const DataTableTestFiles files;
    DataTableModel           model;
    SW_EXPECT_TRUE( model.findSelectedGameDataFile() == nullptr );
    model.setGameDataFiles( { files._first, files._second } );
    model.selectGameDataFile( 1 );
    SW_EXPECT_EQUAL( 1, model.getSelectedGameDataIndex() );
    SW_EXPECT_TRUE( model.getGameDataRawText() == "<Second b=\"2\"/>" );
    SW_EXPECT_TRUE( model.getGameDataDocument().isParsed() );
    SW_EXPECT_FALSE( model.isGameDataDirty() );
    model.selectGameDataFile( 5 ); // 범위 밖은 그대로 둔다
    SW_EXPECT_EQUAL( 1, model.getSelectedGameDataIndex() );
}

// 고친 글은 어떤 선택으로도 말없이 사라지지 않는다.
SW_TEST_CASE( DataTableModelTest, SelectionNeverDiscardsAnEdit )
{
    const DataTableTestFiles files;
    DataTableModel           model;
    model.setGameDataFiles( { files._first, files._second } );
    model.selectGameDataFile( 0 );
    model.replaceGameDataRawText( "<First a=\"9\"/>" );
    SW_EXPECT_TRUE( model.isGameDataDirty() );

    model.selectGameDataFile( 1 );
    SW_EXPECT_EQUAL( 0, model.getSelectedGameDataIndex() );
    SW_EXPECT_TRUE( model.getGameDataRawText() == "<First a=\"9\"/>" );

    model.selectGameDataFile( 0 ); // 같은 파일을 다시 눌러도 디스크 내용으로 덮지 않는다
    SW_EXPECT_TRUE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( model.getGameDataRawText() == "<First a=\"9\"/>" );
}

// 파일이 늘거나 줄어도 고른 파일은 그 파일이다. 옛 번호를 들면 저장이 다른 파일을 덮어쓴다.
SW_TEST_CASE( DataTableModelTest, ReloadedListKeepsTheSelectionByPath )
{
    const DataTableTestFiles files;
    DataTableModel           model;
    model.setGameDataFiles( { files._first, files._second } );
    model.selectGameDataFile( 1 );
    model.replaceGameDataRawText( "<Second b=\"9\"/>" );

    model.setGameDataFiles( { files._second, files._first } ); // 순서가 바뀌었다
    SW_EXPECT_EQUAL( 0, model.getSelectedGameDataIndex() );
    SW_EXPECT_TRUE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( model.save() );
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._second ) == "<Second b=\"9\"/>" );
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._first ) == "<First a=\"1\"/>" );
}

SW_TEST_CASE( DataTableModelTest, VanishedFileLeavesNothingSelectedAndKeepsTheEdit )
{
    const DataTableTestFiles files;
    DataTableModel           model;
    model.setGameDataFiles( { files._first, files._second } );
    model.selectGameDataFile( 1 );
    model.replaceGameDataRawText( "<Second b=\"9\"/>" );

    model.setGameDataFiles( { files._first } );
    SW_EXPECT_EQUAL( -1, model.getSelectedGameDataIndex() );
    SW_EXPECT_TRUE( model.findSelectedGameDataFile() == nullptr );
    SW_EXPECT_FALSE( model.save() ); // 쓸 곳이 없다 — 표시가 남아 저장 확인이 다시 묻는다
    SW_EXPECT_TRUE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._first ) == "<First a=\"1\"/>" );
}

SW_TEST_CASE( DataTableModelTest, SaveWritesAndRevertRestores )
{
    const DataTableTestFiles files;
    DataTableModel           model;
    model.setGameDataFiles( { files._first } );
    model.selectGameDataFile( 0 );
    const uint32 loadCount = model.getGameDataLoadCount();

    model.replaceGameDataRawText( "<First a=\"5\"/>" );
    model.revert();
    SW_EXPECT_FALSE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( model.getGameDataRawText() == "<First a=\"1\"/>" );
    SW_EXPECT_TRUE( *model.getGameDataDocument().findCellValue( 0, "a" ) == "1" );
    SW_EXPECT_TRUE( model.getGameDataLoadCount() > loadCount ); // 패널이 표를 다시 읽는다

    model.replaceGameDataRawText( "<First a=\"6\"/>" );
    SW_EXPECT_TRUE( model.save() );
    SW_EXPECT_FALSE( model.isAnyDirty() );
    SW_EXPECT_FALSE( model.getGameDataDocument().isDirty() );
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._first ) == "<First a=\"6\"/>" );

    model.replaceGameDataRawText( "<First a=\"7\"/>" );
    model.revert();
    SW_EXPECT_TRUE( model.getGameDataRawText() == "<First a=\"6\"/>" ); // 되돌리기는 저장한 글로 간다
}

SW_TEST_CASE( DataTableModelTest, TableEditUpdatesTheRawText )
{
    const DataTableTestFiles files;
    DataTableModel           model;
    model.setGameDataFiles( { files._first } );
    model.selectGameDataFile( 0 );
    SW_REQUIRE_TRUE( model.editGameDataDocument().setCell( 0, "a", "3" ) );
    model.commitGameDataTableEdit();
    SW_EXPECT_TRUE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( model.getGameDataRawText() == "<First a=\"3\"/>" );
}

// 읽지 못한 파일은 고르지 않는다. 앞 파일의 글을 든 채 고르면 저장이 앞 파일의 내용을 이 파일에 쓴다.
SW_TEST_CASE( DataTableModelTest, UnreadableFileIsNotSelected )
{
    SW_EXPECT_LOG( "File.NotFound" );
    SW_EXPECT_LOG( "DataTablePanel.CouldNotReadGame" );
    const DataTableTestFiles files;
    GameDataFileEntry        missing;
    missing._fileName     = "SwDataTableMissing.xml";
    missing._absolutePath = FileUtil::joinPath( FileUtil::getTempDirectory(), missing._fileName );
    DataTableModel model;
    model.setGameDataFiles( { files._first, missing } );
    model.selectGameDataFile( 0 );
    model.selectGameDataFile( 1 );
    SW_EXPECT_EQUAL( -1, model.getSelectedGameDataIndex() );
    SW_EXPECT_TRUE( model.getGameDataRawText().empty() );
    SW_EXPECT_FALSE( FileUtil::exists( missing._absolutePath ) );
}

// ---------------------------------------------------------------------- 현지화 표

SW_TEST_CASE( DataTableModelTest, KeysAreUniqueAndNeedATable )
{
    DataTableModel model;
    SW_EXPECT_FALSE( model.addKey( "hello" ) ); // 표가 없다
    SW_EXPECT_FALSE( model.isLocalizationDirty() );

    model.editSheet()._listTablePath.push_back( "strings.csv" );
    model.editSheet()._listCulture.push_back( "ko" );
    SW_EXPECT_FALSE( model.addKey( "" ) );
    SW_REQUIRE_TRUE( model.addKey( "hello" ) );
    SW_EXPECT_TRUE( model.isLocalizationDirty() );
    SW_EXPECT_FALSE( model.addKey( "hello" ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getSheet()._listRecord.size() );
    const LocalizationRecord& record = model.getSheet()._listRecord[0];
    SW_EXPECT_TRUE( record._bModified );
    SW_EXPECT_EQUAL( size_t{ 1 }, record._listTranslation.size() ); // 문화권마다 한 칸
    SW_EXPECT_TRUE( record._listState[0] == TranslationState::Missing );
}

SW_TEST_CASE( DataTableModelTest, DeletedKeyIsRememberedForTheSave )
{
    DataTableModel model;
    model.editSheet()._listTablePath.push_back( "strings.csv" );
    SW_REQUIRE_TRUE( model.addKey( "a" ) );
    SW_REQUIRE_TRUE( model.addKey( "b" ) );
    model.deleteRecord( 0 );
    model.deleteRecord( 9 ); // 범위 밖은 그대로 둔다
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getSheet()._listRecord.size() );
    SW_EXPECT_TRUE( model.getSheet()._listRecord[0]._key == "b" );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getSheet()._listRemovedKey.size() );
    SW_EXPECT_TRUE( model.getSheet()._listRemovedKey[0] == "a" );
    SW_EXPECT_TRUE( model.addKey( "a" ) ); // 지운 키를 다시 쓸 수 있다
}

SW_TEST_CASE( DataTableModelTest, FilterLooksAtKeySourceAndTranslations )
{
    DataTableModel model;
    model.editSheet()._listTablePath.push_back( "strings.csv" );
    model.editSheet()._listCulture.push_back( "ko" );
    SW_REQUIRE_TRUE( model.addKey( "menu.start" ) );
    SW_REQUIRE_TRUE( model.addKey( "menu.quit" ) );
    SW_REQUIRE_TRUE( model.addKey( "other" ) );
    model.editSheet()._listRecord[1]._source             = "Leave game";
    model.editSheet()._listRecord[2]._listTranslation[0] = "시작";

    vector<size_t> listIndex;
    model.collectVisibleRecords( EditorListFilter{ string_view( "menu" ) }, listIndex );
    SW_EXPECT_EQUAL( size_t{ 2 }, listIndex.size() );
    model.collectVisibleRecords( EditorListFilter{ string_view( "leave" ) }, listIndex );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listIndex.size() );
    SW_EXPECT_EQUAL( size_t{ 1 }, listIndex[0] );
    model.collectVisibleRecords( EditorListFilter{ string_view( "시작" ) }, listIndex );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listIndex.size() );
    SW_EXPECT_EQUAL( size_t{ 2 }, listIndex[0] );
    model.collectVisibleRecords( EditorListFilter{ string_view( "" ) }, listIndex );
    SW_EXPECT_EQUAL( size_t{ 3 }, listIndex.size() );
}
