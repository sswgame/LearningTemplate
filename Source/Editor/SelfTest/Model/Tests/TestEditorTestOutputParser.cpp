#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestRunner.h"

#include "Editor/SelfTest/Model/EditorTestOutputParser.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( EditorTestOutputParserTest, EditorScenariosTurnTheEditorOn )
{
    const vector<string> listEditor = EditorTestOutputParser::makeScenarioArguments( "engine/automation/editor/x.scenario.xml", "dx12" );
    SW_EXPECT_TRUE( listEditor[0] == "-dx12" );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( listEditor.size() ) );
    const vector<string> listGame = EditorTestOutputParser::makeScenarioArguments( "game/a/automation/x.scenario.xml", {} );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listGame.size() ) );
}

// 결함: 인자를 `--test_filter` 로 만들고 `:` 로 이었다 — 실행기의 인자는 `--test-filter` 이고 거르개는 쉼표로 가른다(`TestRunner::matchesFilter`).
SW_TEST_CASE( EditorTestOutputParserTest, UnitTestArgumentsMatchTheRunner )
{
    const vector<string> listArgument = EditorTestOutputParser::makeUnitTestArguments( "A.b,C.d" );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listArgument.size() ) );
    SW_EXPECT_TRUE( listArgument[0] == "-test" );
    SW_EXPECT_TRUE( listArgument[1] == "-test-filter=A.b,C.d" );
    SW_EXPECT_TRUE( test::TestRunner::matchesFilter( "A.b,C.d", "C.d" ) ); // 패널이 만드는 모양을 실행기가 받는다
    SW_EXPECT_FALSE( test::TestRunner::matchesFilter( "A.b,C.d", "C.e" ) );
}

SW_TEST_CASE( EditorTestOutputParserTest, CollectsResultStates )
{
    const vector<string>         listLine{ "[==========] 3 of 3 registered case(s), seed 1", "[ RUN      ] A.pass", "[       OK ] A.pass (1.5 ms)", "[ RUN      ] A.fail",
                                   "[  FAILED  ] A.fail (2 ms)", "[ RUN      ] A.crash", "[  FAILED  ] 1 tests, listed below:" };
    vector<EditorTestCaseResult> listResult;
    EditorTestOutputParser::collectResults( listLine, listResult );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( listResult.size() ) );
    SW_EXPECT_TRUE( listResult[0]._name == "A.pass" );
    SW_EXPECT_TRUE( listResult[0].isPassed() );
    SW_EXPECT_NEAR( 1.5f, listResult[0]._milliseconds, 0.001f );
    SW_EXPECT_FALSE( listResult[1].isPassed() );
    SW_EXPECT_TRUE( string_view( listResult[1].getStateText() ) == "FAILED" );
    SW_EXPECT_FALSE( listResult[2].isPassed() ); // 끝 줄 없이 끝났다
    SW_EXPECT_TRUE( string_view( listResult[2].getStateText() ) == "DID NOT FINISH" );
}

// 결함: 케이스 이름 검사가 `/` 를 몰라 매개변수 케이스(`Suite.Case/3`)의 결과를 버렸다.
SW_TEST_CASE( EditorTestOutputParserTest, ParameterizedCaseKeepsItsResult )
{
    const vector<string>         listLine{ "[ RUN      ] A.param/3", "[  FAILED  ] A.param/3 (2 ms)" };
    vector<EditorTestCaseResult> listResult;
    EditorTestOutputParser::collectResults( listLine, listResult );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listResult.size() ) );
    SW_EXPECT_TRUE( listResult[0]._name == "A.param/3" );
    SW_EXPECT_TRUE( listResult[0]._state == EditorTestCaseState::Failed );
}

SW_TEST_CASE( EditorTestOutputParserTest, CountsOnlyErrorLogLines )
{
    const vector<string> listLine{ "12:00:00 [Error] Core - boom", "12:00:00 [Warning] Core - careful", "12:00:00 [Info] Core - [Error] quoted", "[  FAILED  ] A.b (1 ms)",
                                   "12:00:01 [Error] Core - again" };
    SW_EXPECT_EQUAL( 2u, EditorTestOutputParser::countErrorLines( listLine ) );
}
