// DebugKeyCommandsTest — 디버그 키 명령(`debugkeys.*`)은 등록돼 있고, 인자 수가 틀리거나 모르는 슬롯 · 액션이면 까닭을 답하고 바인딩을 건드리지 않는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"

using namespace sw;

/** @brief [DebugKeyCommandsTest] 인자 수가 틀린 줄은 거절한다 */
SW_TEST_CASE( DebugKeyCommandsTest, WrongArgumentCountIsRejected )
{
    const DevCommandRegistry&     registry = engine::getSubsystem<DevCommandRegistry>();
    const DevCommandRegistration* pRebind  = registry.findCommand( "debugkeys.rebind" );
    const DevCommandRegistration* pReset   = registry.findCommand( "debugkeys.rebind.reset" );
    SW_REQUIRE_NOT_NULL( pRebind );
    SW_REQUIRE_NOT_NULL( pReset );
    string reply;
    SW_EXPECT_FALSE( DevCommandRegistry::run( *pRebind, {}, reply ) );
    SW_EXPECT_FALSE( DevCommandRegistry::run( *pRebind, { "DevConsoleToggle" }, reply ) );
    SW_EXPECT_FALSE( DevCommandRegistry::run( *pRebind, { "DevConsoleToggle", "Key.F2", "0", "extra" }, reply ) );
    SW_EXPECT_FALSE( DevCommandRegistry::run( *pReset, { "DevConsoleToggle", "extra" }, reply ) );
}

/** @brief [DebugKeyCommandsTest] 모르는 슬롯 · 액션은 까닭을 답한다 — 되돌릴 바인딩이 없다 */
SW_TEST_CASE( DebugKeyCommandsTest, UnknownNamesAreReported )
{
    const DevCommandRegistry&     registry = engine::getSubsystem<DevCommandRegistry>();
    const DevCommandRegistration* pRebind  = registry.findCommand( "debugkeys.rebind" );
    const DevCommandRegistration* pReset   = registry.findCommand( "debugkeys.rebind.reset" );
    SW_REQUIRE_NOT_NULL( pRebind );
    SW_REQUIRE_NOT_NULL( pReset );
    string reply;
    SW_EXPECT_FALSE( DevCommandRegistry::run( *pRebind, { "DevConsoleToggle", "NoSuch.Slot" }, reply ) );
    SW_EXPECT_FALSE( reply.empty() );
    reply.clear();
    SW_EXPECT_FALSE( DevCommandRegistry::run( *pRebind, { "NoSuchDebugAction", "Key.F2" }, reply ) );
    SW_EXPECT_FALSE( reply.empty() );
    reply.clear();
    SW_EXPECT_FALSE( DevCommandRegistry::run( *pReset, { "NoSuchDebugAction" }, reply ) );
    SW_EXPECT_FALSE( reply.empty() );
}
