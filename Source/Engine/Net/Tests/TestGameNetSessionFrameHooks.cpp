#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/EngineFrameHooks.h"
#include "Engine/Net/GameNetSession.h"

using namespace sw;

namespace
{
    struct GameNetSessionFrameHooksTestUtil
    {
        /** @brief 지금 시계가 프레임 @p number 이면 true 입니다. */
        static bool isFrameClock( uint64 number )
        {
            LogClock   clock      = LogClock::Tick;
            uint64     readNumber = 0;
            const bool bHasClock  = Logger::getClockNumber( clock, readNumber );
            return bHasClock && clock == LogClock::Frame && readNumber == number;
        }

        static bool hasClock()
        {
            LogClock clock  = LogClock::Tick;
            uint64   number = 0;
            return Logger::getClockNumber( clock, number );
        }
    };
} // namespace

/** @brief [GameNetSessionFrameHooksTest] 세션이 서버이면 프레임 앞 훅이 로그 시계를 프레임 번호로 맞추고, 서버가 아니게 되면 뗀다. 처음부터 서버가 아니면 건드리지 않는다 */
SW_TEST_CASE( GameNetSessionFrameHooksTest, ListenHostShowsTheFrameClock )
{
    using Util = GameNetSessionFrameHooksTestUtil;
    Logger::clearClockNumber();

    GameNetSession         session;
    const EngineFrameHooks hooks = session.makeClientHostFrameHooks();
    EngineFrameContext     context{};

    hooks._onFrameBegin( context ); // 프레임 0 — 세션이 닫혀 있다
    SW_EXPECT_FALSE( Util::hasClock() );

    GameNetSessionParams params;
    params._role = GameNetRole::Server; // 포트 0 — 운영체제가 고른다
    SW_REQUIRE_TRUE( session.initialize( params ) );

    hooks._onFrameBegin( context ); // 프레임 1
    SW_EXPECT_TRUE( Util::isFrameClock( 1 ) );
    hooks._onFrameBegin( context ); // 프레임 2
    SW_EXPECT_TRUE( Util::isFrameClock( 2 ) );

    session.shutdown();
    hooks._onFrameBegin( context );
    SW_EXPECT_FALSE( Util::hasClock() );

    Logger::clearClockNumber();
}

/** @brief [GameNetSessionFrameHooksTest] 프레임 끝 훅은 종료 코드를 정하지 않고, 서버 틱을 모르는 세션에서는 상태 해시를 남기지 않는다 */
SW_TEST_CASE( GameNetSessionFrameHooksTest, FrameEndDoesNotAskToQuit )
{
    GameNetSession         session;
    const EngineFrameHooks hooks = session.makeClientHostFrameHooks();
    EngineFrameContext     context{};
    SW_EXPECT_FALSE( hooks._onFrameEnd( context ).has_value() ); // 종료 코드를 내지 않는다(kEngineFrameRunning)
}
