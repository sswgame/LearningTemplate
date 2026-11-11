/**
 * @file AutomationEnvironmentSteps.cpp
 * @brief 자동화 시나리오의 환경 단계를 구현합니다. 개발 명령 한 줄(`DevCommand`, 언리얼 자동화의 콘솔 명령 실행에 해당)과 창 크기 바꾸기(`ResizeWindow`) · 최소화와 되돌리기(`MinimizeWindow` · `RestoreWindow`)입니다.
 * @details 개발 명령은 게임, 키트, 에디터 모듈이 `SW_DEV_COMMAND` 로 등록한 것이다(에디터의 `play`, `stop`, `editor <커맨드 id>`, `scene.saveAs` 등).
 *          Shipping 에는 개발 명령이 없으므로 `DevCommand` 를 쓴 시나리오는 시작할 때 읽기 오류다.
 */
#include "pch.h"

#include "DevTools/Client/Automation/Scenario/AutomationEnvironmentSteps.h"

#include "Core/String/StringUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
    namespace
    {
        struct AutomationEnvironmentStepsInternal
        {
            static expected<void> validateDevCommand( const AutomationStep& step )
            {
                AutomationDevCommandParams params;
                const expected<void>       read = step.readProperties( params );
                if ( read.has_value() == false )
                    return read;
#if !defined( SW_SHIPPING )
                vector<string> listToken;
                listToken = StringUtil::splitQuotedTokens( params._line );
                if ( listToken.empty() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "line is empty" } );
                // 단계 종류 검사와 같은 때(시작 조건이 참인 프레임)라 게임과 에디터 모듈의 명령이 이미 등록되어 있다.
                if ( engine::getSubsystem<DevCommandRegistry>().findCommand( listToken[0] ) == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "unknown dev command", listToken[0] } );
                return {};
#else
                return unexpected( Error{ ErrorKind::Unsupported, "dev commands are compiled out of Shipping" } );
#endif
            }

            static bool runDevCommand( AutomationStepContext& runner, const AutomationStep& step )
            {
#if !defined( SW_SHIPPING )
                vector<string> listToken;
                listToken                              = StringUtil::splitQuotedTokens( step.readValidatedProperties<AutomationDevCommandParams>()._line );
                const DevCommandRegistration* pCommand = listToken.empty() ? nullptr : engine::getSubsystem<DevCommandRegistry>().findCommand( listToken[0] );
                if ( pCommand == nullptr )
                {
                    // 검사 뒤에 모듈이 언로드됐다(핫 리로드). 실패로 적고 이어 간다.
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
                SW_LOG_INFO_ID( "DevCommand.Reply", "%# -> %#", step.describe(), reply );
                return true;
#else
                (void)runner;
                (void)step;
                return false;
#endif
            }

            /** @brief 창을 최소화하거나(@p bMinimized) 되돌립니다. 최소화 중에도 시나리오 프레임은 돈다(자동화 실행의 페이싱이 최소화보다 앞선다 — `FramePacer`). */
            static bool setMinimized( AutomationStepContext& runner, const AutomationStep& step, bool bMinimized )
            {
                MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                if ( pWindow != nullptr && pWindow->canMinimize() == false )
                {
                    runner.endScenario( AutomationStepEnd::Skip, step.describe() + ": this desktop cannot minimize windows (no window manager)" );
                    return true;
                }
                if ( pWindow == nullptr || pWindow->setMinimized( bMinimized ) == false )
                    runner.recordFailure( step, "this platform could not minimize or restore the window" );
                return true;
            }
            static bool runMinimize( AutomationStepContext& runner, const AutomationStep& step ) { return setMinimized( runner, step, true ); }
            static bool runRestore( AutomationStepContext& runner, const AutomationStep& step ) { return setMinimized( runner, step, false ); }

            /** @brief 창을 창 모드의 그 클라이언트 크기로 바꿉니다. 창의 최소 크기보다 작으면 창이 최소 크기에서 멈춘다(그것을 보는 단언은 뒤 단계가 한다). */
            static bool runResize( AutomationStepContext& runner, const AutomationStep& step )
            {
                const AutomationResizeWindowParams params      = step.readValidatedProperties<AutomationResizeWindowParams>();
                MainWindowSubsystem*               pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                IWindow*                           pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                if ( pWindow == nullptr || pWindow->setDisplayMode( WindowDisplayMode::Windowed, params._width, params._height ) == false )
                {
                    runner.recordFailure( step, "this platform could not resize the window" );
                    return true;
                }
                // 창의 최소 크기보다 작게 요청했으면 창은 최소 크기에서 멈춘다 — 그 크기를 기다린다.
                pWindowHost->setPendingClientSize( std::max( params._width, pWindow->getMinimumClientWidth() ), std::max( params._height, pWindow->getMinimumClientHeight() ) );
                return true;
            }
        };
    } // namespace

    bool AutomationEnvironmentSteps::isWindowResizeApplied()
    {
        MainWindowSubsystem* const pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        return pWindowHost == nullptr || pWindowHost->consumePendingClientSize(); // 창 주인이 없으면 요청도 없다
    }

    SW_AUTOMATION_STEP( devCommand, AutomationEnvironmentSteps::kDevCommandKind, &AutomationEnvironmentStepsInternal::runDevCommand, &AutomationEnvironmentStepsInternal::validateDevCommand, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( resizeWindow, "ResizeWindow", &AutomationEnvironmentStepsInternal::runResize, &AutomationStep::validateProperties<AutomationResizeWindowParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( minimizeWindow, "MinimizeWindow", &AutomationEnvironmentStepsInternal::runMinimize, &AutomationStep::validateProperties<AutomationNoParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( restoreWindow, "RestoreWindow", &AutomationEnvironmentStepsInternal::runRestore, &AutomationStep::validateProperties<AutomationNoParams>, AutomationStepOptions{} );
} // namespace sw
