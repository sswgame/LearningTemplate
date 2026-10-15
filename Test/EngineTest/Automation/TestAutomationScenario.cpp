#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

#include "Engine/Automation/AutomationImageMetric.h"
#include "Engine/Automation/AutomationLogPolicy.h"
#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestFramework.h"

// 자동화 시나리오 — 씬 · 창 없이 실행기를 손으로 돌린다(Engine 의 프레임 앞뒤 자리를 테스트가 대신 부른다).
SW_LOG_CALLER( "TestAutomationScenario" );

namespace
{
    struct TestAutomationScenarioInternal
    {
        static inline uint32 s_switchCount         = 0;
        static inline bool   s_bSwitchDown         = false;
        static inline uint32 s_beforeInputRunCount = 0;
        static inline uint32 s_beforeInputFrame    = 0;
        static inline uint32 s_runFrameCount       = 0; ///< `run` 이 돈 실제 프레임(시나리오 시계가 붙잡혀도 오른다)
        static inline uint32 s_readyAtRunFrame     = 0; ///< `Test.Ready` 가 1 이 되는 실제 프레임

        [[nodiscard]] static bool readSwitchCount( const sw::GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( s_switchCount );
            return true;
        }

        [[nodiscard]] static bool readSwitchDown( const sw::GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = s_bSwitchDown ? 1.0 : 0.0;
            return true;
        }

        [[nodiscard]] static bool readNothing( const sw::GameObjectManager* /*pManager*/, float64& /*outValue*/ ) { return false; }

        /** @brief 느린 준비를 흉내 낸다 — 실제 프레임이 `s_readyAtRunFrame` 에 닿기 전에는 값이 없고, 닿으면 1 이다. */
        [[nodiscard]] static bool readReady( const sw::GameObjectManager* /*pManager*/, float64& outValue )
        {
            if ( s_runFrameCount < s_readyAtRunFrame )
                return false;
            outValue = 1.0;
            return true;
        }

        /** @brief 등록 단계 — 입력 재생 전에 돈다(행동 층 주입 자리). 프레임을 적어 둔다. */
        static bool runMark( sw::AutomationRunner& runner, const sw::AutomationStep& /*step*/ )
        {
            ++s_beforeInputRunCount;
            s_beforeInputFrame = runner.getFrameIndex();
            return true;
        }

        /** @brief 늦게 그려지는 위젯을 누르는 단계를 흉내 낸다 — 대상은 실제 프레임이 `s_readyAtRunFrame` 에 닿아야 있다. 차례 함수가 그때까지 시계를 붙잡는다. */
        static sw::AutomationStepHold holdLate( sw::AutomationRunner& runner, const sw::AutomationStep& step, sw::InputManager& input )
        {
            constexpr uint32 kLateWaitFrames = 30;
            return runner.holdUntilReady( step, input, s_runFrameCount >= s_readyAtRunFrame, kLateWaitFrames );
        }

        static bool runLate( sw::AutomationRunner& runner, const sw::AutomationStep& step )
        {
            if ( s_runFrameCount < s_readyAtRunFrame )
                runner.recordFailure( step, "the late target is not there yet" );
            return true;
        }

        static bool validateMark( const sw::AutomationStep& step, sw::string& outError )
        {
            if ( step._listAttribute.empty() )
                return true;
            outError = step.describe() + ": <TestMark> takes no attribute";
            return false;
        }

        /** @brief 실행기를 끝날 때까지(최대 @p maxFrames) Engine 와 같은 순서로 돌린다. "Switch" 발동을 센다. */
        static sw::AutomationResult run( sw::AutomationRunner& runner, sw::InputManager& input, uint32 maxFrames = 200 )
        {
            s_switchCount               = 0;
            s_bSwitchDown               = false;
            s_beforeInputRunCount       = 0;
            s_runFrameCount             = 0;
            sw::AutomationResult result = sw::AutomationResult::Running;
            for ( uint32 frame = 0; frame < maxFrames && result == sw::AutomationResult::Running; ++frame )
            {
                ++s_runFrameCount;
                runner.onFrameBegin( input );
                input.beginFrame( 1.0f / 60.0f );
                if ( input.getInputMap().wasActionTriggered( "Switch" ) )
                    ++s_switchCount;
                s_bSwitchDown = input.getInputMap().isActionDown( "Switch" );
                result        = runner.onFrameEnd( input );
                input.endFrame();
            }
            return result;
        }

        static void bindSwitch( sw::InputManager& input )
        {
            input.getInputMap().bindAxis1DComposite( "Switch", sw::Key::Q, sw::Key::E, {}, sw::ActionTrigger::Pressed );
        }
    };

