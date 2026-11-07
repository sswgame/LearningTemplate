#include "pch.h"

#include "App/App.h"

#if defined( SW_PLATFORM_WINDOWS )
// 하이브리드 노트북의 드라이버가 이 실행 파일을 고성능(외장) GPU 로 돌린다 — NVIDIA Optimus · AMD 전환 그래픽 둘 다(제조사와 무관).
// GL 은 어댑터를 고르는 API 가 없어 이 값만 따른다. DX · Vulkan 은 따로 OS 선호 순서로 고른다(RHIDxgiAdapter.h, gv_rhiPreferMinimumPower). 언리얼 · 유니티 실행 파일도 내보낸다.
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

    // 루프를 누가 돌리는지는 플랫폼의 일이다. 데스크톱은 프로세스가 돌리고, 웹(`emscripten_set_main_loop`) · Android(`android_main` 의 ALooper)는 그쪽 진입점이 같은 세 함수를 부른다(언리얼 `FEngineLoop::Tick` 과 같다, 기억).
    if ( app.beginMainLoop() )
    {
#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX )
        while ( app.tickMainLoop() )
        {
        }
#else
    #error "Unsupported platform"
#endif
        app.endMainLoop();
    }
    // 종료 코드는 내리기 전에 읽는다(시나리오 결과 — `Engine::requestQuit`).
    const int32 exitCode = app.getExitCode();
    app.shutdown();
    return exitCode;
}
