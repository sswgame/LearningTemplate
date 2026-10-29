#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepValidation.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/FileSystem/AssetIO.h"

using namespace sw;

namespace
{
    /** @brief 통과면 빈 글, 아니면 `<앞머리>: <오류>` 입니다 — 실패 글에 어느 파일 · 단계인지 쓴다. */
    string makeFailureText( bool bPassed, string_view prefix, string_view error )
    {
        string text;
        if ( bPassed == false )
        {
            text += prefix;
            text += ": ";
            text += error;
        }
        return text;
    }

    /** @brief 한 폴더의 시나리오를 실행기와 같은 읽기 · 단계 검사에 넣습니다. 폴더에 시나리오가 있으면 true 입니다. */
    bool checkScenarioFolder( string_view folder )
    {
        vector<AssetID> listScenario;
        if ( AssetIO::collectAssets( AssetID{ folder }, ".scenario.xml", listScenario ) != AssetFolderListing::Listed )
            return false;
        for ( const AssetID& scenarioID : listScenario )
        {
            const string path( scenarioID.view() );
            string       text;
            SW_EXPECT_TRUE( AssetIO::readTextAsset( scenarioID, text ) );
            AutomationScenario scenario;
            string             error;
            const bool         bParsed = scenario.parse( text, path, error );
            SW_EXPECT_EQUAL( string(), makeFailureText( bParsed, path, error ) );
            if ( bParsed == false )
                continue;
            SW_EXPECT_EQUAL( string(), makeFailureText( AutomationStepValidation::validateDesktopSteps( scenario, error ), path, error ) );
            for ( const AutomationStep& step : scenario.getSteps() )
            {
                // 엔진이 서 있고 모든 모듈(게임 · 키트)이 올라왔다 — 이름(단계 · 감시 값 · 전역 변수 · 개발 명령 · 입력 슬롯)을 모르면 그것이 오류다.
                string                         unresolvedName;
                const AutomationStepDependency dependency = AutomationStepValidation::findUnresolvedDependency( step, unresolvedName );
                const bool                     bResolved  = dependency == AutomationStepDependency::Resolved;
                SW_EXPECT_EQUAL( string(), makeFailureText( bResolved, path + " " + step.describe(), string( AutomationStepValidation::getDependencyLabel( dependency ) ) + " '" + unresolvedName + "'" ) );
                if ( bResolved == false )
                    continue;
                AutomationStepValidateFunction pfnValidate = nullptr;
                if ( AutomationStepValidation::findValidator( step._kind, pfnValidate ) != AutomationValidatorLookup::Found )
                    continue;
                string     stepError;
                const bool bValid = pfnValidate( step, stepError );
                SW_EXPECT_EQUAL( string(), makeFailureText( bValid, path + " " + step.describe(), stepError ) );
            }
        }
        return listScenario.empty() == false;
    }
} // namespace

// 실행 없이 모든 시나리오를 실행기의 읽기 · 단계 검사(`AutomationRunner::prepare` 앞부분)에 넣는다 — 엔진과 활성 게임의 자동화 폴더(하위 폴더 포함).
SW_TEST_CASE( ScenarioFilesTest, AllScenariosParseResolveAndValidate )
{
    const string gameFolder = "game/" + StringUtil::toLower( GameSelection::getName().c_str() ) + "/automation";
    SW_EXPECT_TRUE( checkScenarioFolder( "engine/automation" ) );
    (void)checkScenarioFolder( "engine/automation/rhiswap" ); // 조건이 붙은 시나리오(네 백엔드) — 없어도 된다
    (void)checkScenarioFolder( gameFolder );                  // 시나리오가 없는 게임도 있다
}