    SW_AUTOMATION_PROBE( testSwitchCount, "Test.SwitchCount", "Switch action triggers so far", &TestAutomationScenarioInternal::readSwitchCount );
    SW_AUTOMATION_PROBE( testSwitchDown, "Test.SwitchDown", "1 while the Switch action is held", &TestAutomationScenarioInternal::readSwitchDown );
    SW_AUTOMATION_PROBE( testNothing, "Test.Nothing", "never has a value", &TestAutomationScenarioInternal::readNothing );
    SW_AUTOMATION_PROBE( testReady, "Test.Ready", "1 once the harness has run s_readyAtRunFrame frames", &TestAutomationScenarioInternal::readReady );
    SW_AUTOMATION_STEP( testMark, "TestMark", &TestAutomationScenarioInternal::runMark, &TestAutomationScenarioInternal::validateMark, true );
    SW_AUTOMATION_STEP_WITH_HOOKS( testLate, "TestLate", &TestAutomationScenarioInternal::runLate, &TestAutomationScenarioInternal::validateMark, nullptr,
                                   &TestAutomationScenarioInternal::holdLate, false );
} // namespace

/**
 * @brief [AutomationScenarioTest] 입력 단계는 적은 프레임에 가상 키로 들어가고, Expect 탐침이 그 결과를 본다 — Q/E 한 번 = 한 칸, 길게 눌러도 한 칸
 */
SW_TEST_CASE( AutomationScenarioTest, TapStepTriggersOncePerTap )
{
    using Internal = TestAutomationScenarioInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    Internal::bindSwitch( input );

    sw::AutomationRunner runner;
    SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="100">
        <At frame="2"><Tap slot="Key.E" hold="1"/></At>
        <At frame="5"><Tap slot="Key.E" hold="20"/></At>
        <At frame="10"><Expect probe="Test.SwitchDown" equals="1"/></At>
        <At frame="27"><Tap slot="Key.Q" hold="0"/></At>
        <At frame="30"><Expect probe="Test.SwitchDown" equals="0"/><Expect probe="Test.SwitchCount" equals="3"/><Pass/></At>
      </Scenario>)" ) );
    const sw::AutomationResult result = Internal::run( runner, input );
    SW_EXPECT_TRUE_MSG( result == sw::AutomationResult::Passed, runner.getFinishReason().c_str() );
    SW_EXPECT_EQUAL( 30u, runner.getFrameIndex() );
    SW_EXPECT_FALSE( input.isVirtualInputAttached() ); // 끝나면 뗀다
    input.shutdown();
}

/**
 * @brief [AutomationScenarioTest] 개발 명령 단계가 든 시나리오는 개발 명령이 없는 구성(Shipping)에서 읽기 오류가 아니라 건너뜀이다
 * @details Shipping AppScenarioTest 가 `bugit` · `debughud` 를 읽기 오류(11)로 지던 것을 막는다 — 구성에 없는 기능의 시나리오는 그 구성에서 돌 수 없을 뿐이다.
 */
SW_TEST_CASE( AutomationScenarioTest, DevCommandScenarioIsSkippedWithoutDevCommands )
{
    sw::AutomationRunner runner;
    const bool           bStarted = runner.startFromText( R"(<Scenario name="t" startAfter="Immediately">
        <At frame="1"><DevCommand line="help"/></At>
        <At frame="2"><Pass/></At>
      </Scenario>)" );
#if SW_DEV_COMMANDS_ENABLED
    SW_EXPECT_TRUE( bStarted );
    SW_EXPECT_TRUE( runner.getResult() == sw::AutomationResult::Running );
#else
    SW_EXPECT_FALSE( bStarted );
    SW_EXPECT_TRUE_MSG( runner.getResult() == sw::AutomationResult::Skipped, runner.getFinishReason().c_str() );
#endif
}

/**
 * @brief [AutomationScenarioTest] 틀린 Expect 는 실패로 적고 계속 가며, Pass 에 와도 결과는 Failed 다(실패 줄에 받은 값)
 */
SW_TEST_CASE( AutomationScenarioTest, FailedExpectEndsAsFailed )
{
    using Internal = TestAutomationScenarioInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    Internal::bindSwitch( input );

    sw::AutomationRunner runner;
    SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately">
        <At frame="1"><Tap slot="Key.E"/></At>
        <At frame="5"><Expect probe="Test.SwitchCount" equals="3"/><Expect probe="Test.Nothing" atLeast="0"/></At>
        <At frame="6"><Expect probe="Test.SwitchCount" near="1" tolerance="0.5"/><Pass/></At>
      </Scenario>)" ) );
    SW_TEST_DEFENSIVE_SCOPE( "a scenario that fails on purpose logs its failures" );
    const sw::AutomationResult result = Internal::run( runner, input );
    SW_EXPECT_TRUE( result == sw::AutomationResult::Failed );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( runner.getFailures().size() ) );
    SW_EXPECT_TRUE( runner.getFailures()[0].find( "got 1" ) != sw::string::npos );
    SW_EXPECT_TRUE( runner.getFailures()[1].find( "no value" ) != sw::string::npos );
    input.shutdown();
}

/**
 * @brief [AutomationScenarioTest] 모르는 단계 · 속성 · 슬롯 · 탐침과 비교가 없는 Expect 는 시작할 때 읽기 오류다 — 조용히 버리지 않는다
 */
