/**
 * @file ModuleManager/ModuleManagerAPI.h
 * @brief 모듈 관리 라이브러리의 내보내기 매크로 `SW_MODULEMANAGER_API` 와, 개발 계층을 만드는 함수의 `SW_HOST_DEV_API` 입니다.
 * @details 개발 구성의 ModuleManager 는 공유 라이브러리다 — 호스트(App · Server)와 개발 도구(DevTools.dll — 핫 리로드 · 호스트 개발 계층)가 같은 한 벌을 쓴다.
 *          정적이면 DevTools 와 호스트가 한 벌씩 들어 정적 등록 · 상태가 둘이 된다(언리얼 Core 의 ModuleManager 가 DLL 이고 HotReload 가 그 위에 서는 자리).
 *          배포는 정적이라 매크로가 비고(`SW_MODULEMANAGER_EXPORTS` · `SW_MODULEMANAGER_IMPORTS` 둘 다 없음), CMake 가 정의를 준다(`sw_configure_dll_exports( … MODULEMANAGER )`).
 *
 *          `SW_HOST_DEV_API` 는 ModuleManager 헤더에 선언하고 DevTools.dll 이 정의하는 만드는 함수(`makeHostDevLayer` · `makeServerDebugControl`)에 붙는다.
 *          배포 호스트는 버려지는 `if constexpr` 갈래에서만 이름을 보고 DevTools 를 링크하지 않으므로, `DevTools/DevToolsAPI.h`(링크하지 않은 타깃이면 멈춘다)를 쓸 수 없다.
 */
#pragma once
#include "Core/Foundation/Macros.h"

#if defined( SW_PLATFORM_WINDOWS )
    #if defined( SW_MODULEMANAGER_EXPORTS )
        #define SW_MODULEMANAGER_API __declspec( dllexport )
    #elif defined( SW_MODULEMANAGER_IMPORTS )
        #define SW_MODULEMANAGER_API __declspec( dllimport )
    #else
        #define SW_MODULEMANAGER_API
    #endif
    #if defined( SW_DEVTOOLS_EXPORTS )
        #define SW_HOST_DEV_API __declspec( dllexport )
    #elif defined( SW_DEVTOOLS_IMPORTS )
        #define SW_HOST_DEV_API __declspec( dllimport )
    #else
        #define SW_HOST_DEV_API
    #endif
#elif defined( SW_PLATFORM_POSIX )
    #define SW_MODULEMANAGER_API __attribute__( ( visibility( "default" ) ) )
    #define SW_HOST_DEV_API      __attribute__( ( visibility( "default" ) ) )
#else
    #error "Unsupported platform"
#endif
