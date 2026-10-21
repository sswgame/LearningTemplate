/**
 * @file TestAppHeadlessTasks.cpp
 * @brief AppHeadlessTasks::runTable — App 헤드리스 작업 표의 종료 코드 규칙(실패해도 뒤를 계속, `Finished` 에서 멈춤).
 * @details 빌드의 CookAssets 타깃 · `MakePackage.py` 같은 호출자는 종료 코드로만 실패를 안다. 실패 하나가 뒤 작업을 막거나, `Finished` 뒤 작업이 돌면 그 규칙이 깨진다.
 */
#include "pch.h"

#include "App/AppHeadlessTasks.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    using TaskResult = AppHeadlessTasks::TaskResult;

    /** @brief 작업이 불린 횟수(케이스마다 0 으로 되돌린다). */
    uint32 s_callCount = 0;

    TaskResult returnNotRequested( const CommandLineManager& )
    {
        ++s_callCount;
        return TaskResult::NotRequested;
    }
    TaskResult returnSucceeded( const CommandLineManager& )
    {
        ++s_callCount;
        return TaskResult::Succeeded;
    }
    TaskResult returnFailed( const CommandLineManager& )
    {
        ++s_callCount;
        return TaskResult::Failed;
    }
    TaskResult returnFinished( const CommandLineManager& )
    {
        ++s_callCount;
        return TaskResult::Finished;
    }
} // namespace

/** @brief [AppHeadlessTasksTest] 실패한 작업이 있어도 뒤 작업을 모두 돌리고, 마지막에 실패로 돌려준다 */
SW_TEST_CASE( AppHeadlessTasksTest, FailureRunsTheRestAndFailsAtTheEnd )
{
    CommandLineManager                   commandLine;
    const AppHeadlessTasks::TaskFunction arrTask[] = { &returnFailed, &returnSucceeded, &returnNotRequested };
    s_callCount                                    = 0;
    SW_EXPECT_FALSE( AppHeadlessTasks::runTable( arrTask, 3, commandLine ) );
    SW_EXPECT_EQUAL( 3u, s_callCount );
}

/** @brief [AppHeadlessTasksTest] `Finished` 는 뒤 작업을 보지 않고 성공으로 끝낸다 — 앞에서 실패했으면 실패가 남는다 */
SW_TEST_CASE( AppHeadlessTasksTest, FinishedStopsTheTable )
{
    CommandLineManager                   commandLine;
    const AppHeadlessTasks::TaskFunction arrStop[] = { &returnSucceeded, &returnFinished, &returnFailed };
    s_callCount                                    = 0;
    SW_EXPECT_TRUE( AppHeadlessTasks::runTable( arrStop, 3, commandLine ) );
    SW_EXPECT_EQUAL( 2u, s_callCount );

    const AppHeadlessTasks::TaskFunction arrFailThenStop[] = { &returnFailed, &returnFinished, &returnSucceeded };
    s_callCount                                            = 0;
    SW_EXPECT_FALSE( AppHeadlessTasks::runTable( arrFailThenStop, 3, commandLine ) );
    SW_EXPECT_EQUAL( 2u, s_callCount );
}

/** @brief [AppHeadlessTasksTest] 요청된 작업이 없으면 성공이다(빈 표도) */
SW_TEST_CASE( AppHeadlessTasksTest, NothingRequestedSucceeds )
{
    CommandLineManager                   commandLine;
    const AppHeadlessTasks::TaskFunction arrTask[] = { &returnNotRequested, &returnNotRequested };
    s_callCount                                    = 0;
    SW_EXPECT_TRUE( AppHeadlessTasks::runTable( arrTask, 2, commandLine ) );
    SW_EXPECT_EQUAL( 2u, s_callCount );
    SW_EXPECT_TRUE( AppHeadlessTasks::runTable( nullptr, 0, commandLine ) );
}
