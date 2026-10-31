#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Automation/Runner/AutomationBackendMatrix.h"
#include "Engine/Automation/Runner/AutomationExitCode.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/HeadlessSubsystem.h"

// 자동화 런타임(`Automation/Runner`)을 짓는 구성의 시나리오 행렬 작업이다. 짓지 않는 구성(`SW_ENABLE_AUTOMATION=OFF` · 전용 서버)은
// `HeadlessScenarioMatrixOff.cpp` 를 CMake 가 고른다(`Source/Engine/CMakeLists.txt`) — 헤드리스 작업 표가 실행기를 직접 링크하면 그 구성이 링크되지 않았다.
namespace sw
{
    SW_LOG_CALLER( "Headless" );

    bool HeadlessSubsystem::runScenarioMatrixTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
    {
        // 창 · RHI 를 세우기 전에 가로챈다 — 이 프로세스는 기다리기만 한다. `-scenario-backends` 가 없으면 보통 시나리오 실행(이 프로세스가 직접 돈다)이다.
        string backends;
        if ( commandLine.getArgument( CommandLineArgument::SCENARIO_BACKENDS, backends ) == false || backends.empty() )
            return false;
        inoutState.enterHeadless();
        AutomationBackendMatrixOptions options;
        string                         error;
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO, options._scenarioPath );
        (void)commandLine.getArgument( CommandLineArgument::LOG_FILE, options._logFilePath );
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO_REPORT, options._reportPath );
        options._executablePath          = FileUtil::getExecutablePath();
        options._listPassThroughArgument = AutomationBackendMatrix::filterPassThroughArguments( commandLine.getOriginalArguments() );
        options._windowWidth             = engine::getSubsystem<EngineConfig>()._window._width;
        options._windowHeight            = engine::getSubsystem<EngineConfig>()._window._height;
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO_BACKEND_SPLIT, options._bSplitScenarios );
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO_BACKEND_SPLIT_OFFSET, options._splitOffset );
        if ( options._scenarioPath.empty() || AutomationBackendMatrix::parseBackendList( backends, options._listBackend, error ) == false )
        {
            SW_LOG_ERROR( "%#", options._scenarioPath.empty() ? "-scenario-backends needs -scenario=<path>" : error.c_str() );
            inoutState._bFailed         = true;
            inoutState._failureExitCode = getAutomationExitCode( AutomationOutcome::LoadError );
            return true;
        }
        const int32 exitCode        = AutomationBackendMatrix::run( options );
        inoutState._bFailed         = exitCode != 0;
        inoutState._failureExitCode = exitCode;
        return true;
    }
} // namespace sw
