/**
 * @file PlatformOsHeaders.h
 * @brief OS 시스템 헤더만 포함합니다. DirectX/Vulkan/DXC는 넣지 않습니다.
 * @note 그래픽 API 헤더는 Engine/Common/EnginePlatformHeaders.h에 있습니다.
 */
#pragma once
// ------------------------------------------------------------------------------
// 1) Windows — NOMINMAX / WIN32_LEAN_AND_MEAN 후 SDK 헤더
// ------------------------------------------------------------------------------

#if defined( SW_PLATFORM_WINDOWS ) || defined( _WIN32 ) || defined( _WIN64 )
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
// 2) POSIX — Linux / macOS 공통 + 플랫폼별
// ------------------------------------------------------------------------------
#elif defined( SW_PLATFORM_LINUX ) || defined( __linux__ ) || defined( SW_PLATFORM_MACOS ) || defined( __APPLE__ )
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

    #if defined( SW_PLATFORM_LINUX )
        #include <X11/Xatom.h>
        #include <X11/Xlib.h>
        #include <X11/Xutil.h>
        #include <X11/keysym.h>
        #include <sys/eventfd.h>
        #include <sys/inotify.h>

        #include "Core/Common/X11MacroUndef.h"
    #elif defined( SW_PLATFORM_MACOS )
        #include <CoreServices/CoreServices.h>
        #include <mach-o/dyld.h>
        #include <objc/message.h>
        #include <objc/runtime.h>
        #include <pwd.h>
    #endif
#else
    #error "NOT SUPPORTED PLATFORM"
#endif
