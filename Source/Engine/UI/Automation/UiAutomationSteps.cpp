#include "pch.h"

#include "Engine/UI/Automation/UiAutomationSteps.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/UI/Core/UiFocusManager.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"

namespace sw
{
    namespace
    {
        struct UiAutomationStepsInternal
        {
            static constexpr utf8 kNone[] = "none";

            static string describeFocus( const UiSystem& ui )
            {
                const UiFocusManager& focus   = ui.getFocusManager();
                const WidgetTree*     pTree   = focus.getFocusedTree();
                const Widget*         pWidget = pTree != nullptr ? pTree->findWidgetById( focus.getFocusedWidget() ) : nullptr;
                if ( pWidget == nullptr )
                    return kNone;
                return pWidget->getName().empty() ? string( "(unnamed)" ) : string( pWidget->getName().c_str() );
            }

            static string describeActiveScreen( const UiSystem& ui )
            {
                const UiScreen* pScreen = ui.getActiveScreen();
                if ( pScreen == nullptr || pScreen->getDocumentPath().empty() )
                    return kNone;
                return pScreen->getDocumentPath();
            }

            static bool runExpectUi( AutomationRunner& runner, const AutomationStep& step )
            {
                const UiSystem& ui = engine::getUiSystem();
                string          failure;
                if ( UiAutomationSteps::isExpectUiMet( ui, step, failure ) == false )
                    runner.recordFailure( step, failure );
                return true;
            }

            static bool validateLayoutDump( const AutomationStep& step, string& outError )
            {
                if ( step._listAttribute.size() == 1 && step._listAttribute[0]._name == "file" && step._listAttribute[0]._value.empty() == false )
                    return true;
                outError = step.describe() + ": needs only file=\"…\"";
                return false;
            }

            /** @brief 상대 경로면 시나리오 산출물 폴더(`Saved/Automation/<이름>/`) 아래 — 스크린샷과 같은 자리입니다. */
            static bool runLayoutDump( AutomationRunner& runner, const AutomationStep& step )
            {
                const string& file = *step.findAttribute( "file" );
                const string  path = FileUtil::isAbsolutePath( file ) ? file : FileUtil::joinPath( runner.getOutputDirectory(), file );
                (void)FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( path ) );
                if ( FileUtil::writeTextFile( path, engine::getUiSystem().makeLayoutDump() ) == false )
                    runner.recordFailure( step, "could not write " + path );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_STEP( uiLayoutDump, UiAutomationSteps::kLayoutDumpKind, &UiAutomationStepsInternal::runLayoutDump, &UiAutomationStepsInternal::validateLayoutDump,
                        false );
    SW_AUTOMATION_STEP( uiExpect, UiAutomationSteps::kExpectUiKind, &UiAutomationStepsInternal::runExpectUi, &UiAutomationSteps::validateExpectUi, false );
} // namespace sw

namespace sw
{
    void UiAutomationSteps::ensureLinked()
    {
        // 등록자는 이 번역 단위의 정적 객체다 — 이 함수를 부르는 쪽이 있으면 링커가 단위를 버리지 않는다.
    }

    bool UiAutomationSteps::validateExpectUi( const AutomationStep& step, string& outError )
    {
        if ( step._listAttribute.empty() )
        {
            outError = step.describe() + ": needs focus, screen or screens";
            return false;
        }
        for ( const AutomationAttribute& attribute : step._listAttribute )
        {
            if ( attribute._name != "focus" && attribute._name != "screen" && attribute._name != "screens" )
            {
                outError = step.describe() + ": unknown attribute '" + attribute._name + "' (focus · screen · screens)";
                return false;
            }
            int32 count = 0;
            if ( attribute._name == "screens" && ( StringUtil::parseInt( string_view{ attribute._value }, count ) == false || count < 0 ) )
            {
                outError = step.describe() + ": screens must be a count, got '" + attribute._value + "'";
                return false;
            }
        }
        return true;
    }

    bool UiAutomationSteps::isExpectUiMet( const UiSystem& ui, const AutomationStep& step, string& outFailure )
    {
        using Internal = UiAutomationStepsInternal;
        bool bPassed   = true;
        if ( const string* pFocus = step.findAttribute( "focus" ); pFocus != nullptr )
        {
            const string focused = Internal::describeFocus( ui );
            if ( focused != *pFocus )
            {
                outFailure += "focus is '" + focused + "', expected '" + *pFocus + "'. ";
                bPassed = false;
            }
        }
        if ( const string* pScreen = step.findAttribute( "screen" ); pScreen != nullptr )
        {
            const string active   = Internal::describeActiveScreen( ui );
            const bool   bNone    = *pScreen == Internal::kNone;
            const bool   bMatches = bNone ? active == Internal::kNone : FileUtil::normalizePath( *pScreen ) == active;
            if ( bMatches == false )
            {
                outFailure += "active screen is '" + active + "', expected '" + *pScreen + "'. ";
                bPassed = false;
            }
        }
        if ( const string* pCount = step.findAttribute( "screens" ); pCount != nullptr )
        {
            int32 expected = 0;
            (void)StringUtil::parseInt( string_view{ *pCount }, expected ); // validateExpectUi 가 이미 봤다
            if ( ui.getScreenCount() != static_cast<uint32>( expected ) )
            {
                outFailure += "screen count is " + to_string( ui.getScreenCount() ) + ", expected " + *pCount + ". ";
                bPassed = false;
            }
        }
        return bPassed;
    }
} // namespace sw
