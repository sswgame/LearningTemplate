#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/TestChildProcess.h"
#include "TestFramework/TestFramework.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace
{
    /** @brief 앞 케이스가 만든 임시 경로 — 다음 케이스가 그것이 지워졌는지 본다(케이스 경계를 넘어야 볼 수 있는 일이다). */
    sw::vector<sw::string> s_listPathOfPreviousCase;

    /** @brief 단언 하나를 부르고 그 뒤 줄까지 왔는지 남깁니다 — ASSERT 는 함수를 끝내야 하고 EXPECT 는 아니다. */
    void runAssertTrue( bool bCondition, bool& outReachedEnd )
    {
        outReachedEnd = false;
        SW_ASSERT_TRUE( bCondition );
        outReachedEnd = true;
    }

    void runAssertFalse( bool bCondition, bool& outReachedEnd )
    {
        outReachedEnd = false;
        SW_ASSERT_FALSE( bCondition );
        outReachedEnd = true;
    }

    void runAssertEqual( int32 expected, int32 actual, bool& outReachedEnd )
    {
        outReachedEnd = false;
        SW_ASSERT_EQUAL( expected, actual );
        outReachedEnd = true;
    }

    void runAssertNotNull( const void* pValue, bool& outReachedEnd )
    {
        outReachedEnd = false;
        SW_ASSERT_NOT_NULL( pValue );
        outReachedEnd = true;
    }

    void runExpectTrue( bool bCondition, bool& outReachedEnd )
    {
        outReachedEnd = false;
        SW_EXPECT_TRUE( bCondition );
        outReachedEnd = true;
    }

    /** @brief 지역 레지스트리의 케이스 본문 — 예상 밖 Error 를 하나 남깁니다. */
    void logProbeError() { SW_LOG_ERROR( "Deliberate probe error (TestFrameworkTest.HostSuiteCaseFailsOnUnexpectedErrorLog)" ); }

    /** @brief 지역 레지스트리의 케이스 본문 — 방어 시험 구간 안에서 Error 를 남깁니다. */
    void logProbeErrorInDefensiveScope()
    {
        SW_TEST_DEFENSIVE_SCOPE( "probe error inside a defensive scope" );
        SW_LOG_ERROR( "Deliberate probe error inside a defensive scope" );
    }

    /** @brief 테스트 실행 파일 인자 하나로 지역 레지스트리를 설정합니다. */
    void configureWithArgument( test::TestRegistry& registry, const utf8* pArgument )
    {
        utf8       programName[] = "probe";
        sw::string argument( pArgument );
        utf8*      argv[] = { programName, argument.data() };
        registry.configureFromArgs( 2, argv );
    }
} // namespace

/**
 * @brief [TestFrameworkTest] 임시 파일 경로는 **프로세스마다 · 케이스마다** 다르다
 * @details `%TEMP%/test_malformed.wav` 처럼 고정된 이름에 쓰면 안 된다. 같은 테스트
 *          실행 파일이 네 프리셋에서 각각 돌고 CI 는 그것들을 나란히 돌리므로, 한쪽의
 *          `removeFile` 이 다른 쪽이 방금 쓴 파일을 지울 수 있다. 확장자는 그대로 남아야
 *          한다 — 로더가 그것으로 형식을 고른다.
 */
SW_TEST_CASE( TestFrameworkTest, TempPathIsUniquePerProcessAndPerCase )
{
    const sw::string path = test::makeTempPath( "sample.wav" );

    // 임시 폴더 아래에 있고, 확장자와 원래 이름이 그대로 남는다.
    SW_EXPECT_TRUE_MSG( sw::StringUtil::startsWith( path, sw::FileUtil::getTempDirectory() ),
                        "임시 폴더 아래가 아닙니다" );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::endsWith( path, "sample.wav" ), "원래 이름과 확장자가 사라졌습니다" );

    // 케이스 이름이 섞여 있어야 한 프로세스 안에서 두 케이스가 서로를 안 밟는다.
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( path, "TempPathIsUniquePerProcessAndPerCase" ),
                        "케이스 이름이 경로에 없습니다" );

    // 같은 이름을 다시 물으면 같은 경로여야 한다 — 한 케이스 안에서는 안정적이어야 쓴 것을 읽는다.
    SW_EXPECT_EQUAL( path, test::makeTempPath( "sample.wav" ) );

    // 다른 이름은 다른 경로다.
    SW_EXPECT_TRUE( path != test::makeTempPath( "other.wav" ) );
}

