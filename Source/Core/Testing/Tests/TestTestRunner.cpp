#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestRunner.h"

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

/** @brief [TestRunnerTest] 실행 동안 선언 질의가 지금 케이스의 선언을 답한다 — 시나리오 로그 게이트가 묻는 통로다 */
SW_TEST_CASE( TestRunnerTest, ExpectedLogQueryAnswersForTheActiveCase )
{
    SW_EXPECT_LOG( "Test.QueriedWarning" );
    SW_EXPECT_TRUE( sw::test::isExpectedLog( "Test.QueriedWarning" ) );
    SW_EXPECT_FALSE( sw::test::isExpectedLog( "Test.SomethingElse" ) );
    SW_EXPECT_FALSE( sw::test::isExpectedLog( nullptr ) );
    SW_LOG_WARNING_ID( "Test.QueriedWarning", "a warning this case declares on purpose" );
}

SW_TEST_CASE( TestRunnerTest, DeclaredLogIsNotAFailure )
{
    SW_EXPECT_LOG( "Test.DeclaredWarning" );
    SW_LOG_WARNING_ID( "Test.DeclaredWarning", "a warning this case declares on purpose" );
}

SW_TEST_CASE( TestRunnerTest, FilterMatchesTags )
{
    SW_EXPECT_TRUE( TestRunner::matchesFilter( "[slow]", "A.b", "gpu slow" ) );
    SW_EXPECT_FALSE( TestRunner::matchesFilter( "[slow]", "A.b", "slower" ) ); // 낱말 단위다
    SW_EXPECT_FALSE( TestRunner::matchesFilter( "*,-[slow]", "A.b", "slow" ) );
    SW_EXPECT_TRUE( TestRunner::matchesFilter( "*,-[slow]", "A.b", "" ) );
}

SW_TEST_CASE( TestRunnerTest, ShardSplitsEveryNameIntoExactlyOnePart )
{
    uint32 index = 9;
    uint32 count = 9;
    SW_REQUIRE_TRUE( TestRunner::parseShard( "1/3", index, count ) );
    SW_EXPECT_EQUAL( 1u, index );
    SW_EXPECT_EQUAL( 3u, count );
    SW_EXPECT_FALSE( TestRunner::parseShard( "3/3", index, count ) );
    SW_EXPECT_FALSE( TestRunner::parseShard( "1", index, count ) );
    SW_EXPECT_FALSE( TestRunner::parseShard( "0/0", index, count ) );
    uint32 arrHit[3]{};
    for ( const utf8* pName : { "A.a", "A.b", "B.c", "C.d", "D.e", "E.f", "F.g", "G.h" } )
    {
        const uint32 shard = TestRunner::getShardOf( pName, 3 );
        SW_REQUIRE_TRUE( shard < 3 );
        SW_EXPECT_EQUAL( shard, TestRunner::getShardOf( pName, 3 ) ); // 늘 같은 몫
        ++arrHit[shard];
    }
    SW_EXPECT_EQUAL( 8u, arrHit[0] + arrHit[1] + arrHit[2] );
    SW_EXPECT_EQUAL( 0u, TestRunner::getShardOf( "A.a", 1 ) );
}

namespace
{
    /** @brief 픽스처 예 — 케이스마다 새로 서고(준비) 끝나면 사라진다(정리). */
    struct ScratchFileFixture
    {

        string _path;
        ScratchFileFixture()
            : _path{ test::makeScratchPath( "fixture.txt" ) }
        {
            (void)FileUtil::writeTextFile( _path, "ready" );
        }
    };

    constexpr uint32 kArrShardCount[] = { 1u, 2u, 7u };
} // namespace

SW_TEST_CASE_F( ScratchFileFixture, BodySeesWhatTheConstructorPrepared )
{
    string text;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( _path, text ) );
    SW_EXPECT_EQUAL( string( "ready" ), text );
}

SW_TEST_CASE_EACH( TestRunnerTest, ShardIsBelowTheCount, kArrShardCount )
{
    SW_EXPECT_TRUE( TestRunner::getShardOf( "Any.case", param ) < param );
}
