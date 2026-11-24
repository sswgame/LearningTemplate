// BugReportViewModelTest — 지난 BugReport 폴더 줄: 최근 것 먼저, 메모는 info.txt 에서, 다시 읽으라고 할 때만 다시 읽는다(케이스 폴더만 쓴다 — nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Client/Capture/BugReport.h"

#include "Editor/Panels/BugReport/Model/BugReportViewModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct BugReportViewModelTestUtil
    {
        static void writeReport( const AbsolutePath& root, string_view name, string_view note )
        {
            const AbsolutePath directory = root.join( name );
            SW_EXPECT_TRUE( FileUtil::ensureDirectoryExists( directory ) );
            BugReport report;
            report._note = string( note );
            SW_EXPECT_TRUE( report.writeInfo( directory.str() ) );
        }
    };
} // namespace

/** @brief [BugReportViewModelTest] 폴더 이름 역순으로 줄을 세우고 메모를 읽으며, info.txt 없는 폴더는 메모가 빈 줄이다 */
SW_TEST_CASE( BugReportViewModelTest, RowsAreNewestFirst )
{
    const AbsolutePath root = sw::test::makeScratchPath( "BugReports" );
    BugReportViewModelTestUtil::writeReport( root, "20260101-100000", "old" );
    BugReportViewModelTestUtil::writeReport( root, "20260301-090000", "new" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root.join( "20260201-120000" ) ) );

    BugReportViewModel model{ root.str() };
    BugReportViewState state;
    model.gather( state );
    SW_REQUIRE_EQUAL( size_t{ 3 }, state._listRow.size() );
    SW_EXPECT_EQUAL( string( "20260301-090000" ), state._listRow[0]._name );
    SW_EXPECT_EQUAL( string( "new" ), state._listRow[0]._note );
    SW_EXPECT_EQUAL( string( "20260201-120000" ), state._listRow[1]._name );
    SW_EXPECT_TRUE( state._listRow[1]._note.empty() );
    SW_EXPECT_EQUAL( string( "old" ), state._listRow[2]._note );

    BugReportViewModelTestUtil::writeReport( root, "20260401-080000", "later" );
    model.gather( state );
    SW_EXPECT_EQUAL( size_t{ 3 }, state._listRow.size() ); // 다시 읽으라고 하기 전이다
    model.refresh();
    model.gather( state );
    SW_REQUIRE_EQUAL( size_t{ 4 }, state._listRow.size() );
    SW_EXPECT_EQUAL( string( "later" ), state._listRow[0]._note );
}

/** @brief [BugReportViewModelTest] 아직 한 번도 모으지 않은 루트(폴더 없음)는 빈 목록이다 */
SW_TEST_CASE( BugReportViewModelTest, EmptyRootHasNoRows )
{
    const AbsolutePath root = sw::test::makeScratchPath( "NoBugReports" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root ) );
    BugReportViewModel model{ root.str() };
    BugReportViewState state;
    state._listRow.emplace_back(); // 지난 값은 비운다
    model.gather( state );
    SW_EXPECT_TRUE( state._listRow.empty() );
}