SW_TEST_CASE( AutomationScenarioTest, UnknownStepOrAttributeIsALoadError )
{
    using Internal                             = TestAutomationScenarioInternal;
    static constexpr const utf8* kArrBadStep[] = {
        R"(<Jump/>)",
        R"(<Tap slot="Key.E" holdd="2"/>)",
        R"(<Tap slot="Key.NoSuchKey"/>)",
        R"(<Expect probe="Test.NoSuchProbe" equals="1"/>)",
        R"(<Expect probe="Test.SwitchCount"/>)",
        R"(<Expect probe="Test.SwitchCount" equals="1" atMost="2"/>)",
        R"(<Variable name="gv_noSuchVariable" value="1"/>)",
        R"(<TestMark extra="1"/>)",
        R"(<ExpectImage file="a.ppm" metric="darkness" atLeast="0"/>)",
        R"(<ExpectImage file="a.ppm" metric="darkFraction" region="0,0,2,1" atLeast="0"/>)",
        R"(<ExpectImage file="a.ppm" metric="differentFrom" atMost="0.1"/>)",
        R"(<MousePosition x="1.5" y="0.5"/>)",
        R"(<MousePosition x="0.5"/>)",
        R"(<WaitUntil probe="Test.Ready" equals="1" timeoutFrames="-1"/>)",
        R"(<WaitUntil probe="Test.Ready" equals="1" frames="3"/>)",
        R"(<WaitUntil probe="Test.Ready" equals="1"/><Pass/>)",
    };
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    SW_TEST_DEFENSIVE_SCOPE( "bad scenarios are reported as load errors" );
    for ( const utf8* pStep : kArrBadStep )
    {
        sw::AutomationRunner runner;
        const sw::string     text = sw::string( R"(<Scenario name="t" startAfter="Immediately"><At frame="0">)" ) + pStep + "</At></Scenario>";
        SW_ASSERT_TRUE( runner.startFromText( text ) );
        const sw::AutomationResult result = Internal::run( runner, input, 5 );
        SW_EXPECT_TRUE_MSG( result == sw::AutomationResult::LoadError, pStep );
        SW_EXPECT_TRUE_MSG( runner.getFinishReason().find( "frame 0" ) != sw::string::npos, runner.getFinishReason().c_str() );
    }

    // 형식 오류(루트 · 루트 속성 · <At> 밖 엘리먼트)는 읽을 때 진다.
    static constexpr const utf8* kArrBadFile[] = {
        R"(<NotAScenario name="t"/>)",
        R"(<Scenario name="t" fixedDelta="0"/>)",
        R"(<Scenario name="t" input="sometimes"/>)",
        R"(<Scenario name="t" timeOut="3"/>)",
        R"(<Scenario name="t"><Tap slot="Key.E"/></Scenario>)",
        R"(<Scenario name="t"><At><Pass/></At></Scenario>)",
        R"(<Scenario><At frame="0"><Pass/></At></Scenario>)",
    };
    for ( const utf8* pFile : kArrBadFile )
    {
        sw::AutomationRunner runner;
        SW_EXPECT_FALSE_MSG( runner.startFromText( pFile ), pFile );
        SW_EXPECT_TRUE( runner.getResult() == sw::AutomationResult::LoadError );
    }

    // <MousePosition> 은 엔진 단계다(등록표의 엔진 단계 목록에 있어야 "모르는 단계" 가 아니다) — 창이 없는 테스트에서는 커서를 놓을 자리가 없다는 오류.
    {
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately"><At frame="0"><MousePosition x="0.5" y="0.5"/></At></Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( runner, input, 5 ) == sw::AutomationResult::LoadError );
        SW_EXPECT_TRUE_MSG( runner.getFinishReason().find( "no window to place the pointer" ) != sw::string::npos, runner.getFinishReason().c_str() );
    }
    input.shutdown();
}

/**
 * @brief [AutomationScenarioTest] WaitUntil 은 조건이 참이 될 때까지 시나리오 시계를 붙잡는다 — 뒤 단계 · 입력 사건이 그만큼 밀리고, 시한을 넘으면 실패로 적고 간다
 * @details 부하 걸린 기계에서 고정 프레임 대기가 지던 에디터 시나리오(`sequencerkeys`)의 고침이다. 미는 것을 빼면 Tap 이 붙잡힌 동안 나가 frame 3 의 Expect 가 진다.
 */
