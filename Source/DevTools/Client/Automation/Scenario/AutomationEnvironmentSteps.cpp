/**
 * @file AutomationEnvironmentSteps.cpp
 * @brief 자동화 시나리오의 환경 단계를 구현합니다. 개발 명령 한 줄(`DevCommand`, 언리얼 자동화의 콘솔 명령 실행에 해당)과 창 크기 바꾸기(`ResizeWindow`) · 최소화와 되돌리기(`MinimizeWindow` · `RestoreWindow`)입니다.
 * @details 개발 명령은 게임, 키트, 에디터 모듈이 `SW_DEV_COMMAND` 로 등록한 것이다(에디터의 `play`, `stop`, `editor <커맨드 id>`, `scene.saveAs` 등).
 *          Shipping 에는 개발 명령이 없으므로 `DevCommand` 를 쓴 시나리오는 시작할 때 읽기 오류다.
 */
#include "pch.h"

#include "DevTools/Client/Automation/Scenario/AutomationEnvironmentSteps.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Scenario/IAutomationStepContext.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Window/GenericWindow.h"
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

            static bool runDevCommand( IAutomationStepContext& runner, const AutomationStep& step )
            {
#if !defined( SW_SHIPPING )
                const expected<string> line = AutomationEnvironmentSteps::expandVariables( step.readValidatedProperties<AutomationDevCommandParams>()._line );
                if ( line.has_value() == false )
                {
                    runner.recordFailure( step, AutomationStep::describeReason( line.error() ) );
                    return true;
                }
                const vector<string>          listToken = StringUtil::splitQuotedTokens( *line );
                const DevCommandRegistration* pCommand  = listToken.empty() ? nullptr : engine::getSubsystem<DevCommandRegistry>().findCommand( listToken[0] );
                if ( pCommand == nullptr )
                {
                    // 검사 뒤에 모듈이 언로드됐다(핫 리로드). 실패로 적고 이어 간다.
                    runner.recordFailure( step, "the dev command is no longer registered" );
                    return true;
                }
                const vector<string> listArgument( listToken.begin() + 1, listToken.end() );
                string               reply;
                if ( DevCommandRegistry::run( *pCommand, listArgument, reply ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> text;
                    text.appendFormat( "dev command failed: %#", reply.empty() ? string_view{ pCommand->_pUsage } : string_view{ reply } );
                    runner.recordFailure( step, text.view() );
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
            static bool setMinimized( IAutomationStepContext& runner, const AutomationStep& step, bool bMinimized )
            {
                GenericWindow* pWindow = MainWindowSubsystem::findMainWindow();
                if ( pWindow != nullptr && pWindow->canMinimize() == false )
                {
                    StringBuilder<constant::buffer::kMax256> reason;
                    reason.appendFormat( "%#: this desktop cannot minimize windows (no window manager)", step.describe() );
                    runner.endScenario( AutomationStepEnd::Skip, reason.view() );
                    return true;
                }
                if ( pWindow == nullptr || pWindow->setMinimized( bMinimized ) == false )
                    runner.recordFailure( step, "this platform could not minimize or restore the window" );
                return true;
            }
            static bool runMinimize( IAutomationStepContext& runner, const AutomationStep& step ) { return setMinimized( runner, step, true ); }
            static bool runRestore( IAutomationStepContext& runner, const AutomationStep& step ) { return setMinimized( runner, step, false ); }

            /** @brief 창을 창 모드의 그 클라이언트 크기로 바꿉니다. 창의 최소 크기보다 작으면 창이 최소 크기에서 멈춘다(그것을 보는 단언은 뒤 단계가 한다). */
            static bool runResize( IAutomationStepContext& runner, const AutomationStep& step )
            {
                const AutomationResizeWindowParams params      = step.readValidatedProperties<AutomationResizeWindowParams>();
                MainWindowSubsystem*               pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                GenericWindow*                     pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
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

    expected<string> AutomationEnvironmentSteps::expandVariables( string_view line )
    {
        static constexpr string_view kOpen = "{gv_";
        string                       expanded;
        size_t                       position = 0;
        for ( size_t open = line.find( kOpen ); open != string_view::npos; open = line.find( kOpen, position ) )
        {
            const size_t close = line.find( '}', open );
            if ( close == string_view::npos )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the {gv_...} token is not closed", line } );
            const string_view         name  = line.substr( open + 1, close - open - 1 );
            const GlobalVariableInfo* pInfo = engine::getSubsystem<GlobalVariableManager>().findVariable( name );
            if ( pInfo == nullptr )
                return unexpected( Error{ ErrorKind::NotFound, "unknown global variable", name } );
            expanded.append( line.substr( position, open - position ) );
            expanded.append( pInfo->getValueAsString() );
            position = close + 1;
        }
        expanded.append( line.substr( position ) );
        return expanded;
    }

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