/**
 * @brief [TestFrameworkTest] 옆 케이스가 같은 파일 이름을 써도 경로가 겹치지 않는다
 * @details 두 케이스가 같은 파일 이름을 쓰면, 순서대로 돌기는 하지만 앞 케이스가 정리 전에 실패할 때
 *          뒤 케이스가 남은 파일을 읽는다.
 */
SW_TEST_CASE( TestFrameworkTest, TwoCasesAskingForTheSameFileNameGetDifferentPaths )
{
    const sw::string path = test::makeTempPath( "sample.wav" );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( path, "TwoCasesAskingForTheSameFileNameGetDifferentPaths" ),
                        "케이스 이름이 경로에 없습니다" );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( path, "TempPathIsUniquePerProcessAndPerCase" ) == false,
                        "옆 케이스의 경로와 같습니다" );
}

/**
 * @brief [TestFrameworkTest] 실패한 EXPECT 는 실패 하나를 남기고 계속 간다 — 조건 글은 매크로가 풀리기 전 철자다
 * @details 단언은 전부 한 뼈대(`SW_TEST_CHECK_IMPL` · `SW_TEST_CHECK_EQUAL_IMPL`)를 지난다. 그 뼈대가 실패를 놓치거나
 *          두 번 세거나, 조건 글을 안쪽 매크로에서 만들어 `SW_TRUE` 를 `1` 로 찍으면 여기서 진다.
 */
SW_TEST_CASE( TestFrameworkTest, ExpectRecordsOneFailureEachAndKeepsGoing )
{
    sw::vector<test::TestFailure> listFailure;
    {
        test::ScopedFailureCapture capture;
        SW_EXPECT_TRUE( 1 + 1 == 3 );
        SW_EXPECT_TRUE( 1 + 1 == 2 );
        SW_EXPECT_FALSE( SW_TRUE );
        SW_EXPECT_EQUAL( 4, 2 + 1 );
        SW_EXPECT_NOT_EQUAL( 7, 7 );
        SW_EXPECT_NEAR_EQUAL( 1.0, 1.5, 0.25 );
        SW_EXPECT_NULL( &listFailure );
        SW_EXPECT_NOT_NULL( static_cast<const void*>( nullptr ) );
        SW_EXPECT_EMPTY( sw::string( "x" ) );
        listFailure = capture.getListFailure();
    }

    SW_ASSERT_EQUAL( size_t{ 8 }, listFailure.size() );
    SW_EXPECT_STREQ( "1 + 1 == 3", listFailure[0]._condition );
    SW_EXPECT_STREQ( "!(SW_TRUE)", listFailure[1]._condition );
    SW_EXPECT_STREQ( "2 + 1 == 4", listFailure[2]._condition );
    SW_EXPECT_STREQ( "Expected [4], Actual [3]", listFailure[2]._message );
    SW_EXPECT_STREQ( "7 != 7", listFailure[3]._condition );
    SW_EXPECT_STREQ( "Expected not equal to [7]", listFailure[3]._message );
    SW_EXPECT_STREQ( "|1.5 - 1.0| <= 0.25", listFailure[4]._condition );
    SW_EXPECT_STREQ( "Diff [0.5] exceeds tolerance [0.25]", listFailure[4]._message );
    SW_EXPECT_STREQ( "&listFailure == nullptr", listFailure[5]._condition );
    SW_EXPECT_STREQ( "static_cast<const void*>( nullptr ) != nullptr", listFailure[6]._condition );
    SW_EXPECT_STREQ( "sw::string( \"x\" ).empty()", listFailure[7]._condition );

    // 이 파일 · 이 줄을 가리켜야 실패를 따라갈 수 있다.
    SW_EXPECT_TRUE( sw::StringUtil::endsWith( listFailure[0]._file, "TestTestFramework.cpp" ) );
    SW_EXPECT_TRUE( listFailure[0]._line > 0 );
}

/**
 * @brief [TestFrameworkTest] 실패한 ASSERT 는 그 자리에서 함수를 끝내고, EXPECT 는 끝내지 않는다
 * @details ASSERT 는 뒤 줄이 앞 줄의 결과에 기대는 자리(널 포인터를 바로 쓰는 자리)에 쓴다. 멈추지 않으면 실패를
 *          찍어 놓고 그 다음 줄에서 죽는다 — 어느 단언이었는지도 못 남긴 채.
 */
