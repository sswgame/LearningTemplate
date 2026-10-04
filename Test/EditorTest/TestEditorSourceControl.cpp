#include "pch.h"

#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/SourceControl/SourceControlProvider.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorSourceControlTest] `git lfs locks --json` 출력을 잠금 목록으로 읽는다 — 앞의 경고 줄은 넘기고, 형식이 틀리면 false
 */
SW_TEST_CASE( EditorSourceControlTest, GitLfsLockListIsParsed )
{
    const GitLfsSourceControlProvider provider;
    vector<SourceControlLock>         listLock;
    const vector<string>              listLine = {
        "warning: some lfs notice",
        R"([{"id":"3","path":"Resource/game/p/maps/a.scene.xml","owner":{"name":"alice"},"locked_at":"2026-10-04T00:00:00Z"},)",
        R"({"id":"4","path":"Resource/engine/materials/x.material","owner":{"name":"bob"}}])",
    };
    SW_ASSERT_TRUE( provider.parseRefreshOutput( listLine, listLock ) );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listLock.size() ) );
    SW_EXPECT_EQUAL( string( "Resource/game/p/maps/a.scene.xml" ), listLock[0]._path );
    SW_EXPECT_EQUAL( string( "alice" ), listLock[0]._owner );
    SW_EXPECT_EQUAL( string( "4" ), listLock[1]._id );

    SW_EXPECT_TRUE( provider.parseRefreshOutput( { "[]" }, listLock ) );
    SW_EXPECT_TRUE( listLock.empty() );
    SW_EXPECT_FALSE( provider.parseRefreshOutput( { "Remote \"origin\" does not support the LFS locking API" }, listLock ) );
    SW_EXPECT_FALSE( provider.parseRefreshOutput( { R"([{"id":"1"}])" }, listLock ) );
    SW_EXPECT_TRUE( listLock.empty() );
}

/**
 * @brief [EditorSourceControlTest] 잠그기 · 풀기 명령은 경로를 따옴표로 감싼다(공백이 든 경로)
 */
SW_TEST_CASE( EditorSourceControlTest, GitLfsCommandsQuoteThePath )
{
    const GitLfsSourceControlProvider provider;
    SW_EXPECT_EQUAL( string( "git lfs lock \"Resource/game/a b/x.scene.xml\"" ), provider.makeLockCommand( "Resource/game/a b/x.scene.xml" ) );
    SW_EXPECT_EQUAL( string( "git lfs unlock \"Resource/x.material\"" ), provider.makeUnlockCommand( "Resource/x.material" ) );
    SW_EXPECT_EQUAL( string( "git lfs locks --json" ), provider.makeRefreshCommand() );
}

/**
 * @brief [EditorSourceControlTest] 저장소 기준 경로는 원래 철자를 지키고, 저장소 밖 경로는 빈 문자열이다
 */
SW_TEST_CASE( EditorSourceControlTest, RepositoryPathKeepsCaseAndRejectsOutsidePaths )
{
    SW_EXPECT_EQUAL( string( "Resource/Game/X.scene.xml" ), EditorSourceControl::makeRepositoryPath( "D:/Repo", "d:\\repo\\Resource\\Game\\X.scene.xml" ) );
    SW_EXPECT_TRUE( EditorSourceControl::makeRepositoryPath( "D:/Repo", "D:/RepoOther/a.txt" ).empty() );
    SW_EXPECT_TRUE( EditorSourceControl::makeRepositoryPath( "D:/Repo", "D:/Repo" ).empty() );
    SW_EXPECT_TRUE( EditorSourceControl::makeRepositoryPath( "", "D:/Repo/a.txt" ).empty() );
}

/**
 * @brief [EditorSourceControlTest] 공급자가 없으면 잠그지 않고, 상태 글은 잠금 · 읽기 전용을 말한다
 * @details 공급자 없음(git LFS 가 없는 PC)에서도 읽기 전용 표시는 한다 — 파일 속성은 버전 관리와 무관하게 보인다.
 */
SW_TEST_CASE( EditorSourceControlTest, StatusTextAndLockRequestsFollowTheProvider )
{
    EditorSourceControl sourceControl( "D:/Repo" );
    SW_EXPECT_FALSE( sourceControl.requestLock( "D:/Repo/Resource/a.scene.xml" ) );
    SW_EXPECT_FALSE( sourceControl.isBusy() );
    SW_EXPECT_EQUAL( string( "Read-only file" ), sourceControl.describeStatus( "D:/Repo/Resource/a.scene.xml", true ) );
    SW_EXPECT_TRUE( sourceControl.describeStatus( "D:/Repo/Resource/a.scene.xml", false ).empty() );

    sourceControl.setProvider( make_unique<GitLfsSourceControlProvider>() );
    vector<SourceControlLock> listLock( 1 );
    listLock[0]._path  = "Resource/a.scene.xml";
    listLock[0]._owner = "alice";
    sourceControl.setLocks( std::move( listLock ) );

    SW_EXPECT_EQUAL( string( "Locked by alice (Git LFS)" ), sourceControl.describeStatus( "D:/Repo/Resource/a.scene.xml", true ) );
    SW_EXPECT_EQUAL( string( "Read-only - Check Out (lock) before editing" ), sourceControl.describeStatus( "D:/Repo/Resource/b.scene.xml", true ) );
    SW_EXPECT_NOT_NULL( sourceControl.findLock( "D:/Repo/Resource/A.Scene.xml" ) );
    SW_EXPECT_TRUE( sourceControl.requestLock( "D:/Repo/Resource/b.scene.xml" ) );
    SW_EXPECT_TRUE( sourceControl.isBusy() );
    SW_EXPECT_FALSE( sourceControl.requestLock( "E:/Elsewhere/b.scene.xml" ) );
}
