// HeadlessTaskTest — 헤드리스 작업 고르기(`HeadlessTaskRegistration::selectAndRun`): 우선순위가 앞선 하나만 돌고, 이 호스트에서 돌지 않는 작업이 청해지면 아무것도 돌리지 않는다(nogpu).
#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/HeadlessTask.h"

using namespace sw;

namespace
{
    struct HeadlessTaskTestUtil
    {
        /** @brief 표의 함수는 맨 함수 포인터라 청함 · 실행 기록을 정적 칸에 둔다(케이스마다 `reset`). */
        static inline bool   s_bFirstRequested  = false;
        static inline bool   s_bSecondRequested = false;
        static inline uint32 s_firstRunCount    = 0;
        static inline uint32 s_secondRunCount   = 0;

        static void reset( bool bFirst, bool bSecond )
        {
            s_bFirstRequested  = bFirst;
            s_bSecondRequested = bSecond;
            s_firstRunCount    = 0;
            s_secondRunCount   = 0;
        }

        static bool isFirstRequested( const CommandLineManager& /*commandLine*/ ) { return s_bFirstRequested; }
        static bool isSecondRequested( const CommandLineManager& /*commandLine*/ ) { return s_bSecondRequested; }
        static bool runFirst( HeadlessTaskContext& /*context*/ )
        {
            ++s_firstRunCount;
            return true;
        }
        static bool runSecondAndFail( HeadlessTaskContext& /*context*/ )
        {
            ++s_secondRunCount;
            return false;
        }
    };
} // namespace

/**
 * @brief [HeadlessTaskTest] 둘이 청해지면 우선순위가 앞선 하나만 돈다 — 본문이 false 면 상태가 실패이고 헤드리스로 들어갔다
 */
SW_TEST_CASE( HeadlessTaskTest, OnlyTheFirstRequestedTaskRuns )
{
    using Util = HeadlessTaskTestUtil;
    const HeadlessTaskRegistration        first{ "First", 10, HeadlessTaskHost::Any, &Util::isFirstRequested, &Util::runFirst };
    const HeadlessTaskRegistration        second{ "Second", 20, HeadlessTaskHost::Any, &Util::isSecondRequested, &Util::runSecondAndFail };
    const HeadlessTaskRegistration* const arrTask[] = { &first, &second };
    CommandLineManager                    commandLine;

    Util::reset( true, true );
    HeadlessTaskState   bothState;
    HeadlessTaskContext bothContext{ commandLine, bothState, false };
    SW_EXPECT_TRUE( HeadlessTaskRegistration::selectAndRun( arrTask, bothContext ) == HeadlessTaskResult::Ran );
    SW_EXPECT_EQUAL( 1u, Util::s_firstRunCount );
    SW_EXPECT_EQUAL( 0u, Util::s_secondRunCount );
    SW_EXPECT_TRUE( bothState.isHeadless() );
    SW_EXPECT_FALSE( bothState._bFailed );

    Util::reset( false, true );
    HeadlessTaskState   secondState;
    HeadlessTaskContext secondContext{ commandLine, secondState, false };
    SW_EXPECT_TRUE( HeadlessTaskRegistration::selectAndRun( arrTask, secondContext ) == HeadlessTaskResult::Ran );
    SW_EXPECT_EQUAL( 1u, Util::s_secondRunCount );
    SW_EXPECT_TRUE( secondState._bFailed );

    Util::reset( false, false );
    HeadlessTaskState   noneState;
    HeadlessTaskContext noneContext{ commandLine, noneState, false };
    SW_EXPECT_TRUE( HeadlessTaskRegistration::selectAndRun( arrTask, noneContext ) == HeadlessTaskResult::NotRequested );
    SW_EXPECT_FALSE( noneState.isHeadless() );
}

/**
 * @brief [HeadlessTaskTest] 클라이언트 전용 작업이 서버에 청해지면 앞선 작업도 돌리지 않고 실패한다 — 조용히 넘기지 않는다
 * @details 변이: 호스트 검사를 고른 작업에만 하면 앞선 `First` 가 돌아 실행 수 단언이 진다.
 */
SW_TEST_CASE( HeadlessTaskTest, WrongHostTaskRejectsTheWholeRequest )
{
    using Util = HeadlessTaskTestUtil;
    const HeadlessTaskRegistration        first{ "First", 10, HeadlessTaskHost::Any, &Util::isFirstRequested, &Util::runFirst };
    const HeadlessTaskRegistration        clientOnly{ "ClientOnly", 20, HeadlessTaskHost::Client, &Util::isSecondRequested, &Util::runSecondAndFail };
    const HeadlessTaskRegistration* const arrTask[] = { &first, &clientOnly };
    CommandLineManager                    commandLine;

    Util::reset( true, true );
    HeadlessTaskState   serverState;
    HeadlessTaskContext serverContext{ commandLine, serverState, true };
    SW_EXPECT_LOG( "Headless.HeadlessTaskWrongHost" );
    SW_EXPECT_TRUE( HeadlessTaskRegistration::selectAndRun( arrTask, serverContext ) == HeadlessTaskResult::Rejected );
    SW_EXPECT_EQUAL( 0u, Util::s_firstRunCount );
    SW_EXPECT_EQUAL( 0u, Util::s_secondRunCount );
    SW_EXPECT_TRUE( serverState._bFailed );
    SW_EXPECT_TRUE( serverState.isHeadless() );

    Util::reset( true, true );
    HeadlessTaskState   clientState;
    HeadlessTaskContext clientContext{ commandLine, clientState, false };
    SW_EXPECT_TRUE( HeadlessTaskRegistration::selectAndRun( arrTask, clientContext ) == HeadlessTaskResult::Ran );
    SW_EXPECT_EQUAL( 1u, Util::s_firstRunCount );
}