SW_TEST_CASE( TestFrameworkTest, AssertStopsTheFunctionExpectDoesNot )
{
    bool   bAssertTrueRan{ true };
    bool   bAssertFalseRan{ true };
    bool   bAssertEqualRan{ true };
    bool   bAssertNotNullRan{ true };
    bool   bExpectTrueRan{ false };
    bool   bPassingAssertRan{ false };
    size_t failureCount{ 0 };
    {
        test::ScopedFailureCapture capture;
        runAssertTrue( false, bAssertTrueRan );
        runAssertFalse( true, bAssertFalseRan );
        runAssertEqual( 1, 2, bAssertEqualRan );
        runAssertNotNull( nullptr, bAssertNotNullRan );
        runExpectTrue( false, bExpectTrueRan );
        runAssertTrue( true, bPassingAssertRan );
        failureCount = capture.getListFailure().size();
    }

    SW_EXPECT_EQUAL( size_t{ 5 }, failureCount );
    SW_EXPECT_FALSE_MSG( bAssertTrueRan, "SW_ASSERT_TRUE 가 실패했는데 함수가 계속 돌았습니다" );
    SW_EXPECT_FALSE_MSG( bAssertFalseRan, "SW_ASSERT_FALSE 가 실패했는데 함수가 계속 돌았습니다" );
    SW_EXPECT_FALSE_MSG( bAssertEqualRan, "SW_ASSERT_EQUAL 이 실패했는데 함수가 계속 돌았습니다" );
    SW_EXPECT_FALSE_MSG( bAssertNotNullRan, "SW_ASSERT_NOT_NULL 이 실패했는데 함수가 계속 돌았습니다" );
    SW_EXPECT_TRUE_MSG( bExpectTrueRan, "SW_EXPECT_TRUE 가 실패하자 함수가 멈췄습니다" );
    SW_EXPECT_TRUE_MSG( bPassingAssertRan, "통과한 SW_ASSERT_TRUE 가 함수를 끝냈습니다" );
}

/**
 * @brief [TestFrameworkTest] STREQ 는 널을 널끼리만 같다고 본다 — 글자 "<null>" 과도 다르다
 * @details 널을 `sw::string` 으로 만들면 그 자리에서 죽어 뒤 케이스가 통째로 사라진다. 그렇다고 널을 "<null>" 로 바꿔
 *          비교하면 진짜 "<null>" 과 같다고 나온다. 두 쪽을 다 본다.
 */
SW_TEST_CASE( TestFrameworkTest, StreqTellsNullApartFromTheTextNull )
{
    const utf8*                   pNull = nullptr;
    sw::vector<test::TestFailure> listFailure;
    {
        test::ScopedFailureCapture capture;
        SW_EXPECT_STREQ( "<null>", pNull );
        SW_EXPECT_STREQ( pNull, pNull );
        SW_EXPECT_STREQ( "abc", sw::string( "abc" ) );
        SW_EXPECT_STREQ( sw::string_view( "abc" ), "abd" );
        listFailure = capture.getListFailure();
    }

    SW_ASSERT_EQUAL( size_t{ 2 }, listFailure.size() );
    SW_EXPECT_STREQ( "Expected [<null>], Actual [<null>]", listFailure[0]._message );
    SW_EXPECT_STREQ( "Expected [abc], Actual [abd]", listFailure[1]._message );
}

/**
 * @brief [TestFrameworkTest] 덧붙일 말은 글자 · `sw::string` · 널을 다 받는다
 * @details 메시지는 실패할 때만 만든다. 널 메시지(조건에 따라 고른 이름이 비었을 때)를 받아 죽으면 실패를 못 찍는다.
 */
SW_TEST_CASE( TestFrameworkTest, FailureMessageAcceptsLiteralStringAndNull )
{
    const utf8*                   pNoMessage = nullptr;
    sw::vector<test::TestFailure> listFailure;
    {
        test::ScopedFailureCapture capture;
        SW_EXPECT_TRUE_MSG( false, "literal" );
        SW_EXPECT_TRUE_MSG( false, sw::string( "owned" ) );
        SW_EXPECT_FALSE_MSG( true, pNoMessage );
        listFailure = capture.getListFailure();
    }

    SW_ASSERT_EQUAL( size_t{ 3 }, listFailure.size() );
    SW_EXPECT_STREQ( "literal", listFailure[0]._message );
    SW_EXPECT_STREQ( "owned", listFailure[1]._message );
    SW_EXPECT_TRUE( listFailure[2]._message.empty() );
}

