/**
 * @file AndroidMain.cpp
 * @brief Android NativeActivity 의 진입점입니다(`android_native_app_glue` 가 자기 스레드에서 `android_main` 을 부른다). 앱 수명은 데스크톱과 같은 `AppEntry::run` 이다.
 * @details 명령줄이 없다 — 인텐트 추가값(`-scenario` 등)은 남은 일이다(docs/PlatformTargets.md). 표면이 생긴 뒤에 앱을 세운다(RHI 가 창을 찾는다).
 */
#include "pch.h"

#include "App/AppEntry.h"

#if defined( SW_PLATFORM_ANDROID )
    #include "Engine/Window/Android/AndroidWindow.h"

    #include <android_native_app_glue.h>

// NOLINTNEXTLINE(readability-identifier-naming): NDK 글루가 부르는 이름이다
extern "C" void android_main( android_app* pApp )
{
    sw::AndroidWindow::setApplication( pApp );
    if ( sw::AndroidWindow::waitForInitialSurface() == false )
        return; // 표면이 생기기 전에 액티비티가 닫혔다
    utf8  arrProgramName[] = "SWApp";
    utf8* arrArgument[]    = { arrProgramName, nullptr };
    (void)sw::AppEntry::run( 1, arrArgument );
    sw::AndroidWindow::setApplication( nullptr );
}

#endif
