// 테스트 틀 — 케이스 쓰기 경로는 한 케이스 안에서 같은 이름이면 같고 이름마다 다르며, 단언 가로채기(`test::ScopedAssertCapture`)는 멈추지 않고 세고 겹쳐 걸 수 있다.
// 자식 역할(`SW_TEST_CHILD_ROLE` · `runChildRole`)은 목록에 들지 않고, 인자 · 출력이 오가며, 멈추면 시한에 죽는다.
// 거르개 · 몫 · 스냅숏 · 로그 선언 · 픽스처 · 매개변수 케이스는 `TestTestRunner.cpp` 가 본다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Process/StandardOutput.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestRunner.h"
#include "Core/Time/MonotonicClock.h"

#include <algorithm>

using namespace sw;

/**
 * @brief 같은 이름을 다시 물으면 같은 경로다(쓴 것을 읽는다), 다른 이름은 다른 경로이고 이름 · 확장자는 그대로 남는다(로더가 확장자로 형식을 고른다).
 */
SW_TEST_CASE( TestFrameworkTest, ScratchPathIsStableWithinACaseAndDistinctPerName )
{
    const string path = test::makeScratchPath( "sample.wav" );
    SW_EXPECT_TRUE( StringUtil::endsWith( path, "sample.wav" ) );
    SW_EXPECT_EQUAL( path, test::makeScratchPath( "sample.wav" ) );
    SW_EXPECT_TRUE( path != test::makeScratchPath( "other.wav" ) );
    SW_EXPECT_TRUE( FileUtil::isDirectory( FileUtil::getDirectoryPart( path ) ) ); // 폴더는 처음 부를 때 선다
}

#if defined( SW_DO_CHECKS )
/**
 * @brief 단언 가로채기 안에서는 SW_ASSERT 가 멈추지 않고 세어지며 그 뒤 줄이 돈다 — 겹쳐 건 가로채기가 풀려도 바깥은 그대로다(Debug 만 단언이 있다).
 */
SW_TEST_CASE( TestFrameworkTest, AssertCaptureCountsInsteadOfBreaking )
{
    test::ScopedAssertCapture outer;
    bool                      bReachedAfterAssert = false;
    SW_ASSERT( 1 + 1 == 3 );
    bReachedAfterAssert = true;
    SW_EXPECT_TRUE( bReachedAfterAssert );
    SW_EXPECT_EQUAL( 1u, outer.getCount() );

    {
        test::ScopedAssertCapture inner;
        SW_ASSERT( false );
        SW_EXPECT_EQUAL( 1u, inner.getCount() );
    }
    SW_ASSERT( false ); // 안쪽이 풀려도 바깥 가로채기는 그대로다
    SW_EXPECT_EQUAL( 3u, outer.getCount() );
}
#endif

/**
 * @brief 자식 역할: 인자를 표식으로 찍고 끝나거나(`hang` 이면) 아무것도 찍지 않고 서 있다(크래시 처리기가 멈춘 자식을 흉내 낸다).
 */
SW_TEST_CHILD_ROLE( TestFrameworkTest, ChildEchoesOrHangs, ._timeoutSeconds = 600.0f )
{
    const string argument = test::getChildRoleArgument();
    SW_REQUIRE_FALSE( argument.empty() );
    if ( argument == "hang" )
        MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + 600 * constant::time::kNanosecondsPerSecond );
    SW_EXPECT_TRUE( StandardOutput::write( "SW_CHILD_ECHO:" + argument + "\n" ) );
}

/**
 * @brief 자식 역할은 평소 실행의 목록 · 케이스 수에 들지 않는다 — 단언 없는 자식 역할이 평소 실행에서 실패로 세지 않게.
 */
SW_TEST_CASE( TestFrameworkTest, ChildRolesStayOutOfTheCaseList )
{
    const vector<string> listName = test::TestRunner::collectCaseNames();
    SW_EXPECT_TRUE( std::find( listName.begin(), listName.end(), "TestFrameworkTest.ChildEchoesOrHangs" ) == listName.end() );
    SW_EXPECT_TRUE( std::find( listName.begin(), listName.end(), "TestFrameworkTest.ChildRolesStayOutOfTheCaseList" ) != listName.end() );
}

