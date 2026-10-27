#include "pch.h"

#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/ScenarioRecorder.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Probe/AutomationProbe.h"

#include "TestFramework/TestFramework.h"

// 게임 로직 쪽 자동화 — 시점 속성(`time` · `after`), 질의 탐침(`target` · `key` · 글 값 `is`), 상태 이력(`ExpectVisited`), 기록(`Record`),
// 입력 맵 액션 단계(`Action` · `ActionAxis`), 녹화 검사점. 씬 · 창 없이 실행기를 손으로 돌린다.

namespace
{
    struct TestAutomationGameplayStepsInternal
    {
        static inline uint32     s_frameCount = 0; ///< `run` 이 돈 실제 프레임
        static inline uint32     s_jumpCount  = 0;
        static inline sw::float2 s_move{};

        /** @brief 질의 탐침 — 대상 이름이 `guard` 면 프레임에 따라 Idle → Patrol(10) → Chase(20) 를 내고, 아니면 값이 없다. */
        [[nodiscard]] static bool readGuardState( const sw::AutomationProbeQuery& query, sw::AutomationProbeValue& outValue )
        {
            if ( query._target != "guard" )
            {
                outValue.setText( "no guard named '" + sw::string( query._target ) + "'" );
                return false;
            }
            outValue.setText( s_frameCount < 10 ? "Idle" : ( s_frameCount < 20 ? "Patrol" : "Chase" ) );
            return true;
        }

        /** @brief 질의 탐침 — `key` 의 글 길이(키를 받는지 본다). */
        [[nodiscard]] static bool readKeyLength( const sw::AutomationProbeQuery& query, sw::AutomationProbeValue& outValue )
        {
            outValue.setNumber( static_cast<float64>( query._key.size() ) );
            return true;
        }

        [[nodiscard]] static bool readJumpCount( const sw::GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( s_jumpCount );
            return true;
        }

        [[nodiscard]] static bool readMoveX( const sw::GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( s_move._x );
            return true;
        }

        [[nodiscard]] static bool readMoveY( const sw::GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( s_move._y );
            return true;
        }

        /** @brief 실행기를 Engine 과 같은 순서로 돌린다. "Jump" 발동을 세고 "Move" 값을 적는다. */
        static sw::AutomationOutcome run( sw::AutomationRunner& runner, sw::InputManager& input, uint32 maxFrames = 300 )
        {
            s_frameCount                 = 0;
            s_jumpCount                  = 0;
            s_move                       = sw::float2{};
            sw::AutomationOutcome result = sw::AutomationOutcome::Running;
            for ( uint32 frame = 0; frame < maxFrames && result == sw::AutomationOutcome::Running; ++frame )
            {
                runner.onFrameBegin( input );
                input.beginFrame( 1.0f / 60.0f );
                if ( input.getInputMap().wasActionTriggered( "Jump" ) )
                    ++s_jumpCount;
                s_move = input.getInputMap().getVector2D( input.getInputMap().getActionHandle( "Move" ) );
                result = runner.onFrameEnd( input );
                input.endFrame();
                ++s_frameCount;
            }
            return result;
        }
    };

    SW_AUTOMATION_QUERY_PROBE( testGameGuardState, "TestGame.GuardState", "Idle, Patrol, Chase by frame for target guard", &TestAutomationGameplayStepsInternal::readGuardState, true,
                               false );
    SW_AUTOMATION_QUERY_PROBE( testGameKeyLength, "TestGame.KeyLength", "Length of the key attribute", &TestAutomationGameplayStepsInternal::readKeyLength, false, true );
    SW_AUTOMATION_PROBE( testGameJumpCount, "TestGame.JumpCount", "Jump action triggers so far", &TestAutomationGameplayStepsInternal::readJumpCount );
    SW_AUTOMATION_PROBE( testGameMoveX, "TestGame.MoveX", "Move action X this frame", &TestAutomationGameplayStepsInternal::readMoveX );
    SW_AUTOMATION_PROBE( testGameMoveY, "TestGame.MoveY", "Move action Y this frame", &TestAutomationGameplayStepsInternal::readMoveY );
} // namespace

