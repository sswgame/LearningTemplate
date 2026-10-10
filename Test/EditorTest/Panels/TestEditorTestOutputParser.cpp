#include "pch.h"

#include "Editor/Panels/EditorTestOutputParser.h"

#include "TestFramework/TestFramework.h"

// EditorTestOutputParser — Test Runner 창이 *Test.exe 의 출력(--test_list · 실행 줄)을 읽는 규칙(ImGui 없음).

/**
 * @brief [EditorTestOutputParserTest] --test_list 출력에서 케이스 이름만 모으고, 로그 줄 · host 스위트 목록 · 중복은 빼다
 */
SW_TEST_CASE( EditorTestOutputParserTest, ListOutputBecomesCaseNames )
{
    const sw::vector<sw::string> listLine{
        "Registered tests (3 selected / 400 total):",
        "  FrameProfilerTest.ScopeOverflowDoesNotReadPastTable",
        "[2026-10-10 21:0:0] [Test] [Info] -   FrameProfilerTest.ScopeOverflowDoesNotReadPastTable",
        "  FrameProfilerTest.PercentilesFollowTheDistribution",
        "  RenderPassGPUTest.ViewModesProduceDistinctPictures",
        "  FrameProfilerTest.PercentilesFollowTheDistribution",
        "Host suites - CI cannot run these (1):",
        "  RenderPassGPUTest - needs a GPU",
    };
    sw::vector<sw::string> listName;
    sw::editor::EditorTestOutputParser::collectTestNames( listLine, listName );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), listName.size() );
    SW_EXPECT_TRUE( listName[0] == "FrameProfilerTest.ScopeOverflowDoesNotReadPastTable" );
    SW_EXPECT_TRUE( listName[2] == "RenderPassGPUTest.ViewModesProduceDistinctPictures" );
    SW_EXPECT_TRUE( sw::editor::EditorTestOutputParser::findSuiteName( listName[2] ) == "RenderPassGPUTest" );
    SW_EXPECT_TRUE( sw::editor::EditorTestOutputParser::makeFilter( listName ) ==
                    "FrameProfilerTest.ScopeOverflowDoesNotReadPastTable:FrameProfilerTest.PercentilesFollowTheDistribution:"
                    "RenderPassGPUTest.ViewModesProduceDistinctPictures" );
}

/**
 * @brief [EditorTestOutputParserTest] 실행 줄이 케이스 결과로 — 통과 · 실패 · 건너뜀 · 끝나지 않은 케이스, 요약 줄은 케이스가 아니다
 */
SW_TEST_CASE( EditorTestOutputParserTest, RunOutputBecomesCaseResults )
{
    const sw::vector<sw::string> listLine{
        "[ RUN      ] A.Passes",
        "[       OK ] A.Passes (1.50 ms)",
        "[ RUN      ] A.Fails",
        "  [FAILED] D:/x.cpp:10",
        "[  FAILED  ] A.Fails (2.25 ms)",
        "[ RUN      ] A.Skips",
        "[  SKIPPED ] A.Skips",
        "[ RUN      ] B.Crashes",
        "[  FAILED  ] 1 test, listed below:",
    };
    sw::vector<sw::editor::EditorTestCaseResult> listResult;
    sw::editor::EditorTestOutputParser::collectResults( listLine, listResult );
    SW_ASSERT_EQUAL( static_cast<size_t>( 4 ), listResult.size() );
    SW_EXPECT_TRUE( listResult[0]._state == sw::editor::EditorTestCaseState::Passed );
    SW_EXPECT_NEAR_EQUAL( 1.5f, listResult[0]._milliseconds, 0.001f );
    SW_EXPECT_TRUE( listResult[1]._state == sw::editor::EditorTestCaseState::Failed );
    SW_EXPECT_NEAR_EQUAL( 2.25f, listResult[1]._milliseconds, 0.001f );
    SW_EXPECT_TRUE( listResult[2]._state == sw::editor::EditorTestCaseState::Skipped );
    SW_EXPECT_TRUE( listResult[3]._name == "B.Crashes" );
    SW_EXPECT_TRUE( listResult[3]._state == sw::editor::EditorTestCaseState::Running ); // 끝 줄 없이 끝났다(프로세스가 그 케이스에서 죽었다)
}

/**
 * @brief [EditorTestOutputParserTest] --test_repeat 실행은 반복마다 한 줄이고, 한 반복의 실패가 다른 반복의 통과에 덮이지 않는다
 */
SW_TEST_CASE( EditorTestOutputParserTest, RepeatedRunKeepsEachIteration )
{
    const sw::vector<sw::string> listLine{
        "[ RUN      ] A.Flaky",
        "[       OK ] A.Flaky (1.00 ms)",
        "",
        "Repeating all tests (iteration 2 / 2) . . .",
        "",
        "[ RUN      ] A.Flaky",
        "[  FAILED  ] A.Flaky (1.00 ms)",
    };
    sw::vector<sw::editor::EditorTestCaseResult> listResult;
    sw::editor::EditorTestOutputParser::collectResults( listLine, listResult );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listResult.size() );
    SW_EXPECT_EQUAL( 1u, listResult[0]._iteration );
    SW_EXPECT_TRUE( listResult[0]._state == sw::editor::EditorTestCaseState::Passed );
    SW_EXPECT_EQUAL( 2u, listResult[1]._iteration );
    SW_EXPECT_TRUE( listResult[1]._state == sw::editor::EditorTestCaseState::Failed );
}
