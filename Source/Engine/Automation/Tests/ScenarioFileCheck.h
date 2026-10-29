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

    /** @brief 한 폴더의 시나리오를 실행기와 같은 읽기 · 단계 검사에 넣습니다. 폴더에 시나리오가 있으면 true 입니다. */
    inline bool checkScenarioFolder( string_view folder )
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
