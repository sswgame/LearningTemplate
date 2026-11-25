#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Commands/Asset/EditorDataTableCommands.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
#include "Editor/Panels/DataTable/Model/DataTableViewModel.h"

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
        static LocalizationRecord& addRecord( DataTableViewModel& model, string_view key, string_view translation, TranslationState state )
        {
            LocalizationSheet& sheet = model.editSheet();
            if ( sheet._listCulture.empty() )
            {
                sheet._listCulture.push_back( "ko" );
                sheet._listTablePath.push_back( test::makeScratchPath( "strings.xml" ) );
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

SW_TEST_CASE( DataTableViewModelTest, OpeningLoadsTheFile )
{
    const DataTableTestFiles   files;
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    SW_EXPECT_TRUE( model.getGameDataPath().empty() );
    SW_REQUIRE_TRUE( model.openGameDataFile( files._second._absolutePath ) );
    SW_EXPECT_TRUE( model.getGameDataPath() == files._second._absolutePath.str() );
    SW_EXPECT_TRUE( model.getGameDataRawText() == "<Second b=\"2\"/>" );
    SW_EXPECT_TRUE( model.getGameDataDocument().isParsed() );
    SW_EXPECT_FALSE( model.isGameDataDirty() );
}

// 연 파일은 경로로 연 그 파일이다 — 목록이 늘거나 줄거나 순서가 바뀌어도 저장은 그 파일에 쓴다(옛 번호를 들면 다른 파일을 덮어썼다).
SW_TEST_CASE( DataTableViewModelTest, ReloadedListKeepsTheOpenFile )
{
    const DataTableTestFiles   files;
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    model.setGameDataFiles( { files._first, files._second } );
    SW_REQUIRE_TRUE( model.openGameDataFile( files._second._absolutePath ) );
    model.replaceGameDataRawText( "<Second b=\"9\"/>" );

    model.setGameDataFiles( { files._first } ); // 연 파일이 목록에서 빠졌다
    SW_EXPECT_TRUE( model.getGameDataPath() == files._second._absolutePath.str() );
    SW_EXPECT_TRUE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( model.saveDocument() );
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._second ) == "<Second b=\"9\"/>" );
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._first ) == "<First a=\"1\"/>" );
}

