#include "pch.h"

#include "Core/Container/StringUtil.h"

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
        static uint32 countKind( const sw::AutomationScenario& scenario, sw::string_view kind )
        {
            uint32 count = 0;
            for ( const sw::AutomationStep& step : scenario.getSteps() )
            {
                if ( sw::string_view{ step._kind } == kind )
                    ++count;
            }
            return count;
        }

        static float32 readFloat( const sw::AutomationStep& step, sw::string_view name )
        {
            float32           value      = 0.0f;
            const sw::string* pAttribute = step.findAttribute( name );
            if ( pAttribute != nullptr )
                (void)sw::StringUtil::parseFloat( *pAttribute, value ); // 읽지 못하면 0 으로 남아 기대값과 어긋난다
            return value;
        }
    };
} // namespace

/**
 * @brief [ScenarioRecorderTest] 녹화 프레임은 다시 읽히는 시나리오가 되고, 단계로 못 쓰는 입력은 건너뛰어 센다
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
    settings._name                        = "recorded.test";
    settings._clientWidth                 = 100;
    settings._clientHeight                = 50;
    settings._firstFrame                  = 10;
    const sw::ScenarioRecordOutput output = sw::ScenarioRecorder::toScenarioXML( listFrame, settings );

    sw::AutomationScenario scenario;
    sw::string             error;
    SW_ASSERT_TRUE( scenario.parse( output._xmlText, "recorded.scenario.xml", error ) );
    SW_EXPECT_STREQ( "recorded.test", scenario.getName().c_str() );

    // 마우스 버튼 이름은 엔진 서비스(타입 레지스트리)가 있어야 다시 읽힌다. 없으면 건너뛴 입력으로 센다.
    sw::InputSlot parsed{};
    const bool    bSlotNames = sw::InputSlotUtil::tryParse( sw::InputSlotUtil::toText( sw::InputSlot::fromMouseButton( sw::MouseButton::Left ) ), parsed );
    const uint32  slotSteps  = bSlotNames ? 2u : 0u;
    SW_EXPECT_EQUAL( slotSteps / 2, TestScenarioRecorderInternal::countKind( scenario, "Press" ) );
    SW_EXPECT_EQUAL( slotSteps / 2, TestScenarioRecorderInternal::countKind( scenario, "Release" ) );
    SW_EXPECT_EQUAL( 1u, TestScenarioRecorderInternal::countKind( scenario, "MousePosition" ) );
    SW_EXPECT_EQUAL( 1u, TestScenarioRecorderInternal::countKind( scenario, "MouseDelta" ) );
    SW_EXPECT_EQUAL( 1u, TestScenarioRecorderInternal::countKind( scenario, "Text" ) );
    SW_EXPECT_EQUAL( 1u, TestScenarioRecorderInternal::countKind( scenario, "Pass" ) );
    SW_EXPECT_EQUAL( 1u, TestScenarioRecorderInternal::countKind( scenario, "Wheel" ) );
    SW_EXPECT_EQUAL( 4u + slotSteps, output._writtenStepCount );
    SW_EXPECT_EQUAL( 2u - slotSteps, output._skippedEventCount );

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
 * @brief [ScenarioRecorderTest] 휠은 프레임마다 합친 `Wheel` 단계로 왕복하고, 포커스 · 포인터 출입 · 패드 연결 · 조합 중 글자는 건너뛴다
 */
