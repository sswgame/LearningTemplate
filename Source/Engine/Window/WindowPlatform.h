/**
 * @file WindowPlatform.h
 * @brief 플랫폼 창을 만드는 자리입니다. 플랫폼 폴더(`Window/<OS>/`)가 스스로 등록하고, `IWindow` · `ISplashWindow` 는 구체 형식을 모릅니다.
 * @details 창 생성이 한 파일의 `#if` 사슬과 구체 형식 표면 플랫폼 하나가 늘 때마다 공용 파일이 그 플랫폼 헤더를 읽는다. `RHIStaticBackendRegistrar` 와 같은 모양이고,
 *          언리얼 `FPlatformApplicationMisc::CreateApplication` 이 플랫폼 파일에 있는 것과 같은 자리다(기억). 창이 없는 구성(리눅스 전용 서버)은 등록하지 않아 `find` 가 nullptr 이다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class ISplashWindow;
    class IWindow;

    /** @brief 플랫폼 하나의 창 공장입니다. */
    struct WindowPlatformFactory
    {
        unique_ptr<IWindow> ( *_pfnCreateWindow )();       ///< 주 창을 만듭니다. 고를 수 없으면 오류를 남기고 nullptr 입니다
        unique_ptr<ISplashWindow> ( *_pfnCreateSplash )(); ///< 스플래시 창을 만듭니다
        bool ( *_pfnIsSplashSupported )();                 ///< 스플래시를 띄울 수 있는 환경인지 봅니다
    };

    /** @brief 이 프로세스의 창 공장 자리입니다. */
    struct SW_API WindowPlatform
    {
        /** @brief 등록된 공장입니다. 창이 없는 구성이면 nullptr 입니다. */
        static const WindowPlatformFactory* find();

        /** @brief 플랫폼 폴더의 `.cpp` 가 정적 변수로 둡니다. 한 프로세스에 하나입니다(둘이면 단언). */
        struct SW_API Registrar
        {
            explicit Registrar( const WindowPlatformFactory& factory );
        };
    };
} // namespace sw
