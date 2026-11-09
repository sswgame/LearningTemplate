#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Asset/EditorDataTableCommands.h"
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
            _first._absolutePath  = test::makeScratchPath( _first._fileName );
            _second._fileName     = "SwDataTableSecond.xml";
            _second._absolutePath = test::makeScratchPath( _second._fileName );
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

    /** @brief 문화권 ko 하나에 키를 더합니다. */
    struct DataTableSheetTestUtil
    {
        static LocalizationRecord& addRecord( DataTableModel& model, string_view key, string_view translation, TranslationState state )
        {
            LocalizationSheet& sheet = model.editSheet();
            if ( sheet._listCulture.empty() )
            {
                sheet._listCulture.push_back( "ko" );
                sheet._listTablePath.push_back( "strings.xml" );
            }
            SW_EXPECT_TRUE( model.addKey( key ) );
            LocalizationRecord& record = sheet._listRecord.back();
            record._listTranslation[0] = translation;
            record._listState[0]       = state;
            return record;
        }
    };
    using SheetUtil = DataTableSheetTestUtil;
} // namespace

// ---------------------------------------------------------------------- 게임 데이터 파일

SW_TEST_CASE( DataTableModelTest, OpeningLoadsTheFile )
{
    const DataTableTestFiles files;
    DataTableModel           model;
    SW_EXPECT_TRUE( model.getGameDataPath().empty() );
    SW_REQUIRE_TRUE( model.openGameDataFile( files._second._absolutePath ) );
    SW_EXPECT_TRUE( model.getGameDataPath() == files._second._absolutePath );
    SW_EXPECT_TRUE( model.getGameDataRawText() == "<Second b=\"2\"/>" );
    SW_EXPECT_TRUE( model.getGameDataDocument().isParsed() );
    SW_EXPECT_FALSE( model.isGameDataDirty() );
}

// 연 파일은 경로로 연 그 파일이다 — 목록이 늘거나 줄거나 순서가 바뀌어도 저장은 그 파일에 쓴다(옛 번호를 들면 다른 파일을 덮어썼다).
SW_TEST_CASE( DataTableModelTest, ReloadedListKeepsTheOpenFile )
{
    const DataTableTestFiles files;
    DataTableModel           model;
    model.setGameDataFiles( { files._first, files._second } );
    SW_REQUIRE_TRUE( model.openGameDataFile( files._second._absolutePath ) );
    model.replaceGameDataRawText( "<Second b=\"9\"/>" );

    model.setGameDataFiles( { files._first } ); // 연 파일이 목록에서 빠졌다
    SW_EXPECT_TRUE( model.getGameDataPath() == files._second._absolutePath );
    SW_EXPECT_TRUE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( model.saveDocument() );
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._second ) == "<Second b=\"9\"/>" );
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._first ) == "<First a=\"1\"/>" );
}

// 다시 열기(문서 패널의 다시 읽기 · "저장 안 함")는 디스크의 글로 간다. 저장은 dirty 와 무관하게 연 파일을 쓴다(저장 왕복).
SW_TEST_CASE( DataTableModelTest, SaveWritesAndReopenRestores )
{
    const DataTableTestFiles files;
    DataTableModel           model;
    SW_REQUIRE_TRUE( model.openGameDataFile( files._first._absolutePath ) );
    const uint32 loadCount = model.getGameDataLoadCount();

    model.replaceGameDataRawText( "<First a=\"5\"/>" );
    SW_REQUIRE_TRUE( model.openGameDataFile( files._first._absolutePath ) );
    SW_EXPECT_FALSE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( model.getGameDataRawText() == "<First a=\"1\"/>" );
    SW_EXPECT_TRUE( *model.getGameDataDocument().findCellValue( 0, "a" ) == "1" );
    SW_EXPECT_TRUE( model.getGameDataLoadCount() > loadCount ); // 패널이 표를 다시 읽는다

    model.replaceGameDataRawText( "<First a=\"6\"/>" );
    SW_EXPECT_TRUE( model.saveDocument() );
    SW_EXPECT_FALSE( model.isAnyDirty() );
    SW_EXPECT_FALSE( model.getGameDataDocument().isDirty() );
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._first ) == "<First a=\"6\"/>" );

    SW_EXPECT_TRUE( model.saveDocument() ); // 고친 것이 없어도 쓴다 — 같은 글이다
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._first ) == "<First a=\"6\"/>" );
}