/**
 * @brief [AutomationGameplayStepsTest] `<At>` 은 frame · time(초) · after(앞 At 에서 프레임) · afterSeconds 중 하나로 시점을 정하고, 둘을 주면 읽기 오류다
 */
SW_TEST_CASE( AutomationGameplayStepsTest, AtTimingTakesFramesSecondsOrRelativeOffsets )
{
    sw::AutomationScenario scenario;
    sw::string             error;
    SW_ASSERT_TRUE_MSG( scenario.parse( R"(<Scenario name="t" fixedDelta="0.05">
        <At time="1"><Pass/></At>
        <At after="3"><Pass/></At>
        <At afterSeconds="0.5"><Pass/></At>
        <At frame="2"><Pass/></At>
      </Scenario>)",
                                        "t", error ),
                        error.c_str() );
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( scenario.getSteps().size() ) );
    // 프레임 순으로 정렬된다: 2 · 20(1 초 / 0.05) · 23 · 33
    SW_EXPECT_EQUAL( 2u, scenario.getSteps()[0]._frameIndex );
    SW_EXPECT_EQUAL( 20u, scenario.getSteps()[1]._frameIndex );
    SW_EXPECT_EQUAL( 23u, scenario.getSteps()[2]._frameIndex );
    SW_EXPECT_EQUAL( 33u, scenario.getSteps()[3]._frameIndex );

    SW_EXPECT_FALSE( scenario.parse( R"(<Scenario name="t"><At frame="1" time="1"><Pass/></At></Scenario>)", "t", error ) );
    SW_EXPECT_TRUE_MSG( error.find( "exactly one" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_FALSE( scenario.parse( R"(<Scenario name="t"><At when="1"><Pass/></At></Scenario>)", "t", error ) );
}

/**
 * @brief [AutomationGameplayStepsTest] 질의 탐침은 target · key 를 받고 글 값은 is 로 비교한다 — 실패 줄은 어느 단계 · 어느 대상 · 무엇을 기대했고 무엇을 봤는지 적는다
 */
SW_TEST_CASE( AutomationGameplayStepsTest, QueryProbeComparesTextAndReportsTheTarget )
{
    using Internal = TestAutomationGameplayStepsInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    {
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="100">
        <At frame="5"><Expect probe="TestGame.GuardState" target="guard" is="Idle"/><Expect probe="TestGame.KeyLength" key="abcd" equals="4"/></At>
        <At frame="15"><Expect probe="TestGame.GuardState" target="guard" is="Patrol"/></At>
        <At frame="16"><Pass/></At>
      </Scenario>)" ) );
        const sw::AutomationOutcome result = Internal::run( runner, input );
        SW_EXPECT_TRUE_MSG( result == sw::AutomationOutcome::Passed, runner.getFinishReason().c_str() );
    }
    {
        SW_TEST_DEFENSIVE_SCOPE( "a wrong state and a missing target are failures that name the step and the target" );
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="100">
        <At frame="5"><Expect probe="TestGame.GuardState" target="guard" is="Chase"/></At>
        <At frame="6"><Expect probe="TestGame.GuardState" target="nobody" is="Idle"/></At>
        <At frame="7"><Expect probe="TestGame.GuardState" target="guard" atLeast="1"/><Pass/></At>
      </Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( runner, input ) == sw::AutomationOutcome::Failed );
        SW_ASSERT_EQUAL( 3u, static_cast<uint32>( runner.getFailures().size() ) );
        SW_EXPECT_TRUE_MSG( runner.getFailures()[0].find( "frame 5 <Expect> #1: Expect TestGame.GuardState(guard) is 'Chase', got Idle" ) != sw::string::npos,
                            runner.getFailures()[0].c_str() );
        SW_EXPECT_TRUE_MSG( runner.getFailures()[1].find( "no guard named 'nobody'" ) != sw::string::npos, runner.getFailures()[1].c_str() );
        SW_EXPECT_TRUE_MSG( runner.getFailures()[2].find( "is text" ) != sw::string::npos, runner.getFailures()[2].c_str() );
    }
    {
        SW_TEST_DEFENSIVE_SCOPE( "a query probe without its target, or a scene probe with one, is a load error" );
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately"><At frame="1"><Expect probe="TestGame.GuardState" is="Idle"/></At></Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( runner, input ) == sw::AutomationOutcome::LoadError );
        sw::AutomationRunner sceneRunner;
        SW_ASSERT_TRUE( sceneRunner.startFromText(
            R"(<Scenario name="t" startAfter="Immediately"><At frame="1"><Expect probe="TestGame.JumpCount" target="x" equals="0"/></At></Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( sceneRunner, input ) == sw::AutomationOutcome::LoadError );
    }
    input.shutdown();
}

/**
 * @brief [AutomationGameplayStepsTest] ExpectVisited 는 시작부터 프레임마다 값 이력을 적어 "이 상태를 거쳤다" · "이 순서로 거쳤다" 를 본다. 빠진 값과 본 이력을 실패에 적는다
 */
SW_TEST_CASE( AutomationGameplayStepsTest, ExpectVisitedChecksTheStateHistory )
{
    using Internal = TestAutomationGameplayStepsInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    {
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="100">
        <At frame="25">
          <ExpectVisited probe="TestGame.GuardState" target="guard" sequence="Idle, Patrol, Chase"/>
          <ExpectVisited probe="TestGame.GuardState" target="guard" is="Patrol" since="12"/>
          <Pass/>
        </At>
      </Scenario>)" ) );
        const sw::AutomationOutcome result = Internal::run( runner, input );
        SW_EXPECT_TRUE_MSG( result == sw::AutomationOutcome::Passed, runner.getFinishReason().c_str() );
    }
    {
        SW_TEST_DEFENSIVE_SCOPE( "a state that was never reached is reported with what was seen" );
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="100">
        <At frame="15"><ExpectVisited probe="TestGame.GuardState" target="guard" sequence="Idle,Chase"/><Pass/></At>
      </Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( runner, input ) == sw::AutomationOutcome::Failed );
        SW_ASSERT_EQUAL( 1u, static_cast<uint32>( runner.getFailures().size() ) );
        SW_EXPECT_TRUE_MSG( runner.getFailures()[0].find( "missing 'Chase'" ) != sw::string::npos, runner.getFailures()[0].c_str() );
        SW_EXPECT_TRUE_MSG( runner.getFailures()[0].find( "Idle@0 > Patrol@10" ) != sw::string::npos, runner.getFailures()[0].c_str() );
    }
    input.shutdown();
}

