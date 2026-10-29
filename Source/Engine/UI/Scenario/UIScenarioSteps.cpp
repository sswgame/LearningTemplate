/**
 * @file UIScenarioSteps.cpp
 * @brief 자동화 시나리오의 UI 단계를 정의합니다. `ExpectUI`(포커스 위젯, 활성 화면 문서, 화면 수)를 단언하고 `UILayoutDump` 로 위젯 사각형을 파일에 씁니다.
 * @details 단계는 시나리오 실행기(`AutomationRunner`)가 레지스트리에서 찾는다(언리얼 Automation Driver 가 위젯을 찾아 단언하는 것에 해당한다).
 *          단계는 등록(`AutomationStepRegistry`)으로만 닿고 이름으로 부르는 곳이 없어 헤더를 두지 않는다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/UIFocusManager.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

namespace sw
{
    struct AutomationStep;

    class UISystem;

    /**
     * @struct UIScenarioSteps
     * @brief `<ExpectUI focus="이름|none" screen="문서 경로|none" screens="수"/>` 입니다. 적은 속성만 확인하며 하나 이상 적어야 합니다.
     * @details `focus` 는 포커스 위젯의 이름(조각 안이면 `조각.이름`)이고 `none` 은 포커스 없음이다. `screen` 은 활성 화면(입력과 포커스를 받는 맨 위 화면)의 문서 경로
     *          (`normalizePath` 를 쓰며 대소문자 무시)이고, 코드 화면이거나 없으면 `none` 이다. `screens` 는 스택의 화면 수(닫는 중인 화면 포함)다.
     */
    struct UIScenarioSteps
    {
        static constexpr utf8 kExpectUIKind[]   = "ExpectUI";
        static constexpr utf8 kLayoutDumpKind[] = "UILayoutDump"; ///< `<UILayoutDump file="x.txt"/>` 입니다. `UISystem::makeLayoutDump` 를 산출물 폴더에 씁니다.

        /** @brief 속성을 검사합니다. 모르는 속성, 빈 단계, 수가 아닌 `screens` 면 false 와 이유를 반환합니다. */
        [[nodiscard]] static bool validateExpectUI( const AutomationStep& step, string& outError );
        /** @brief @p ui 가 단계의 기대와 같으면 true 입니다. 다르면 false 를 반환하고 @p outFailure 에 기대와 실제를 둡니다. */
        [[nodiscard]] static bool isExpectUIMet( const UISystem& ui, const AutomationStep& step, string& outFailure );
    };
} // namespace sw

namespace sw
{
    namespace
    {
        struct UIScenarioStepsInternal
        {
            static constexpr utf8 kNone[] = "none";

            static string describeFocus( const UISystem& ui )
            {
                const UIFocusManager& focus   = ui.getInputRouter().getFocusManager();
                const WidgetTree*     pTree   = focus.getFocusedTree();
                const Widget*         pWidget = pTree != nullptr ? pTree->findWidgetByID( focus.getFocusedWidget() ) : nullptr;
                if ( pWidget == nullptr )
                    return kNone;
                return pWidget->getName().empty() ? string( "(unnamed)" ) : string( pWidget->getName().c_str() );
            }

            static string describeActiveScreen( const UISystem& ui )
            {
                const UIScreen* pScreen = ui.getActiveScreen();
                if ( pScreen == nullptr || pScreen->getDocumentPath().empty() )
                    return kNone;
                return pScreen->getDocumentPath();
            }

            static bool runExpectUI( AutomationStepContext& runner, const AutomationStep& step )
            {
                const UISystem& ui = engine::getSubsystem<UISystem>();
                string          failure;
                if ( UIScenarioSteps::isExpectUIMet( ui, step, failure ) == false )
                    runner.recordFailure( step, failure );
                return true;
            }

            static bool validateLayoutDump( const AutomationStep& step, string& outError )
            {
                const bool bValidFileOnly = step._listAttribute.size() == 1 && step._listAttribute[0]._name == "file" && step._listAttribute[0]._value.empty() == false;
                if ( bValidFileOnly )
                    return true;
                outError = step.describe() + ": needs only file=\"…\"";
                return false;
            }

            /** @brief 상대 경로면 시나리오 산출물 폴더(`Saved/Automation/<이름>/`) 아래에 둡니다. 스크린샷과 같은 위치입니다. */
            static bool runLayoutDump( AutomationStepContext& runner, const AutomationStep& step )
            {
                const string& file = *step.findAttribute( "file" );
                const string  path = FileUtil::isAbsolutePath( file ) ? file : FileUtil::joinPath( runner.getOutputDirectory(), file );
                if ( FileUtil::ensureParentDirectoryExists( path ) == false || FileUtil::writeTextFile( path, engine::getSubsystem<UISystem>().makeLayoutDump() ) == false )
                    runner.recordFailure( step, "could not write " + path );
                return true;
            }

            /**
             * @brief 로딩 화면이 떠 있으면 게임 입력이 막혀 있고 그림이 덮여 있습니다. `ScenePlaying` 시나리오는 로딩 화면이 사라질 때까지 시작하지 않고, 화면 캡처도 미뤄집니다.
             * @details 화면 UI 를 막은 동안(멈춘 에디터, `isOnScreenSuppressed`)은 로딩 화면이 그려지지도 입력을 막지도 않는다 — 그동안 닫을 게임 update 가 돌지 않아 남아 있을 뿐이다.
             */
            static bool isLoadingScreenBlocking()
            {
                const UISystem* pUISystem = engine::findSubsystem<UISystem>();
                return pUISystem != nullptr && pUISystem->isInitialized() && pUISystem->isOnScreenSuppressed() == false && pUISystem->isLoadingScreenShown();
            }
        };
    } // namespace

    SW_AUTOMATION_START_BLOCKER( uiLoadingScreen, "UILoadingScreen", &UIScenarioStepsInternal::isLoadingScreenBlocking );
    SW_AUTOMATION_STEP( uiLayoutDump, UIScenarioSteps::kLayoutDumpKind, &UIScenarioStepsInternal::runLayoutDump, &UIScenarioStepsInternal::validateLayoutDump,
                        false );
    SW_AUTOMATION_STEP( uiExpect, UIScenarioSteps::kExpectUIKind, &UIScenarioStepsInternal::runExpectUI, &UIScenarioSteps::validateExpectUI, false );
} // namespace sw

namespace sw
{
    bool UIScenarioSteps::validateExpectUI( const AutomationStep& step, string& outError )
    {
        if ( step._listAttribute.empty() )
        {
            outError = step.describe() + ": needs focus, screen or screens";
            return false;
        }
        for ( const AutomationAttribute& attribute : step._listAttribute )
        {
            const bool bUnknownAttribute = attribute._name != "focus" && attribute._name != "screen" && attribute._name != "screens";
            if ( bUnknownAttribute )
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

    bool UIScenarioSteps::isExpectUIMet( const UISystem& ui, const AutomationStep& step, string& outFailure )
    {
        using Internal = UIScenarioStepsInternal;
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
            if ( StringUtil::parseInt( string_view{ *pCount }, expected ) == false )
            {
                outFailure += "screens '" + *pCount + "' is not a number. "; // 검사 뒤 단계가 바뀌었다(핫 리로드)
                bPassed = false;
            }
            else if ( ui.getScreenCount() != static_cast<uint32>( expected ) )
            {
                outFailure += "screen count is " + to_string( ui.getScreenCount() ) + ", expected " + *pCount + ". ";
                bPassed = false;
            }
        }
        return bPassed;
    }
} // namespace sw