// 다시 열기(문서 패널의 다시 읽기 · "저장 안 함")는 디스크의 글로 간다. 저장은 dirty 와 무관하게 연 파일을 쓴다(저장 왕복).
SW_TEST_CASE( DataTableViewModelTest, SaveWritesAndReopenRestores )
{
    const DataTableTestFiles   files;
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
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

SW_TEST_CASE( DataTableViewModelTest, TableEditUpdatesTheRawText )
{
    const DataTableTestFiles   files;
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    SW_REQUIRE_TRUE( model.openGameDataFile( files._first._absolutePath ) );
    SW_REQUIRE_TRUE( model.editGameDataDocument().setCell( 0, "a", "3" ) );
    model.commitGameDataTableEdit();
    SW_EXPECT_TRUE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( model.getGameDataRawText() == "<First a=\"3\"/>" );
}

// 읽지 못한 파일은 열지 않는다. 앞 파일의 글을 든 채 열면 저장이 앞 파일의 내용을 이 파일에 쓴다.
SW_TEST_CASE( DataTableViewModelTest, UnreadableFileIsNotOpened )
{
    SW_EXPECT_LOG( "File.NotFound" );
    SW_EXPECT_LOG( "DataTablePanel.CouldNotReadGame" );
    const DataTableTestFiles   files;
    const AbsolutePath         missingPath = test::makeScratchPath( "SwDataTableMissing.xml" );
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    SW_REQUIRE_TRUE( model.openGameDataFile( files._first._absolutePath ) );
    SW_EXPECT_FALSE( model.openGameDataFile( missingPath ) );
    SW_EXPECT_TRUE( model.getGameDataPath().empty() );
    SW_EXPECT_TRUE( model.getGameDataRawText().empty() );
    SW_EXPECT_FALSE( model.saveGameDataFile() ); // 쓸 곳이 없다
    SW_EXPECT_FALSE( FileUtil::exists( missingPath ) );
    SW_EXPECT_TRUE( DataTableTestFiles::read( files._first ) == "<First a=\"1\"/>" );
}

// ---------------------------------------------------------------------- 현지화 표

SW_TEST_CASE( DataTableViewModelTest, KeysAreUniqueAndNeedATable )
{
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    SW_EXPECT_FALSE( model.addKey( "hello" ) ); // 표가 없다
    SW_EXPECT_FALSE( model.isLocalizationDirty() );

    model.editSheet()._listTablePath.push_back( test::makeScratchPath( "strings.csv" ) );
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

// 지우기 · 더하기 · 번역 편집은 Ctrl+Z 로 되돌아온다 — 끌기(글 치기) 하나가 한 단계이고, 지운 키는 제자리로 돌아오며 저장이 지우지 않는다.
SW_TEST_CASE( DataTableViewModelTest, LocalizationEditsUndoPerRecord )
{
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    (void)SheetUtil::addRecord( model, "a", "에이", TranslationState::Current );
    (void)SheetUtil::addRecord( model, "b", "비", TranslationState::Current );
    LocalizationSheet& sheet     = model.editSheet();
    const size_t       stepCount = model.getLocalizationStack().getCommandCount();

    model.beginRecordEdit( sheet._listRecord[0] );
    sheet._listRecord[0]._listTranslation[0] = "에";
    model.markTranslationEdited( sheet._listRecord[0], 0 );
    sheet._listRecord[0]._listTranslation[0] = "새 글";
    model.markTranslationEdited( sheet._listRecord[0], 0 );
    model.flushRecordEdit( true ); // 아직 치는 중이다
    SW_EXPECT_EQUAL( stepCount, model.getLocalizationStack().getCommandCount() );
    model.flushRecordEdit( false );
    SW_EXPECT_EQUAL( stepCount + 1, model.getLocalizationStack().getCommandCount() );

    model.deleteRecord( 0 );
    SW_REQUIRE_EQUAL( size_t{ 1 }, sheet._listRecord.size() );
    SW_EXPECT_TRUE( model.getLocalizationStack().undo() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, sheet._listRecord.size() );
    SW_EXPECT_TRUE( sheet._listRecord[0]._key == "a" );
    SW_EXPECT_TRUE( sheet._listRecord[0]._listTranslation[0] == "새 글" );
    SW_EXPECT_TRUE( sheet._listRemovedKey.empty() );
    SW_EXPECT_TRUE( model.getLocalizationStack().undo() );
    SW_EXPECT_TRUE( sheet._listRecord[0]._listTranslation[0] == "에이" );
    SW_EXPECT_TRUE( model.getLocalizationStack().redo() );
    SW_EXPECT_TRUE( sheet._listRecord[0]._listTranslation[0] == "새 글" );
    SW_EXPECT_EQUAL( 2u, model.countCultureProgress( 0 )._currentCount );

    model.reloadLocalization(); // 다시 읽으면 단계가 가리키던 표가 없다
    SW_EXPECT_EQUAL( size_t{ 0 }, model.getLocalizationStack().getCommandCount() );
}

SW_TEST_CASE( DataTableViewModelTest, DeletedKeyIsRememberedForTheSave )
{
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    model.editSheet()._listTablePath.push_back( test::makeScratchPath( "strings.csv" ) );
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

SW_TEST_CASE( DataTableViewModelTest, FilterLooksAtKeySourceAndTranslations )
{
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    model.editSheet()._listTablePath.push_back( test::makeScratchPath( "strings.csv" ) );
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
SW_TEST_CASE( DataTableViewModelTest, BrokenXMLIsNotSaved )
{
    SW_EXPECT_LOG( "DataTablePanel.GameDataNotValidXML" );
    const DataTableTestFiles   files;
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
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
SW_TEST_CASE( DataTableViewModelTest, RawEditInPlaceUpdatesTheDocument )
{
    const DataTableTestFiles   files;
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    SW_REQUIRE_TRUE( model.openGameDataFile( files._first._absolutePath ) );
    model.editGameDataRawText() = "<First a=\"8\"/>";
    model.commitGameDataRawEdit();
    SW_EXPECT_TRUE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( *model.getGameDataDocument().findCellValue( 0, "a" ) == "8" );
}

// ---------------------------------------------------------------------- 현지화 상태

// 결함: 칸을 고쳐도 상태가 읽은 때 그대로라, 고친 번역이 저장한 뒤에도 "낡음" 색으로 남았다.
SW_TEST_CASE( DataTableViewModelTest, EditingUpdatesTheTranslationState )
{
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    LocalizationRecord&        record = SheetUtil::addRecord( model, "a", "옛 글", TranslationState::Stale );

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

SW_TEST_CASE( DataTableViewModelTest, ConfirmOnlyTouchesStaleAndReview )
{
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
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
SW_TEST_CASE( DataTableViewModelTest, ConfirmedTranslationIsSavedAsCurrent )
{
    const AbsolutePath folder      = test::makeScratchPath( "SwDataTableConfirm" );
    const AbsolutePath projectPath = folder.join( "t.locproject.xml" );
    const string       oldHash     = SourceTextEntry::formatSourceHash( SourceTextEntry::computeSourceHash( "Older" ) );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( projectPath, "<LocalizationProject name=\"t\" sourceCulture=\"en\"><cultures><item>ko</item></cultures><stringTables><item>t.strings.xml</item></stringTables></LocalizationProject>" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( folder.join( "t.strings.xml" ), "<SourceStringTable culture=\"en\"><entries><entry key=\"a\" source=\"Old\" /></entries></SourceStringTable>" ) );
    StringBuilder<constant::buffer::kMax256> translationText;
    translationText.appendFormat( "<TranslationTable culture=\"ko\"><entries><entry key=\"a\" text=\"글\" sourceHash=\"%#\" /></entries></TranslationTable>", oldHash );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( folder.join( "ko.translation.xml" ), translationText.view() ) );

    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    SW_REQUIRE_TRUE( EditorDataTableCommands::loadLocalizationProject( projectPath.str(), model.editSheet() ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getSheet()._listRecord.size() );
    SW_REQUIRE_TRUE( model.getSheet()._listRecord[0]._listState[0] == TranslationState::Stale );

    model.confirmTranslation( model.editSheet()._listRecord[0], 0 );
    SW_EXPECT_TRUE( model.isLocalizationDirty() );
    model.saveLocalization();
    SW_EXPECT_FALSE( model.isLocalizationDirty() );

    LocalizationSheet saved;
    SW_REQUIRE_TRUE( EditorDataTableCommands::loadLocalizationProject( projectPath.str(), saved ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, saved._listRecord.size() );
    SW_EXPECT_TRUE( saved._listRecord[0]._listState[0] == TranslationState::Current );
    SW_EXPECT_TRUE( saved._listRecord[0]._listTranslation[0] == "글" );
    (void)FileUtil::removeDirectory( folder );
}

// 할 일만 보기: 없음 · 낡음 · 검토가 남은 행만 지난다. 검색과 함께 걸린다.
SW_TEST_CASE( DataTableViewModelTest, NeedsWorkFilterSkipsFinishedRows )
{
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
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

SW_TEST_CASE( DataTableViewModelTest, TranslationLengthCountsCharactersNotBytes )
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
SW_TEST_CASE( DataTableViewModelTest, SheetOfAnotherProjectIsDiscarded )
{
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    model.setLocalizationProjects( { "a.locproject", "b.locproject" } );
    model.selectProject( 1 );
    SW_EXPECT_EQUAL( 1, model.getSelectedProjectIndex() );

    LocalizationSheet stale;
    stale._projectPath = test::makeScratchPath( "a.locproject" );
    SW_EXPECT_FALSE( model.acceptLoadedSheet( 0, std::move( stale ) ) );
    SW_EXPECT_FALSE( model.isLocalizationLoaded() );

    LocalizationSheet current;
    current._projectPath = test::makeScratchPath( "b.locproject" );
    SW_EXPECT_TRUE( model.acceptLoadedSheet( 1, std::move( current ) ) );
    SW_EXPECT_TRUE( model.isLocalizationLoaded() );
    SW_EXPECT_TRUE( model.getSheet()._projectPath == test::makeScratchPath( "b.locproject" ) );

    model.setLocalizationProjects( { "c.locproject" } ); // 고른 번호가 목록 밖이면 0 번이다
    SW_EXPECT_EQUAL( 0, model.getSelectedProjectIndex() );
}

// 결함: 읽는 동안 앞 표에 키를 더할 수 있었고, 그러면 dirty 라 읽은 결과를 버리기만 해 표가 영영 읽히지 않았다.
SW_TEST_CASE( DataTableViewModelTest, ReloadEmptiesTheSheetUntilItIsRead )
{
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, []( string_view ) {} ) };
    DataTableViewModel         model{ recorder };
    model.editSheet()._listTablePath.push_back( test::makeScratchPath( "strings.xml" ) );
    SW_REQUIRE_TRUE( model.addKey( "a" ) );
    model.reloadLocalization();
    SW_EXPECT_FALSE( model.isLocalizationDirty() );
    SW_EXPECT_TRUE( model.getSheet()._listRecord.empty() );
    SW_EXPECT_FALSE( model.addKey( "b" ) );
    SW_EXPECT_FALSE( model.isLocalizationDirty() );
}

// 게임 데이터 표 편집은 문서 패널의 창구로 알리고(되돌리기 한 단계), 되돌리기가 준 글을 적용하면 저장한 글과 같을 때 dirty 가 풀린다(감사 E-13).
SW_TEST_CASE( DataTableViewModelTest, GameDataEditNotifiesAndUndoTextRoundTrips )
{
    const DataTableTestFiles   files;
    uint32                     notifyCount{ 0 };
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, [&notifyCount]( string_view )
    { ++notifyCount; } ) };
    DataTableViewModel         model{ recorder };
    SW_EXPECT_TRUE( model.captureGameDataText().empty() ); // 열린 표가 없으면 되돌릴 글이 없다
    SW_REQUIRE_TRUE( model.openGameDataFile( files._first._absolutePath ) );
    const string before = model.captureGameDataText();

    model.replaceGameDataRawText( "<First a=\"7\"/>" );
    SW_EXPECT_EQUAL( notifyCount, 1u );
    SW_EXPECT_TRUE( model.isGameDataDirty() );

    model.applyGameDataText( before );
    SW_EXPECT_TRUE( model.captureGameDataText() == before );
    SW_EXPECT_FALSE( model.isGameDataDirty() );
    SW_EXPECT_TRUE( *model.getGameDataDocument().findCellValue( 0, "a" ) == "1" );
}
