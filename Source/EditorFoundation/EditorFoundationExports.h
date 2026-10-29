/**
 * @file EditorFoundationExports.h
 * @brief EditorFoundation DLL 의 `SW_EDITOR_FOUNDATION_API` 입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"

#if defined( SW_PLATFORM_WINDOWS )
    #if defined( SW_EDITOR_FOUNDATION_EXPORTS )
        #define SW_EDITOR_FOUNDATION_API __declspec( dllexport )
    #else
        #define SW_EDITOR_FOUNDATION_API __declspec( dllimport )
    #endif
#else
    #define SW_EDITOR_FOUNDATION_API __attribute__( ( visibility( "default" ) ) )
#endif
