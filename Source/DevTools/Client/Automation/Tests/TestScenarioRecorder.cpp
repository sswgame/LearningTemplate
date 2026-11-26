#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "DevTools/Client/Automation/Scenario/ScenarioRecorder.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Input/RawInputEvent.h"

using namespace sw;

// 녹화한 프레임 사이의 간격은 고정 대기(`<Wait>`)가 아니라 그리지 않고 진행하는 고정 걸음(`<AdvanceSimulation>`)으로 쓴다 — 쓴 글이 그대로 읽히고, 단계는 녹화한 프레임에 놓인다.
SW_TEST_CASE( ScenarioRecorderTest, GapsBetweenRecordedFramesBecomeAdvanceSimulation )
{
    vector<InputReplayFrame> listFrame( 4 );
    listFrame[0]._listRawEvent.push_back( RawInputEvent::makeKeyDown( Key::A ) );
    listFrame[3]._listRawEvent.push_back( RawInputEvent::makeKeyUp( Key::A ) );
    ScenarioRecordSettings settings;
    settings._firstFrame = 1;

    const ScenarioRecordOutput output = ScenarioRecorder::toScenarioXML( listFrame, settings );
    SW_EXPECT_EQUAL( 2u, output._writtenStepCount );
    SW_EXPECT_TRUE( output._xmlText.find( "<Wait" ) == string::npos );
    SW_EXPECT_TRUE( output._xmlText.find( "<AdvanceSimulation frames=\"1\"" ) != string::npos );  // 프레임 0 → 첫 녹화 프레임(1)
    SW_EXPECT_TRUE( output._xmlText.find( "<AdvanceSimulation frames=\"3\"" ) != string::npos );  // 누름(1) → 뗌(4)
    SW_EXPECT_TRUE( output._xmlText.find( "<AdvanceSimulation frames=\"10\"" ) != string::npos ); // 마지막 입력 → Pass(꼬리 10 프레임)

    AutomationScenario   scenario;
    const expected<void> parsed = scenario.parse( output._xmlText, "recorded.scenario.xml" );
    SW_REQUIRE_TRUE( parsed.has_value() );
    const vector<AutomationStep>& listStep = scenario.getSteps();
    SW_REQUIRE_TRUE( listStep.size() == 3u ); // Press · Release · Pass
    SW_EXPECT_EQUAL( 1u, listStep[0]._frameIndex );
    SW_EXPECT_EQUAL( 4u, listStep[1]._frameIndex );
    SW_EXPECT_EQUAL( 14u, listStep[2]._frameIndex );
    SW_EXPECT_EQUAL( 2u, scenario.findAdvanceSimulationFramesStartingAt( 2 ) ); // 누름 뒤 2 · 3 프레임을 그리지 않고 흘린다
}
