#include "pch.h"

#include "Core/Log/Logger.h"

#include "Engine/Automation/AutomationImageMetric.h"
#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

// 자동화 시나리오 — 씬 · 창 없이 실행기를 손으로 돌린다(EngineLoop 의 프레임 앞뒤 자리를 시험이 대신 부른다).
SW_LOG_CALLER( "TestAutomationScenario" );

namespace
{
    struct TestAutomationScenarioInternal
    {
        static inline uint32 s_switchCount         = 0;
        static inline bool   s_bSwitchDown         = false;
        static inline uint32 s_beforeInputRunCount = 0;
        static inline uint32 s_beforeInputFrame    = 0;

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

        /** @brief 등록 단계 — 입력 재생 전에 돈다(행동 층 주입 자리). 프레임을 적어 둔다. */
        static bool runMark( sw::AutomationRunner& runner, const sw::AutomationStep& /*step*/ )
        {
            ++s_beforeInputRunCount;
            s_beforeInputFrame = runner.getFrameIndex();
            return true;
        }

        static bool validateMark( const sw::AutomationStep& step, sw::string& outError )
        {
            if ( step._listAttribute.empty() )
                return true;
            outError = step.describe() + ": <TestMark> takes no attribute";
            return false;
        }

        /** @brief 실행기를 끝날 때까지(최대 @p maxFrames) EngineLoop 와 같은 순서로 돌린다. "Switch" 발동을 센다. */
        static sw::AutomationResult run( sw::AutomationRunner& runner, sw::InputManager& input, uint32 maxFrames = 200 )
        {
            s_switchCount               = 0;
            s_bSwitchDown               = false;
            s_beforeInputRunCount       = 0;
            sw::AutomationResult result = sw::AutomationResult::Running;
            for ( uint32 frame = 0; frame < maxFrames && result == sw::AutomationResult::Running; ++frame )
            {
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
    SW_AUTOMATION_STEP( testMark, "TestMark", &TestAutomationScenarioInternal::runMark, &TestAutomationScenarioInternal::validateMark, true );
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

    // <MousePosition> 은 엔진 단계다(등록표의 엔진 단계 목록에 있어야 "모르는 단계" 가 아니다) — 창이 없는 시험에서는 커서를 놓을 자리가 없다는 오류.
    {
        sw::AutomationRunner runner;
        SW_ASSERT_TRUE( runner.startFromText( R"(<Scenario name="t" startAfter="Immediately"><At frame="0"><MousePosition x="0.5" y="0.5"/></At></Scenario>)" ) );
        SW_EXPECT_TRUE( Internal::run( runner, input, 5 ) == sw::AutomationResult::LoadError );
        SW_EXPECT_TRUE_MSG( runner.getFinishReason().find( "no window to place the pointer" ) != sw::string::npos, runner.getFinishReason().c_str() );
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
        // 시험 하네스에는 플레이를 시작한 씬이 없다(있어도 이 시험 동안 플레이를 시작하지 않는다) — 단계는 하나도 돌지 않는다.
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
 * @brief [AutomationScenarioTest] 같은 이름의 탐침 · 단계는 두 번 등록되지 않고, 엔진 단계 이름은 등록표가 받지 않는다
 */
SW_TEST_CASE( AutomationScenarioTest, RegistriesRejectDuplicateNames )
{
    using Internal = TestAutomationScenarioInternal;
    SW_TEST_DEFENSIVE_SCOPE( "duplicate registrations are reported" );
    static const sw::AutomationProbeRegistration kDuplicateProbe{ "Test.SwitchCount", "duplicate", &Internal::readNothing };
    SW_EXPECT_FALSE( sw::AutomationProbes::registerProbe( &kDuplicateProbe ) );
    float64 value = -1.0;
    SW_EXPECT_TRUE( sw::AutomationProbes::find( "Test.SwitchCount" )->_pFunction( nullptr, value ) ); // 앞 등록이 남아 있다

    static const sw::AutomationStepRegistration kEngineNamedStep{ "Tap", &Internal::runMark, nullptr, false };
    SW_EXPECT_FALSE( sw::AutomationStepRegistry::registerStep( &kEngineNamedStep ) );
    static const sw::AutomationStepRegistration kDuplicateStep{ "TestMark", &Internal::runMark, nullptr, false };
    SW_EXPECT_FALSE( sw::AutomationStepRegistry::registerStep( &kDuplicateStep ) );
    SW_EXPECT_TRUE( sw::AutomationStepRegistry::find( "TestMark" )->_bBeforeInput );
}

/**
 * @brief [AutomationScenarioTest] PPM 을 읽어 영역 지표를 잰다 — 왼쪽 반이 어두운 4×4 그림
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
