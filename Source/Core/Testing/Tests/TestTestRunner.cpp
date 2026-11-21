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
    const AbsolutePath path = test::makeScratchPath( "probe.txt" );
    SW_EXPECT_TRUE( StringUtil::endsWith( path.str(), "probe.txt" ) );
    SW_EXPECT_TRUE( path.str().find( "TestRunnerTest.ScratchPathIsUniquePerCase" ) != string::npos );
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

/**
 * @brief [TestRunnerTest] 선언한 id 는 정보 로그여도 센다 — 설계가 정보 로그로 정한 사실(능력 밖 요청을 받지 않는 등)도 기대할 수 있다.
 *        결함이면 선언한 정보 로그를 영원히 못 보아 "never appeared" 로 진다(2026-11-20 `DisplayOutputTest`)
 */
SW_TEST_CASE( TestRunnerTest, DeclaredInfoLogIsCounted )
{
    SW_EXPECT_LOG( "Test.DeclaredInfo" );
    SW_LOG_INFO_ID( "Test.DeclaredInfo", "an info line this case declares on purpose" );
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
            : _path{ test::makeScratchPath( "fixture.txt" ).str() }
        {
            (void)FileUtil::writeTextFile( FileUtil::toDiskPath( _path ), "ready" );
        }
    };

    constexpr uint32 kArrShardCount[] = { 1u, 2u, 7u };
} // namespace

SW_TEST_CASE_F( ScratchFileFixture, BodySeesWhatTheConstructorPrepared )
{
    string text;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::toDiskPath( _path ), text ) );
    SW_EXPECT_EQUAL( string( "ready" ), text );
}

SW_TEST_CASE_EACH( TestRunnerTest, ShardIsBelowTheCount, kArrShardCount )
{
    SW_EXPECT_TRUE( TestRunner::getShardOf( "Any.case", param ) < param );
}

/** @brief 자식 역할 — 환경이 없다고 건너뛴다(단언 없음). */
SW_TEST_CHILD_ROLE( TestRunnerTest, SkipChildSkipsWithAReason )
{
    SW_TEST_SKIP( "the child has no such environment" );
}

/** @brief 자식 역할 — 까닭 없이 건너뛴다(실패여야 한다). */
SW_TEST_CHILD_ROLE( TestRunnerTest, SkipChildWithoutAReason )
{
    SW_TEST_SKIP( "" );
}

/**
 * @brief [TestRunnerTest] 건너뛴 케이스는 통과로 세지 않고 건너뜀 줄 · 수 · 까닭이 따로 남으며, 단언 0 건 규칙에 걸리지 않는다(실행은 성공). 까닭 없는 건너뜀은 실패다
 */
SW_TEST_CASE( TestRunnerTest, SkippedCaseIsReportedApartFromPassAndFail, ._tags = "slow", ._timeoutSeconds = 240.0f )
{
    constexpr float32              kChildTimeoutSeconds = 120.0f;
    const sw::test::ChildRunResult skipped              = sw::test::runChildRole( "TestRunnerTest.SkipChildSkipsWithAReason", {}, kChildTimeoutSeconds );
    SW_EXPECT_TRUE( skipped._exitCode.has_value() && *skipped._exitCode == 0 );
    SW_EXPECT_TRUE( skipped._output.find( "[  SKIPPED ] TestRunnerTest.SkipChildSkipsWithAReason" ) != string::npos );
    SW_EXPECT_TRUE( skipped._output.find( "the child has no such environment" ) != string::npos );
    SW_EXPECT_TRUE( skipped._output.find( "0 passed, 0 failed, 1 skipped" ) != string::npos );
    SW_EXPECT_TRUE( skipped._output.find( "made no assertion" ) == string::npos );

    const sw::test::ChildRunResult noReason = sw::test::runChildRole( "TestRunnerTest.SkipChildWithoutAReason", {}, kChildTimeoutSeconds );
    SW_EXPECT_TRUE( noReason._exitCode.has_value() && *noReason._exitCode != 0 );
    SW_EXPECT_TRUE( noReason._output.find( "SW_TEST_SKIP needs a reason" ) != string::npos );
}

/**
 * @brief [TestRunnerTest] 거르개는 쉼표로 가른다 — 콜론 · 세미콜론 · 공백이 든 거르개는 인자 오류다(맞는 케이스 없음과 다른 길)
 * @details GoogleTest 의 콜론 문법을 쓴 거르개가 아무 케이스에도 맞지 않아 `NO TESTS` 로만 졌다.
 */