/**
 * @brief [AutomationGameplayStepsTest] WaitUntil 의 timeoutSeconds 는 고정 프레임 시간으로 바뀌고("N 초 안에 닿는다"), Record 는 값을 보고의 records 에 남긴다
 */
SW_TEST_CASE( AutomationGameplayStepsTest, WaitWithinSecondsAndRecordValues )
{
    using Internal = TestAutomationGameplayStepsInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    {
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" fixedDelta="0.1" timeoutFrames="100">
        <At frame="1"><WaitUntil probe="TestGame.GuardState" target="guard" is="Chase" timeoutSeconds="3"/></At>
        <At frame="2"><Record probe="TestGame.GuardState" target="guard"/><Record probe="TestGame.KeyLength" key="xyz"/><Pass/></At>
      </Scenario>)" ) );
        const sw::AutomationOutcome result = Internal::run( runner, input );
        SW_EXPECT_TRUE_MSG( result == sw::AutomationOutcome::Passed, runner.getFinishReason().c_str() );
        SW_ASSERT_EQUAL( 2u, static_cast<uint32>( runner.getRecordLines().size() ) );
        SW_EXPECT_TRUE_MSG( runner.getRecordLines()[0] == "frame 2 TestGame.GuardState(guard) = Chase", runner.getRecordLines()[0].c_str() );
        SW_EXPECT_TRUE_MSG( runner.getRecordLines()[1] == "frame 2 TestGame.KeyLength(xyz) = 3", runner.getRecordLines()[1].c_str() );
    }
    {
        SW_TEST_DEFENSIVE_SCOPE( "a goal not reached within the seconds is a failure" );
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" fixedDelta="0.1" timeoutFrames="100">
        <At frame="1"><WaitUntil probe="TestGame.GuardState" target="guard" is="Chase" timeoutSeconds="0.5"/></At>
        <At frame="2"><Pass/></At>
      </Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( runner, input ) == sw::AutomationOutcome::Failed );
        SW_ASSERT_EQUAL( 1u, static_cast<uint32>( runner.getFailures().size() ) );
        SW_EXPECT_TRUE_MSG( runner.getFailures()[0].find( "gave up after 5" ) != sw::string::npos, runner.getFailures()[0].c_str() );
    }
    input.shutdown();
}

