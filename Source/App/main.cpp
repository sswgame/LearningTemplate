#include "pch.h"

#include "App/App.h"
#include "App/AppEntry.h"

#if defined( SW_PLATFORM_WEB )
    #include <emscripten/emscripten.h>
#endif

#if defined( SW_PLATFORM_WINDOWS )
// 하이브리드 노트북의 드라이버가 이 실행 파일을 고성능(외장) GPU 로 돌린다 — NVIDIA Optimus · AMD 전환 그래픽 둘 다(제조사와 무관).
// GL 은 어댑터를 고르는 API 가 없어 이 값만 따른다. DX · Vulkan 은 따로 OS 선호 순서로 고른다(RHIDxgiAdapter.h, gv_rhiPreferMinimumPower). 언리얼 · 유니티 실행 파일도 내보낸다.
extern "C"
{
    __declspec( dllexport ) unsigned long NvOptimusEnablement                  = 1;
    __declspec( dllexport ) int           AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace sw
{
    namespace
    {
        struct AppEntryInternal
        {
            /** @brief 루프를 끝낸 앱을 내리고 종료 코드를 돌려줍니다. 종료 코드는 내리기 전에 읽는다(시나리오 결과 — `Engine::requestQuit`). */
            static int32 finish( App& app )
            {
                app.endMainLoop();
                const int32 exitCode = app.getExitCode();
                app.shutdown();
                return exitCode;
            }

#if defined( SW_PLATFORM_WEB )
            /** @brief 브라우저 프레임 콜백입니다(requestAnimationFrame). 루프가 끝나면 콜백을 풀고 앱을 내린 뒤 프로세스 종료 코드를 낸다. */
            static void tickWebFrame( void* pUserData )
            {
                (void)pUserData;
                if ( getWebApp()->tickMainLoop() )
                    return;
                emscripten_cancel_main_loop();
                const int32 exitCode = finish( *getWebApp() );
                getWebApp().reset();
                emscripten_force_exit( exitCode );
            }

            /** @brief 브라우저 루프가 도는 동안의 앱입니다 — `main` 이 돌아온 뒤에도 살아야 해서 정적 자리에 둔다. */
            static unique_ptr<App>& getWebApp()
            {
                static unique_ptr<App> s_app;
                return s_app;
            }
#endif
        };
    } // namespace

    int32 AppEntry::run( int32 argc, utf8* pArgv[] )
    {
#if defined( SW_PLATFORM_WEB )
        // 브라우저는 `main` 이 돌아와야 화면을 그린다 — 앱은 힙에 두고 프레임 콜백이 루프를 돈다(emscripten_set_main_loop, 언리얼 HTML5 와 같은 모양, 기억).
        unique_ptr<App>& app = AppEntryInternal::getWebApp();
        app                  = make_unique<App>();
        if ( app->initialize( argc, pArgv ) == false )
        {
            const int32 exitCode = app->getInitFailureExitCode();
            app->shutdown();
            app.reset();
            return exitCode;
        }
        if ( app->beginMainLoop() == false )
        {
            const int32 exitCode = AppEntryInternal::finish( *app );
            app.reset();
            return exitCode;
        }
        emscripten_set_main_loop_arg( &AppEntryInternal::tickWebFrame, nullptr, 0, true ); // 돌아오지 않는다(simulate_infinite_loop)
        return 0;
#elif defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
        App app{};
        if ( app.initialize( argc, pArgv ) == false )
        {
            // 종료 코드는 내리기 전에 고른다. 일부만 초기화된 서브시스템도 정해진 순서로 내려야 렌더 스레드 비우기 · 누수 리포트를 건너뛰지 않는다.
            const int32 exitCode = app.getInitFailureExitCode();
            app.shutdown();
            return exitCode;
        }
        // 데스크톱은 프로세스가 루프를 돈다. Android 는 `android_main`(App/Android)이 이 함수를 부르고, 메시지(ALooper)는 창의 `processMessages` 가 매 틱 비운다.
        if ( app.beginMainLoop() )
        {
            while ( app.tickMainLoop() )
            {
            }
        }
        return AppEntryInternal::finish( app );
#else
    #error "Unsupported platform"
#endif
    }
} // namespace sw

#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_WEB )
int32 main( int32 argc, utf8* pArgv[] )
{
    return sw::AppEntry::run( argc, pArgv );
}
#elif defined( SW_PLATFORM_ANDROID )
// Android 는 `main` 이 없다 — NativeActivity 가 `android_main` 을 부른다(App/Android/AndroidMain.cpp).
#else
    #error "Unsupported platform"
#endif