SW_TEST_CASE( AutomationScenarioTest, WaitUntilHoldsTheScenarioClock )
{
    using Internal = TestAutomationScenarioInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    Internal::bindSwitch( input );
    {
        Internal::s_readyAtRunFrame = 40;
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="20">
        <At frame="2"><WaitUntil probe="Test.Ready" equals="1"/></At>
        <At frame="3"><Expect probe="Test.SwitchCount" equals="0"/></At>
        <At frame="4"><Tap slot="Key.E"/></At>
        <At frame="5"><Expect probe="Test.SwitchCount" equals="1"/><Pass/></At>
      </Scenario>)" ) );
        const sw::AutomationResult result = Internal::run( runner, input );
        SW_EXPECT_TRUE_MSG( result == sw::AutomationResult::Passed, runner.getFinishReason().c_str() );
        SW_EXPECT_EQUAL( 5u, runner.getFrameIndex() );
        SW_EXPECT_TRUE( Internal::s_runFrameCount >= 40u ); // 시나리오 시한(20) 보다 오래 돌았다 — 붙잡은 프레임은 시한에 세지 않는다
    }
    {
        SW_TEST_DEFENSIVE_SCOPE( "a WaitUntil that never comes true logs its failure" );
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately">
        <At frame="1"><WaitUntil probe="Test.Nothing" atLeast="0" timeoutFrames="5"/></At>
        <At frame="2"><Pass/></At>
      </Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( runner, input ) == sw::AutomationResult::Failed );
        SW_ASSERT_EQUAL( 1u, static_cast<uint32>( runner.getFailures().size() ) );
        SW_EXPECT_TRUE_MSG( runner.getFailures()[0].find( "gave up after 5" ) != sw::string::npos, runner.getFailures()[0].c_str() );
    }
    input.shutdown();
}

/**
 * @brief [AutomationScenarioTest] 차례 함수가 `holdUntilReady` 로 대상이 생길 때까지 시계를 붙잡는다 — 늦게 그려진 대상도 그 단계에서 누르고, 끝내 없으면 시한 뒤 단계가 실패를 적는다
 * @details 에디터 입력 단계(`EditorClick` · `EditorDrag` · `EditorHover`)가 레이블을 기다리는 방식이다(백로그 A-3 이던 Vulkan 1 회 "레이블 없음").
 *          변이: `holdUntilReady` 가 늘 Run 을 돌려주면 첫 시나리오가 "not there yet" 으로 진다.
 */
SW_TEST_CASE( AutomationScenarioTest, HoldUntilReadyWaitsForALateTarget )
{
    using Internal = TestAutomationScenarioInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    {
        Internal::s_readyAtRunFrame = 12;
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="20">
        <At frame="2"><TestLate/></At>
        <At frame="3"><Pass/></At>
      </Scenario>)" ) );
        const sw::AutomationResult result = Internal::run( runner, input );
        SW_EXPECT_TRUE_MSG( result == sw::AutomationResult::Passed, runner.getFailures().empty() ? runner.getFinishReason().c_str() : runner.getFailures()[0].c_str() );
        SW_EXPECT_EQUAL( 3u, runner.getFrameIndex() ); // 붙잡은 프레임은 시나리오 프레임으로 세지 않는다
    }
    {
        SW_TEST_DEFENSIVE_SCOPE( "a target that never appears fails after the wait" );
        Internal::s_readyAtRunFrame = 1000;
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately">
        <At frame="1"><TestLate/></At>
        <At frame="2"><Pass/></At>
      </Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( runner, input ) == sw::AutomationResult::Failed );
        SW_ASSERT_EQUAL( 1u, static_cast<uint32>( runner.getFailures().size() ) );
        SW_EXPECT_TRUE_MSG( runner.getFailures()[0].find( "not there yet" ) != sw::string::npos, runner.getFailures()[0].c_str() );
        SW_EXPECT_TRUE( Internal::s_runFrameCount >= 30u ); // 시한(30 프레임)까지 기다렸다
    }
    input.shutdown();
}

/**
 * @brief [AutomationScenarioTest] Pass 없이 timeoutFrames 를 넘으면 시간 초과, 시작 조건이 오지 않으면 startTimeoutFrames 뒤 시간 초과
 */
SW_TEST_CASE( AutomationScenarioTest, TimeoutWithoutPass )
{
    using Internal = TestAutomationScenarioInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    SW_TEST_DEFENSIVE_SCOPE( "timed out scenarios log an error line" );
    {
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="8"><At frame="1"><Tap slot="Key.E"/></At></Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( runner, input ) == sw::AutomationResult::TimedOut );
        SW_EXPECT_EQUAL( 9u, runner.getFrameIndex() );
    }
    {
        // 테스트 하네스에는 플레이를 시작한 씬이 없다(있어도 이 테스트 동안 플레이를 시작하지 않는다) — 단계는 하나도 돌지 않는다.
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startTimeoutFrames="4"><At frame="0"><TestMark/></At></Scenario>)" ) );
        const sw::AutomationResult result = Internal::run( runner, input, 50 );
        if ( runner.findActiveObjectManager() == nullptr || runner.findActiveObjectManager()->hasBegunPlay() == false )
        {
            SW_EXPECT_TRUE( result == sw::AutomationResult::TimedOut );
            SW_EXPECT_EQUAL( 0u, Internal::s_beforeInputRunCount );
        }
    }
    input.shutdown();
}

/**
 * @brief [AutomationScenarioTest] `<Load>` 는 부하 선언을 읽고(모르는 속성 · 창 없음 · `<Pass/>` 는 읽기 오류), 시작 프레임에 측정 창(`gv_profileSeconds`)을 열며 `timeoutFrames` 를 보지 않고, 창이 닫혀 루프가 끝나면 통과다
 */
