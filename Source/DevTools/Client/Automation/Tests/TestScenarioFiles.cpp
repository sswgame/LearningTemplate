#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Client/Automation/Runner/AutomationStepArgs.h"
#include "DevTools/Client/Automation/Tests/ScenarioFileCheck.h"

#include "Engine/Config/GameSelection.h"
#include "Engine/Foundation/EngineServices.h"

using namespace sw;

// 실행 없이 모든 시나리오를 실행기의 읽기 · 단계 검사(`AutomationRunner::prepare` 앞부분)에 넣는다 — 엔진과 활성 게임의 자동화 폴더(하위 폴더 포함).
SW_TEST_CASE( ScenarioFilesTest, AllScenariosParseResolveAndValidate )
{
    const IModuleLoader* const pModuleLoader = engine::findSubsystem<IModuleLoader>(); // 선택: 호스트 없는 하니스에는 없다 — 모듈을 요구하는 시나리오는 이름 검사를 건너뛴다
    const string               gameFolder    = "game/" + StringUtil::toLower( GameSelection::getName().c_str() ) + "/automation";
    SW_EXPECT_TRUE( test::checkScenarioFolder( "engine/automation", pModuleLoader ) );
    (void)test::checkScenarioFolder( "engine/automation/rhiswap", pModuleLoader ); // 조건이 붙은 시나리오(네 백엔드) — 없어도 된다
    (void)test::checkScenarioFolder( gameFolder, pModuleLoader );                  // 시나리오가 없는 게임도 있다
    (void)test::checkScenarioFolder( gameFolder + "/desktop", pModuleLoader );     // 혼자 도는 데스크톱 시나리오(Test/Scenario 가 따로 등록한다)
    (void)test::checkScenarioFolder( gameFolder + "/sequence", pModuleLoader );    // 두 번 기동 쌍
    (void)test::checkScenarioFolder( gameFolder + "/showcase", pModuleLoader );    // 따로 띄우는 쇼케이스 씬
}

// `<ExpectImage>` 는 시나리오 끝에서 판정한다 - 같은 파일을 두 번 찍으면 앞 단언이 뒤 그림을 보므로 읽을 때 거절한다.
SW_TEST_CASE( ScenarioFilesTest, ScreenshotFileTakenTwiceIsRejected )
{
    const string       kText = "<Scenario name=\"test.screenshots\"><Screenshot file=\"a.ppm\"/><Screenshot file=\"b.ppm\"/><Screenshot file=\"a.ppm\"/><Pass/></Scenario>";
    AutomationScenario scenario;
    SW_REQUIRE_TRUE( scenario.parse( kText, "test.scenario.xml" ).has_value() );
    const expected<void> rejected = AutomationStepValidation::validateScreenshotFiles( scenario );
    SW_REQUIRE_FALSE( rejected.has_value() );
    SW_EXPECT_TRUE( rejected.error().detail().find( "a.ppm" ) != string_view::npos );
}

// 입력은 그 프레임의 틱 전에 든다 - 같은 프레임에서 `<Expect>` 아래에 적힌 입력은 단언보다 먼저 돌므로, 읽기가 그 입력을 다음 프레임으로 넘긴다(적은 순서가 곧 도는 순서다).
// 예전에는 사이에 `<Wait frames="1"/>` 를 쓰라고 거절했다 - 프레임 경계를 쓰는 사람이 세지 않는다.
SW_TEST_CASE( ScenarioFilesTest, InputBelowEvaluationMovesToTheNextFrame )
{
    AutomationScenario scenario;
    SW_REQUIRE_TRUE( scenario.parse( "<Scenario name=\"test.phase\"><Expect watch=\"Scene.Transitioning\" equals=\"0\"/><Press slot=\"Key.D\"/><Pass/></Scenario>", "test.scenario.xml" ).has_value() );
    SW_REQUIRE_EQUAL( size_t{ 3 }, scenario.getSteps().size() );
    SW_EXPECT_EQUAL( scenario.getSteps()[0]._frameIndex + 1u, scenario.getSteps()[1]._frameIndex );
    SW_EXPECT_TRUE( AutomationStepValidation::validateStepPhaseOrder( scenario ).has_value() );
}

