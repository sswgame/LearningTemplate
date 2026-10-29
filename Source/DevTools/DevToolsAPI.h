/**
 * @file DevToolsAPI.h
 * @brief 개발 도구 공유 라이브러리(`DevTools.dll`)의 내보내기 매크로 `SW_DEVTOOLS_API` 입니다.
 * @details 호스트(App · Server)와 EditorModule · GameFramework 가 같은 라이브러리 하나를 쓴다 — 정적 링크면 정적 등록(개발 명령 · HUD 섹션)이 이미지마다 한 번씩 돈다.
 *          `SW_DEVTOOLS_EXPORTS`(DevTools 자신) · `SW_DEVTOOLS_IMPORTS`(링크한 쪽)는 CMake 가 준다(`sw_configureDLLExports( … DEVTOOLS )`).
 *          둘 다 없으면 DevTools 를 링크하지 않은 타깃(엔진 · 배포 타깃)이다 — 그 타깃이 개발 도구 헤더를 include 하면 여기서 멈춘다(링크할 때가 아니라 컴파일에서).
 */
#pragma once
#include "Core/Foundation/Macros.h"

#if !defined( SW_DEVTOOLS_EXPORTS ) && !defined( SW_DEVTOOLS_IMPORTS )
    #error "DevTools headers are only for targets that link DevTools - the engine and shipping targets do not know the dev tools"
#endif

#if defined( SW_PLATFORM_WINDOWS )
    #if defined( SW_DEVTOOLS_EXPORTS )
        #define SW_DEVTOOLS_API __declspec( dllexport )
    #else
        #define SW_DEVTOOLS_API __declspec( dllimport )
    #endif
#else
    #define SW_DEVTOOLS_API __attribute__( ( visibility( "default" ) ) )
#endif
