#include "TestFramework/TestFramework.h"

using namespace sw;
using sw::test::TestRunner;

SW_TEST_CASE( TestRunnerTest, FilterIncludesAndExcludes )
{
    SW_EXPECT_TRUE( TestRunner::matchesFilter( "*", "A.b" ) );
    SW_EXPECT_TRUE( TestRunner::matchesFilter( "A.*", "A.b" ) );
    SW_EXPECT_FALSE( TestRunner::matchesFilter( "B.*", "A.b" ) );
    SW_EXPECT_TRUE( TestRunner::matchesFilter( "B.*,A.*", "A.b" ) );
    SW_EXPECT_FALSE( TestRunner::matchesFilter( "*,-A.b", "A.b" ) );
    SW_EXPECT_TRUE( TestRunner::matchesFilter( "-A.c", "A.b" ) ); // 빼기만 있으면 나머지는 모두 든다
}

SW_TEST_CASE( TestRunnerTest, ParseArgumentsRejectsUnknownFlags )
{
    test::TestRunOptions options;
    string               error;
    const char*          arrArgument[] = { "CoreTest", "--filter=A.*", "--list" };
    SW_REQUIRE_TRUE( TestRunner::parseArguments( 3, arrArgument, options, error ) );
    SW_EXPECT_EQUAL( string( "A.*" ), options._filter );
    SW_EXPECT_TRUE( options._bListOnly );
    const char* arrBad[] = { "CoreTest", "--frobnicate" };
    SW_EXPECT_FALSE( TestRunner::parseArguments( 2, arrBad, options, error ) );
    SW_EXPECT_FALSE( error.empty() );
}
