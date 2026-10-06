/**
 * @file AutomationEnvironmentSteps.cpp
 * @brief 자동화 시나리오의 환경 단계 — 개발 명령 한 줄(`DevCommand`, 언리얼 자동화의 콘솔 명령 실행과 같은 자리)과 창 크기 바꾸기(`ResizeWindow`).
 * @details 개발 명령은 게임 · 키트 · 에디터 모듈이 `SW_DEV_COMMAND` 로 등록한 것이다(에디터의 `play` · `stop` · `editor <커맨드 id>` · `scene.saveAs` …).
 *          Shipping 에는 개발 명령이 없으므로 `DevCommand` 를 쓴 시나리오는 시작할 때 읽기 오류다.
 */
#include "pch.h"

#include "Engine/Automation/AutomationEnvironmentSteps.h"

#include "Core/String/StringUtil.h"

#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Utility/Console/DevCommandRegistry.h"
#include "Engine/Utility/Console/DevConsole.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    SW_LOG_CALLER( "AutomationEnvironmentSteps" );

    namespace
    {
        struct AutomationEnvironmentStepsInternal
        {
            /** @brief 속성이 @p listAllowed 안에만 있고 @p listRequired 가 모두 있는지 봅니다. */
            static bool checkAttributes( const AutomationStep& step, std::initializer_list<string_view> listAllowed,
                                         std::initializer_list<string_view> listRequired, string& outError )
            {
                for ( const AutomationAttribute& attribute : step._listAttribute )
                {
                    bool bKnown = false;
                    for ( const string_view allowed : listAllowed )
                    {
                        bKnown = bKnown || string_view{ attribute._name } == allowed;
                    }
                    if ( bKnown == false )
                    {
                        outError = step.describe() + ": unknown attribute '" + attribute._name + "'";
                        return false;
                    }
                }
                for ( const string_view required : listRequired )
                {
                    if ( step.findAttribute( required ) == nullptr )
                    {
                        outError = step.describe() + ": needs " + string( required ) + "=\"…\"";
                        return false;
                    }
                }
                return true;
            }

            static bool validateDevCommand( const AutomationStep& step, string& outError )
            {
                if ( checkAttributes( step, { "line" }, { "line" }, outError ) == false )
                    return false;
#if SW_DEV_COMMANDS_ENABLED
                vector<string> listToken;
                DevConsole::tokenize( *step.findAttribute( "line" ), listToken );
                if ( listToken.empty() )
                {
                    outError = step.describe() + ": line is empty";
                    return false;
                }
                // 단계 종류 검사와 같은 때(시작 조건이 참인 프레임)라 게임 · 에디터 모듈의 명령이 이미 올라와 있다.
                if ( DevCommandRegistry::get().findCommand( listToken[0] ) == nullptr )
                {
                    outError = step.describe() + ": unknown dev command '" + listToken[0] + "'";
                    return false;
                }
                return true;
#else
                outError = step.describe() + ": dev commands are compiled out of Shipping";
                return false;
#endif
            }

            static bool runDevCommand( AutomationRunner& runner, const AutomationStep& step )
            {
#if SW_DEV_COMMANDS_ENABLED
                vector<string> listToken;
                DevConsole::tokenize( *step.findAttribute( "line" ), listToken );
                const DevCommandRegistration* pCommand = listToken.empty() ? nullptr : DevCommandRegistry::get().findCommand( listToken[0] );
                if ( pCommand == nullptr )
                {
                    // 검사 뒤에 모듈이 내려갔다(핫 리로드) — 실패로 적고 잇는다.
                    runner.recordFailure( step, "the dev command is no longer registered" );
                    return true;
                }
                const vector<string> listArgument( listToken.begin() + 1, listToken.end() );
                string               reply;
                if ( pCommand->_pFunc( listArgument, reply ) == false )
                {
                    runner.recordFailure( step, "dev command failed: " + ( reply.empty() ? string( pCommand->_pUsage ) : reply ) );
                    return true;
                }
                SW_LOG_INFO( "%# -> %#", step.describe(), reply );
                return true;
#else
                (void)runner;
                (void)step;
                return false;
#endif
            }

            /** @brief 창 크기 속성(픽셀, 1..16384)을 읽습니다. */
            [[nodiscard]] static bool parseSize( const AutomationStep& step, string_view name, uint32& outValue )
            {
                constexpr int32 kMaxWindowSide = 16384;
                const string*   pText          = step.findAttribute( name );
                int32           value          = 0;
                if ( pText == nullptr || StringUtil::parseInt( string_view{ *pText }, value ) == false || value < 1 || value > kMaxWindowSide )
                    return false;
                outValue = static_cast<uint32>( value );
                return true;
            }

            static bool validateResize( const AutomationStep& step, string& outError )
            {
                if ( checkAttributes( step, { "width", "height" }, { "width", "height" }, outError ) == false )
                    return false;
                uint32 width  = 0;
                uint32 height = 0;
                if ( parseSize( step, "width", width ) == false || parseSize( step, "height", height ) == false )
                {
                    outError = step.describe() + ": width and height must be pixels in 1..16384";
                    return false;
                }
                return true;
            }

            /** @brief 창을 창 모드의 그 클라이언트 크기로 바꿉니다. 창의 최소 크기보다 작으면 창이 최소 크기에서 멈춘다(그것을 보는 단언은 뒤 단계가 한다). */
            static bool runResize( AutomationRunner& runner, const AutomationStep& step )
            {
                uint32 width  = 0;
                uint32 height = 0;
                (void)parseSize( step, "width", width ); // 검사에서 봤다
                (void)parseSize( step, "height", height );
                IWindow* pWindow = IWindow::getActiveWindow();
                if ( pWindow == nullptr || pWindow->setDisplayMode( WindowDisplayMode::Windowed, width, height ) == false )
                    runner.recordFailure( step, "this platform could not resize the window" );
                return true;
            }
        };
    } // namespace

    void AutomationEnvironmentSteps::ensureLinked() {}

    SW_AUTOMATION_STEP( devCommand, "DevCommand", &AutomationEnvironmentStepsInternal::runDevCommand, &AutomationEnvironmentStepsInternal::validateDevCommand,
                        false );
    SW_AUTOMATION_STEP( resizeWindow, "ResizeWindow", &AutomationEnvironmentStepsInternal::runResize, &AutomationEnvironmentStepsInternal::validateResize, false );
} // namespace sw
