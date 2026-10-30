/**
 * @file ScenarioFileCheck.h
 * @brief 시나리오 폴더 하나를 실행기와 같은 읽기 · 단계 검사에 넣는 테스트 도우미입니다(실행 없이). 엔진 · 편집기 단위 테스트가 함께 쓴다.
 * @details 편집기 단계(`EditorClick` …)는 편집기 모듈이 등록하므로 편집기 폴더는 편집기 단위 테스트(`editor.unitTests` 가 돌린다)가 검사한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepValidation.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/Module/HostServices.h"

namespace sw::test
{
    /** @brief 통과면 빈 글, 아니면 `<앞머리>: <오류>` 입니다 — 실패 글에 어느 파일 · 단계인지 쓴다. */
    inline string makeScenarioFailureText( bool bPassed, string_view prefix, string_view error )
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

    /** @brief @p scenario 의 `requiresModule` 이 모두 켜졌으면 true 입니다 — 실행기(`AutomationRunner::skipIfThisBuildCannotRun`)와 같은 판정이다. 호스트가 없으면(nullptr) 꺼짐으로 본다. */
    inline bool areRequiredModulesActive( const AutomationScenario& scenario, const IModuleLoader* pModuleLoader )
    {
        for ( const string& moduleName : scenario.getTestOptions()._listRequiredModule )
        {
            if ( pModuleLoader == nullptr || pModuleLoader->isModuleActive( moduleName ) == false )
                return false;
        }
        return true;
    }

    /**
     * @brief 한 폴더의 시나리오를 실행기와 같은 읽기 · 단계 검사에 넣습니다. 폴더에 시나리오가 있으면 true 입니다.
     * @param pModuleLoader 호스트의 모듈 창구 — `requiresModule` 의 모듈이 꺼진 시나리오는 이름 검사를 건너뛴다. 엔진 · 편집기가 저마다의 창구로 찾아 넘긴다.
     */
    inline bool checkScenarioFolder( string_view folder, const IModuleLoader* pModuleLoader )
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
            SW_EXPECT_EQUAL( string(), makeScenarioFailureText( bParsed, path, error ) );
            if ( bParsed == false )
                continue;
            SW_EXPECT_EQUAL( string(), makeScenarioFailureText( AutomationStepValidation::validateDesktopSteps( scenario, error ), path, error ) );
            if ( areRequiredModulesActive( scenario, pModuleLoader ) == false )
                continue; // 그 모듈이 없는 빌드 — 이름은 그 모듈이 등록한다(실행기는 건너뜀으로 끝낸다)
            for ( const AutomationStep& step : scenario.getSteps() )
            {
                // 엔진이 서 있고 모든 모듈(게임 · 키트)이 올라왔다 — 이름(단계 · 감시 값 · 전역 변수 · 개발 명령 · 입력 슬롯)을 모르면 그것이 오류다.
                string                         unresolvedName;
                const AutomationStepDependency dependency = AutomationStepValidation::findUnresolvedDependency( step, unresolvedName );
                const bool                     bResolved  = dependency == AutomationStepDependency::Resolved;
                SW_EXPECT_EQUAL( string(), makeScenarioFailureText( bResolved, path + " " + step.describe(), string( AutomationStepValidation::getDependencyLabel( dependency ) ) + " '" + unresolvedName + "'" ) );
                if ( bResolved == false )
                    continue;
                AutomationStepValidateFunction pfnValidate = nullptr;
                if ( AutomationStepValidation::findValidator( step._kind, pfnValidate ) != AutomationValidatorLookup::Found )
                    continue;
                string     stepError;
                const bool bValid = pfnValidate( step, stepError );
                SW_EXPECT_EQUAL( string(), makeScenarioFailureText( bValid, path + " " + step.describe(), stepError ) );
            }
        }
        return listScenario.empty() == false;
    }
} // namespace sw::test