SW_TEST_CASE( DataTableModelTest, TableEditUpdatesTheRawText )
{
    const DataTableTestFiles files;
    DataTableModel           model;
    SW_REQUIRE_TRUE( model.openGameDataFile( files._first._absolutePath ) );
    SW_REQUIRE_TRUE( model.editGameDataDocument().setCell( 0, "a", "3" ) );
    model.commitGameDataTableEdit();
    SW_EXPECT_TRUE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( model.getGameDataRawText() == "<First a=\"3\"/>" );
}

// 읽지 못한 파일은 열지 않는다. 앞 파일의 글을 든 채 열면 저장이 앞 파일의 내용을 이 파일에 쓴다.
SW_TEST_CASE( DataTableModelTest, UnreadableFileIsNotOpened )
{
    SW_EXPECT_LOG( "File.NotFound" );
    SW_EXPECT_LOG( "DataTablePanel.CouldNotReadGame" );
    const DataTableTestFiles files;
    const string             missingPath = test::makeScratchPath( "SwDataTableMissing.xml" );
    DataTableModel           model;
    SW_REQUIRE_TRUE( model.openGameDataFile( files._first._absolutePath ) );
    SW_EXPECT_FALSE( model.openGameDataFile( missingPath ) );
    SW_EXPECT_TRUE( model.getGameDataPath().empty() );
    SW_EXPECT_TRUE( model.getGameDataRawText().empty() );
    SW_EXPECT_FALSE( model.saveGameDataFile() ); // 쓸 곳이 없다
    SW_EXPECT_FALSE( FileUtil::exists( missingPath ) );
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._first ) == "<First a=\"1\"/>" );
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

// ---------------------------------------------------------------------- 게임 데이터 원문

// 결함: 깨진 XML 도 그대로 저장되어 게임이 다음에 그 파일을 읽지 못했다.
SW_TEST_CASE( DataTableModelTest, BrokenXMLIsNotSaved )
{
    SW_EXPECT_LOG( "DataTablePanel.GameDataNotValidXML" );
    const DataTableTestFiles files;
    DataTableModel           model;
    SW_REQUIRE_TRUE( model.openGameDataFile( files._first._absolutePath ) );
    model.replaceGameDataRawText( "<First a=" );
    SW_EXPECT_FALSE( model.getGameDataDocument().isParsed() );
    SW_EXPECT_FALSE( model.saveDocument() );
    SW_EXPECT_TRUE( model.isGameDataDirty() ); // 고친 글은 남는다 — 글 상자에서 고쳐 다시 저장한다
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._first ) == "<First a=\"1\"/>" );
    model.replaceGameDataRawText( "<First a=\"2\"/>" );
    SW_EXPECT_TRUE( model.saveDocument() );
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._first ) == "<First a=\"2\"/>" );
}

// 글 상자는 모델의 글을 바로 고치고(길이 상한 없음) 알린다.
SW_TEST_CASE( DataTableModelTest, RawEditInPlaceUpdatesTheDocument )
{
    const DataTableTestFiles files;
    DataTableModel           model;
    SW_REQUIRE_TRUE( model.openGameDataFile( files._first._absolutePath ) );
    model.editGameDataRawText() = "<First a=\"8\"/>";
    model.commitGameDataRawEdit();
    SW_EXPECT_TRUE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( *model.getGameDataDocument().findCellValue( 0, "a" ) == "8" );
}

// ---------------------------------------------------------------------- 현지화 상태

