#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Tests/ScenarioFileCheck.h"
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
    string             error;
    SW_REQUIRE_TRUE( scenario.parse( kText, "test.scenario.xml", error ) );
    SW_EXPECT_FALSE( AutomationStepValidation::validateScreenshotFiles( scenario, error ) );
    SW_EXPECT_TRUE( error.find( "a.ppm" ) != string::npos );
}

// 입력은 그 프레임의 틱 전에 든다 - 같은 프레임에서 `<Expect>` 아래에 적힌 입력은 단언보다 먼저 돌므로 읽을 때 거절하고, 사이에 한 프레임을 두면 받는다.
SW_TEST_CASE( ScenarioFilesTest, InputBelowJudgeInSameFrameIsRejected )
{
    AutomationScenario scenario;
    string             error;
    SW_REQUIRE_TRUE( scenario.parse( "<Scenario name=\"test.phase\"><Expect watch=\"Scene.Transitioning\" equals=\"0\"/><Press slot=\"Key.D\"/><Pass/></Scenario>", "test.scenario.xml", error ) );
    SW_EXPECT_FALSE( AutomationStepValidation::validateStepPhaseOrder( scenario, error ) );
    SW_EXPECT_TRUE( error.find( "<Press>" ) != string::npos );
    SW_REQUIRE_TRUE( scenario.parse( "<Scenario name=\"test.phase\"><Expect watch=\"Scene.Transitioning\" equals=\"0\"/><Wait frames=\"1\"/><Press slot=\"Key.D\"/><Pass/></Scenario>",
                                     "test.scenario.xml", error ) );
    SW_EXPECT_TRUE( AutomationStepValidation::validateStepPhaseOrder( scenario, error ) );
}
