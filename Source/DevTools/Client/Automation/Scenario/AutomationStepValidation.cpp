#include "pch.h"

#include "DevTools/Client/Automation/Scenario/AutomationStepValidation.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringUtil.h"

#include "DevTools/Client/Automation/Scenario/AutomationEnvironmentSteps.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"

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
        const bool    bNeedsVariable = step._kind == "Variable" || step._kind == "ExpectVariable";
        const string* pVariable      = bNeedsVariable ? step.findAttribute( "name" ) : nullptr;
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
#if !defined( SW_SHIPPING )
            vector<string> listToken;
            if ( pLine != nullptr )
                listToken = StringUtil::splitQuotedTokens( *pLine );
            // line 이 없거나 비면 이름을 묻기 전에 검사 함수가 속성 오류로 거절한다 — 그 거절을 보려고 여기서는 모르는 것으로 세지 않는다.
            if ( listToken.empty() == false && engine::getSubsystem<DevCommandRegistry>().findCommand( listToken[0] ) == nullptr )
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

    expected<void> AutomationStepValidation::validateDesktopSteps( const AutomationScenario& scenario )
    {
        if ( scenario.getInputMode() == VirtualInputMode::Mixed )
            return {};
        if ( scenario.getTestOptions()._bRequiresForeground )
        {
            return unexpected( Error{ ErrorKind::InvalidArgument,
                                      "requireForeground=\"true\" takes the OS foreground (desktop-global state), so the scenario must declare input=\"mixed\" and run alone in the Desktop suite" } );
        }
        for ( const AutomationStep& step : scenario.getSteps() )
        {
            const AutomationStepRegistration* pRegistration = AutomationStepRegistry::find( step._kind );
            if ( pRegistration == nullptr || pRegistration->_bUsesDesktop == false )
                continue;
            return unexpected( Error{ ErrorKind::InvalidArgument,
                                      "this step reads or writes desktop-global state (OS cursor clip / foreground / screen), so the scenario must declare input=\"mixed\" and run alone in the Desktop suite",
                                      step.describe() } );
        }
        return {};
    }

    expected<void> AutomationStepValidation::validateScreenshotFiles( const AutomationScenario& scenario )
    {
        vector<string_view> listFile;
        for ( const AutomationStep& step : scenario.getSteps() )
        {
            const string* pFile = step._kind == "Screenshot" ? step.findAttribute( "file" ) : nullptr;
            if ( pFile == nullptr )
                continue;
            if ( std::find( listFile.begin(), listFile.end(), string_view{ *pFile } ) != listFile.end() )
            {
                return unexpected( Error{ ErrorKind::AlreadyExists, "the file was already taken by an earlier <Screenshot> - <ExpectImage> is judged at the end, so give each shot its own file",
                                          step.describe() }
                                       .appendDetail( " " )
                                       .appendDetail( *pFile ) );
            }
            listFile.push_back( *pFile );
        }
        return {};
    }

    expected<void> AutomationStepValidation::validateStepPhaseOrder( const AutomationScenario& scenario )
    {
        const AutomationStep* pFrameEndStep = nullptr; // 지금 프레임에 앞서 적힌 틱 뒤 단계
        for ( const AutomationStep& step : scenario.getSteps() )
        {
            if ( pFrameEndStep != nullptr && pFrameEndStep->_frameIndex != step._frameIndex )
                pFrameEndStep = nullptr;
            const AutomationStepRegistration* pRegistration = AutomationStepRegistry::find( step._kind );
            // 모르는 단계 · 실행 없이 기다리기만 하는 단계(`WaitUntil` · `EditorWaitStable` — 제 프레임을 혼자 갖는다)는 건너뛴다.
            if ( pRegistration == nullptr || pRegistration->isWaitOnly() )
                continue;
            const bool bRunsAtFrameStart = pRegistration->_pRun == nullptr || pRegistration->_bBeforeInput;
            if ( bRunsAtFrameStart == false )
            {
                if ( pFrameEndStep == nullptr )
                    pFrameEndStep = &step;
                continue;
            }
            if ( pFrameEndStep != nullptr )
            {
                return unexpected( Error{ ErrorKind::InvalidArgument, "a frame-start step runs before the judging step written above it - put <Wait frames=\"1\"/> between them",
                                          step.describe() }
                                       .appendDetail( " before " )
                                       .appendDetail( pFrameEndStep->describe() ) );
            }
        }
        return {};
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
} // namespace sw
