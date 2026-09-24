/**
 * @file OpenGLRHIDeviceInternal.h
 * @brief OpenGL 백엔드의 여러 TU 가 함께 쓰는 플랫폼 헤더 · 확장 상수 · 내부 도우미입니다.
 * @details `OpenGLRHIDevice.cpp` 를 초기화 · 제출 · 코어로 나누면서 세 TU 가 같은 WGL/GLX 상수와
 *          같은 도우미를 쓰게 됐습니다. 익명 네임스페이스에 두면 TU 마다 복사되어 "정의만 하고 안 쓰는"
 *          경고가 나고, 실제로 그렇게 됐습니다.
 * @note 백엔드 내부 전용입니다.
 */
#pragma once
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"

#include <glad/glad.h>

#if defined( SW_PLATFORM_WINDOWS )
    // WGL 시스템 헤더. glad 와 부딪치지 않게 PlatformHeaders 가 아니라 여기에만 둔다.
    #include <gl/GL.h>
    #define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
    #define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
    #define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
    #define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
    #define WGL_CONTEXT_FLAGS_ARB            0x2094
    #define WGL_CONTEXT_DEBUG_BIT_ARB        0x00000001 // KHR_debug 메시지를 드라이버가 만들 의무가 생기는 비트 (비-Shipping 전용)
using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC( WINAPI* )( HDC hDC, HGLRC hShareContext, const int32* pAttribList );
#elif defined( SW_PLATFORM_LINUX )
    #define GLX_GLXEXT_LEGACY
    #include <GL/glx.h>
    #include "Core/Common/X11MacroUndef.h"
    #define GLX_CONTEXT_MAJOR_VERSION_ARB    0x2091
    #define GLX_CONTEXT_MINOR_VERSION_ARB    0x2092
    #define GLX_CONTEXT_PROFILE_MASK_ARB     0x9126
    #define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
    #define GLX_CONTEXT_FLAGS_ARB            0x2094
    #define GLX_CONTEXT_DEBUG_BIT_ARB        0x00000001 // KHR_debug 메시지를 드라이버가 만들 의무가 생기는 비트 (비-Shipping 전용)
typedef GLXContext ( *PFNGLXCREATECONTEXTATTRIBSARBPROC )( Display*, GLXFBConfig, GLXContext, int32, const int32* );
#endif

namespace sw
{
    /** @brief OpenGL 백엔드 조각들이 함께 쓰는 내부 도우미입니다. */
    struct OpenGLRHIDeviceInternal
    {
#if defined( SW_PLATFORM_LINUX )
        static inline thread_local int32 t_glxXError{ 0 };

        static int32 glxXErrorHandler( Display*, XErrorEvent* )
        {
            t_glxXError = 1;
            return 0;
        }

        struct GlxXErrorScope
        {
            Display*      _pDpy{ nullptr };
            XErrorHandler _prev{ nullptr };

            explicit GlxXErrorScope( Display* pDpy )
                : _pDpy{ pDpy }
                , _prev{ XSetErrorHandler( &OpenGLRHIDeviceInternal::glxXErrorHandler ) }
            {
                OpenGLRHIDeviceInternal::t_glxXError = 0;
            }

            ~GlxXErrorScope()
            {
                if ( _pDpy != nullptr )
                    XSync( _pDpy, 0 );
                XSetErrorHandler( _prev );
            }

            bool failed()
            {
                if ( _pDpy != nullptr )
                    XSync( _pDpy, 0 );
                const bool bHadError                 = OpenGLRHIDeviceInternal::t_glxXError != 0;
                OpenGLRHIDeviceInternal::t_glxXError = 0;
                return bHadError;
            }
        };
#endif
    };
} // namespace sw