SW_TEST_CASE( AutomationScenarioTest, LoadScenarioOpensTheMeasureWindowAndIgnoresFrameTimeout )
{
    using Internal = TestAutomationScenarioInternal;
    {
        sw::AutomationScenario scenario;
        sw::string             error;
        SW_ASSERT_TRUE_MSG( scenario.parse( R"(<Scenario name="t"><Load clients="200" rampUpSeconds="2.5" durationSeconds="30" network="lte" meshes="8000" movers="1"/></Scenario>)", "",
                                            error ),
                            error.c_str() );
        const sw::AutomationLoad& load = scenario.getLoad();
        SW_EXPECT_TRUE( load._bDeclared );
        SW_EXPECT_EQUAL( 200u, load._clientCount );
        SW_EXPECT_EQUAL( 8000u, load._meshCount );
        SW_EXPECT_EQUAL( 1u, load._moverCount );
        SW_EXPECT_NEAR_EQUAL( 2.5f, load._rampUpSeconds, 1.0e-6f );
        SW_EXPECT_NEAR_EQUAL( 30.0f, load._durationSeconds, 1.0e-6f );
        SW_EXPECT_EQUAL( sw::string( "lte" ), load._network );
    }
    static constexpr const utf8* kArrBadLoad[] = {
        R"(<Scenario name="t"><Load meshes="8"/></Scenario>)",
        R"(<Scenario name="t"><Load durationSeconds="5" network="dialup"/></Scenario>)",
        R"(<Scenario name="t"><Load durationSeconds="5" bots="3"/></Scenario>)",
        R"(<Scenario name="t"><Load durationSeconds="5"/><Load durationSeconds="5"/></Scenario>)",
        R"(<Scenario name="t"><Load durationSeconds="5"/><At frame="3"><Pass/></At></Scenario>)",
    };
    for ( const utf8* pFile : kArrBadLoad )
    {
        sw::AutomationScenario scenario;
        sw::string             error;
        SW_EXPECT_FALSE_MSG( scenario.parse( pFile, "", error ), pFile );
        SW_EXPECT_TRUE_MSG( error.find( "Load" ) != sw::string::npos || error.find( "Pass" ) != sw::string::npos, error.c_str() );
    }

    sw::GlobalVariableManager& variables = sw::engine::getSubsystem<sw::GlobalVariableManager>();
    sw::InputManager           input;
    SW_ASSERT_TRUE( input.initialize() );
    if ( variables.findVariable( "gv_profileSeconds" ) == nullptr )
    {
        // 프레임 프로파일러가 없는 구성(Shipping)은 측정 창을 열 수 없으니 <Load> 를 읽기 오류로 끝낸다.
        SW_TEST_DEFENSIVE_SCOPE( "a build without the frame profiler rejects <Load>" );
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="8"><Load durationSeconds="2.2"/><At frame="1"><TestMark/></At></Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( runner, input, 30 ) == sw::AutomationResult::LoadError );
        input.shutdown();
        return;
    }
    const sw::string profileSeconds = variables.findVariable( "gv_profileSeconds" )->getValueAsString();
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [profileSeconds]()
    {
        (void)sw::engine::getSubsystem<sw::GlobalVariableManager>().setValueFromString( "gv_profileSeconds", profileSeconds ); // 원래 값으로 — 실패해도 다음 케이스가 이 변수를 읽지 않는다
    } ) );
    {
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately" timeoutFrames="8"><Load durationSeconds="2.2"/><At frame="1"><TestMark/></At></Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( runner, input, 30 ) == sw::AutomationResult::Running ); // 프레임 시한(8)을 넘어도 측정 창이 끝낼 때까지 돈다
        SW_EXPECT_EQUAL( sw::string( "3" ), variables.findVariable( "gv_profileSeconds" )->getValueAsString() );
        // 측정 창이 닫혀 엔진이 루프를 끝내면(창 닫힘 자리) 단계를 다 돈 부하 시나리오는 통과다.
        SW_EXPECT_TRUE_MSG( runner.onWindowClosed( &input ) == sw::AutomationResult::Passed, runner.getFinishReason().c_str() );
    }
    input.shutdown();
}

/**
 * @brief [AutomationScenarioTest] 등록 단계는 시작할 때 검사되고, 입력 전 단계는 그 프레임의 onFrameBegin 에서 돈다
 */
SW_TEST_CASE( AutomationScenarioTest, RegisteredStepRunsOnItsFrame )
{
    using Internal = TestAutomationScenarioInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::AutomationRunner runner;
    SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately"><At frame="3"><TestMark/></At><At frame="4"><Pass/></At></Scenario>)" ) );
    SW_EXPECT_TRUE( Internal::run( runner, input ) == sw::AutomationResult::Passed );
    SW_EXPECT_EQUAL( 1u, Internal::s_beforeInputRunCount );
    SW_EXPECT_EQUAL( 3u, Internal::s_beforeInputFrame );
    input.shutdown();
}

