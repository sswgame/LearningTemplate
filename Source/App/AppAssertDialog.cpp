#include "pch.h"

#include "App/AppAssertDialog.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Common/EngineServices.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 의 대화상자 구현과 탐침입니다. */
        struct AppAssertDialogInternal
        {
#if defined( SW_DEBUG ) && defined( SW_PLATFORM_WINDOWS )
            /** @brief 이 전역 변수가 0 이 아니거나 비어 있지 않으면 자동 실행입니다(프로파일, 에디터 자체 테스트). 이름으로 읽으며, App 은 모듈을 모릅니다. */
            static constexpr const utf8* kArrAutomationVariable[] = { "gv_profileFrames", "gv_profileSeconds", "gv_editorSelfTest" };

            /** @brief 사람이 지켜보지 않는 실행이면 true 를 반환합니다(-unattended, 시나리오, 프로파일, 자체 테스트). */
            static bool isAutomatedRun( const CommandLineManager& commandLine )
            {
                if ( commandLine.isArgumentProvided( CommandLineArgument::UNATTENDED ) || commandLine.isArgumentProvided( CommandLineArgument::SCENARIO ) )
                    return true;
                for ( const utf8* pName : kArrAutomationVariable )
                {
                    const GlobalVariableInfo* pVariable = engine::getSubsystem<GlobalVariableManager>().findVariable( pName );
                    if ( pVariable == nullptr )
                        continue;
                    const bool bSet = pVariable->_type == GlobalVariableType::String ? pVariable->getValueAsString().empty() == false
                                                                                     : pVariable->getValueAsFloat() != 0.0f;
                    if ( bSet )
                        return true;
                }
                return false;
            }

            /** @brief 단언을 사용자에게 묻습니다. 디버거가 붙어 있으면 묻지 않고 멈춥니다. 개발자가 그 위치를 직접 보려는 것이기 때문입니다. */
            static internal::AssertAction showAssertDialog( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line )
            {
                if ( IsDebuggerPresent() != FALSE )
                    return internal::AssertAction::Break;
                StringBuilder<constant::kMaxBuffer8192> text;
                text.appendFormat( "Assertion failed: %s\n%s\n\n%s(%d)\n\n"
                                   "Continue  - ignore this time\nTry Again - break into the debugger (or write a crash report)\nCancel    - ignore this assert for the rest of the run",
                                   pExpression, pMessage != nullptr ? pMessage : "", pFile, line );
                const wstring wideText = StringUtil::utf8ToUtf16( text.c_str() );
                const int32   result   = MessageBoxW( nullptr, wideText.c_str(), L"SW Engine - Assertion", MB_CANCELTRYCONTINUE | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND );
                switch ( result )
                {
                    case IDCONTINUE:
                        return internal::AssertAction::IgnoreOnce;
                    case IDCANCEL:
                        return internal::AssertAction::IgnoreAlways;
                    default:
                        return internal::AssertAction::Break;
                }
            }
#endif

            /** @brief 탐침 `App.AssertDialogInstalled` 입니다. 대화상자가 설치돼 있으면 1 입니다. */
            [[nodiscard]] static bool readAssertDialogInstalled( const GameObjectManager* /*pManager*/, float64& outValue )
            {
#if defined( SW_DEBUG )
                outValue = internal::hasAssertDialog() ? 1.0 : 0.0;
#else
                outValue = 0.0;
#endif
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_PROBE( appAssertDialogInstalled, "App.AssertDialogInstalled", "1 when App installed the interactive assert dialog (never in automated runs)",
                         &AppAssertDialogInternal::readAssertDialogInstalled );
} // namespace sw

namespace sw
{
    void AppAssertDialog::installForInteractiveRun( [[maybe_unused]] const CommandLineManager& commandLine )
    {
#if defined( SW_DEBUG ) && defined( SW_PLATFORM_WINDOWS )
        if ( AppAssertDialogInternal::isAutomatedRun( commandLine ) == false )
            internal::setAssertDialog( &AppAssertDialogInternal::showAssertDialog );
#endif
    }
} // namespace sw
