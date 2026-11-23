/**
 * @file AppEntry.h
 * @brief 프로세스 진입점 하나가 앱을 세우고 루프를 돌리고 내리는 입구입니다. 데스크톱 · 웹은 `main`, Android 는 `android_main` 이 부릅니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class App;

    /** @brief 진입점이 부르는 앱 수명 한 벌입니다(`App` 의 initialize → beginMainLoop → updateMainLoop … → endMainLoop → shutdown). */
    struct AppEntry
    {
        /**
         * @brief 앱을 돌리고 종료 코드를 돌려줍니다. 웹은 끝에서 돌아오지 않는다(`emscripten_force_exit`).
         * @details 모든 플랫폼이 이 함수 안에서 루프를 돈다(언리얼 `GuardedMain` 과 같은 자리) — 웹은 프레임 사이를 JSPI 로 멈춘다.
         */
        static int32 run( int32 argc, utf8* pArgv[] );
    };
} // namespace sw
