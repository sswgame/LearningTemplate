#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/ScenarioRecorder.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Resource/ResourcePaths.h"

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

/**
 * @brief [ScenarioRecorderTest] 디스크 경로로 쓴 녹화는 붙인 `saved` 도메인의 id 로 다시 읽힌다 — 시나리오 입구는 리소스 id 만 받는다
 */
SW_TEST_CASE( ScenarioRecorderTest, RecordingIsReadBackThroughTheSavedDomain )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    constexpr const utf8* kOutputPath = "Saved/Automation/Recorded/UnitReplay.scenario.xml";
    SW_EXPECT_EQUAL( sw::string( "saved/Automation/Recorded/UnitReplay.scenario.xml" ), sw::ScenarioRecorder::makeReplayID( kOutputPath ) );
    SW_EXPECT_TRUE( sw::ScenarioRecorder::makeReplayID( "Saved/BugIt/a.scenario.xml" ).empty() );

    SW_ASSERT_TRUE( sw::ScenarioRecorder::mountSavedDomain( sw::FileUtil::getCurrentPath() ) );
    SW_ASSERT_TRUE( sw::FileUtil::ensureParentDirectoryExists( kOutputPath ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( kOutputPath, R"(<Scenario name="UnitReplay"><At frame="1"><Pass/></At></Scenario>)" ) );
    sw::AutomationScenario scenario;
    sw::string             error;
    SW_EXPECT_TRUE_MSG( scenario.loadFromPath( sw::ResourceID{ sw::ScenarioRecorder::makeReplayID( kOutputPath ) }, error ), error.c_str() );
    SW_EXPECT_TRUE( scenario.getName() == "UnitReplay" );
    (void)sw::FileUtil::tryRemoveFile( kOutputPath ); // 다음 실행이 새로 쓴다
}

/**
 * @brief [ScenarioRecorderTest] 입력 맵이 있으면 액션이 재생 때 누를 단추(그 장치의 첫 단추 바인딩)는 액션 단계로 쓴다 — 키 배치를 바꿔도 녹화가 산다.
 *        그 액션의 첫 단추가 아닌 단추와 어느 액션에도 없는 단추는 키 단계로 남는다
 */
SW_TEST_CASE( ScenarioRecorderTest, MappedButtonsBecomeActionSteps )
{
    sw::InputMap inputMap;
    inputMap.registerLayer( "Gameplay", 0, true, false, false );
    inputMap.bind( "Jump", sw::Key::Space, sw::ActionTrigger::Pressed, "Gameplay" );
    inputMap.bind( "Jump", sw::Key::W, sw::ActionTrigger::Pressed, "Gameplay" ); // 둘째 키보드 단추 — 재생은 Space 를 누른다

    sw::vector<sw::InputReplayFrame> listFrame( 3 );
    listFrame[0]._listRawEvent.push_back( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) );
    listFrame[1]._listRawEvent.push_back( sw::RawInputEvent::makeKeyUp( sw::Key::Space ) );
    listFrame[2]._listRawEvent.push_back( sw::RawInputEvent::makeKeyDown( sw::Key::W ) );
    listFrame[2]._listRawEvent.push_back( sw::RawInputEvent::makeKeyDown( sw::Key::F5 ) );

    sw::ScenarioRecordSettings settings;
    settings._pInputMap                   = &inputMap;
    const sw::ScenarioRecordOutput output = sw::ScenarioRecorder::toScenarioXML( listFrame, settings );

    sw::AutomationScenario scenario;
    sw::string             error;
    SW_ASSERT_TRUE( scenario.parse( output._xmlText, "recorded.scenario.xml", error ) );
    SW_EXPECT_EQUAL( 1u, TestScenarioRecorderInternal::countKind( scenario, "ActionPress" ) );
    SW_EXPECT_EQUAL( 1u, TestScenarioRecorderInternal::countKind( scenario, "ActionRelease" ) );
    for ( const sw::AutomationStep& step : scenario.getSteps() )
    {
        if ( sw::string_view{ step._kind } != "ActionPress" )
            continue;
        SW_EXPECT_STREQ( "Jump", step.findAttribute( "name" )->c_str() );
        SW_EXPECT_STREQ( "keyboard", step.findAttribute( "device" )->c_str() );
        SW_EXPECT_EQUAL( 1u, step._frameIndex );
    }
    // W(첫 단추가 아님) · F5(맵에 없음)는 키 단계다. 키 이름을 다시 읽지 못하면(엔진 서비스 없음) 건너뛴 입력으로 센다.
    SW_EXPECT_EQUAL( 2u, TestScenarioRecorderInternal::countKind( scenario, "Press" ) + output._skippedEventCount );
}

