// DevConsoleTest — 개발 콘솔의 줄 상태: 내장 명령 clear 가 출력 줄만 비우고 기록은 남긴다, 자동완성 후보에 내장 명령이 든다, 비밀 인자는 기록 · 출력에 남지 않는다(nogpu).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Console/DevConsole.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"

using namespace sw;

namespace
{
    /** @brief 테스트 동안만 엔진 레지스트리에 올린 비밀 인자 명령입니다(소멸자가 뺀다). 본문은 받은 비밀번호를 그대로 돌려준다 — 실행은 가리지 않은 줄로 돈다. */
    class ScopedSecretCommand
    {
    public:
        ScopedSecretCommand()
            : _registration{ "test.secretlogin", "test.secretlogin <account> <password>", "help", &ScopedSecretCommand::run, nullptr }
            , _bRegistered{ SW_FALSE }
        {
            _bRegistered = engine::getSubsystem<DevCommandRegistry>().registerEntry( &_registration ) ? SW_TRUE : SW_FALSE;
        }
        ~ScopedSecretCommand() { engine::getSubsystem<DevCommandRegistry>().unregisterEntry( &_registration ); }
        ScopedSecretCommand( const ScopedSecretCommand& )            = delete;
        ScopedSecretCommand& operator=( const ScopedSecretCommand& ) = delete;

        bool isRegistered() const { return _bRegistered == SW_TRUE; }

    private:
        static bool run( const vector<string>& listArgument, string& outReply )
        {
            outReply = listArgument.size() == 2 && listArgument[1] == "hunter2" ? "accepted" : "wrong";
            return true;
        }

    private:
        DevCommandRegistration _registration;
        uint8                  _bRegistered : 1;
    };
} // namespace

/**
 * @brief [DevConsoleTest] 비밀번호를 받는 명령은 원래 줄로 돌되, 기록(위 화살표)과 출력 줄에는 비밀번호가 남지 않는다
 */
SW_TEST_CASE( DevConsoleTest, SecretArgumentStaysOutOfHistoryAndOutput )
{
    // 틀리면: 친 줄을 그대로 남기면 출력 · 기록 단언이, 가린 줄로 실행하면 답 단언이 진다.
    const ScopedSecretCommand command;
    SW_REQUIRE_TRUE( command.isRegistered() );
    DevConsole console( nullptr );
    SW_EXPECT_TRUE( console.submit( "test.secretlogin alice hunter2" ) == DevConsoleResult::Ok );
    SW_REQUIRE_EQUAL( size_t{ 1 }, console.getHistory().size() );
    SW_EXPECT_EQUAL( string( "test.secretlogin alice ***" ), console.getHistory()[0] );
    SW_REQUIRE_EQUAL( size_t{ 2 }, console.getOutput().size() );
    SW_EXPECT_EQUAL( string( "> test.secretlogin alice ***" ), console.getOutput()[0]._text );
    SW_EXPECT_EQUAL( string( "accepted" ), console.getOutput()[1]._text );
}

/**
 * @brief [DevConsoleTest] `clear` 는 출력 줄을 모두 비우고 친 줄 · 답을 다시 남기지 않는다 — 기록(위 화살표)에는 남는다
 */
SW_TEST_CASE( DevConsoleTest, ClearEmptiesTheOutputAndKeepsTheHistory )
{
    DevConsole console( nullptr );
    console.appendNote( "one\ntwo" );
    SW_REQUIRE_EQUAL( size_t{ 2 }, console.getOutput().size() );

    SW_EXPECT_TRUE( console.submit( "  clear " ) == DevConsoleResult::Ok );
    SW_EXPECT_TRUE( console.getOutput().empty() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, console.getHistory().size() );
    SW_EXPECT_EQUAL( string( "clear" ), console.getHistory()[0] );

    console.appendNote( "three" );
    console.clearOutput();
    SW_EXPECT_TRUE( console.getOutput().empty() );
}

/**
 * @brief [DevConsoleTest] 줄을 들지 않는 길로 온 `clear` 는 답 없이 성공이고, 첫 낱말 자동완성 후보에 든다
 */
SW_TEST_CASE( DevConsoleTest, ClearIsABuiltinCommand )
{
    const DevConsole console( nullptr );
    string           reply = "stale";
    SW_EXPECT_TRUE( console.execute( "clear", reply ) == DevConsoleResult::Ok );
    SW_EXPECT_TRUE( reply.empty() );

    vector<string> listCandidate;
    console.collectCompletions( "cle", listCandidate );
    SW_EXPECT_TRUE( VectorUtil::contains( listCandidate, string( "clear" ) ) );
}
