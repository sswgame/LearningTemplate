#include "pch.h"

#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/ScenarioRecorder.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/RawInputEvent.h"

#include "TestFramework/TestFramework.h"

// 시나리오 녹화의 변환 — 녹화 프레임을 시나리오 글로 바꾸고 다시 읽어 단계 종류와 프레임을 본다(창 · 장치 없이 순수 변환만).

namespace
{
    struct TestScenarioRecorderInternal
    {
        static sw::uint32 countKind( const sw::AutomationScenario& scenario, sw::string_view kind )
        {
            sw::uint32 count = 0;
            for ( const sw::AutomationStep& step : scenario.getSteps() )
            {
                if ( sw::string_view{ step._kind } == kind )
                    ++count;
            }
            return count;
        }
    };
} // namespace

/**
 * @brief [ScenarioRecorderTest] 녹화 프레임은 다시 읽히는 시나리오가 되고, 단계로 못 쓰는 입력(휠)은 건너뛰어 센다
 */
SW_TEST_CASE( ScenarioRecorderTest, FramesBecomeParsableInputSteps )
{
    sw::vector<sw::InputReplayFrame> listFrame( 4 );
    listFrame[0]._listRawEvent.push_back( sw::RawInputEvent::makeMouseMove( 99, 49 ) );
    listFrame[1]._listRawEvent.push_back( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left ) );
    listFrame[1]._listRawEvent.push_back( sw::RawInputEvent::makeMouseWheel( 1.0f ) );
    listFrame[3]._listRawEvent.push_back( sw::RawInputEvent::makeMouseButtonUp( sw::MouseButton::Left ) );
    listFrame[3]._listRawEvent.push_back( sw::RawInputEvent::makeTextInput( "a<b" ) );
    listFrame[3]._listRawEvent.push_back( sw::RawInputEvent::makeMouseRawDelta( 2.0f, -3.0f ) );

    sw::ScenarioRecordSettings settings;
    settings._name         = "recorded.test";
    settings._clientWidth  = 100;
    settings._clientHeight = 50;
    settings._firstFrame   = 10;
    const sw::ScenarioRecordOutput output = sw::ScenarioRecorder::toScenarioXml( listFrame, settings );

    sw::AutomationScenario scenario;
    sw::string             error;
    SW_ASSERT_TRUE( scenario.parse( output._xmlText, "recorded.scenario.xml", error ) );
    SW_EXPECT_STREQ( "recorded.test", scenario.getName().c_str() );

    // 마우스 버튼 이름은 엔진 서비스(타입 레지스트리)가 있어야 다시 읽힌다. 없으면 건너뛴 입력으로 센다.
    sw::InputSlot     parsed{};
    const bool        bSlotNames = sw::InputSlotUtil::tryParse( sw::InputSlotUtil::toText( sw::InputSlot::fromMouseButton( sw::MouseButton::Left ) ), parsed );
    const sw::uint32  slotSteps  = bSlotNames ? 2u : 0u;
    SW_EXPECT_EQUAL( slotSteps / 2, TestScenarioRecorderInternal::countKind( scenario, "Press" ) );
    SW_EXPECT_EQUAL( slotSteps / 2, TestScenarioRecorderInternal::countKind( scenario, "Release" ) );
    SW_EXPECT_EQUAL( 1u, TestScenarioRecorderInternal::countKind( scenario, "MousePosition" ) );
    SW_EXPECT_EQUAL( 1u, TestScenarioRecorderInternal::countKind( scenario, "MouseDelta" ) );
    SW_EXPECT_EQUAL( 1u, TestScenarioRecorderInternal::countKind( scenario, "Text" ) );
    SW_EXPECT_EQUAL( 1u, TestScenarioRecorderInternal::countKind( scenario, "Pass" ) );
    SW_EXPECT_EQUAL( 3u + slotSteps, output._writtenStepCount );
    SW_EXPECT_EQUAL( 1u + ( 2u - slotSteps ), output._skippedEventCount );

    for ( const sw::AutomationStep& step : scenario.getSteps() )
    {
        const sw::string_view kind = step._kind;
        if ( kind == "MousePosition" )
        {
            SW_EXPECT_EQUAL( 10u, step._frameIndex );
            SW_EXPECT_TRUE( ( *step.findAttribute( "x" ) )[0] == '1' ); // 99 / (100 - 1) = 1
        }
        else if ( kind == "Text" )
        {
            SW_EXPECT_EQUAL( 13u, step._frameIndex );
            SW_EXPECT_STREQ( "a<b", step.findAttribute( "value" )->c_str() );
        }
        else if ( kind == "Pass" )
        {
            SW_EXPECT_EQUAL( 23u, step._frameIndex );
        }
    }
}

/**
 * @brief [ScenarioRecorderTest] 출력 경로는 Saved/Automation/ 아래의 상대 `.scenario.xml` 만 받는다
 */
SW_TEST_CASE( ScenarioRecorderTest, OutputPathStaysUnderSavedAutomation )
{
    SW_EXPECT_TRUE( sw::ScenarioRecorder::isAllowedOutputPath( "Saved/Automation/Recorded/a.scenario.xml" ) );
    SW_EXPECT_FALSE( sw::ScenarioRecorder::isAllowedOutputPath( "Saved/Automation/../a.scenario.xml" ) );
    SW_EXPECT_FALSE( sw::ScenarioRecorder::isAllowedOutputPath( "Saved/BugIt/a.scenario.xml" ) );
    SW_EXPECT_FALSE( sw::ScenarioRecorder::isAllowedOutputPath( "C:/Saved/Automation/a.scenario.xml" ) );
    SW_EXPECT_FALSE( sw::ScenarioRecorder::isAllowedOutputPath( "Saved/Automation/a.xml" ) );
}
