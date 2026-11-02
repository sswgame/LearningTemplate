#include "pch.h"

#include "App/App.h"

#if defined( SW_PLATFORM_WINDOWS )
// 하이브리드 노트북의 드라이버가 이 실행 파일을 외장 GPU 로 돌린다 — GL 은 어댑터를 고르는 API 가 없고 DXGI 의 기본 순서도 이 값을 따른다(언리얼 · 유니티 실행 파일도 내보낸다).
extern "C"
{
    __declspec( dllexport ) unsigned long NvOptimusEnablement                  = 1;
    __declspec( dllexport ) int           AmdPowerXpressRequestHighPerformance = 1;
}
#endif

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
    // 종료 코드는 내리기 전에 읽는다(시나리오 결과 — `Engine::requestQuit`).
    const int32 exitCode = app.getExitCode();
    app.shutdown();
    return exitCode;
}
