#include "pch.h"

#include "App/App.h"

int32 main( int32 argc, utf8* pArgv[] )
{
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