/**
 * @brief [AutomationGameplayStepsTest] 액션 단계는 입력 맵의 바인딩을 풀어 낸다 — 탭 · 연속 탭 · 길게(초) · 2D 합성 축 · 패드 스틱, 모르는 액션은 읽기 오류
 */
SW_TEST_CASE( AutomationGameplayStepsTest, ActionStepsPressTheBoundSlots )
{
    using Internal = TestAutomationGameplayStepsInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getInputMap().bind( "Jump", sw::Key::Space );
    input.getInputMap().bind( "Jump", sw::GamepadButton::A );
    input.getInputMap().createAction( "Move", sw::InputActionValueType::Axis2D );
    input.getInputMap().bindVector2D( "Move", sw::Key::W, sw::Key::S, sw::Key::A, sw::Key::D );
    input.getInputMap().bindGamepadStick2D( "Move", sw::GamepadStick::Left, 0.0f );
    input.getInputMap().createAction( "Look", sw::InputActionValueType::Axis2D ); // 폰이 기본으로 선언하는 시선 액션 — 안 쓰지만 맵에 없으면 플레이어 조종자가 경고한다
    {
        // 점프하고 0.1 초 뒤 오른쪽 위로 이동 — 이동하는 동안 두 번 더 점프(간격 4 프레임)
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="100">
        <At frame="2"><Action name="Jump"/></At>
        <At afterSeconds="0.1"><ActionAxis name="Move" x="1" y="1" seconds="0.5"/><Action name="Jump" count="2" interval="4" device="gamepad"/></At>
        <At after="5"><Expect probe="TestGame.MoveX" atLeast="0.5"/><Expect probe="TestGame.MoveY" atLeast="0.5"/></At>
        <At after="30"><Expect probe="TestGame.MoveX" equals="0"/><Expect probe="TestGame.JumpCount" equals="3"/></At>
        <At after="2"><ActionAxis name="Move" x="-0.5" device="gamepad" frames="3"/></At>
        <At after="1"><Expect probe="TestGame.MoveX" near="-0.5" tolerance="0.01"/><Pass/></At>
      </Scenario>)" ) );
        const sw::AutomationOutcome result = Internal::run( runner, input );
        SW_EXPECT_TRUE_MSG( result == sw::AutomationOutcome::Passed, ( runner.getFailures().empty() ? runner.getFinishReason() : runner.getFailures()[0] ).c_str() );
    }
    {
        SW_TEST_DEFENSIVE_SCOPE( "an action the input map does not have is a load error" );
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately"><At frame="1"><Action name="Fly"/></At></Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( runner, input ) == sw::AutomationOutcome::LoadError );
        SW_EXPECT_TRUE_MSG( runner.getFinishReason().find( "no action 'Fly'" ) != sw::string::npos, runner.getFinishReason().c_str() );
    }
    input.shutdown();
}

/**
 * @brief [AutomationGameplayStepsTest] 녹화는 검사점을 끝 프레임의 Expect 로 싣고(수는 near, 글은 is), 켜면 가상 입력도 적으며 씬 플레이 시작으로 쓴다
 */