/**
 * @brief [AutomationScenarioTest] ExpectLog 는 since 프레임 뒤의 줄만 센다 — 실행기 자신의 [Scenario] 줄은 세지 않는다
 */
SW_TEST_CASE( AutomationScenarioTest, ExpectLogCountsLinesSinceAFrame )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::AutomationRunner runner;
    SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately">
        <At frame="2"><ExpectLog contains="[AutoProbe] tick" count="3"/></At>
        <At frame="4"><ExpectLog contains="[AutoProbe] tick" count="2" since="3"/><ExpectLog contains="[AutoProbe]" atLeast="5"/><Pass/></At>
      </Scenario>)" ) );
    sw::AutomationResult result = sw::AutomationResult::Running;
    for ( uint32 frame = 0; frame < 20 && result == sw::AutomationResult::Running; ++frame )
    {
        runner.onFrameBegin( input );
        input.beginFrame( 1.0f / 60.0f );
        SW_LOG_WARNING( "[AutoProbe] tick %#", frame ); // Warning — Info 는 Shipping 에서 호출째 사라져 셀 줄이 없다
        result = runner.onFrameEnd( input );
        input.endFrame();
    }
    SW_EXPECT_TRUE_MSG( result == sw::AutomationResult::Passed, runner.getFinishReason().c_str() );
    input.shutdown();
}

/**
 * @brief [AutomationScenarioTest] 로그 게이트 — 선언하지 않은 Warning 을 남긴 시나리오는 `<Pass/>` 에 닿아도 실패고, 방어 구간 줄은 보지 않는다
 * @details 게이트를 걸리게 할 줄은 방어 구간 밖에서 남겨야 하므로 테스트 로그에 `[AutoProbe] undeclared` 경고 한 줄이 남는다.
 */
SW_TEST_CASE( AutomationScenarioTest, UnexpectedWarningFailsAPassingScenario )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::AutomationRunner runner;
    SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately"><At frame="3"><Pass/></At></Scenario>)" ) );
    sw::AutomationResult result = sw::AutomationResult::Running;
    for ( uint32 frame = 0; frame < 20 && result == sw::AutomationResult::Running; ++frame )
    {
        runner.onFrameBegin( input );
        input.beginFrame( 1.0f / 60.0f );
        if ( frame == 1 )
        {
            SW_TEST_DEFENSIVE_SCOPE( "the gate ignores lines a test marked as expected" );
            SW_LOG_WARNING( "[AutoProbe] marked" );
        }
        if ( frame == 2 )
            SW_LOG_WARNING( "[AutoProbe] undeclared" ); // 이 줄이 게이트에 걸린다
        result = runner.onFrameEnd( input );
        input.endFrame();
    }
    SW_EXPECT_TRUE( result == sw::AutomationResult::Failed );
    SW_EXPECT_TRUE_MSG( runner.getFinishReason().find( sw::AutomationLogPolicy::kAllowListPath ) != sw::string::npos, runner.getFinishReason().c_str() );
    input.shutdown();
}

/**
 * @brief [AutomationScenarioTest] 허용 목록은 항목마다 이유가 있어야 읽히고, 부분 문자열로 줄을 허용한다
 */
SW_TEST_CASE( AutomationScenarioTest, LogAllowListNeedsAReasonPerEntry )
{
    sw::AutomationLogPolicy policy;
    sw::string              error;
    SW_EXPECT_TRUE( policy.parseAllowList( R"({ "allowed": [ { "contains": "font has no glyph", "reason": "system font varies per PC" } ] })", "ok.json", error ) );
    SW_EXPECT_TRUE( policy.isAllowed( "[Warning] [FontService] font has no glyph for U+AC00" ) );
    SW_EXPECT_FALSE( policy.isAllowed( "[Warning] [FontService] font is missing" ) );
    SW_EXPECT_FALSE( policy.parseAllowList( R"({ "allowed": [ { "contains": "x" } ] })", "noreason.json", error ) );
    SW_EXPECT_TRUE( error.find( "reason" ) != sw::string::npos );
    SW_EXPECT_TRUE( sw::AutomationLogPolicy::isExemptMessage( "[Expected Defensive Test] broken file" ) );
    SW_EXPECT_TRUE( sw::AutomationLogPolicy::isExemptMessage( "[Scenario] FAIL t" ) );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::AutomationLogPolicy repository;
    SW_EXPECT_TRUE_MSG( repository.loadAllowList( error ), error.c_str() );
}

/**
 * @brief [AutomationScenarioTest] 같은 이름의 탐침 · 단계는 두 번 등록되지 않고, 엔진 단계 이름은 등록표가 받지 않는다
 */
