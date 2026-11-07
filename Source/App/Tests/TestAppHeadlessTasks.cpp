#include "pch.h"

#include "App/AppHeadlessTasks.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    struct AppHeadlessTasksTestUtil
    {
        using TaskResult = AppHeadlessTasks::TaskResult;

        /** @brief 표의 함수는 맨 함수 포인터라 불린 수를 정적 칸에 센다(케이스마다 `reset`). */
        static inline uint32 s_calledCount = 0;

        static void reset() { s_calledCount = 0; }

        template <TaskResult kResult>
        static TaskResult countAndReturn( const CommandLineManager& /*commandLine*/ )
        {
            ++s_calledCount;
            return kResult;
        }
    };
} // namespace

/** @brief [AppHeadlessTasksTest] 실패한 작업이 있어도 뒤의 작업을 돌리고, 끝에 실패로 답한다 */
SW_TEST_CASE( AppHeadlessTasksTest, FailureRunsTheRestAndFails )
{
    using Util                                          = AppHeadlessTasksTestUtil;
    using Result                                        = AppHeadlessTasks::TaskResult;
    constexpr AppHeadlessTasks::TaskFunction kArrTask[] = { &Util::countAndReturn<Result::Failed>, &Util::countAndReturn<Result::NotRequested>, &Util::countAndReturn<Result::Succeeded> };

    Util::reset();
    CommandLineManager commandLine;
    SW_EXPECT_FALSE( AppHeadlessTasks::runTable( kArrTask, 3, commandLine ) );
    SW_EXPECT_EQUAL( 3u, Util::s_calledCount );
}

/** @brief [AppHeadlessTasksTest] `Finished` 는 뒤의 작업을 보지 않고 멈춘다 — 앞선 실패가 없으면 성공이고, 있으면 실패가 남는다 */
SW_TEST_CASE( AppHeadlessTasksTest, FinishedStopsTheTable )
{
    using Util                                                 = AppHeadlessTasksTestUtil;
    using Result                                               = AppHeadlessTasks::TaskResult;
    constexpr AppHeadlessTasks::TaskFunction kArrFinished[]    = { &Util::countAndReturn<Result::Succeeded>, &Util::countAndReturn<Result::Finished>, &Util::countAndReturn<Result::Failed> };
    constexpr AppHeadlessTasks::TaskFunction kArrFailedFirst[] = { &Util::countAndReturn<Result::Failed>, &Util::countAndReturn<Result::Finished>, &Util::countAndReturn<Result::Succeeded> };

    CommandLineManager commandLine;
    Util::reset();
    SW_EXPECT_TRUE( AppHeadlessTasks::runTable( kArrFinished, 3, commandLine ) );
    SW_EXPECT_EQUAL( 2u, Util::s_calledCount );

    Util::reset();
    SW_EXPECT_FALSE( AppHeadlessTasks::runTable( kArrFailedFirst, 3, commandLine ) );
    SW_EXPECT_EQUAL( 2u, Util::s_calledCount );
}

/** @brief [AppHeadlessTasksTest] 빈 표는 아무것도 부르지 않고 성공이다 */
SW_TEST_CASE( AppHeadlessTasksTest, EmptyTableSucceeds )
{
    CommandLineManager commandLine;
    SW_EXPECT_TRUE( AppHeadlessTasks::runTable( nullptr, 0, commandLine ) );
}
