// git 작업 트리 질의 — porcelain 출력 해석(고침 · 더하기 · 지움 · 이름 바꾸기 · 추적 밖 · 충돌 · 따옴표 경로)과 되돌릴 수 있는 변경.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/SourceControl/SourceControlProvider.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [GitChangeCommandsTest] porcelain 줄마다 종류 · 경로 · 색인 여부를 읽는다. 이름 바꾸기는 옛 경로를, 따옴표 경로는 벗긴다
 */
SW_TEST_CASE( GitChangeCommandsTest, ParsesPorcelainStatus )
{
    const vector<string> listLine = {
        " M Content/game/empty/maps/editortest.scene.xml",
        "M  Source/Editor/a.cpp",
        "A  Source/Editor/new.h",
        " D Content/old.material",
        "R  Content/a.material -> Content/b.material",
        "?? \"Content/with space.txt\"",
        "UU Source/conflict.cpp",
        "",
    };
    vector<SourceControlChange> listChange;
    SW_REQUIRE_TRUE( GitChangeCommands::parseStatusOutput( listLine, listChange ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 7 ), listChange.size() );
    SW_EXPECT_TRUE( listChange[0]._kind == SourceControlChangeKind::Modified );
    SW_EXPECT_FALSE( listChange[0]._bStaged );
    SW_EXPECT_EQUAL( string( "Content/game/empty/maps/editortest.scene.xml" ), listChange[0]._path );
    SW_EXPECT_TRUE( listChange[1]._bStaged );
    SW_EXPECT_TRUE( listChange[2]._kind == SourceControlChangeKind::Added );
    SW_EXPECT_TRUE( listChange[3]._kind == SourceControlChangeKind::Deleted );
    SW_EXPECT_TRUE( listChange[4]._kind == SourceControlChangeKind::Renamed );
    SW_EXPECT_EQUAL( string( "Content/a.material" ), listChange[4]._originalPath );
    SW_EXPECT_EQUAL( string( "Content/b.material" ), listChange[4]._path );
    SW_EXPECT_TRUE( listChange[5]._kind == SourceControlChangeKind::Untracked );
    SW_EXPECT_EQUAL( string( "Content/with space.txt" ), listChange[5]._path );
    SW_EXPECT_TRUE( listChange[6]._kind == SourceControlChangeKind::Conflicted );
}

/**
 * @brief [GitChangeCommandsTest] 모르는 줄이 하나라도 있으면 틀린 목록을 내지 않는다
 */
SW_TEST_CASE( GitChangeCommandsTest, RejectsUnknownLines )
{
    vector<SourceControlChange> listChange;
    SW_EXPECT_FALSE( GitChangeCommands::parseStatusOutput( { " M ok.txt", "fatal: not a git repository" }, listChange ) );
    SW_EXPECT_TRUE( listChange.empty() );
}

/**
 * @brief [GitChangeCommandsTest] 되돌리기는 추적 파일만 — 추적 밖(되돌리면 지워진다) · 충돌은 하지 않는다
 */
SW_TEST_CASE( GitChangeCommandsTest, RevertOnlyTrackedChanges )
{
    SW_EXPECT_TRUE( GitChangeCommands::canRevert( SourceControlChange{ "a.txt", "", SourceControlChangeKind::Modified, false } ) );
    SW_EXPECT_TRUE( GitChangeCommands::canRevert( SourceControlChange{ "a.txt", "", SourceControlChangeKind::Added, true } ) );
    SW_EXPECT_FALSE( GitChangeCommands::canRevert( SourceControlChange{ "a.txt", "", SourceControlChangeKind::Untracked, false } ) );
    SW_EXPECT_FALSE( GitChangeCommands::canRevert( SourceControlChange{ "a.txt", "", SourceControlChangeKind::Conflicted, false } ) );
    const ProcessCommand revert = GitChangeCommands::makeRevertCommand( "Content/a.material" );
    SW_EXPECT_EQUAL( string( "Content/a.material" ), revert._listArgument.back() );
}
