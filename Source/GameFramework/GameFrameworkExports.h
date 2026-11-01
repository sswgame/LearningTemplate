/**
 * @file GameFrameworkExports.h
 * @brief GameFramework DLL(`SW_GF_API`)과 키트 모듈(`SW_KIT_API` · `SW_KIT_SERVER_API`)의 내보내기 매크로입니다.
 * @details 정의는 이 층이 가진다 — 계약 층(ModuleAPI)은 위층 이름을 모른다(예전 `ModuleAPI/Export/GameFrameworkAPI.h`, StructureRefactor 8 절 18).
 */
#pragma once
#include "Core/Foundation/Macros.h"

#if defined( SW_PLATFORM_WINDOWS )
    #if defined( SW_GF_EXPORTS )
        #define SW_GF_API __declspec( dllexport )
    #elif defined( SW_GF_IMPORTS )
        #define SW_GF_API __declspec( dllimport )
    #else
        #define SW_GF_API
    #endif
#else
    #define SW_GF_API __attribute__( ( visibility( "default" ) ) )
#endif

#if defined( SW_PLATFORM_WINDOWS )
    #if defined( SW_KIT_EXPORTS )
        #define SW_KIT_API __declspec( dllexport )
    #elif defined( SW_KIT_IMPORTS )
        #define SW_KIT_API __declspec( dllimport )
    #else
        #define SW_KIT_API
    #endif
    #if defined( SW_KIT_SERVER_EXPORTS )
        #define SW_KIT_SERVER_API __declspec( dllexport )
    #elif defined( SW_KIT_IMPORTS )
        #define SW_KIT_SERVER_API __declspec( dllimport )
    #else
        #define SW_KIT_SERVER_API
    #endif
#else
    #define SW_KIT_API        __attribute__( ( visibility( "default" ) ) )
    #define SW_KIT_SERVER_API __attribute__( ( visibility( "default" ) ) )
#endif

// `registerGameFrameworkTypes()` 는 두지 않는다. GameFramework 의 리플렉션 타입은
// `Engine` 이 서비스를 묶은 직후 `engine::registerModuleTypes( "GameFramework" )` 로
// 직접 등록한다. 그것이 기준이고 실제로 도는 유일한 경로다. 같은 일을 하는 함수를 여기 하나 더
// export 하지 말 것 — 등록 자리가 둘이면 어느 쪽이 도는지 알 수 없다.