SW_TEST_CASE( AutomationGameplayStepsTest, RecordingCarriesCheckpointsAndSyntheticInput )
{
    sw::vector<sw::InputReplayFrame> listFrame( 3 );
    sw::RawInputEvent                synthetic = sw::RawInputEvent::makeKeyDown( sw::Key::Space );
    synthetic._bSynthetic                      = SW_TRUE;
    listFrame[1]._listRawEvent.push_back( synthetic );

    sw::ScenarioRecordSettings settings;
    settings._name                   = "roundtrip";
    settings._firstFrame             = 0;
    settings._bIncludeSynthetic      = true;
    settings._bStartWhenScenePlaying = true;
    settings._listCheckpoint.push_back( sw::ScenarioRecordCheckpoint{ "Farm.FarmerX", "", "", "8.25", false } );
    settings._listCheckpoint.push_back( sw::ScenarioRecordCheckpoint{ "AI.State", "AutoFarmer", "", "Till", true } );
    const sw::ScenarioRecordOutput output = sw::ScenarioRecorder::toScenarioXML( listFrame, settings );

    sw::AutomationScenario scenario;
    sw::string             error;
    SW_ASSERT_TRUE_MSG( scenario.parse( output._xmlText, "recorded", error ), error.c_str() );
    SW_EXPECT_TRUE( scenario.getStartCondition() == sw::AutomationStartCondition::ScenePlaying );
    // 고정 프레임 시간은 자르지 않는다(0.0167 로 쓰면 재생이 프레임마다 더 흘러 검사점이 어긋난다).
    const float32 deltaError = scenario.getFixedDelta() - 1.0f / 60.0f;
    SW_EXPECT_TRUE_MSG( -1.0e-7f <= deltaError && deltaError <= 1.0e-7f, sw::to_string( scenario.getFixedDelta() ).c_str() );
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( scenario.getSteps().size() ) ); // Press · Expect · Expect · Pass
    SW_EXPECT_TRUE( scenario.getSteps()[0]._kind == "Press" );
    SW_EXPECT_EQUAL( 1u, scenario.getSteps()[0]._frameIndex );
    SW_EXPECT_TRUE( scenario.getSteps()[1]._kind == "Expect" );
    SW_EXPECT_EQUAL( 2u, scenario.getSteps()[1]._frameIndex ); // 마지막 녹화 프레임
    SW_EXPECT_TRUE( scenario.getSteps()[1].findAttribute( "near" ) != nullptr );
    SW_EXPECT_TRUE( *scenario.getSteps()[2].findAttribute( "is" ) == "Till" );
    SW_EXPECT_TRUE( *scenario.getSteps()[2].findAttribute( "target" ) == "AutoFarmer" );
    SW_EXPECT_TRUE( scenario.getSteps()[3]._kind == "Pass" );
    SW_EXPECT_EQUAL( 3u, scenario.getSteps()[3]._frameIndex );

    settings._bIncludeSynthetic = false;
    settings._listCheckpoint.clear();
    const sw::ScenarioRecordOutput withoutSynthetic = sw::ScenarioRecorder::toScenarioXML( listFrame, settings );
    SW_EXPECT_EQUAL( 0u, withoutSynthetic._writtenStepCount );
}

/**
 * @brief [AutomationGameplayStepsTest] 배타 시나리오는 시작 조건을 기다리는 프레임에도 사람의 장치를 버리고 포인터를 (0, 0) 에 둔다
 * @details 기다리는 동안 게임이 실제 마우스 위치를 읽으면 NileCity 건설 커서처럼 게임 상태에 남아 같은 시나리오가 실행마다 다른 해시를 냈다.
 */
SW_TEST_CASE( AutomationGameplayStepsTest, ExclusiveScenarioBlocksTheDevicesWhileWaiting )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::AutomationRunner runner;
    SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="ScenePlaying" startTimeoutFrames="5"><At frame="1"><Pass/></At></Scenario>)" ) );
    runner.onFrameBegin( input ); // 씬이 없으니 시작 조건을 기다린다
    SW_EXPECT_TRUE( input.isVirtualInputAttached() );
    SW_EXPECT_TRUE( input.isOsDeviceInputBlocked() );
    SW_EXPECT_FALSE( runner.hasEnded() );
    input.shutdown();
}