/**
 * @brief [TestFrameworkTest] `--host_suites` 는 선언한 스위트로 가른다 — exclude 는 빼고, only 는 그것만, 기본은 전부
 * @details `<타깃>_NoGPU` · `<타깃>_HostOnly` 가 이 둘로 돈다. 두 쪽이 겹치거나 빈 곳이 생기면 CI 가 GPU 스위트를 돌거나
 *          어떤 스위트가 아무 데서도 안 돈다.
 */
SW_TEST_CASE( TestFrameworkTest, HostSuitesArgumentSplitsByDeclaration )
{
    const test::TestCaseInfo gpuCase{ "GpuProbeTest", "One", {} };
    const test::TestCaseInfo plainCase{ "PlainProbeTest", "One", {} };

    test::TestRegistry defaultRegistry;
    defaultRegistry.registerHostSuite( "GpuProbeTest", "needs a GPU" );
    SW_EXPECT_TRUE( defaultRegistry.isSelected( gpuCase ) );
    SW_EXPECT_TRUE( defaultRegistry.isSelected( plainCase ) );

    test::TestRegistry excludeRegistry;
    excludeRegistry.registerHostSuite( "GpuProbeTest", "needs a GPU" );
    configureWithArgument( excludeRegistry, "--host_suites=exclude" );
    SW_EXPECT_FALSE( excludeRegistry.isSelected( gpuCase ) );
    SW_EXPECT_TRUE( excludeRegistry.isSelected( plainCase ) );

    test::TestRegistry onlyRegistry;
    onlyRegistry.registerHostSuite( "GpuProbeTest", "needs a GPU" );
    configureWithArgument( onlyRegistry, "--host_suites=only" );
    SW_EXPECT_TRUE( onlyRegistry.isSelected( gpuCase ) );
    SW_EXPECT_FALSE( onlyRegistry.isSelected( plainCase ) );

    // 이름 필터와 함께 쓰면 둘 다 맞아야 고른다.
    test::TestRegistry filteredRegistry;
    filteredRegistry.registerHostSuite( "GpuProbeTest", "needs a GPU" );
    configureWithArgument( filteredRegistry, "--host_suites=only" );
    filteredRegistry.setFilter( "Plain*" );
    SW_EXPECT_FALSE( filteredRegistry.isSelected( gpuCase ) );
    SW_EXPECT_FALSE( filteredRegistry.isSelected( plainCase ) );
}

/**
 * @brief [TestFrameworkTest] 효력 없는 실행은 진다 — 모르는 `--host_suites` 값, 아무것도 안 고른 `only`
 * @details 모르는 값을 "전부" 로 읽으면 CI 가 GPU 스위트를 돈다. 아무것도 안 고른 `only` 는 `_HostOnly` 가 빈 그물이라는 뜻이다.
 */
SW_TEST_CASE( TestFrameworkTest, IneffectiveHostSuitesRunFails )
{
    SW_TEST_SUPPRESS_LOGS();

    test::TestRegistry bogusRegistry;
    configureWithArgument( bogusRegistry, "--host_suites=bogus" );
    SW_EXPECT_EQUAL( 1, bogusRegistry.runAllTests() );

    // 호스트 선언이 없는 실행 파일의 `only` — 고른 것이 없다.
    test::TestRegistry onlyRegistry;
    onlyRegistry.registerTest( "PlainProbeTest", "One", {} );
    configureWithArgument( onlyRegistry, "--host_suites=only" );
    SW_EXPECT_EQUAL( 1, onlyRegistry.runAllTests() );

    // 케이스가 하나도 없는 스위트를 가리키는 선언 — 이름을 바꾸고 선언을 놓친 것이다.
    test::TestRegistry staleRegistry;
    staleRegistry.registerHostSuite( "RenamedAwayTest", "needs a GPU" );
    configureWithArgument( staleRegistry, "--host_suites=exclude" );
    SW_EXPECT_EQUAL( 1, staleRegistry.runAllTests() );
}

/**
 * @brief [TestFrameworkTest] 케이스를 하나도 고르지 않는 `--test_filter` 는 진다 — 구분자를 틀린 필터(`A.*:B.*`)가 0/0 으로 통과하지 않게
 * @details 구분자는 쉼표다. `:` 로 이으면 패턴 하나("A.*:B.*")가 되어 아무것도 맞지 않는데, 그 실행이 0 으로 끝나면 확인한 줄 안다.
 *          빼기만 적은 필터(`-A.*`)는 일부러 고르지 않는 것이라 그대로 통과한다.
 */
