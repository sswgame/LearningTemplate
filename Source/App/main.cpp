#include "pch.h"

#include "App/App.h"

#include "Engine/Telemetry/CrashReportService.h"

int32 main( int32 argc, utf8* pArgv[] )
{
    // 크래시 보고 프로세스(`--crash-reporter=<폴더>`) — 앞 실행이 띄운다. 엔진 · 게임 모듈을 세우지 않고 묶음만 올리고 끝낸다.
    const int32 reporterExitCode = sw::CrashReportService::runReporterFromCommandLine( argc, pArgv );
    if ( reporterExitCode >= 0 )
        return reporterExitCode;

    sw::App app{};
    if ( app.initialize( argc, pArgv ) == false )
    {
        // 종료 코드는 내리기 전에 고른다. 일부만 초기화된 서브시스템도 정해진 순서로 내려야 렌더 스레드 비우기 · 누수 리포트를 건너뛰지 않는다.
        const int32 exitCode = app.getInitFailureExitCode();
        app.shutdown();
        return exitCode;
    }

    app.run();
    // 종료 코드는 내리기 전에 읽는다(시나리오 결과 — `EngineLoop::requestQuit`).
    const int32 exitCode = app.getExitCode();
    app.shutdown();
    return exitCode;
}
