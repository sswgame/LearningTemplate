#include "pch.h"

#include "Core/String/StringBuilder.h"
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
    const IModuleLoader* const               pModuleLoader = engine::findSubsystem<IModuleLoader>(); // 선택: 호스트 없는 하니스에는 없다 — 모듈을 요구하는 시나리오는 이름 검사를 건너뛴다
    StringBuilder<constant::buffer::kMax256> gameFolder;
    gameFolder.appendFormat( "game/%#/automation", StringUtil::toLower( GameSelection::getName().c_str() ) );
    SW_EXPECT_TRUE( test::validateScenarioFolder( "engine/automation", pModuleLoader ) );
    (void)test::validateScenarioFolder( "engine/automation/rhiswap", pModuleLoader ); // 조건이 붙은 시나리오(네 백엔드) — 없어도 된다
    (void)test::validateScenarioFolder( gameFolder.view(), pModuleLoader );           // 시나리오가 없는 게임도 있다
    // desktop: 혼자 도는 데스크톱 시나리오(Test/Scenario 가 따로 등록한다), sequence: 두 번 기동 쌍, showcase: 따로 띄우는 쇼케이스 씬
    for ( const string_view subFolder : { "desktop", "sequence", "showcase" } )
    {
        StringBuilder<constant::buffer::kMax256> folder;
        folder.appendFormat( "%#/%#", gameFolder.view(), subFolder );
        (void)test::validateScenarioFolder( folder.view(), pModuleLoader );
    }
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

// `<Tap hold="N">` 뒤 단계는 떼는 프레임 뒤에 돈다 - 누르는 동안 돌던 같은 슬롯의 다음 `<Tap>` 은 아직 눌린 키를 다시 눌러 새 누름이 되지 않았다(일시정지 메뉴의 둘째 Esc).
SW_TEST_CASE( ScenarioFilesTest, HeldTapPushesLaterStepsPastTheRelease )
{
    AutomationScenario scenario;
    SW_REQUIRE_TRUE( scenario.parse( "<Scenario name=\"test.hold\"><Tap slot=\"Key.Escape\" hold=\"6\"/><Tap slot=\"Key.Escape\" hold=\"1\"/><Tap slot=\"Key.A\"/><Pass/></Scenario>", "test.scenario.xml" )
                         .has_value() );
    SW_REQUIRE_EQUAL( size_t{ 4 }, scenario.getSteps().size() );
    SW_EXPECT_TRUE( scenario.getSteps()[1]._frameIndex > scenario.getSteps()[0]._frameIndex + 6u ); // 첫 탭이 뗀(+6) 뒤에 누른다
    SW_EXPECT_EQUAL( scenario.getSteps()[1]._frameIndex, scenario.getSteps()[2]._frameIndex );      // hold 1 은 지금처럼 다음 단계를 밀지 않는다
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

// 벽시계로 도는 시나리오(`realTime`)는 다른 프로세스의 서버와 같은 빠르기로 돈다 - 프레임의 길이가 정해지지 않아 `<AdvanceSimulation>` 을 받지 않는다.
SW_TEST_CASE( ScenarioFilesTest, RealTimeScenarioHasNoFixedFrames )
{
    AutomationScenario scenario;
    SW_REQUIRE_TRUE( scenario.parse( "<Scenario name=\"test.realtime\" fixedDelta=\"0.0166667\" realTime=\"true\"><Pass/></Scenario>", "test.scenario.xml" ).has_value() );
    SW_EXPECT_TRUE( scenario.isRealTime() );
    SW_EXPECT_FALSE( scenario.parse( "<Scenario name=\"test.realtime\" fixedDelta=\"0.0166667\" realTime=\"true\"><AdvanceSimulation frames=\"3\"/><Pass/></Scenario>", "test.scenario.xml" ).has_value() );
    SW_EXPECT_FALSE( scenario.parse( "<Scenario name=\"test.realtime\" realTime=\"maybe\"><Pass/></Scenario>", "test.scenario.xml" ).has_value() );
    AutomationScenario fixedScenario;
    SW_REQUIRE_TRUE( fixedScenario.parse( "<Scenario name=\"test.fixed\" fixedDelta=\"0.0166667\"><Pass/></Scenario>", "test.scenario.xml" ).has_value() );
    SW_EXPECT_FALSE( fixedScenario.isRealTime() );
}

// `render="true"` 는 모든 프레임을 그리는 진행이다 - 프레임 수는 같고 그리지 않는 구간을 두지 않는다. 옛 `<Wait>` 는 읽기 오류다(대신 쓸 요소를 알린다).
SW_TEST_CASE( ScenarioFilesTest, RenderedAdvanceKeepsEveryFrameAndWaitIsGone )
{
    AutomationScenario scenario;
    SW_REQUIRE_TRUE( scenario.parse( "<Scenario name=\"test.advance\" fixedDelta=\"0.0166667\"><Expect watch=\"Scene.Transitioning\" equals=\"0\"/><AdvanceSimulation frames=\"3\" render=\"true\"/><Pass/></Scenario>",
                                     "test.scenario.xml" )
                         .has_value() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, scenario.getSteps().size() );
    SW_EXPECT_EQUAL( scenario.getSteps()[0]._frameIndex + 3u, scenario.getSteps()[1]._frameIndex );
    SW_EXPECT_FALSE( scenario.isAdvanceSimulationFrame( 1 ) );
    SW_EXPECT_FALSE( scenario.isAdvanceSimulationFrame( 2 ) );
    SW_EXPECT_FALSE( scenario.parse( "<Scenario name=\"test.advance\" fixedDelta=\"0.0166667\"><AdvanceSimulation frames=\"3\" render=\"maybe\"/><Pass/></Scenario>", "test.scenario.xml" ).has_value() );
    SW_EXPECT_FALSE( scenario.parse( "<Scenario name=\"test.wait\" fixedDelta=\"0.0166667\"><Wait frames=\"3\"/><Pass/></Scenario>", "test.scenario.xml" ).has_value() );
}

namespace ScenarioFilesTestUtil
{
    /** @brief `<WaitUntil>` 한 줄(@p attributes 는 watch · 시한 뒤에 붙는 비교)을 읽어 실행기와 같은 단계 검사에 넣습니다. */
    expected<void> validateWaitUntil( const string& attributes, AutomationScenario& outScenario )
    {
        StringBuilder<constant::buffer::kMax256> text;
        text.appendFormat( "<Scenario name=\"test.increase\" fixedDelta=\"0.0166667\"><WaitUntil watch=\"Scene.ObjectCount\" timeoutFrames=\"10\" %#/><Pass/></Scenario>", attributes );
        const expected<void> parsed = outScenario.parse( text.view(), "test.scenario.xml" );
        if ( parsed.has_value() == false )
            return parsed;
        AutomationStepValidateFunction pfnValidate = nullptr;
        if ( AutomationStepValidation::findValidator( "WaitUntil", pfnValidate ) != AutomationValidatorResult::Found )
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