SW_TEST_CASE( TestFrameworkTest, FilterThatSelectsNothingFails )
{
    SW_TEST_SUPPRESS_LOGS();

    test::TestRegistry typoRegistry;
    typoRegistry.registerTest( "PlainProbeTest", "One", {} );
    typoRegistry.setFilter( "PlainProbeTest.*:OtherProbeTest.*" );
    SW_EXPECT_EQUAL( 1, typoRegistry.runAllTests() );

    test::TestRegistry excludeRegistry;
    excludeRegistry.registerTest( "PlainProbeTest", "One", {} );
    excludeRegistry.setFilter( "-PlainProbeTest.*" );
    SW_EXPECT_EQUAL( 0, excludeRegistry.runAllTests() );
}

/**
 * @brief [TestFrameworkTest] 호스트 스위트 케이스는 예상 밖 Error 로그 하나로 진다 — 방어 구간의 Error 와 일반 스위트는 그대로 통과
 * @details GPU 시험은 검증 레이어 · 드라이버 오류를 Error 로그로만 남기고 단언은 통과할 수 있다(Vulkan 구간이 `[Error]` 17 줄을 찍으며
 *          통과한 적이 있다). 이 케이스는 일부러 Error 한 줄을 남기므로 그 줄이 이 실행의 출력에 보인다.
 */
SW_TEST_CASE( TestFrameworkTest, HostSuiteCaseFailsOnUnexpectedErrorLog )
{
    test::TestRegistry hostRegistry;
    hostRegistry.registerHostSuite( "GpuProbeTest", "needs a GPU" );
    hostRegistry.registerTest( "GpuProbeTest", "LogsError", SW_DELEGATE_FUNCTION( sw::Delegate<void()>, logProbeError ) );
    SW_EXPECT_TRUE_MSG( hostRegistry.runAllTests() == 1, "a host suite case that logs an unexpected Error must fail" );

    test::TestRegistry defensiveRegistry;
    defensiveRegistry.registerHostSuite( "GpuProbeTest", "needs a GPU" );
    defensiveRegistry.registerTest( "GpuProbeTest", "LogsDefensiveError", SW_DELEGATE_FUNCTION( sw::Delegate<void()>, logProbeErrorInDefensiveScope ) );
    SW_EXPECT_TRUE_MSG( defensiveRegistry.runAllTests() == 0, "an Error inside SW_TEST_DEFENSIVE_SCOPE is expected, not a failure" );

    test::TestRegistry plainRegistry;
    plainRegistry.registerTest( "PlainProbeTest", "LogsError", SW_DELEGATE_FUNCTION( sw::Delegate<void()>, logProbeError ) );
    SW_EXPECT_TRUE_MSG( plainRegistry.runAllTests() == 0, "only host suites count Error logs" );
}

/**
 * @brief [TestFrameworkTest] 알려진 Error 선언은 그 스위트 · 그 문구만 견딘다
 * @details `SW_TEST_KNOWN_ERROR_LOG` 는 원인이 엔진에 있고 아직 못 고친 Error 를 그 스위트에서만 빼 준다. 다른 문구나 다른 스위트까지
 *          빼 주면 호스트 스위트의 Error 판정이 다시 구멍이 된다.
 */
SW_TEST_CASE( TestFrameworkTest, KnownErrorLogIsToleratedOnlyForItsSuiteAndText )
{
    test::TestRegistry knownRegistry;
    knownRegistry.registerHostSuite( "GpuProbeTest", "needs a GPU" );
    knownRegistry.registerKnownErrorLog( "GpuProbeTest", "Deliberate probe error", "probe" );
    knownRegistry.registerTest( "GpuProbeTest", "LogsError", SW_DELEGATE_FUNCTION( sw::Delegate<void()>, logProbeError ) );
    SW_EXPECT_TRUE_MSG( knownRegistry.runAllTests() == 0, "a declared known error must be tolerated" );

    test::TestRegistry otherTextRegistry;
    otherTextRegistry.registerHostSuite( "GpuProbeTest", "needs a GPU" );
    otherTextRegistry.registerKnownErrorLog( "GpuProbeTest", "some other text", "probe" );
    otherTextRegistry.registerTest( "GpuProbeTest", "LogsError", SW_DELEGATE_FUNCTION( sw::Delegate<void()>, logProbeError ) );
    SW_EXPECT_TRUE_MSG( otherTextRegistry.runAllTests() == 1, "a known error must not tolerate a different text" );

    test::TestRegistry otherSuiteRegistry;
    otherSuiteRegistry.registerHostSuite( "GpuProbeTest", "needs a GPU" );
    otherSuiteRegistry.registerKnownErrorLog( "OtherProbeTest", "Deliberate probe error", "probe" );
    otherSuiteRegistry.registerTest( "GpuProbeTest", "LogsError", SW_DELEGATE_FUNCTION( sw::Delegate<void()>, logProbeError ) );
    SW_EXPECT_TRUE_MSG( otherSuiteRegistry.runAllTests() == 1, "a known error must not tolerate another suite" );
}

