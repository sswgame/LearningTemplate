/**
 * @file EnginePlatformHeaders.h
 * @brief OS 헤더(PlatformOSHeaders)와 그래픽 API 시스템 헤더(Core/RHI)를 모읍니다.
 * @note 서드파티(vulkan, glad, imgui, dxc 등)는 여기 넣지 않습니다. 쓰는 곳에서 직접 include 합니다.
 *       Windows 에서 DXC 를 쓸 수 있는지만 `SW_HAS_DXC_API` 로 표시합니다.
 */
#pragma once
#include "Core/Foundation/PlatformOSHeaders.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include <d3d11.h>
    #include <d3d11_1.h>
    #include <d3d11shader.h>
    #include <d3d12.h>
    #include <d3d12sdklayers.h>
    #include <d3d12shader.h>
    #include <d3dcompiler.h>
    #include <dxgi1_4.h>
    #include <mfapi.h>
    #include <mfidl.h>
    #include <mfreadwrite.h>
    #include <xaudio2.h>
    #define SW_HAS_DXC_API 1

#elif defined( SW_PLATFORM_LINUX )
    #if __has_include( <dxcapi.h> )
        #define SW_HAS_DXC_API 1
    #endif
#elif defined( SW_PLATFORM_ANDROID ) || defined( SW_PLATFORM_WEB )
// 쿠킹본만 읽는 플랫폼이다 — DXC 가 없어 `ShaderCompiler::compileHLSL` 은 오류를 돌려주고 링크에 컴파일러가 들지 않는다. 쿠킹은 데스크톱 호스트가 한다(언리얼 `IShaderFormat` 도 쿠커 쪽이다, 기억).
#else
    #error "Unsupported platform"
#endif
