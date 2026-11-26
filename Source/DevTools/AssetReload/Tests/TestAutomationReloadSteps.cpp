// 플레이 중 리로드 단계의 읽기 — 모듈 다시 짓기 판 표 · 설정 덮어쓰기 경로 · 단계 속성 검사(nogpu). 빌드 도구를 부르는 실행은 시나리오(desktop/editor/playreload)가 본다.
#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/AssetReload/AutomationReloadSteps.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Config/ConfigManager.h"

using namespace sw;

namespace
{
    /** @brief 단계 하나뿐인 시나리오 글을 읽어 그 단계를 등록된 검사 함수에 넣습니다(실행기의 시작 검사와 같은 길). */
    expected<void> validateSingleStep( string_view stepXML )
    {
        AutomationScenario                        scenario;
        StringBuilder<constant::buffer::kMax1024> text;
        text.appendFormat( "<Scenario name=\"test.reloadsteps\">%#<Pass/></Scenario>", stepXML );
        const auto parsed = scenario.parse( text.view(), "test.scenario.xml" );
        if ( parsed.has_value() == false )
            return parsed;
        const AutomationStep&                   step          = scenario.getSteps().front();
        const AutomationStepRegistration* const pRegistration = AutomationStepRegistry::find( step._kind );
        if ( pRegistration == nullptr || pRegistration->_pValidate == nullptr )
            return sw::unexpected( Error{ ErrorKind::NotFound, "no registered step validator", step._kind } );
        return pRegistration->_pValidate( step );
    }
} // namespace

