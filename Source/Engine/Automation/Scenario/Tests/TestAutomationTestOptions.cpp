// AutomationTestOptionsTest — 시나리오 선언에서 나오는 글: `<Launch>` 변수의 명령줄 인자, 시나리오 경로의 이름 줄기(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"

using namespace sw;

/**
 * @brief [AutomationTestOptionsTest] `<Launch>` 변수는 선언 순서대로 `-이름=값` 을 빈칸으로 이어 붙인 명령줄이 되고, 선언이 없으면 빈 글이다
 */
SW_TEST_CASE( AutomationTestOptionsTest, LaunchVariablesBecomeCommandLineArguments )
{
    AutomationTestOptions options;
    SW_EXPECT_TRUE( options.makeLaunchArguments().empty() );

    options._listLaunchVariable.push_back( AutomationAttribute{ "gv_a", "1" } );
    SW_EXPECT_EQUAL( string( "-gv_a=1" ), options.makeLaunchArguments() );
    options._listLaunchVariable.push_back( AutomationAttribute{ "gv_b", "x" } );
    SW_EXPECT_EQUAL( string( "-gv_a=1 -gv_b=x" ), options.makeLaunchArguments() );
}

/**
 * @brief [AutomationTestOptionsTest] 이름 줄기는 마지막 `/` 뒤 파일 이름의 첫 `.` 앞이다 — 폴더의 점은 보지 않고, 확장자가 없으면 파일 이름 그대로다
 */
SW_TEST_CASE( AutomationTestOptionsTest, ScenarioStemIsTheFileNameBeforeItsFirstDot )
{
    SW_EXPECT_EQUAL( string( "fog" ), getAutomationScenarioStem( "game/playground/automation/fog.scenario.xml" ) );
    SW_EXPECT_EQUAL( string( "fog" ), getAutomationScenarioStem( "fog.scenario.xml" ) );
    SW_EXPECT_EQUAL( string( "fog" ), getAutomationScenarioStem( "game/v1.2/fog" ) );
    SW_EXPECT_TRUE( getAutomationScenarioStem( "" ).empty() );
}