// `<AdvanceSimulation frames>` 는 고정 프레임 시간을 적은 시나리오에서만 받는다 - 벽시계로 도는 시나리오에서 N 프레임은 길이가 없다. 받으면 뒤 단계가 N 프레임 뒤에 돈다.
SW_TEST_CASE( ScenarioFilesTest, AdvanceSimulationNeedsAFixedFrameTime )
{
    AutomationScenario scenario;
    SW_EXPECT_FALSE( scenario.parse( "<Scenario name=\"test.advance\"><AdvanceSimulation frames=\"3\"/><Pass/></Scenario>", "test.scenario.xml" ).has_value() );
    SW_EXPECT_FALSE( scenario.parse( "<Scenario name=\"test.advance\" fixedDelta=\"0.0166667\"><AdvanceSimulation frames=\"0\"/><Pass/></Scenario>", "test.scenario.xml" ).has_value() );
    SW_EXPECT_FALSE( scenario.parse( "<Scenario name=\"test.advance\" fixedDelta=\"0.0166667\"><AdvanceSimulation seconds=\"1\"/><Pass/></Scenario>", "test.scenario.xml" ).has_value() );
    SW_REQUIRE_TRUE(
        scenario.parse( "<Scenario name=\"test.advance\" fixedDelta=\"0.0166667\"><Expect watch=\"Scene.Transitioning\" equals=\"0\"/><AdvanceSimulation frames=\"3\"/><Pass/></Scenario>", "test.scenario.xml" )
            .has_value() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, scenario.getSteps().size() );
    SW_EXPECT_EQUAL( scenario.getSteps()[0]._frameIndex + 3u, scenario.getSteps()[1]._frameIndex );
    // 진행 구간은 단계가 도는 프레임을 빼고 그 사이다 - 판정 · 사진 프레임은 늘 보통 프레임이다.
    SW_EXPECT_FALSE( scenario.isAdvanceSimulationFrame( 0 ) );
    SW_EXPECT_TRUE( scenario.isAdvanceSimulationFrame( 1 ) );
    SW_EXPECT_TRUE( scenario.isAdvanceSimulationFrame( 2 ) );
    SW_EXPECT_FALSE( scenario.isAdvanceSimulationFrame( 3 ) );
}

namespace ScenarioFilesTestUtil
{
    /** @brief `<WaitUntil>` 한 줄(@p attributes 는 watch · 시한 뒤에 붙는 비교)을 읽어 실행기와 같은 단계 검사에 넣습니다. */
    expected<void> validateWaitUntil( const string& attributes, AutomationScenario& outScenario )
    {
        const string         text   = "<Scenario name=\"test.increase\" fixedDelta=\"0.0166667\"><WaitUntil watch=\"Scene.ObjectCount\" timeoutFrames=\"10\" " + attributes + "/><Pass/></Scenario>";
        const expected<void> parsed = outScenario.parse( text, "test.scenario.xml" );
        if ( parsed.has_value() == false )
            return parsed;
        AutomationStepValidateFunction pfnValidate = nullptr;
        if ( AutomationStepValidation::findValidator( "WaitUntil", pfnValidate ) != AutomationValidatorLookup::Found )
            return sw::unexpected( Error{ ErrorKind::NotFound, "WaitUntil has no validator in this process" } );
        return pfnValidate( outScenario.getSteps()[0] );
    }
} // namespace ScenarioFilesTestUtil

// `increaseFrom` · `increaseBy` 는 짝이다 — 한쪽만 적으면 무엇에서 늘었는지 몰라 읽을 때(단계 검사) 거절한다. 짝이면 받는다.
SW_TEST_CASE( ScenarioFilesTest, IncreaseComparisonNeedsBothHalves )
{
    AutomationScenario scenario;
    SW_EXPECT_FALSE( ScenarioFilesTestUtil::validateWaitUntil( "increaseBy=\"60\"", scenario ).has_value() );
    SW_EXPECT_FALSE( ScenarioFilesTestUtil::validateWaitUntil( "increaseFrom=\"before\"", scenario ).has_value() );
    SW_EXPECT_FALSE( ScenarioFilesTestUtil::validateWaitUntil( "increaseFrom=\"before\" increaseBy=\"many\"", scenario ).has_value() );
    SW_EXPECT_TRUE( ScenarioFilesTestUtil::validateWaitUntil( "increaseFrom=\"before\" increaseBy=\"60\"", scenario ).has_value() );
}

