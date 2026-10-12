/**
 * @file GameFrameworkAPI.h
 * @brief GameFramework 공유 라이브러리의 내보내기 매크로 `SW_GF_API` 입니다. 정의는 여기 한 곳입니다.
 * @details GameFramework(`GameFrameworkExports.h`)와 RuntimeAPI 의 게임 서비스 연결 선언(`Service/GameServiceBinding.h`)이 같이 쓴다.
 *          RuntimeAPI 는 GameFramework 를 include 하지 않으므로 매크로를 이쪽에 둔다.
 *          Windows: `SW_GF_EXPORTS` 면 dllexport, `SW_GF_IMPORTS`(GameFramework 를 링크한 쪽)면 dllimport, 둘 다 없으면(STATIC · Shipping) 비운다.
 *          그 밖의 플랫폼: default visibility.
 */
#pragma once
#include "Core/Common/Macros.h"

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
