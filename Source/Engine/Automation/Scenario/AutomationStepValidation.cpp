#include "pch.h"

#include "Engine/Automation/Scenario/AutomationStepValidation.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Automation/Scenario/AutomationEnvironmentSteps.h"
#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineServices.h"

#include <algorithm>

namespace sw
{
    AutomationValidatorLookup AutomationStepValidation::findValidator( string_view kind, AutomationStepValidateFunction& outValidate )
    {
        const AutomationStepRegistration* pStep = AutomationStepRegistry::find( kind );
        if ( pStep != nullptr )
        {
            outValidate = pStep->_pValidate;
            return outValidate != nullptr ? AutomationValidatorLookup::Found : AutomationValidatorLookup::NoValidator;
        }
        const AutomationStepValidatorRegistration* pValidator = AutomationStepRegistry::findValidator( kind );
        if ( pValidator == nullptr )
            return AutomationValidatorLookup::Unknown;
        outValidate = pValidator->_pValidate;
        return AutomationValidatorLookup::Found;
    }

    AutomationStepDependency AutomationStepValidation::findUnresolvedDependency( const AutomationStep& step, string& outName )
    {
        outName                                  = step._kind;
        AutomationStepValidateFunction pValidate = nullptr;
        if ( findValidator( step._kind, pValidate ) == AutomationValidatorLookup::Unknown )
            return AutomationStepDependency::StepKind;

        const string* pWatch = step.findAttribute( "watch" );
        if ( pWatch != nullptr && AutomationWatchRegistry::find( *pWatch ) == nullptr )
        {
            outName = *pWatch;
            return AutomationStepDependency::Watch;
        }

        // `Variable` · `ExpectVariable` 의 검사 함수가 `name` 을 전역 변수 관리자에 묻는다. 엔진 서비스가 묶이지 않았으면 묻지 못하므로 모르는 것으로 센다.
        const bool    bAsksVariable = step._kind == "Variable" || step._kind == "ExpectVariable";
        const string* pVariable     = bAsksVariable ? step.findAttribute( "name" ) : nullptr;
        const bool    bUnknownVariable =
            pVariable != nullptr && ( engine::areEngineServicesBound() == false || engine::getSubsystem<GlobalVariableManager>().findVariable( *pVariable ) == nullptr );
        if ( bUnknownVariable )
        {
            outName = *pVariable;
            return AutomationStepDependency::GlobalVariable;
        }

        // 입력 단계(`Tap` · `Press` · `Release`)의 검사 함수가 `slot` 의 키 · 버튼 이름을 리플렉션 enum 표(`KeyCodeUtil::fromName` — 엔진 서비스의 TypeRegistry)에 묻는다.
        const string* pSlot = step.findAttribute( "slot" );
        if ( pSlot != nullptr && engine::areEngineServicesBound() == false )
        {
            outName = *pSlot;
            return AutomationStepDependency::InputSlot;
        }

        if ( step._kind == AutomationEnvironmentSteps::kDevCommandKind )
        {
            const string* pLine = step.findAttribute( "line" );
#if SW_DEV_COMMANDS_ENABLED
            vector<string> listToken;
            if ( pLine != nullptr )
                listToken = StringUtil::splitQuotedTokens( *pLine );
            // line 이 없거나 비면 이름을 묻기 전에 검사 함수가 속성 오류로 거절한다 — 그 거절을 보려고 여기서는 모르는 것으로 세지 않는다.
            if ( listToken.empty() == false && DevCommandRegistry::get().findCommand( listToken[0] ) == nullptr )
            {
                outName = listToken[0];
                return AutomationStepDependency::DevCommand;
            }
#else
            (void)pLine;
            outName = "DevCommand";
            return AutomationStepDependency::DevCommand;
#endif
        }
        outName = step._kind;
        return AutomationStepDependency::Resolved;
    }

    bool AutomationStepValidation::validateDesktopSteps( const AutomationScenario& scenario, string& outError )
    {
        if ( scenario.getInputMode() == VirtualInputMode::Mixed )
            return true;
        if ( scenario.getTestOptions()._bRequiresForeground )
        {
            outError = "requireForeground=\"true\" takes the OS foreground (desktop-global state), so the scenario must declare input=\"mixed\" and run alone in the Desktop suite";
            return false;
        }
        for ( const AutomationStep& step : scenario.getSteps() )
        {
            const AutomationStepRegistration* pRegistration = AutomationStepRegistry::find( step._kind );
            if ( pRegistration == nullptr || pRegistration->_bUsesDesktop == false )
                continue;
            outError = step.describe() + ": this step reads or writes desktop-global state (OS cursor clip / foreground / screen), so the scenario must declare input=\"mixed\" " +
                       "and run alone in the Desktop suite";
            return false;
        }
        return true;
    }

    bool AutomationStepValidation::validateScreenshotFiles( const AutomationScenario& scenario, string& outError )
    {
        vector<string_view> listFile;
        for ( const AutomationStep& step : scenario.getSteps() )
        {
            const string* pFile = step._kind == "Screenshot" ? step.findAttribute( "file" ) : nullptr;
            if ( pFile == nullptr )
                continue;
            if ( std::find( listFile.begin(), listFile.end(), string_view{ *pFile } ) != listFile.end() )
            {
                outError = step.describe() + ": '" + *pFile + "' was already taken by an earlier <Screenshot> - <ExpectImage> is judged at the end, so give each shot its own file";
                return false;
            }
            listFile.push_back( *pFile );
        }
        return true;
    }

    const utf8* AutomationStepValidation::getDependencyLabel( AutomationStepDependency dependency )
    {
        switch ( dependency )
        {
            case AutomationStepDependency::Resolved:
                return "resolved";
            case AutomationStepDependency::StepKind:
                return "step kind not registered in this process";
            case AutomationStepDependency::Watch:
                return "watch not registered in this process";
            case AutomationStepDependency::GlobalVariable:
                return "global variable not registered in this process";
            case AutomationStepDependency::DevCommand:
                return "dev command not registered in this process";
            case AutomationStepDependency::InputSlot:
                return "input slot names need the engine services (TypeRegistry)";
        }
        return "unknown";
    }

    AutomationStepValidatorRegistrar::AutomationStepValidatorRegistrar( const AutomationStepValidatorRegistration* pRegistration )
        : _pRegistration{ AutomationStepRegistry::registerValidator( pRegistration ) ? pRegistration : nullptr }
    {
    }

    AutomationStepValidatorRegistrar::~AutomationStepValidatorRegistrar()
    {
        if ( _pRegistration != nullptr )
            AutomationStepRegistry::unregisterValidator( _pRegistration );
    }
} // namespace sw