/**
 * @brief [TestFrameworkTest] 단언 가로채기 안에서는 SW_ASSERT · SW_LOG_ASSERT 가 멈추지 않고 세어지며, 그 뒤 줄이 돈다
 * @details Debug 의 단언은 디버거에서 멈춘다 — 가로채기가 없으면 단언이 걸리는 입력을 시험할 수 없다. 겹쳐 건 가로채기도 바깥이 끝날 때까지 유지된다.
 */
SW_TEST_CASE( TestFrameworkTest, AssertCaptureCountsInsteadOfBreaking )
{
    if ( test::ScopedAssertCapture::kAssertsAreActive == false )
        SW_TEST_SKIP( "asserts are compiled out outside Debug" );

    SW_TEST_DEFENSIVE_SCOPE( "deliberate assertions inside an assert capture" );
    test::ScopedAssertCapture outer;
    bool                      bReachedAfterAssert = false;
    SW_ASSERT( 1 + 1 == 3 );
    bReachedAfterAssert = true;
    SW_EXPECT_TRUE( bReachedAfterAssert );
    SW_EXPECT_EQUAL( 1u, outer.getCount() );

    {
        test::ScopedAssertCapture inner;
        SW_LOG_ASSERT( false, "deliberate %#", "assert" );
        SW_EXPECT_EQUAL( 1u, inner.getCount() );
    }
    // 안쪽이 풀려도 바깥 가로채기는 그대로다.
    SW_ASSERT( false );
    SW_EXPECT_EQUAL( 3u, outer.getCount() );
}

/**
 * @brief [TestFrameworkTest] 케이스가 만든 임시 경로를 남겨 둔다 — 바로 다음 케이스가 그것이 지워졌는지 본다
 * @details 파일 · 폴더(안에 파일) · 테스트가 알려 주지 않은 옆 파일(엔진이 쿠킹해 두는 `.bin` · `.meta` 를 흉내) 셋을 만든다.
 *          지우는 것은 케이스가 **끝난 뒤** 프레임워크라 같은 케이스 안에서는 볼 수 없다.
 */
SW_TEST_CASE( TestFrameworkTest, TempPathsOfACaseAreCreated )
{
    s_listPathOfPreviousCase.clear();

    const sw::string filePath = test::makeTempPath( "Probe.txt" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( filePath, "probe" ) );

    const sw::string directory = test::makeTempDirectory( "ProbeDir" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( directory, "nested.txt" ), "nested" ) );

    // 테스트가 이름을 받은 적 없는 옆 파일 — 엔진이 `x.xml` 옆에 `x.bin` · `x.meta` 를 쓰는 것과 같다.
    const sw::string siblingPath = filePath + ".meta";
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( siblingPath, "meta" ) );

    s_listPathOfPreviousCase = { filePath, directory, siblingPath };
}

/**
 * @brief [TestFrameworkTest] 앞 케이스의 임시 경로는 그 케이스가 끝날 때 프레임워크가 지웠다
 * @details 케이스가 끝에서 `removeFile` 을 손으로 부르면 단언으로 일찍 빠질 때 그 줄에 닿지 않아 파일이 남는다.
 *          앞 케이스 없이 이것만 고르면 볼 것이 없어 건너뛴다.
 */
SW_TEST_CASE( TestFrameworkTest, TempPathsOfThePreviousCaseAreGone )
{
    if ( s_listPathOfPreviousCase.empty() )
        SW_TEST_SKIP( "run together with TestFrameworkTest.TempPathsOfACaseAreCreated" );

    for ( const sw::string& path : s_listPathOfPreviousCase )
        SW_EXPECT_FALSE_MSG( sw::FileUtil::fileExists( path ) || sw::FileUtil::directoryExists( path ), path.c_str() );
    s_listPathOfPreviousCase.clear();
}

/**
 * @brief [TestFrameworkTest] 자식 역할: 환경 변수대로 표식을 찍고 끝나거나, 끝나지 않는다. 그냥 실행하면 건너뛴다.
 */
