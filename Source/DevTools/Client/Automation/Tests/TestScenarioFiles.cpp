#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

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
