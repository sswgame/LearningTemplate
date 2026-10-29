/**
 * @file DevToolsAPI.h
 * @brief 개발 도구 공유 라이브러리(`DevTools.dll`)의 내보내기 매크로 `SW_DEVTOOLS_API` 입니다.
 * @details 호스트(App · Server)와 EditorModule · GameFramework 가 같은 라이브러리 하나를 쓴다 — 정적 링크면 정적 등록(개발 명령 · HUD 섹션)이 이미지마다 한 번씩 돈다.
 *          Windows: `SW_DEVTOOLS_EXPORTS` 면 dllexport, `SW_DEVTOOLS_IMPORTS`(링크한 쪽)면 dllimport. 그 밖의 플랫폼: default visibility.
 */
#pragma once
#include "Core/Foundation/Macros.h"

#if defined( SW_PLATFORM_WINDOWS )
    #if defined( SW_DEVTOOLS_EXPORTS )
        #define SW_DEVTOOLS_API __declspec( dllexport )
    #elif defined( SW_DEVTOOLS_IMPORTS )
        #define SW_DEVTOOLS_API __declspec( dllimport )
    #else
        #define SW_DEVTOOLS_API
    #endif
#else
    #define SW_DEVTOOLS_API __attribute__( ( visibility( "default" ) ) )
#endif
