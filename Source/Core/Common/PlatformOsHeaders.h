/**
 * @file PlatformOsHeaders.h
 * @brief OS 시스템 헤더만 포함합니다. DirectX · Vulkan · DXC 는 넣지 않습니다.
 * @note 그래픽 API 헤더는 Engine/Common/EnginePlatformHeaders.h 에 있습니다.
 * @note X11 은 넣지 않습니다 — 이 헤더는 PCH 를 거쳐 모든 TU 에 들어가므로, X11 의 흔한 이름 매크로(`Convex` · `None` …)가
 *       서드파티 헤더까지 덮습니다. X11 을 쓰는 `.cpp` 가 `Core/Common/X11Headers.h` 를 직접 include 합니다.
 */
#pragma once
// ------------------------------------------------------------------------------
// 1) Windows — SDK 헤더. NOMINMAX · WIN32_LEAN_AND_MEAN 은 CMake 가 모든 TU 에 정의한다(cmake/Modules/Platform/Windows.cmake)
//    — 이 헤더보다 먼저 windows.h 를 include 하는 서드파티 헤더에도 걸린다. 아래 정의는 빌드 플래그 없이 헤더를 읽는 도구용이다.
// ------------------------------------------------------------------------------

#if defined( SW_PLATFORM_WINDOWS )
    #if !defined( NOMINMAX )
        /** @brief Windows.h 의 min/max 매크로를 막습니다. */
        #define NOMINMAX
    #endif
    #if !defined( WIN32_LEAN_AND_MEAN )
        /** @brief 잘 안 쓰는 Windows API 를 빼 컴파일 시간을 줄입니다. */
        #define WIN32_LEAN_AND_MEAN
    #endif

    #include <Windows.h>
    #include <Unknwn.h>
    #include <DbgHelp.h>
    #include <Xinput.h>
    #include <commdlg.h>
    #include <crtdbg.h>
    #include <delayimp.h>
    #include <intrin.h>
    #include <malloc.h>
    #include <sdkddkver.h>
    #include <wrl/client.h>

// ------------------------------------------------------------------------------
// 2) POSIX — Linux
// ------------------------------------------------------------------------------
#elif defined( SW_PLATFORM_LINUX )
    #include <cxxabi.h>
    #include <dirent.h>
    #include <dlfcn.h>
    #include <execinfo.h>
    #include <pthread.h>
    #include <sched.h>
    #include <sys/mman.h>
    #include <sys/select.h>
    #include <sys/stat.h>
    #include <sys/time.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>

    #include <sys/eventfd.h>
    #include <sys/inotify.h>
#else
    #error "NOT SUPPORTED PLATFORM"
#endif