// 실행 때의 판정: 기억한 값이 없거나 수가 아니면 오류, 기억한 값에서 increaseBy 미만으로 늘면 거짓, 이상이면 참이다.
SW_TEST_CASE( ScenarioFilesTest, IncreaseComparisonCountsFromTheStoredValue )
{
    AutomationScenario scenario;
    SW_REQUIRE_TRUE( ScenarioFilesTestUtil::validateWaitUntil( "increaseFrom=\"before\" increaseBy=\"60\"", scenario ).has_value() );
    const AutomationStep& step = scenario.getSteps()[0];

    SW_EXPECT_FALSE( AutomationStepArgsUtil::matchesIncrease( step, 200.0, nullptr ).has_value() ); // 그 이름으로 Store 한 적이 없다
    const string notNumber = "text";
    SW_EXPECT_FALSE( AutomationStepArgsUtil::matchesIncrease( step, 200.0, &notNumber ).has_value() );

    const string         stored = "100";
    const expected<bool> below  = AutomationStepArgsUtil::matchesIncrease( step, 159.0, &stored );
    SW_REQUIRE_TRUE( below.has_value() );
    SW_EXPECT_FALSE( *below );
    const expected<bool> exact = AutomationStepArgsUtil::matchesIncrease( step, 160.0, &stored );
    SW_REQUIRE_TRUE( exact.has_value() );
    SW_EXPECT_TRUE( *exact );
    const expected<bool> above = AutomationStepArgsUtil::matchesIncrease( step, 500.0, &stored );
    SW_REQUIRE_TRUE( above.has_value() );
    SW_EXPECT_TRUE( *above );
}

// 사진은 다음에 그리는 패킷이 찍는다 - `<Screenshot>` 바로 뒤의 진행 구간 · 입력은 그 프레임 처음에 돌므로, 실행기가 사진이 실릴 때까지 그 프레임을 붙잡는다
// (`AutomationRunner::holdClockForScreenshot`). 붙잡을 프레임은 처음 할 일이 있는 프레임뿐이다 - 판정만 있는 프레임은 사진 패킷보다 뒤에 돈다.
SW_TEST_CASE( ScenarioFilesTest, FrameAfterScreenshotKnowsItsFrameStartJob )
{
    AutomationScenario scenario;
    SW_REQUIRE_TRUE( scenario.parse( "<Scenario name=\"test.shot\" fixedDelta=\"0.0166667\"><Screenshot file=\"a.ppm\"/><AdvanceSimulation frames=\"3\"/><Pass/></Scenario>", "test.scenario.xml" )
                         .has_value() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, scenario.getSteps().size() );
    SW_EXPECT_EQUAL( 0u, scenario.getSteps()[0]._frameIndex );
    // 사진 프레임(0) 다음 프레임이 진행 구간의 첫 프레임이다 - 사진이 실리기 전에 시작하면 사진이 구간 뒤로 밀린다.
    SW_EXPECT_FALSE( scenario.hasFrameStartJobAt( 1, 0 ) );
    SW_EXPECT_TRUE( scenario.hasFrameStartJobAt( 1, 1 ) );
    SW_EXPECT_FALSE( scenario.hasFrameStartJobAt( 1, 2 ) );

    SW_REQUIRE_TRUE( scenario.parse( "<Scenario name=\"test.shot\"><Screenshot file=\"a.ppm\"/><Press slot=\"Key.D\"/><Expect watch=\"Scene.Transitioning\" equals=\"0\"/><Pass/></Scenario>", "test.scenario.xml" )
                         .has_value() );
    SW_REQUIRE_EQUAL( size_t{ 4 }, scenario.getSteps().size() );
    // 사진 아래 입력은 다음 프레임 처음에 든다 - 붙잡을 프레임이다. 같은 프레임의 판정은 프레임 끝이라 붙잡지 않는다.
    SW_EXPECT_EQUAL( 1u, scenario.getSteps()[1]._frameIndex );
    SW_EXPECT_TRUE( scenario.hasFrameStartJobAt( 1, 1 ) );
    SW_EXPECT_FALSE( scenario.hasFrameStartJobAt( 2, 1 ) );
}
