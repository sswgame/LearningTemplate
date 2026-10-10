/**
 * @file EditorExports.h
 * @brief EditorModule SHARED DLL 의 SW_EDITOR_API 입니다(Dev 전용 — 확장 모듈 `GF_Editor_*` · `SWGameEditor` 가 링크합니다).
 */
#pragma once
#include "Core/Common/Macros.h"

// ------------------------------------------------------------------------------
// 1) SW_EDITOR_API — EditorModule 내보내기
//    Windows: dllexport/dllimport, 그 외: default visibility
//    EditorTest 처럼 소스를 직접 컴파일하는 곳(둘 다 정의 안 됨)에서는 빈 매크로
// ------------------------------------------------------------------------------

#if defined( SW_PLATFORM_WINDOWS )
    #if defined( SW_EDITOR_EXPORTS )
        #define SW_EDITOR_API __declspec( dllexport )
    #elif defined( SW_EDITOR_IMPORTS )
        #define SW_EDITOR_API __declspec( dllimport )
    #else
        #define SW_EDITOR_API
    #endif
#else
    #define SW_EDITOR_API __attribute__( ( visibility( "default" ) ) )
#endif
