// DevConsoleTest — 개발 콘솔의 줄 상태: 내장 명령 clear 가 출력 줄만 비우고 기록은 남긴다, 자동완성 후보에 내장 명령이 든다(nogpu).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Console/DevConsole.h"

using namespace sw;

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