/** @brief [AutomationReloadStepsTest] 판 표에서 이름으로 판을 읽는다 — 없는 판 · 빈 글 · 빌드 트리를 벗어나는 머리 경로 · JSON 이 아닌 표는 오류다 */
SW_TEST_CASE( AutomationReloadStepsTest, VariantTableReadsOnlyWellFormedVariants )
{
    const auto parse = []( string_view text )
    { return ConfigManager::parseConfig<ModuleRebuildVariantTable>( text, "table.json" ); };
    const expected<ModuleRebuildVariantTable> table = parse( R"({ "_mapVariant": { "next": { "_header": "sw/dev/Probe.h", "_text": "#define STEP 1\n" } } })" );
    SW_REQUIRE_TRUE( table.has_value() );
    const expected<ModuleRebuildVariant> next = table->find( "next" );
    SW_REQUIRE_TRUE( next.has_value() );
    SW_EXPECT_EQUAL( string( "sw/dev/Probe.h" ), next->_header );
    SW_EXPECT_EQUAL( string( "#define STEP 1\n" ), next->_text );
    SW_EXPECT_FALSE( table->find( "missing" ).has_value() );

    SW_EXPECT_FALSE( parse( R"({ "_mapVariant": { "escape": { "_header": "../Source/Probe.h", "_text": "x" } } })" ).has_value() ); // `..` 로 추적 소스를 덮지 못한다
    SW_EXPECT_FALSE( parse( R"({ "_mapVariant": { "blank": { "_header": "sw/dev/Probe.h", "_text": "" } } })" ).has_value() );
    SW_EXPECT_LOG( "Config.NotJSONObject" );
    SW_EXPECT_FALSE( parse( "[ 1, 2 ]" ).has_value() );
}

/** @brief [AutomationReloadStepsTest] 저장소의 판 표(Config/Dev/ModuleRebuildVariants.json)는 시나리오가 쓰는 두 판을 읽힌다 — 데이터가 틀리면 시나리오 전에 여기서 진다 */
SW_TEST_CASE( AutomationReloadStepsTest, ShippedVariantTableHasTheScenarioVariants )
{
    const expected<ModuleRebuildVariantTable> table = ConfigManager::readConfig( ModuleRebuildVariantTable::kConfigFile );
    SW_REQUIRE_TRUE( table.has_value() );
    const expected<ModuleRebuildVariant> next   = table->find( "probe-next" );
    const expected<ModuleRebuildVariant> broken = table->find( "probe-broken" );
    SW_REQUIRE_TRUE( next.has_value() );
    SW_REQUIRE_TRUE( broken.has_value() );
    SW_EXPECT_EQUAL( next->_header, broken->_header );                // 같은 판 머리를 덮는다 — 끝의 되돌리기가 한 파일이다
    SW_EXPECT_TRUE( broken->_text.find( "#error" ) != string::npos ); // 컴파일 오류 판
}

/** @brief [AutomationReloadStepsTest] 설정 덮어쓰기 경로는 Config/ 아래 .json 상대 경로만 받는다 */
SW_TEST_CASE( AutomationReloadStepsTest, ConfigFileMustBeARelativeJSONPath )
{
    SW_EXPECT_TRUE( AutomationReloadSteps::validateConfigFile( "Editor/editortooldefaults.json" ).has_value() );
    SW_EXPECT_FALSE( AutomationReloadSteps::validateConfigFile( "" ).has_value() );
    SW_EXPECT_FALSE( AutomationReloadSteps::validateConfigFile( "../Source/a.json" ).has_value() );
    SW_EXPECT_FALSE( AutomationReloadSteps::validateConfigFile( "Editor/../../a.json" ).has_value() );
    SW_EXPECT_FALSE( AutomationReloadSteps::validateConfigFile( "/etc/a.json" ).has_value() );
    SW_EXPECT_FALSE( AutomationReloadSteps::validateConfigFile( "C:/a.json" ).has_value() );
    SW_EXPECT_FALSE( AutomationReloadSteps::validateConfigFile( "Editor/a.xml" ).has_value() );
}

/** @brief [AutomationReloadStepsTest] 단계 속성 검사 — OverwriteConfig 는 configFile 이 필수이고 경로 규칙을 따르며, RestoreOverwrites 는 속성이 없다 */
SW_TEST_CASE( AutomationReloadStepsTest, StepAttributesAreValidatedAtLoad )
{
    SW_EXPECT_TRUE( validateSingleStep( "<OverwriteConfig configFile=\"Editor/editortooldefaults.json\"/>" ).has_value() );
    SW_EXPECT_FALSE( validateSingleStep( "<OverwriteConfig/>" ).has_value() );
    SW_EXPECT_FALSE( validateSingleStep( "<OverwriteConfig configFile=\"../Config/a.json\"/>" ).has_value() );
    SW_EXPECT_FALSE( validateSingleStep( "<OverwriteConfig configFile=\"Editor/a.json\" form=\"typo\"/>" ).has_value() ); // 모르는 속성
    SW_EXPECT_TRUE( validateSingleStep( "<RestoreOverwrites/>" ).has_value() );
    SW_EXPECT_FALSE( validateSingleStep( "<RestoreOverwrites file=\"x\"/>" ).has_value() );
}

/** @brief [AutomationReloadStepsTest] RebuildModule 의 인자 — name 은 필수, variant 는 선택이다(등록은 모듈 리로드가 있는 구성만 — 스키마는 어디서나 본다) */
SW_TEST_CASE( AutomationReloadStepsTest, RebuildModuleNeedsAName )
{
    AutomationScenario scenario;
    SW_REQUIRE_TRUE( scenario.parse( "<Scenario name=\"test.rebuild\"><RebuildModule variant=\"probe-next\"/><RebuildModule name=\"SWGame\"/>"
                                     "<RebuildModule name=\"SWGame\" variant=\"probe-next\"/><Pass/></Scenario>",
                                     "test.scenario.xml" )
                         .has_value() );
    SW_REQUIRE_TRUE( scenario.getSteps().size() >= 3u );
    SW_EXPECT_FALSE( AutomationStep::validateProperties<AutomationRebuildModuleParams>( scenario.getSteps()[0] ).has_value() );
    SW_EXPECT_TRUE( AutomationStep::validateProperties<AutomationRebuildModuleParams>( scenario.getSteps()[1] ).has_value() );
    SW_EXPECT_TRUE( AutomationStep::validateProperties<AutomationRebuildModuleParams>( scenario.getSteps()[2] ).has_value() );
}