// 결함: 칸을 고쳐도 상태가 읽은 때 그대로라, 고친 번역이 저장한 뒤에도 "낡음" 색으로 남았다.
SW_TEST_CASE( DataTableModelTest, EditingUpdatesTheTranslationState )
{
    DataTableModel      model;
    LocalizationRecord& record = SheetUtil::addRecord( model, "a", "옛 글", TranslationState::Stale );

    record._listTranslation[0] = "새 글";
    model.markTranslationEdited( record, 0 );
    SW_EXPECT_TRUE( record._listState[0] == TranslationState::Current );
    SW_EXPECT_TRUE( record._bModified );

    model.markSourceEdited( record ); // 번역이 있던 원문을 고치면 낡은 것이 된다
    SW_EXPECT_TRUE( record._listState[0] == TranslationState::Stale );

    record._listTranslation[0].clear();
    model.markTranslationEdited( record, 0 );
    SW_EXPECT_TRUE( record._listState[0] == TranslationState::Missing );
    model.markSourceEdited( record ); // 없는 번역은 낡을 수 없다
    SW_EXPECT_TRUE( record._listState[0] == TranslationState::Missing );

    LocalizationRecord& review = SheetUtil::addRecord( model, "b", "글", TranslationState::Review );
    model.markSourceEdited( review ); // 검토 표시는 남는다
    SW_EXPECT_TRUE( review._listState[0] == TranslationState::Review );
}

SW_TEST_CASE( DataTableModelTest, ConfirmOnlyTouchesStaleAndReview )
{
    DataTableModel model;
    (void)SheetUtil::addRecord( model, "stale", "글", TranslationState::Stale );
    (void)SheetUtil::addRecord( model, "missing", "", TranslationState::Missing );
    LocalizationSheet& sheet        = model.editSheet();
    sheet._listRecord[1]._bModified = false;

    model.confirmTranslation( sheet._listRecord[1], 0 );
    SW_EXPECT_TRUE( sheet._listRecord[1]._listState[0] == TranslationState::Missing );
    SW_EXPECT_FALSE( sheet._listRecord[1]._bModified );

    model.confirmTranslation( sheet._listRecord[0], 0 );
    SW_EXPECT_TRUE( sheet._listRecord[0]._listState[0] == TranslationState::Current );
    SW_EXPECT_EQUAL( uint32{ 1 }, model.countCurrentTranslations( 0 ) );
}

// 결함: 원문이 바뀐 뒤 번역이 그대로 맞아도 낡음 표시를 걷을 길이 없었다(글이 같으면 저장이 해시를 건드리지 않았다).
SW_TEST_CASE( DataTableModelTest, ConfirmedTranslationIsSavedAsCurrent )
{
    const string folder      = test::makeScratchPath( "SwDataTableConfirm" );
    const string projectPath = FileUtil::joinPath( folder, "t.locproject.xml" );
    const string oldHash     = SourceTextEntry::formatSourceHash( SourceTextEntry::computeSourceHash( "Older" ) );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( projectPath, "<LocalizationProject name=\"t\" sourceCulture=\"en\"><cultures><item>ko</item></cultures><stringTables><item>t.strings.xml</item></stringTables></LocalizationProject>" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "t.strings.xml" ), "<SourceStringTable culture=\"en\"><entries><entry key=\"a\" source=\"Old\" /></entries></SourceStringTable>" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "ko.translation.xml" ), string( "<TranslationTable culture=\"ko\"><entries><entry key=\"a\" text=\"글\" sourceHash=\"" ) + oldHash + "\" /></entries></TranslationTable>" ) );

    DataTableModel model;
    SW_REQUIRE_TRUE( EditorDataTableCommands::loadLocalizationProject( projectPath, model.editSheet() ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getSheet()._listRecord.size() );
    SW_REQUIRE_TRUE( model.getSheet()._listRecord[0]._listState[0] == TranslationState::Stale );

    model.confirmTranslation( model.editSheet()._listRecord[0], 0 );
    SW_EXPECT_TRUE( model.isLocalizationDirty() );
    model.saveLocalization();
    SW_EXPECT_FALSE( model.isLocalizationDirty() );

    LocalizationSheet saved;
    SW_REQUIRE_TRUE( EditorDataTableCommands::loadLocalizationProject( projectPath, saved ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, saved._listRecord.size() );
    SW_EXPECT_TRUE( saved._listRecord[0]._listState[0] == TranslationState::Current );
    SW_EXPECT_TRUE( saved._listRecord[0]._listTranslation[0] == "글" );
    (void)FileUtil::removeDirectory( folder );
}

// 할 일만 보기: 없음 · 낡음 · 검토가 남은 행만 지난다. 검색과 함께 걸린다.
SW_TEST_CASE( DataTableModelTest, NeedsWorkFilterSkipsFinishedRows )
{
    DataTableModel model;
    (void)SheetUtil::addRecord( model, "menu.done", "끝", TranslationState::Current );
    (void)SheetUtil::addRecord( model, "menu.stale", "낡음", TranslationState::Stale );
    (void)SheetUtil::addRecord( model, "other.missing", "", TranslationState::Missing );

    vector<size_t> listIndex;
    model.collectVisibleRecords( EditorListFilter{ string_view( "" ) }, listIndex, LocalizationRowFilter::NeedsWork );
    SW_EXPECT_EQUAL( size_t{ 2 }, listIndex.size() );
    model.collectVisibleRecords( EditorListFilter{ string_view( "menu" ) }, listIndex, LocalizationRowFilter::NeedsWork );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listIndex.size() );
    SW_EXPECT_EQUAL( size_t{ 1 }, listIndex[0] );
}

