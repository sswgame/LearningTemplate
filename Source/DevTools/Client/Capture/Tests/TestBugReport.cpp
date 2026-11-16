#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Client/Capture/BugReport.h"

using namespace sw;

// 다른 프로세스의 보관 개수 정리가 목록을 읽는 사이에 폴더를 지울 수 있다. 사라진 리포트는 오류 로그 없이 "없음" 이다(로그가 나면 이 테스트가 진다).
SW_TEST_CASE( BugReportTest, ReportThatVanishedIsNotAnError )
{
    BugReport report;
    SW_EXPECT_FALSE( BugReport::readInfo( test::makeScratchPath( "SwNoSuchBugReport" ), report ) );
}

/** @brief [BugReportTest] 재현 환경(플랫폼 · 타깃 · 씬 · 설정 해시)과 입력 기록 길이가 info.txt 를 돌아 그대로 온다 */
SW_TEST_CASE( BugReportTest, EnvironmentAndInputPropertiesRoundTrip )
{
    const string directory = test::makeScratchPath( "SwBugReportEnvironment" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( directory ) );
    BugReport written;
    written._note            = "env";
    written._platform        = "Windows";
    written._target          = "App";
    written._sceneHash       = BugReport::formatHash( 0x0123456789abcdefull );
    written._configHash      = BugReport::formatHash( 42 );
    written._inputFrameCount = 120;
    written._inputSeconds    = 2.0f;
    SW_REQUIRE_TRUE( written.writeInfo( directory ) );

    BugReport read;
    SW_REQUIRE_TRUE( BugReport::readInfo( directory, read ) );
    SW_EXPECT_EQUAL( string( "Windows" ), read._platform );
    SW_EXPECT_EQUAL( string( "App" ), read._target );
    SW_EXPECT_EQUAL( string( "0123456789abcdef" ), read._sceneHash );
    SW_EXPECT_EQUAL( string( "000000000000002a" ), read._configHash );
    SW_EXPECT_EQUAL( uint32( 120 ), read._inputFrameCount );
    SW_EXPECT_NEAR( 2.0f, read._inputSeconds, 1.0e-3f );
}

/** @brief [BugReportTest] 환경 대조는 다른 칸만 `칸: 기록 -> 지금` 으로 낸다. 기록하지 못한 빈 칸은 대조하지 않는다 */
SW_TEST_CASE( BugReportTest, EnvironmentMismatchListsOnlyDifferingProperties )
{
    BugReport recorded;
    recorded._platform   = "Windows";
    recorded._sceneHash  = "aaaa";
    recorded._configHash = ""; // 기록하지 못했다
    BugReport current    = recorded;
    SW_EXPECT_TRUE( BugReport::findEnvironmentMismatches( recorded, current ).empty() );

    current._sceneHash            = "bbbb";
    current._configHash           = "cccc";
    const vector<string> listLine = BugReport::findEnvironmentMismatches( recorded, current );
    SW_REQUIRE_EQUAL( size_t( 1 ), listLine.size() );
    SW_EXPECT_EQUAL( string( "scene_hash: aaaa -> bbbb" ), listLine[0] );
}

/** @brief [BugReportTest] 폴더 해시는 바이트와 상대 경로를 모두 접고, 같은 내용이면 같다(열거 순서와 무관) */
SW_TEST_CASE( BugReportTest, FolderHashFollowsBytesAndNames )
{
    const string directory = test::makeScratchPath( "SwBugReportFolderHash" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( directory ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( directory, "a.json" ), "{}" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( directory, "b.json" ), "[1]" ) );
    uint64 first = 0;
    SW_REQUIRE_TRUE( BugReport::computeFolderHash( directory, first ) );
    uint64 again = 0;
    SW_REQUIRE_TRUE( BugReport::computeFolderHash( directory, again ) );
    SW_EXPECT_EQUAL( first, again );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( directory, "b.json" ), "[2]" ) );
    uint64 changedBytes = 0;
    SW_REQUIRE_TRUE( BugReport::computeFolderHash( directory, changedBytes ) );
    SW_EXPECT_TRUE( changedBytes != first );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( directory, "b.json" ), "[1]" ) );
    SW_REQUIRE_TRUE( FileUtil::renamePath( FileUtil::joinPath( directory, "b.json" ), FileUtil::joinPath( directory, "c.json" ) ) );
    uint64 renamed = 0;
    SW_REQUIRE_TRUE( BugReport::computeFolderHash( directory, renamed ) );
    SW_EXPECT_TRUE( renamed != first );
}
