// CommandletTest — 헤드리스 작업 고르기(`CommandletRegistration::selectAndRun`): 우선순위가 앞선 하나만 돌고, 이 호스트에서 돌지 않는 작업이 청해지면 아무것도 돌리지 않는다(nogpu).
#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/Commandlet.h"

using namespace sw;

namespace
{
    struct CommandletTestUtil
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
        static bool runFirst( CommandletContext& /*context*/ )
        {
            ++s_firstRunCount;
            return true;
        }
        static bool runSecondAndFail( CommandletContext& /*context*/ )
        {
            ++s_secondRunCount;
            return false;
        }
    };
} // namespace

/**
 * @brief [CommandletTest] 둘이 청해지면 우선순위가 앞선 하나만 돈다 — 본문이 false 면 상태가 실패이고 헤드리스로 들어갔다
 */
SW_TEST_CASE( CommandletTest, OnlyTheFirstRequestedTaskRuns )
{
    using Util = CommandletTestUtil;
    const CommandletRegistration        first{ "First", 10, CommandletHost::Any, &Util::isFirstRequested, &Util::runFirst };
    const CommandletRegistration        second{ "Second", 20, CommandletHost::Any, &Util::isSecondRequested, &Util::runSecondAndFail };
    const CommandletRegistration* const arrTask[] = { &first, &second };
    CommandLineManager                  commandLine;

    Util::reset( true, true );
    CommandletState   bothState;
    CommandletContext bothContext{ commandLine, bothState, false };
    SW_EXPECT_TRUE( CommandletRegistration::selectAndRun( arrTask, bothContext, CommandletLeakBaseline::Keep ) == CommandletResult::Ran );
    SW_EXPECT_EQUAL( 1u, Util::s_firstRunCount );
    SW_EXPECT_EQUAL( 0u, Util::s_secondRunCount );
    SW_EXPECT_TRUE( bothState.isHeadless() );
    SW_EXPECT_FALSE( bothState._bFailed );

    Util::reset( false, true );
    CommandletState   secondState;
    CommandletContext secondContext{ commandLine, secondState, false };
    SW_EXPECT_TRUE( CommandletRegistration::selectAndRun( arrTask, secondContext, CommandletLeakBaseline::Keep ) == CommandletResult::Ran );
    SW_EXPECT_EQUAL( 1u, Util::s_secondRunCount );
    SW_EXPECT_TRUE( secondState._bFailed );

    Util::reset( false, false );
    CommandletState   noneState;
    CommandletContext noneContext{ commandLine, noneState, false };
    SW_EXPECT_TRUE( CommandletRegistration::selectAndRun( arrTask, noneContext, CommandletLeakBaseline::Keep ) == CommandletResult::NotRequested );
    SW_EXPECT_FALSE( noneState.isHeadless() );
}

/**
 * @brief [CommandletTest] 클라이언트 전용 작업이 서버에 청해지면 앞선 작업도 돌리지 않고 실패한다 — 조용히 넘기지 않는다
 * @details 변이: 호스트 검사를 고른 작업에만 하면 앞선 `First` 가 돌아 실행 수 단언이 진다.
 */
SW_TEST_CASE( CommandletTest, WrongHostTaskRejectsTheWholeRequest )
{
    using Util = CommandletTestUtil;
    const CommandletRegistration        first{ "First", 10, CommandletHost::Any, &Util::isFirstRequested, &Util::runFirst };
    const CommandletRegistration        clientOnly{ "ClientOnly", 20, CommandletHost::Client, &Util::isSecondRequested, &Util::runSecondAndFail };
    const CommandletRegistration* const arrTask[] = { &first, &clientOnly };
    CommandLineManager                  commandLine;

    Util::reset( true, true );
    CommandletState   serverState;
    CommandletContext serverContext{ commandLine, serverState, true };
    SW_EXPECT_LOG( "Headless.CommandletWrongHost" );
    SW_EXPECT_TRUE( CommandletRegistration::selectAndRun( arrTask, serverContext, CommandletLeakBaseline::Keep ) == CommandletResult::Rejected );
    SW_EXPECT_EQUAL( 0u, Util::s_firstRunCount );
    SW_EXPECT_EQUAL( 0u, Util::s_secondRunCount );
    SW_EXPECT_TRUE( serverState._bFailed );
    SW_EXPECT_TRUE( serverState.isHeadless() );

    Util::reset( true, true );
    CommandletState   clientState;
    CommandletContext clientContext{ commandLine, clientState, false };
    SW_EXPECT_TRUE( CommandletRegistration::selectAndRun( arrTask, clientContext, CommandletLeakBaseline::Keep ) == CommandletResult::Ran );
    SW_EXPECT_EQUAL( 1u, Util::s_firstRunCount );
}