SW_TEST_CASE( TestFrameworkTest, ChildRoleEchoesOrHangs )
{
    const utf8* pMode = std::getenv( "SW_TEST_CHILD_MODE" );
    if ( pMode == nullptr )
        SW_TEST_SKIP( "child only — ChildProcessGetsItsEnvironmentAndOutputIsCaptured · HangingChildIsKilledAtTheDeadline launch it" );

    if ( sw::string_view( pMode ) == "hang" )
    {
        // 크래시 처리기가 멈춘 자식을 흉내 낸다 — 아무것도 찍지 않고 서 있다.
        std::this_thread::sleep_for( std::chrono::seconds( 600 ) );
        return;
    }
    std::fprintf( stdout, "SW_CHILD_ECHO:%s\n", pMode );
    std::fflush( stdout );
}

/**
 * @brief [TestFrameworkTest] 자식은 걸어 준 환경 변수를 받고, 그 출력이 돌아오며, 부모의 환경은 되돌려진다
 */
SW_TEST_CASE( TestFrameworkTest, ChildProcessGetsItsEnvironmentAndOutputIsCaptured )
{
    SW_ASSERT_TRUE( std::getenv( "SW_TEST_CHILD_MODE" ) == nullptr );

    const test::ChildEnvironmentVariable arrEnvironment[] = {
        { "SW_TEST_CHILD_MODE", "marker42" }
    };
    const test::ChildRunResult child = test::runThisExecutableAsChild( "TestFrameworkTest.ChildRoleEchoesOrHangs", arrEnvironment, 60 );
    SW_ASSERT_TRUE( child._bLaunched );
    SW_EXPECT_FALSE( child._bTimedOut );
    SW_EXPECT_EQUAL( 0, child._exitCode );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( child._output, "SW_CHILD_ECHO:marker42" ), child.getOutputTail().c_str() );
    SW_EXPECT_TRUE_MSG( std::getenv( "SW_TEST_CHILD_MODE" ) == nullptr, "자식에게 걸어 준 환경 변수가 부모에 남았습니다 — 다음 케이스가 자식 역할을 합니다" );
}

/**
 * @brief [TestFrameworkTest] 멈춘 자식은 시한에 죽고 그 사실이 남는다 — 테스트 실행 파일 전체가 CTest 시한까지 서 있지 않는다
 * @details 자식 하나가 멈추면 시한 없이는 실행 파일 전체가 CTest 시한까지 서 있다 지고, 어느 자식이 어디까지 갔는지도
 *          남지 않는다. 시한이 없으면 이 케이스는 600 초를 기다린다.
 */
SW_TEST_CASE( TestFrameworkTest, HangingChildIsKilledAtTheDeadline )
{
    const test::ChildEnvironmentVariable arrEnvironment[] = {
        { "SW_TEST_CHILD_MODE", "hang" }
    };
    const sw::Stopwatch        stopwatch;
    const test::ChildRunResult child = test::runThisExecutableAsChild( "TestFrameworkTest.ChildRoleEchoesOrHangs", arrEnvironment, 1 );

    const int64 elapsedSeconds = stopwatch.getElapsedMilliseconds() / 1000;

    SW_ASSERT_TRUE( child._bLaunched );
    SW_EXPECT_TRUE( child._bTimedOut );
    SW_EXPECT_NOT_EQUAL( 0, child._exitCode );
    SW_EXPECT_TRUE_MSG( elapsedSeconds < 60, "시한(1 초)을 넘긴 자식을 죽이지 못하고 기다렸습니다" );
}

/**
 * @brief [TestFrameworkTest] 작업 스레드 여럿이 동시에 실패해도 실패는 하나도 빠지지 않고 깨지지 않는다
 * @details 단언은 `runParallel` 본문 · `std::thread` 람다에서도 불린다. 기록이 락 없는 `push_back` 이면 둘이 동시에 실패할 때
 *          벡터가 깨진다 — 병렬 코드가 틀렸다는 것을 알려야 할 그 순간에 테스트 실행 파일이 죽거나 실패 수가 틀린다.
 *          (작업 스레드 안의 `SW_ASSERT_*` 는 그 람다만 끝낸다 — gtest 와 같다.)
 */