SW_TEST_CASE( AutomationScenarioTest, RegistriesRejectDuplicateNames )
{
    using Internal = TestAutomationScenarioInternal;
    SW_TEST_DEFENSIVE_SCOPE( "duplicate registrations are reported" );
    static const sw::AutomationProbeRegistration kDuplicateProbe{ "Test.SwitchCount", "duplicate", &Internal::readNothing, nullptr, false, false };
    SW_EXPECT_FALSE( sw::AutomationProbes::registerProbe( &kDuplicateProbe ) );
    float64 value = -1.0;
    SW_EXPECT_TRUE( sw::AutomationProbes::find( "Test.SwitchCount" )->_pFunction( nullptr, value ) ); // 앞 등록이 남아 있다

    static const sw::AutomationStepRegistration kEngineNamedStep{ "Tap", &Internal::runMark, nullptr, nullptr, nullptr, false };
    SW_EXPECT_FALSE( sw::AutomationStepRegistry::registerStep( &kEngineNamedStep ) );
    static const sw::AutomationStepRegistration kDuplicateStep{ "TestMark", &Internal::runMark, nullptr, nullptr, nullptr, false };
    SW_EXPECT_FALSE( sw::AutomationStepRegistry::registerStep( &kDuplicateStep ) );
    SW_EXPECT_TRUE( sw::AutomationStepRegistry::find( "TestMark" )->_bBeforeInput );
    static const sw::AutomationStepRegistration kNothingToDo{ "TestNothing", nullptr, nullptr, nullptr, nullptr, false };
    SW_EXPECT_FALSE( sw::AutomationStepRegistry::registerStep( &kNothingToDo ) ); // 실행도 준비도 없는 단계는 받지 않는다
}

/**
 * @brief [AutomationScenarioTest] 엔진 단계도 레지스트리에 있다 — 입력 단계는 준비만, 조건 · 스크린샷 기다리기는 차례 함수를 든다
 * @details 실행기는 단계 이름으로 분기하지 않는다. 엔진 단계 이름의 등록이 거절되는 것은 엔진 단계가 같은 목록에 먼저 올라 있어서다.
 */
SW_TEST_CASE( AutomationScenarioTest, EngineStepsAreRegistered )
{
    static constexpr const utf8* kArrInputKind[] = { "Press", "Release", "Tap", "MouseDelta", "MousePosition", "GamepadAxis", "Text" };
    for ( const utf8* pKind : kArrInputKind )
    {
        const sw::AutomationStepRegistration* pRegistration = sw::AutomationStepRegistry::find( pKind );
        SW_ASSERT_NOT_NULL( pRegistration );
        SW_EXPECT_TRUE( pRegistration->_pRun == nullptr && pRegistration->_pPrepare != nullptr && pRegistration->_pValidate != nullptr );
    }
    static constexpr const utf8* kArrRunKind[] = { "Variable", "Expect", "ExpectLog", "Screenshot", "ExpectImage", "CloseWindow", "ExpectExitWithin", "Pass", "Fail", "Skip" };
    for ( const utf8* pKind : kArrRunKind )
    {
        const sw::AutomationStepRegistration* pRegistration = sw::AutomationStepRegistry::find( pKind );
        SW_ASSERT_NOT_NULL( pRegistration );
        SW_EXPECT_TRUE( pRegistration->_pRun != nullptr && pRegistration->_pValidate != nullptr && pRegistration->_bBeforeInput == false );
    }
    SW_EXPECT_TRUE( sw::AutomationStepRegistry::find( "WaitUntil" )->_pHold != nullptr );
    SW_EXPECT_TRUE( sw::AutomationStepRegistry::find( "ExpectImage" )->_pHold != nullptr );
    SW_EXPECT_TRUE( sw::AutomationStepRegistry::find( "Expect" )->_pHold == nullptr );
}

/**
 * @brief [AutomationScenarioTest] 핫 리로드의 교체 이미지는 같은 이름의 단계를 대체하고, 그 이미지가 취소되면(등록을 빼면) 앞 등록이 되살아난다
 * @details 엔진 단계도 같은 목록이라 같은 규칙이다. 리로드 밖의 같은 이름은 위 `RegistriesRejectDuplicateNames` 처럼 거절한다.
 */
SW_TEST_CASE( AutomationScenarioTest, ReloadImageReplacesStepAndRestoresOnCancel )
{
    using Internal                                         = TestAutomationScenarioInternal;
    const sw::AutomationStepRegistration*       pEngineTap = sw::AutomationStepRegistry::find( "Tap" );
    static const sw::AutomationStepRegistration kReloadTap{ "Tap", &Internal::runMark, nullptr, nullptr, nullptr, false };
    SW_ASSERT_NOT_NULL( pEngineTap );
    {
        sw::RegistrationReloadScope scope;
        SW_EXPECT_TRUE( sw::AutomationStepRegistry::registerStep( &kReloadTap ) );
    }
    SW_EXPECT_TRUE( sw::AutomationStepRegistry::find( "Tap" ) == &kReloadTap );
    sw::AutomationStepRegistry::unregisterStep( &kReloadTap ); // 교체 이미지가 취소됐다
    SW_EXPECT_TRUE_MSG( sw::AutomationStepRegistry::find( "Tap" ) == pEngineTap, "the engine Tap step is not restored after the reload image is withdrawn" );
}

/**
 * @brief [AutomationScenarioTest] PPM 을 읽어 영역 지표를 측정한다 — 왼쪽 반이 어두운 4×4 그림
 * @details darkFraction 은 영역 중앙값 대비라, 영역 전체가 같은 밝기면 0 이고(반쪽만 보면 0) 밝고 어두운 것이 섞여야 어두운 몫이 나온다.
 */
