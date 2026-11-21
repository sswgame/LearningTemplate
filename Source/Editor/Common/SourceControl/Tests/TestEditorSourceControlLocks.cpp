// 소스 컨트롤 잠금 조회 — 잠금 목록(갱신 결과 · `setLocks`)에서 절대 경로의 잠금과 상태 문구를 찾고, 공급자를 바꾸면 목록을 비운다(nogpu, git 을 띄우지 않는다).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/SourceControl/EditorSourceControl.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorSourceControlLocksTest] 저장소 안 절대 경로는 저장소 기준 경로로 잠금을 찾고 잠근 사람을 상태 문구에 보인다. 잠기지 않았거나 저장소 밖이면 없다
 */
SW_TEST_CASE( EditorSourceControlLocksTest, FindsTheLockOfARepositoryFile )
{
    const string        root = FileUtil::normalizeSeparators( test::makeScratchPath( "repo" ).str() );
    EditorSourceControl sourceControl{ root };
    sourceControl.setLocks( {
        SourceControlLock{ "Content/hero.prefab.xml", "alice", "7" }
    } );

    const string             lockedPath = FileUtil::joinPath( root, "Content/hero.prefab.xml" );
    const SourceControlLock* pLock      = sourceControl.findLock( lockedPath );
    SW_REQUIRE_NOT_NULL( pLock );
    SW_EXPECT_EQUAL( string( "alice" ), pLock->_owner );
    StringBuilder<constant::buffer::kMax128> expectedStatus;
    expectedStatus.appendFormat( "Locked by alice (%#)", sourceControl.getProvider().getName() );
    SW_EXPECT_EQUAL( string( expectedStatus.view() ), sourceControl.describeStatus( lockedPath, false ) );

    const string otherPath = FileUtil::joinPath( root, "Content/other.prefab.xml" );
    SW_EXPECT_NULL( sourceControl.findLock( otherPath ) );
    SW_EXPECT_TRUE( sourceControl.describeStatus( otherPath, false ).empty() );
    SW_EXPECT_NULL( sourceControl.findLock( FileUtil::normalizeSeparators( test::makeScratchPath( "elsewhere/hero.prefab.xml" ).str() ) ) );
}

/**
 * @brief [EditorSourceControlLocksTest] 공급자를 바꾸면 앞 공급자의 잠금 목록을 버린다 — 다른 서버의 잠금을 보이지 않는다
 */
SW_TEST_CASE( EditorSourceControlLocksTest, ChangingTheProviderDropsTheLocks )
{
    const string        root = FileUtil::normalizeSeparators( test::makeScratchPath( "repo" ).str() );
    EditorSourceControl sourceControl{ root };
    sourceControl.setLocks( {
        SourceControlLock{ "Content/hero.prefab.xml", "alice", "7" }
    } );
    sourceControl.setProvider( nullptr ); // 없음 공급자로 돌아간다
    SW_EXPECT_NULL( sourceControl.findLock( FileUtil::joinPath( root, "Content/hero.prefab.xml" ) ) );
}