SW_TEST_CASE( TestFrameworkTest, FailuresFromManyThreadsAreAllRecorded )
{
    constexpr uint32 kThreadCount        = 8;
    constexpr uint32 kFailurePerThread   = 300;
    size_t           recordedCount       = 0;
    bool             bEveryRecordIsWhole = true;
    {
        test::ScopedFailureCapture capture;
        sw::vector<std::thread>    listThread;
        for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
        {
            listThread.emplace_back( []()
            {
                for ( uint32 failureIndex = 0; failureIndex < kFailurePerThread; ++failureIndex )
                    SW_EXPECT_TRUE_MSG( failureIndex == kFailurePerThread, "from a worker thread" );
            } );
        }
        for ( std::thread& thread : listThread )
            thread.join();

        recordedCount = capture.getListFailure().size();
        for ( const test::TestFailure& failure : capture.getListFailure() )
            bEveryRecordIsWhole = bEveryRecordIsWhole && failure._message == "from a worker thread";
    }

    SW_EXPECT_EQUAL( size_t{ kThreadCount * kFailurePerThread }, recordedCount );
    SW_EXPECT_TRUE( bEveryRecordIsWhole );
}

/**
 * @brief [TestFrameworkTest] `--test_shuffle=<씨앗>` 은 같은 케이스를 빠짐없이, 스위트를 붙인 채로, 씨앗마다 같은 순서로 섞는다
 * @details 순서에 기대는 테스트를 찾으려고 섞는다 — 진 순서를 **다시 만들 수 있어야** 고칠 수 있다. 스위트를 붙여 두는 것은 gtest 와 같다.
 *          섞지 않으면 등록 순서 그대로다.
 */
SW_TEST_CASE( TestFrameworkTest, ShuffleKeepsEveryCaseAndReplaysWithTheSameSeed )
{
    const auto registerProbes = []( test::TestRegistry& registry )
    {
        for ( const utf8* pSuite : { "AlphaProbeTest", "BetaProbeTest", "GammaProbeTest", "DeltaProbeTest" } )
        {
            for ( const utf8* pCase : { "One", "Two", "Three", "Four", "Five" } )
                registry.registerTest( pSuite, pCase, {} );
        }
    };
    const auto toNames = []( const sw::vector<const test::TestCaseInfo*>& listRun )
    {
        sw::vector<sw::string> listName;
        for ( const test::TestCaseInfo* pTestInfo : listRun )
            listName.push_back( pTestInfo->fullName() );
        return listName;
    };

    test::TestRegistry plainRegistry;
    registerProbes( plainRegistry );
    const sw::vector<sw::string> listPlain = toNames( plainRegistry.buildRunOrder( 0 ) );
    SW_ASSERT_EQUAL( size_t{ 20 }, listPlain.size() );
    SW_EXPECT_STREQ( "AlphaProbeTest.One", listPlain.front() );
    SW_EXPECT_STREQ( "DeltaProbeTest.Five", listPlain.back() );

    test::TestRegistry shuffledRegistry;
    registerProbes( shuffledRegistry );
    configureWithArgument( shuffledRegistry, "--test_shuffle=7" );
    const sw::vector<sw::string> listFirst  = toNames( shuffledRegistry.buildRunOrder( 0 ) );
    const sw::vector<sw::string> listReplay = toNames( shuffledRegistry.buildRunOrder( 0 ) );
    const sw::vector<sw::string> listNext   = toNames( shuffledRegistry.buildRunOrder( 1 ) );

    SW_EXPECT_TRUE_MSG( listFirst == listReplay, "같은 씨앗 · 같은 회차인데 순서가 다르다 — 진 순서를 다시 만들 수 없다" );
    SW_EXPECT_TRUE_MSG( listFirst != listPlain, "섞었는데 등록 순서 그대로다" );
    SW_EXPECT_TRUE_MSG( listFirst != listNext, "회차가 바뀌었는데 같은 순서다 — --test_repeat 와 함께 쓰면 같은 순서만 되풀이한다" );

    // 빠짐없이 — 정렬하면 등록한 것과 같다.
    sw::vector<sw::string> listSorted   = listFirst;
    sw::vector<sw::string> listExpected = listPlain;
    std::sort( listSorted.begin(), listSorted.end() );
    std::sort( listExpected.begin(), listExpected.end() );
    SW_EXPECT_TRUE( listSorted == listExpected );

    // 스위트는 붙어 있다 — 스위트가 바뀌는 자리가 정확히 셋.
    uint32 suiteChangeCount = 0;
    for ( size_t index = 1; index < listFirst.size(); ++index )
    {
        const sw::string previousSuite = listFirst[index - 1].substr( 0, listFirst[index - 1].find( '.' ) );
        const sw::string currentSuite  = listFirst[index].substr( 0, listFirst[index].find( '.' ) );
        suiteChangeCount += previousSuite != currentSuite ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( 3u, suiteChangeCount );
}
