#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Automation/Runner/AutomationExitCode.h"
#include "Engine/Automation/Runner/AutomationPerBackend.h"
#include "Engine/Automation/Runner/AutomationSession.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Engine.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/HeadlessSubsystem.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Renderer/RenderThread.h"

namespace sw
{
    SW_LOG_CALLER( "Automation" );

    // 실행기는 스스로 엔진 범위에 선다 — 이 폴더(`Automation/Runner`)를 짓지 않는 구성은 등록이 없어 호스트가 찾지 못한다(`IAutomationRuntime`).
    SW_MODULE_SUBSYSTEM( Engine, AutomationSession, IAutomationRuntime );

    bool AutomationSession::Runtime::start( Engine& engine, SubsystemScope& host, const CommandLineManager& commandLine )
    {
        string recordPath;
        string recordProbes;
        if ( commandLine.getArgument( CommandLineArgument::SCENARIO_RECORD, recordPath ) && recordPath.empty() == false )
        {
            (void)commandLine.getArgument( CommandLineArgument::SCENARIO_RECORD_PROBES, recordProbes ); // 없으면 검사점 없이 입력만 녹화한다
            (void)_owner.beginRecording( recordPath, recordProbes );                                    // 거절하면 이유를 로그로 남기고 녹화 없이 돈다
        }

        string scenarioPath;
        string reportPath;
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO_REPORT, reportPath ); // 없으면 보고 파일을 쓰지 않는다
        RenderThread* pRenderThread = engine.getRenderThread();
        const bool    bKeepGoing    = commandLine.isArgumentProvided( CommandLineArgument::SCENARIO_KEEP_GOING );
        const bool    bScenario     = commandLine.getArgument( CommandLineArgument::SCENARIO, scenarioPath ) && scenarioPath.empty() == false;
        const bool    bStarted      = bScenario && _owner.begin( scenarioPath, reportPath, bKeepGoing );
        if ( bStarted && pRenderThread != nullptr )
            pRenderThread->setScenarioCaptureEnabled( true );

        // 늘 붙인다 — 게임 · 콘솔의 재생(`ScenarioPlayback::play`)은 기동 뒤 아무 때나 시작한다. 쉬는 세션의 프레임 훅은 아무것도 하지 않는다.
        _frameSubscription = engine.getFrameHooks().add( _owner.makeFrameHooks() );
        // 재생기는 호스트 범위에 인터페이스로 건다 — `ScenarioPlayback` 이 범위에서 찾는다(정적 칸을 두지 않는다).
        const bool bPlayerBound = host.bind<IScenarioPlayer>( &_owner._scenarioPlayer );
        SW_ASSERT( bPlayerBound );
        (void)bPlayerBound;
        _pHost = &host;
        return true;
    }

    bool AutomationSession::Runtime::isBusy() const
    {
        // 재생(데모 · 벤치마크 경로)은 사람이 보는 화면이라 보통 프레임 속도로 돈다 — 무인 실행만 센다.
        return _owner.isBusy() && _owner.isPlaying() == false;
    }

    void AutomationSession::Runtime::detach()
    {
        if ( _pHost == nullptr )
            return;
        _pHost->unbind<IScenarioPlayer>(); // 재생기(세션)를 내리기 전에 푼다
        _pHost = nullptr;
        _owner._runTitle.reset();
        _frameSubscription.reset();
    }

    void AutomationSession::Runtime::runPerBackend( string_view backends, CommandLineManager& commandLine, HeadlessTaskState& inoutState )
    {
        // 창 · RHI 를 세우기 전에 가로챈다 — 이 프로세스는 기다리기만 한다.
        inoutState.enterHeadless();
        AutomationPerBackendOptions options;
        string                      error;
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO, options._scenarioPath );
        (void)commandLine.getArgument( CommandLineArgument::LOG_FILE, options._logFilePath );
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO_REPORT, options._reportPath );
        options._executablePath          = FileUtil::getExecutablePath();
        options._listPassThroughArgument = AutomationPerBackend::filterPassThroughArguments( commandLine.getOriginalArguments() );
        options._windowWidth             = engine::getSubsystem<EngineConfig>()._window._width;
        options._windowHeight            = engine::getSubsystem<EngineConfig>()._window._height;
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO_BACKEND_SPLIT, options._bSplitScenarios );
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO_BACKEND_SPLIT_OFFSET, options._splitOffset );
        if ( options._scenarioPath.empty() || AutomationPerBackend::parseBackendList( backends, options._listBackend, error ) == false )
        {
            SW_LOG_ERROR( "%#", options._scenarioPath.empty() ? "-scenario-backends needs -scenario=<path>" : error.c_str() );
            inoutState._bFailed         = true;
            inoutState._failureExitCode = getAutomationExitCode( AutomationOutcome::LoadError );
            return;
        }
        const int32 exitCode        = AutomationPerBackend::run( options );
        inoutState._bFailed         = exitCode != 0;
        inoutState._failureExitCode = exitCode;
    }
} // namespace sw
