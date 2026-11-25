#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Host/EditorModuleCommandlets.h"

using namespace sw;

namespace
{
    struct EditorModuleCommandletsTestUtil
    {
        /** @brief 표의 함수는 맨 함수 포인터라 불린 수를 정적 칸에 센다(케이스마다 `reset`). */
        static inline uint32 s_runCount = 0;

        static void reset() { s_runCount = 0; }

        template <bool kRequested>
        static bool isRequested( const CommandLineManager& /*commandLine*/ )
        {
            return kRequested;
        }

        template <bool kSucceeded>
        static bool countAndReturn( const CommandLineManager& /*commandLine*/, EditorModuleHeadlessSession& /*editorModule*/ )
        {
            ++s_runCount;
            return kSucceeded;
        }
    };
} // namespace

/** @brief [EditorModuleCommandletsTest] 실패한 작업이 있어도 뒤의 작업을 돌리고, 끝에 실패로 답한다 */
SW_TEST_CASE( EditorModuleCommandletsTest, FailureRunsTheRestAndFails )
{
    using Util                                  = EditorModuleCommandletsTestUtil;
    constexpr EditorModuleCommandlet kArrTask[] = {
        {&Util::isRequested<true>,    CommandletHost::Any, &Util::countAndReturn<false>},
        {&Util::isRequested<true>, CommandletHost::Client,  &Util::countAndReturn<true>},
    };

    Util::reset();
    CommandLineManager          commandLine;
    EditorModuleHeadlessSession editorModule; // 모듈을 찾는 작업이 없으면 아무것도 올리지 않는다
    SW_EXPECT_FALSE( EditorModuleCommandlet::runTable( kArrTask, commandLine, editorModule ) );
    SW_EXPECT_EQUAL( 2u, Util::s_runCount );
}

/** @brief [EditorModuleCommandletsTest] 청해지지 않은 줄은 돌리지 않는다 — 청해졌는가와 실행이 같은 줄에서 나온다 */
SW_TEST_CASE( EditorModuleCommandletsTest, RunsOnlyRequestedRows )
{
    using Util                                  = EditorModuleCommandletsTestUtil;
    constexpr EditorModuleCommandlet kArrTask[] = {
        {&Util::isRequested<false>,    CommandletHost::Any, &Util::countAndReturn<false>},
        { &Util::isRequested<true>,    CommandletHost::Any,  &Util::countAndReturn<true>},
        {&Util::isRequested<false>, CommandletHost::Client, &Util::countAndReturn<false>},
    };

    Util::reset();
    CommandLineManager          commandLine;
    EditorModuleHeadlessSession editorModule;
    SW_EXPECT_TRUE( EditorModuleCommandlet::runTable( kArrTask, commandLine, editorModule ) );
    SW_EXPECT_EQUAL( 1u, Util::s_runCount );
}

/** @brief [EditorModuleCommandletsTest] 빈 표는 아무것도 부르지 않고 성공이다 */
SW_TEST_CASE( EditorModuleCommandletsTest, EmptyTableSucceeds )
{
    CommandLineManager          commandLine;
    EditorModuleHeadlessSession editorModule;
    SW_EXPECT_TRUE( EditorModuleCommandlet::runTable( {}, commandLine, editorModule ) );
}

/** @brief [EditorModuleCommandletsTest] 에디터 모듈 세션은 부르는 작업이 없으면 모듈을 올리지 않는다(헤드리스 실행 한 번에 많아야 한 번 올린다) */
SW_TEST_CASE( EditorModuleCommandletsTest, EditorModuleSessionLoadsLazily )
{
    const EditorModuleHeadlessSession editorModule;
    SW_EXPECT_EQUAL( 0u, editorModule.getLoadCount() );
}
