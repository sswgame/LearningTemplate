/**
 * @file GameFrameworkExports.h
 * @brief GameFramework SHARED DLL 용 SW_GF_API 입니다.
 */
#pragma once
#include "Core/Common/Macros.h"

// ------------------------------------------------------------------------------
// 1) SW_GF_API — GameFramework SHARED 내보내기
//    Windows: dllexport/dllimport, 그 외: default visibility
//    STATIC(Shipping)에서는 빈 매크로
// ------------------------------------------------------------------------------

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

// `registerGameFrameworkTypes()` 는 두지 않는다. GameFramework 의 리플렉션 타입은
// `EngineLoop` 이 서비스를 묶은 직후 `engine::registerModuleTypes( "GameFramework" )` 로
// 직접 등록한다. 그것이 기준이고 실제로 도는 유일한 경로다. 같은 일을 하는 함수를 여기 하나 더
// export 하지 말 것 — 등록 자리가 둘이면 어느 쪽이 도는지 알 수 없다.
