/**
 * @file GameFrameworkAPI.h
 * @brief GameFramework 공유 라이브러리의 내보내기 매크로 `SW_GF_API` 와 키트의 `SW_KIT_API` 입니다. 정의는 여기 한 곳입니다.
 * @details `SW_GF_API` 는 GameFramework.dll(Base) 것이다. GameFramework(`GameFrameworkExports.h`)와 RuntimeAPI 의 게임 서비스 연결 선언
 *          (`Service/GameServiceBinding.h`)이 같이 쓴다. RuntimeAPI 는 GameFramework 를 include 하지 않으므로 매크로를 이쪽에 둔다.
 *          Windows: `SW_GF_EXPORTS` 면 dllexport, `SW_GF_IMPORTS`(GameFramework 를 링크한 쪽)면 dllimport, 둘 다 없으면(STATIC · Shipping) 비운다.
 *          `SW_KIT_API` 는 키트 DLL 자기 헤더(`GameFramework/Kits`)만 쓴다. 키트 TU 는 `SW_GF_EXPORTS` 를 정의하지 않고 `SW_KIT_EXPORTS` 를 정의하므로
 *          include 한 Base 선언은 dllimport 로 남는다 — Base 클래스의 암시적 생성자 · 소멸자를 키트 DLL 이 다시 내보내지 않는다(백로그 C-3).
 *          `SW_KIT_SERVER_API` 는 서버 키트(`GF_Server_<X>`, 폴더 `Server/`)의 자기 헤더만 쓴다. 서버 키트 TU 는 `SW_KIT_SERVER_EXPORTS` 만 정의하므로 같이 include 한
 *          공유 키트 헤더의 `SW_KIT_API` 는 dllimport 로 남는다 — 서버 키트 DLL 이 공유 키트 클래스를 다시 내보내지 않는다(C-3).
 *          모든 키트 TU 가 `SW_KIT_IMPORTS` 를 정의한다(PCH 와 같은 명령줄 정의) — 자기 내보내기 매크로가 먼저 읽힌다.
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