/**
 * @brief 자식은 인자를 받고 그 출력이 돌아오며 통과하면 0 으로 끝난다
 */
SW_TEST_CASE( TestFrameworkTest, ChildRoleGetsItsArgumentAndOutputIsCaptured, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const test::ChildRunResult child = test::runChildRole( "TestFrameworkTest.ChildEchoesOrHangs", "marker42", 120.0f );
    SW_EXPECT_FALSE( child._bTimedOut );
    SW_REQUIRE_TRUE_MSG( child._exitCode.has_value(), child.getOutputTail() );
    SW_EXPECT_EQUAL( 0, *child._exitCode );
    SW_EXPECT_TRUE_MSG( StringUtil::contains( child._output, "SW_CHILD_ECHO:marker42" ), child.getOutputTail() );
}

/**
 * @brief 멈춘 자식은 시한에 죽고 그 사실이 남는다 — 시한이 없으면 이 케이스는 자식의 600 초를 기다린다(실행 전체가 케이스 시한에 진다).
 */
SW_TEST_CASE( TestFrameworkTest, HangingChildIsKilledAtTheDeadline, ._tags = "slow", ._timeoutSeconds = 60.0f )
{
    const Stopwatch            stopwatch;
    const test::ChildRunResult child = test::runChildRole( "TestFrameworkTest.ChildEchoesOrHangs", "hang", 3.0f );
    SW_EXPECT_TRUE( child._bLaunched );
    SW_EXPECT_TRUE( child._bTimedOut );
    SW_REQUIRE_TRUE( child._exitCode.has_value() ); // 죽인 자식도 거둔다 — 종료 코드가 있다
    SW_EXPECT_NOT_EQUAL( 0, *child._exitCode );
    SW_EXPECT_TRUE_MSG( stopwatch.getElapsedMilliseconds() < 30000, "the harness waited for a child past its 3 s deadline" );
}

namespace
{
    /** @brief 안정 이름을 가진 매개변수 원소입니다 — 이름이 `TestFrameworkTest.NamedParam/<이름>` 이 된다. */
    struct NamedTestParam
    {
        const utf8* _pTestName;
        int32       _value;
    };
    constexpr NamedTestParam kArrNamedParam[] = {
        {"alpha", 1},
        { "beta", 2}
    };
} // namespace

/** @brief 매개변수 원소가 `_pTestName` 을 가지면 케이스 이름이 번호가 아니라 그 이름이다 — 배열 순서를 바꿔도 같은 입력을 가리킨다 */
SW_TEST_CASE_EACH( TestFrameworkTest, NamedParam, kArrNamedParam )
{
    SW_EXPECT_TRUE( param._value > 0 );
}

/** @brief 안정 이름으로 등록된 케이스가 목록에 이름으로 있다(번호 이름은 없다) */
SW_TEST_CASE( TestFrameworkTest, NamedParamsRegisterByName )
{
    const vector<string> listName = test::TestRunner::collectCaseNames();
    SW_EXPECT_TRUE( std::find( listName.begin(), listName.end(), string( "TestFrameworkTest.NamedParam/alpha" ) ) != listName.end() );
    SW_EXPECT_TRUE( std::find( listName.begin(), listName.end(), string( "TestFrameworkTest.NamedParam/beta" ) ) != listName.end() );
    SW_EXPECT_TRUE( std::find( listName.begin(), listName.end(), string( "TestFrameworkTest.NamedParam/0" ) ) == listName.end() );
}

/** @brief 격리 케이스는 자식 프로세스에서 돈다(부모 실행기가 `--test-isolated-case` 로 띄운다). 자식의 판정이 부모 실행의 이 케이스 결과다 */
SW_TEST_CASE( TestFrameworkTest, IsolatedCaseRunsInAChild, ._timeoutSeconds = 60.0f, ._bIsolated = true )
{
    SW_EXPECT_TRUE( test::isRunningIsolated() );
}

/** @brief 보통 케이스는 부모 프로세스에서 돈다 */
SW_TEST_CASE( TestFrameworkTest, PlainCaseRunsInProcess )
{
    SW_EXPECT_FALSE( test::isRunningIsolated() );
}