SW_TEST_CASE( ScenarioRecorderTest, WheelRoundTripsAndWindowStateIsSkipped )
{
    sw::vector<sw::InputReplayFrame> listFrame( 3 );
    listFrame[0]._listRawEvent.push_back( sw::RawInputEvent::makeMouseWheel( 1.0f ) );
    listFrame[0]._listRawEvent.push_back( sw::RawInputEvent::makeMouseWheel( 2.0f ) );
    listFrame[0]._listRawEvent.push_back( sw::RawInputEvent::makeMouseHorizontalWheel( -1.5f ) );
    listFrame[2]._listRawEvent.push_back( sw::RawInputEvent::makeMouseWheel( -1.0f ) );
    sw::RawInputEvent focus{};
    focus._type = sw::RawInputEventType::FocusLost;
    listFrame[1]._listRawEvent.push_back( focus );
    sw::RawInputEvent pointer{};
    pointer._type = sw::RawInputEventType::PointerLeft;
    listFrame[1]._listRawEvent.push_back( pointer );

    sw::ScenarioRecordSettings settings;
    settings._name                        = "recorded.wheel";
    const sw::ScenarioRecordOutput output = sw::ScenarioRecorder::toScenarioXML( listFrame, settings );
    SW_EXPECT_EQUAL( 2u, output._writtenStepCount );
    SW_EXPECT_EQUAL( 2u, output._skippedEventCount );

    sw::AutomationScenario scenario;
    sw::string             error;
    SW_ASSERT_TRUE( scenario.parse( output._xmlText, "recorded.scenario.xml", error ) );
    SW_EXPECT_EQUAL( 2u, TestScenarioRecorderInternal::countKind( scenario, "Wheel" ) );

    uint32 wheelIndex = 0;
    for ( const sw::AutomationStep& step : scenario.getSteps() )
    {
        if ( sw::string_view{ step._kind } != "Wheel" )
            continue;
        const float32 valueY = TestScenarioRecorderInternal::readFloat( step, "y" );
        if ( wheelIndex == 0 )
        {
            SW_EXPECT_EQUAL( settings._firstFrame, step._frameIndex );
            SW_EXPECT_NEAR_EQUAL( 3.0f, valueY, 1e-4f );
            SW_ASSERT_TRUE( step.findAttribute( "x" ) != nullptr );
            SW_EXPECT_NEAR_EQUAL( -1.5f, TestScenarioRecorderInternal::readFloat( step, "x" ), 1e-4f );
        }
        else
        {
            SW_EXPECT_EQUAL( settings._firstFrame + 2u, step._frameIndex );
            SW_EXPECT_NEAR_EQUAL( -1.0f, valueY, 1e-4f );
            SW_EXPECT_TRUE( step.findAttribute( "x" ) == nullptr ); // 가로 휠이 없으면 x 를 쓰지 않는다
        }
        ++wheelIndex;
    }
}

/**
 * @brief [ScenarioRecorderTest] 두 번째 누름이 DoubleClick 으로 와도 Press 둘 · Release 둘이 된다
 */
SW_TEST_CASE( ScenarioRecorderTest, DoubleClickBecomesSecondPress )
{
    sw::InputSlot parsed{};
    if ( sw::InputSlotUtil::tryParse( sw::InputSlotUtil::toText( sw::InputSlot::fromMouseButton( sw::MouseButton::Left ) ), parsed ) == false )
        return; // 마우스 버튼 이름은 엔진 서비스가 있어야 다시 읽힌다

    sw::vector<sw::InputReplayFrame> listFrame( 4 );
    listFrame[0]._listRawEvent.push_back( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left ) );
    listFrame[1]._listRawEvent.push_back( sw::RawInputEvent::makeMouseButtonUp( sw::MouseButton::Left ) );
    listFrame[2]._listRawEvent.push_back( sw::RawInputEvent::makeMouseDoubleClick( sw::MouseButton::Left ) );
    listFrame[3]._listRawEvent.push_back( sw::RawInputEvent::makeMouseButtonUp( sw::MouseButton::Left ) );

    sw::ScenarioRecordSettings settings;
    settings._name                        = "recorded.double";
    const sw::ScenarioRecordOutput output = sw::ScenarioRecorder::toScenarioXML( listFrame, settings );
    SW_EXPECT_EQUAL( 0u, output._skippedEventCount );

    sw::AutomationScenario scenario;
    sw::string             error;
    SW_ASSERT_TRUE( scenario.parse( output._xmlText, "recorded.scenario.xml", error ) );
    SW_EXPECT_EQUAL( 2u, TestScenarioRecorderInternal::countKind( scenario, "Press" ) );
    SW_EXPECT_EQUAL( 2u, TestScenarioRecorderInternal::countKind( scenario, "Release" ) );
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
