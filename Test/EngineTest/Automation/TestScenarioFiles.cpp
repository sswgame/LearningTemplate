#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepValidation.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief `Content/<domain>` 아래(재귀)의 `*.scenario.xml` 입니다. */
    vector<string> collectScenarioFiles( string_view domain )
    {
        vector<string> listFile;
        (void)FileUtil::collectFiles( string( SW_TEST_CONTENT_DIR ) + "/" + string( domain ), ".xml", listFile, true ); // 폴더가 없으면 빈 목록 — 아래 단언이 잡는다
        vector<string> listScenario;
        for ( const string& path : listFile )
        {
            if ( StringUtil::endsWith( path, ".scenario.xml" ) )
                listScenario.push_back( path );
        }
        return listScenario;
    }

    /** @brief 통과면 빈 글, 아니면 `<앞머리>: <오류>` 입니다 — `SW_EXPECT_EQUAL( string(), … )` 이 실패 글에 어느 파일 · 단계인지 쓰게 한다. */
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
} // namespace

// 엔진 없이 실행기와 같은 읽기 · 단계 검사를 거친다(실행기의 `AutomationRunner::prepare` 앞부분). 이 실행 파일에 등록되지 않은 이름(App · 에디터 · 게임이
// 올리는 단계 · 감시 값, 엔진 서비스가 있어야 아는 전역 변수 · 개발 명령)은 실행 때 실행기가 본다 — 여기서는 읽은 단계의 속성 오류를 잡는다.
SW_TEST_CASE( ScenarioFilesTest, EngineScenariosParseAndPassStepValidation )
{
    const vector<string> listScenario = collectScenarioFiles( "engine/automation" );
    SW_REQUIRE_TRUE( listScenario.empty() == false );
    for ( const string& path : listScenario )
    {
        string text;
        SW_EXPECT_TRUE( FileUtil::readTextFile( path, text ) );
        AutomationScenario scenario;
        string             error;
        const bool         bParsed = scenario.parse( text, path, error );
        SW_EXPECT_EQUAL( string(), makeFailureText( bParsed, path, error ) );
        if ( bParsed == false )
            continue;
        SW_EXPECT_EQUAL( string(), makeFailureText( AutomationStepValidation::validateDesktopSteps( scenario, error ), path, error ) );
        for ( const AutomationStep& step : scenario.getSteps() )
        {
            string unresolvedName;
            if ( AutomationStepValidation::findUnresolvedDependency( step, unresolvedName ) != AutomationStepDependency::Resolved )
                continue;
            AutomationStepValidateFunction pfnValidate = nullptr;
            if ( AutomationStepValidation::findValidator( step._kind, pfnValidate ) != AutomationValidatorLookup::Found )
                continue;
            string       stepError;
            const bool   bValid = pfnValidate( step, stepError );
            const string prefix = path + " " + step.describe();
            SW_EXPECT_EQUAL( string(), makeFailureText( bValid, prefix, stepError ) );
        }
    }
}
