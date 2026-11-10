#include "pch.h"

#include "App/AppEntry.h"

#include "App/App.h"

#if defined( SW_PLATFORM_WEB )
    #include "Core/File/UserDataPath.h"

    #include <emscripten/emscripten.h>
    #include <stdlib.h>

namespace
{
    /** @brief 웹 영속 폴더입니다 — IndexedDB 를 비추는 IDBFS 마운트(유니티 WebGL 의 /idbfs 와 같은 모양). */
    constexpr const char* kWebPersistentRoot = "/persistent";
} // namespace

// IDBFS 를 붙이고 지난 세션의 파일을 읽어 올 때까지 기다린다(JSPI) — 본문은 App/Web/WebPersistentRoot.js(Emscripten JS 라이브러리)다.
extern "C" void swMountWebPersistentRoot( const char* pRoot );
// 영속 폴더를 IndexedDB 로 내리기 시작한다(기다리지 않는다 — 페이지가 남아 있어 끝난다). 앱이 끝날 때 부른다: 숨김 · 닫힘 없이 끝난 세션의 마지막 세이브를 잃지 않게.
extern "C" void swFlushWebPersistentRoot();
#endif

namespace sw
{
    int32 AppEntry::run( int32 argc, utf8* pArgv[] )
    {
#if defined( SW_PLATFORM_WEB )
        // 세이브 · 설정 자리 — 브라우저에는 사용자 폴더가 없어 부팅이 영속 마운트를 정한다(UserDataPath).
        swMountWebPersistentRoot( kWebPersistentRoot );
        (void)::setenv( kUserDataRootVariable, kWebPersistentRoot, 1 );
        // 브라우저는 `main` 이 돌아와야 화면을 그린다 — 앱은 힙에 두고 프레임 콜백이 루프를 돈다(emscripten_set_main_loop, 언리얼 HTML5 와 같은 모양, 기억).
        unique_ptr<App>& app = getWebApp();
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
            const int32 exitCode = app->finishMainLoop();
            app.reset();
            return exitCode;
        }
        emscripten_set_main_loop_arg( &AppEntry::tickWebFrame, nullptr, 0, true ); // 돌아오지 않는다(simulate_infinite_loop)
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
        return app.finishMainLoop();
#else
    #error "Unsupported platform"
#endif
    }
#if defined( SW_PLATFORM_WEB )
    void AppEntry::tickWebFrame( void* pUserData )
    {
        (void)pUserData;
        if ( getWebApp()->tickMainLoop() )
            return;
        emscripten_cancel_main_loop();
        const int32 exitCode = getWebApp()->finishMainLoop();
        getWebApp().reset();
        swFlushWebPersistentRoot(); // 앱이 다 내린 뒤(세이브 · 설정 쓰기가 끝난 뒤) 한 번
        emscripten_force_exit( exitCode );
    }

    unique_ptr<App>& AppEntry::getWebApp()
    {
        static unique_ptr<App> s_app;
        return s_app;
    }
#endif
} // namespace sw