SW_TEST_CASE( AutomationScenarioTest, ImageMetricsReadAPpm )
{
    // 왼쪽 두 열은 (20,20,20), 오른쪽 두 열은 (200,100,50)
    sw::string ppm = "P6\n# test\n4 4\n255\n";
    for ( uint32 y = 0; y < 4; ++y )
    {
        for ( uint32 x = 0; x < 4; ++x )
        {
            const bool bDark = x < 2;
            ppm.push_back( static_cast<utf8>( bDark ? 20 : 200 ) );
            ppm.push_back( static_cast<utf8>( bDark ? 20 : 100 ) );
            ppm.push_back( static_cast<utf8>( bDark ? 20 : 50 ) );
        }
    }
    sw::AutomationImage image;
    sw::string          error;
    SW_ASSERT_TRUE_MSG( sw::AutomationImageMetric::parsePpm( reinterpret_cast<const uint8*>( ppm.data() ), ppm.size(), image, error ), error.c_str() );
    SW_EXPECT_EQUAL( 4u, image._width );

    const auto measure = [&image]( const utf8* pRegion, sw::AutomationImageMetricKind kind, const sw::AutomationImage* pReference = nullptr )
    {
        sw::AutomationImageRegion region{};
        float64                   value = -1.0;
        sw::string                reason;
        SW_EXPECT_TRUE( sw::AutomationImageMetric::tryParseRegion( pRegion, region ) );
        SW_EXPECT_TRUE_MSG( sw::AutomationImageMetric::measure( image, region, kind, 0.7f, pReference, value, reason ), reason.c_str() );
        return value;
    };
    SW_EXPECT_NEAR_EQUAL( 0.5, measure( "0,0,1,1", sw::AutomationImageMetricKind::DarkFraction ), 1e-9 );
    SW_EXPECT_NEAR_EQUAL( 0.0, measure( "0,0,0.5,1", sw::AutomationImageMetricKind::DarkFraction ), 1e-9 );
    SW_EXPECT_NEAR_EQUAL( 0.0, measure( "0.5,0,1,1", sw::AutomationImageMetricKind::DarkFraction ), 1e-9 );
    SW_EXPECT_NEAR_EQUAL( 20.0 / 255.0, measure( "0,0,0.5,1", sw::AutomationImageMetricKind::MeanLuma ), 1e-6 );
    SW_EXPECT_NEAR_EQUAL( 150.0 / 255.0, measure( "0.5,0,1,1", sw::AutomationImageMetricKind::MeanRedMinusBlue ), 1e-6 );
    SW_EXPECT_NEAR_EQUAL( 0.0, measure( "0,0,1,1", sw::AutomationImageMetricKind::DifferentFrom, &image ), 1e-9 );

    // 깨진 PPM · 틀린 영역은 거절한다.
    sw::AutomationImage       broken;
    sw::AutomationImageRegion region{};
    SW_EXPECT_FALSE( sw::AutomationImageMetric::parsePpm( reinterpret_cast<const uint8*>( ppm.data() ), ppm.size() - 5, broken, error ) );
    SW_EXPECT_FALSE( sw::AutomationImageMetric::parsePpm( reinterpret_cast<const uint8*>( "P3 1 1 255 0 0 0" ), 16, broken, error ) );
    SW_EXPECT_FALSE( sw::AutomationImageMetric::tryParseRegion( "0.5,0,0.5,1", region ) );
    SW_EXPECT_FALSE( sw::AutomationImageMetric::tryParseRegion( "0,0,1", region ) );
}

/**
 * @brief [AutomationScenarioTest] 렌더러가 없으면 ExpectImage 는 스크린샷을 기다리다 실패로 적는다(조용히 통과하지 않는다)
 */
SW_TEST_CASE( AutomationScenarioTest, ExpectImageWithoutRendererFails )
{
    using Internal = TestAutomationScenarioInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::AutomationRunner runner;
    SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately">
        <At frame="1"><Screenshot file="a.ppm"/></At>
        <At frame="2"><ExpectImage file="a.ppm" metric="meanLuma" atLeast="0"/><Pass/></At>
      </Scenario>)" ) );
    SW_TEST_DEFENSIVE_SCOPE( "no renderer takes the screenshot" );
    sw::string                 pendingPath;
    const sw::AutomationResult result = Internal::run( runner, input, 100 );
    SW_EXPECT_TRUE( result == sw::AutomationResult::Failed );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( runner.getFailures().size() ) );
    SW_EXPECT_TRUE( runner.getFailures()[0].find( "never completed" ) != sw::string::npos );
    SW_EXPECT_TRUE( runner.takePendingScreenshotPath( pendingPath ) ); // 아무도 가져가지 않았다
    SW_EXPECT_TRUE( pendingPath.find( "Saved/Automation/t/a.ppm" ) != sw::string::npos );
    input.shutdown();
}