SW_TEST_CASE( DataTableModelTest, TranslationLengthCountsCharactersNotBytes )
{
    LocalizationRecord record;
    record._listTranslation.push_back( "가나다" ); // 9 바이트
    SW_EXPECT_EQUAL( uint32{ 3 }, record.measureTranslation( 0 ) );
    SW_EXPECT_FALSE( record.isTranslationTooLong( 0 ) ); // 최대 길이 0 은 제한 없음
    record._maxLength = 3;
    SW_EXPECT_FALSE( record.isTranslationTooLong( 0 ) );
    record._maxLength = 2;
    SW_EXPECT_TRUE( record.isTranslationTooLong( 0 ) );
}

// 결함: 읽기가 도는 사이 다른 프로젝트를 고르면 앞 프로젝트의 표를 받아, 콤보와 표 · 저장 대상이 어긋났다.
SW_TEST_CASE( DataTableModelTest, SheetOfAnotherProjectIsDiscarded )
{
    DataTableModel model;
    model.setLocalizationProjects( { "a.locproject", "b.locproject" } );
    model.selectProject( 1 );
    SW_EXPECT_EQUAL( 1, model.getSelectedProjectIndex() );

    LocalizationSheet stale;
    stale._projectPath = "a.locproject";
    SW_EXPECT_FALSE( model.acceptLoadedSheet( 0, std::move( stale ) ) );
    SW_EXPECT_FALSE( model.isLocalizationLoaded() );

    LocalizationSheet current;
    current._projectPath = "b.locproject";
    SW_EXPECT_TRUE( model.acceptLoadedSheet( 1, std::move( current ) ) );
    SW_EXPECT_TRUE( model.isLocalizationLoaded() );
    SW_EXPECT_TRUE( model.getSheet()._projectPath == "b.locproject" );

    model.setLocalizationProjects( { "c.locproject" } ); // 고른 번호가 목록 밖이면 0 번이다
    SW_EXPECT_EQUAL( 0, model.getSelectedProjectIndex() );
}

// 결함: 읽는 동안 앞 표에 키를 더할 수 있었고, 그러면 dirty 라 읽은 결과를 버리기만 해 표가 영영 읽히지 않았다.
SW_TEST_CASE( DataTableModelTest, ReloadEmptiesTheSheetUntilItIsRead )
{
    DataTableModel model;
    model.editSheet()._listTablePath.push_back( "strings.xml" );
    SW_REQUIRE_TRUE( model.addKey( "a" ) );
    model.reloadLocalization();
    SW_EXPECT_FALSE( model.isLocalizationDirty() );
    SW_EXPECT_TRUE( model.getSheet()._listRecord.empty() );
    SW_EXPECT_FALSE( model.addKey( "b" ) );
    SW_EXPECT_FALSE( model.isLocalizationDirty() );
}
