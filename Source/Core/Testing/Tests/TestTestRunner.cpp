#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestRunner.h"

SW_LOG_CALLER( "TestTestRunner" );

using namespace sw;
using sw::test::TestRunner;

SW_TEST_CASE( TestRunnerTest, FilterIncludesAndExcludes )
{
    SW_EXPECT_TRUE( TestRunner::matchesFilter( "*", "A.b" ) );
    SW_EXPECT_TRUE( TestRunner::matchesFilter( "A.*", "A.b" ) );
    SW_EXPECT_FALSE( TestRunner::matchesFilter( "B.*", "A.b" ) );
    SW_EXPECT_TRUE( TestRunner::matchesFilter( "B.*,A.*", "A.b" ) );
    SW_EXPECT_FALSE( TestRunner::matchesFilter( "*,-A.b", "A.b" ) );
    SW_EXPECT_TRUE( TestRunner::matchesFilter( "-A.c", "A.b" ) ); // 빼기만 있으면 나머지는 모두 든다
}

SW_TEST_CASE( TestRunnerTest, ScratchPathIsUniquePerCase )
{
    const string path = test::makeScratchPath( "probe.txt" );
    SW_EXPECT_TRUE( StringUtil::endsWith( path, "probe.txt" ) );
    SW_EXPECT_TRUE( path.find( "TestRunnerTest.ScratchPathIsUniquePerCase" ) != string::npos );
    SW_EXPECT_TRUE( FileUtil::writeTextFile( path, "scratch" ) );
}

SW_TEST_CASE( TestRunnerTest, SnapshotComparesWithTheCommittedFile )
{
    SW_EXPECT_SNAPSHOT( "greeting", "hello\nsnapshot\n" );
}

SW_TEST_CASE( TestRunnerTest, DeclaredLogIsNotAFailure )
{
    SW_EXPECT_LOG( "Test.DeclaredWarning" );
    SW_LOG_WARNING_ID( "Test.DeclaredWarning", "a warning this case declares on purpose" );
}