/**
 * @brief [ScenarioRecorderTest] 액션의 스틱(그 액션의 첫 패드 축 바인딩)은 값 구간마다 `ActionAxis`(값 · 구간 길이)로 쓴다. 액션이 없는 스틱과 트리거 축은 `GamepadAxis` 로 남는다
 * @details 왼쪽 스틱을 프레임 0 에 x 0.5, 프레임 2 에 y 1 로 기울이고 프레임 3 에 놓는다 — 구간은 [0, 2) (0.5, 0) 과 [2, 3) (0.5, 1) 둘이다.
 */
SW_TEST_CASE( ScenarioRecorderTest, MappedSticksBecomeActionAxisRuns )
{
    sw::InputMap inputMap;
    inputMap.registerLayer( "Gameplay", 0, true, false, false );
    inputMap.bindGamepadStick2D( "Move", sw::GamepadStick::Left, 0.15f, "Gameplay" );

    sw::vector<sw::InputReplayFrame> listFrame( 5 );
    listFrame[0]._listRawEvent.push_back( sw::RawInputEvent::makeGamepadAxis( 0, 0.5f ) );
    listFrame[1]._listRawEvent.push_back( sw::RawInputEvent::makeGamepadAxis( 2, 0.25f ) ); // 오른쪽 스틱 — 액션이 없다
    listFrame[2]._listRawEvent.push_back( sw::RawInputEvent::makeGamepadAxis( 1, 1.0f ) );
    listFrame[3]._listRawEvent.push_back( sw::RawInputEvent::makeGamepadAxis( 0, 0.0f ) );
    listFrame[3]._listRawEvent.push_back( sw::RawInputEvent::makeGamepadAxis( 1, 0.0f ) );
    listFrame[4]._listRawEvent.push_back( sw::RawInputEvent::makeGamepadAxis( 4, 0.75f ) ); // 트리거

    sw::ScenarioRecordSettings settings;
    settings._pInputMap                   = &inputMap;
    settings._firstFrame                  = 10;
    const sw::ScenarioRecordOutput output = sw::ScenarioRecorder::toScenarioXML( listFrame, settings );

    sw::AutomationScenario scenario;
    sw::string             error;
    SW_ASSERT_TRUE( scenario.parse( output._xmlText, "recorded.scenario.xml", error ) );
    SW_EXPECT_EQUAL( 2u, TestScenarioRecorderInternal::countKind( scenario, "ActionAxis" ) );
    SW_EXPECT_EQUAL( 2u, TestScenarioRecorderInternal::countKind( scenario, "GamepadAxis" ) );
    for ( const sw::AutomationStep& step : scenario.getSteps() )
    {
        if ( sw::string_view{ step._kind } != "ActionAxis" )
            continue;
        SW_EXPECT_STREQ( "Move", step.findAttribute( "name" )->c_str() );
        SW_EXPECT_STREQ( "gamepad", step.findAttribute( "device" )->c_str() );
        SW_EXPECT_NEAR_EQUAL( 0.5f, TestScenarioRecorderInternal::readFloat( step, "x" ), 1.0e-4f );
        if ( step._frameIndex == 10u )
        {
            SW_EXPECT_NEAR_EQUAL( 0.0f, TestScenarioRecorderInternal::readFloat( step, "y" ), 1.0e-4f );
            SW_EXPECT_NEAR_EQUAL( 2.0f, TestScenarioRecorderInternal::readFloat( step, "frames" ), 1.0e-4f );
        }
        else
        {
            SW_EXPECT_EQUAL( 12u, step._frameIndex );
            SW_EXPECT_NEAR_EQUAL( 1.0f, TestScenarioRecorderInternal::readFloat( step, "y" ), 1.0e-4f );
            SW_EXPECT_NEAR_EQUAL( 1.0f, TestScenarioRecorderInternal::readFloat( step, "frames" ), 1.0e-4f );
        }
    }
}
