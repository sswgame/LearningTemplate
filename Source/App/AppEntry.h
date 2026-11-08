/**
 * @file AppEntry.h
 * @brief 프로세스 진입점 하나가 앱을 세우고 루프를 돌리고 내리는 입구입니다. 데스크톱 · 웹은 `main`, Android 는 `android_main` 이 부릅니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class App;

    /** @brief 진입점이 부르는 앱 수명 한 벌입니다(`App` 의 initialize → beginMainLoop → tickMainLoop … → endMainLoop → shutdown). */
    struct AppEntry
    {
        /**
         * @brief 앱을 돌리고 종료 코드를 돌려줍니다. 웹은 브라우저 프레임 콜백에 루프를 넘기고 돌아오지 않는다(끝나면 `emscripten_force_exit`).
         * @details 플랫폼마다 루프를 누가 돌리는지는 다르지만 세 함수(begin · tick · end)는 같다 — 언리얼 `GuardedMain` 과 같은 자리다(기억).
         */
        static int32 run( int32 argc, utf8* pArgv[] );

#if defined( SW_PLATFORM_WEB )
    private:
        /** @brief 브라우저 프레임 콜백입니다(requestAnimationFrame). 루프가 끝나면 콜백을 풀고 앱을 내린 뒤 프로세스 종료 코드를 낸다. */
        static void tickWebFrame( void* pUserData );
        /** @brief 브라우저 루프가 도는 동안의 앱입니다 — `main` 이 돌아온 뒤에도 살아야 해서 프로세스 수명 자리에 둔다(감춘 싱글턴이 아니라 진입점의 몫). */
        static unique_ptr<App>& getWebApp();
#endif
    };
} // namespace sw