SW_TEST_CASE( TestRunnerTest, FilterGrammarRejectsOtherSeparators )
{
    for ( const utf8* pBad : { "A.*:B.*", "A.*;B.*", "A.* B.*", "A.*, B.*", "[slow tag]", "[]" } )
    {
        const string error = TestRunner::findFilterError( pBad );
        SW_EXPECT_TRUE_MSG( error.find( "separate patterns with a comma" ) != string::npos, string( pBad ) );
    }
    for ( const utf8* pGood : { "*", "A.*,B.c", "A.*,-A.b", "[slow]", "*,-[slow]", "Suite.Name/alpha", "A.?b", "TestRunnerTest.*," } )
        SW_EXPECT_TRUE_MSG( TestRunner::findFilterError( pGood ).empty(), string( pGood ) );
}

/** @brief [TestRunnerTest] 등록된 케이스에 맞지 않는 더하기 패턴은 이름으로 나온다(빼기 패턴은 보지 않는다) — `A.*,Typo.*` 가 A 만 돌고 통과하지 않는다 */
SW_TEST_CASE( TestRunnerTest, UnmatchedPatternIsReported )
{
    const vector<string> listUnmatched = TestRunner::findUnmatchedPatterns( "TestRunnerTest.*,NoSuchSuiteAnywhere.*,-AlsoNoSuchSuite.*" );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listUnmatched.size() ) );
    SW_EXPECT_EQUAL( string( "NoSuchSuiteAnywhere.*" ), listUnmatched[0] );
    SW_EXPECT_TRUE( TestRunner::findUnmatchedPatterns( "TestRunnerTest.FilterGrammarRejectsOtherSeparators" ).empty() );
    SW_EXPECT_TRUE( TestRunner::findUnmatchedPatterns( "[slow]" ).empty() ); // 태그 패턴도 태그를 단 케이스에 맞으면 된다
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( TestRunner::findUnmatchedPatterns( "[no-such-tag-anywhere]" ).size() ) );
}

/** @brief 자식 역할 — 선언하지 않은 Warning 하나를 남긴다(실패 종류 `UndeclaredLog`, 붙일 글이 나와야 한다). */
SW_TEST_CHILD_ROLE( TestRunnerTest, UndeclaredWarningChild )
{
    SW_EXPECT_TRUE( true );
    SW_LOG_WARNING_ID( "Test.UndeclaredOnPurpose", "a warning the child does not declare" );
}

/**
 * @brief [TestRunnerTest] 진 실행의 끝 줄은 `TEST_RESULT=FAILED` 이고, 선언하지 않은 로그 실패는 붙일 `SW_EXPECT_LOG` 글과 보고의 실패 종류를 낸다
 * @details 끝 줄은 표준 출력의 마지막 줄이다 — 출력을 잘라 본 쪽도 결과를 읽는다(`WINDOWS_OK=` 와 같은 꼴).
 */
SW_TEST_CASE( TestRunnerTest, FailedRunEndsWithTheResultLineAndTheFailureKind, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const sw::test::ChildRunResult child = sw::test::runChildRole( "TestRunnerTest.UndeclaredWarningChild", {}, 120.0f );
    SW_REQUIRE_TRUE_MSG( child._exitCode.has_value(), child.getOutputTail() );
    SW_EXPECT_TRUE( *child._exitCode != 0 );
    SW_EXPECT_TRUE_MSG( StringUtil::contains( child._output, "add SW_EXPECT_LOG( \"Test.UndeclaredOnPurpose\" )" ), child.getOutputTail() );
    // 진 실행은 쓰기 폴더에 보고를 남긴다(`[  REPORT  ] <경로>`) — 케이스의 실패 종류가 든다(보고 판 4).
    const size_t reportAt = child._output.find( "[  REPORT  ] " );
    SW_REQUIRE_TRUE_MSG( reportAt != string::npos, child.getOutputTail() );
    const size_t reportEnd  = child._output.find_first_of( "\r\n", reportAt );
    const string reportPath = child._output.substr( reportAt + 13, reportEnd == string::npos ? string::npos : reportEnd - reportAt - 13 );
    string       report;
    SW_REQUIRE_TRUE_MSG( FileUtil::readTextFile( FileUtil::toDiskPath( reportPath ), report ), reportPath );
    SW_EXPECT_TRUE_MSG( StringUtil::contains( report, "\"failureKind\": \"UndeclaredLog\"" ), report );
    SW_EXPECT_TRUE_MSG( StringUtil::contains( report, "\"UndeclaredLog\": 1" ), report );
    // 끝 줄 — 실행기가 마지막으로 찍는 줄이다.
    const size_t resultAt = child._output.rfind( "TEST_RESULT=" );
    SW_REQUIRE_TRUE_MSG( resultAt != string::npos, child.getOutputTail() );
    SW_EXPECT_TRUE_MSG( StringUtil::startsWith( string_view( child._output ).substr( resultAt ), "TEST_RESULT=FAILED run=1 failed=1 skipped=0" ), child.getOutputTail() );
}
