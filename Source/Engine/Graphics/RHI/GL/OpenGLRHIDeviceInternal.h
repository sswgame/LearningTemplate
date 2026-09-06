/**
 * @file OpenGLRHIDeviceInternal.h
 * @brief OpenGL 백엔드 TU 들이 공유하는 플랫폼 헤더·확장 상수·내부 헬퍼
 * @details `OpenGLRHIDevice.cpp` 를 초기화/제출/코어로 나누면서, 세 TU 가 같은 WGL/GLX 상수와
 *          같은 헬퍼를 쓴다. 익명 네임스페이스에 두면 TU 마다 복사되어 "정의만 하고 안 쓰는"
 *          경고가 나고, 실제로 그렇게 됐다.
 * @note 백엔드 내부 전용이다.
 */
#pragma once
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"

#include <glad/glad.h>

#if defined( SW_PLATFORM_WINDOWS )
    // WGL system header — kept local (not in PlatformHeaders) to avoid clashing with glad.
    #include <gl/GL.h>
    #define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
    #define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
    #define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
    #define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC( WINAPI* )( HDC hDC, HGLRC hShareContext, const int32* pAttribList );
#elif defined( SW_PLATFORM_LINUX )
    #define GLX_GLXEXT_LEGACY
    #include <GL/glx.h>
    #include "Core/Common/X11MacroUndef.h"
    #define GLX_CONTEXT_MAJOR_VERSION_ARB    0x2091
    #define GLX_CONTEXT_MINOR_VERSION_ARB    0x2092
    #define GLX_CONTEXT_PROFILE_MASK_ARB     0x9126
    #define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
typedef GLXContext ( *PFNGLXCREATECONTEXTATTRIBSARBPROC )( Display*, GLXFBConfig, GLXContext, int32, const int32* );
#endif

namespace sw
{
    /** @brief OpenGL 백엔드 조각들이 공유하는 내부 헬퍼. */
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
                const bool b                         = OpenGLRHIDeviceInternal::t_glxXError != 0;
                OpenGLRHIDeviceInternal::t_glxXError = 0;
                return b;
            }
        };
#endif

        static void applyVsyncInterval( void* pHdc, void* pHrc, bool vsync )
        {
            (void)pHdc;
#if defined( SW_PLATFORM_WINDOWS )
            (void)pHrc;
            using PFNWGLSWAPINTERVALEXTPROC                       = BOOL( WINAPI* )( int32 );
            static PFNWGLSWAPINTERVALEXTPROC s_wglSwapIntervalEXT = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>( wglGetProcAddress( "wglSwapIntervalEXT" ) );
            if ( s_wglSwapIntervalEXT != nullptr )
                s_wglSwapIntervalEXT( vsync ? 1 : 0 );
#elif defined( SW_PLATFORM_LINUX )
            (void)pHrc;
            using PFNGLXSWAPINTERVALEXTPROC = void ( * )( Display*, GLXDrawable, int32 );
            static PFNGLXSWAPINTERVALEXTPROC s_glXSwapIntervalEXT =
                reinterpret_cast<PFNGLXSWAPINTERVALEXTPROC>( glXGetProcAddressARB( (const GLubyte*)"glXSwapIntervalEXT" ) );
            if ( s_glXSwapIntervalEXT != nullptr && pHdc != nullptr )
            {
                Display* pDpy = static_cast<Display*>( pHdc );
                s_glXSwapIntervalEXT( pDpy, glXGetCurrentDrawable(), vsync ? 1 : 0 );
            }
#elif defined( SW_PLATFORM_MACOS )
            (void)pHdc;
            if ( pHrc != nullptr )
            {
                id              context                               = static_cast<id>( pHrc );
                GLint           interval                              = vsync ? 1 : 0;
                constexpr GLint kNsOpenGlContextParameterSwapInterval = 222;
                ( (void ( * )( id, SEL, GLint*, GLint ))objc_msgSend )(
                    context, sel_registerName( "setValues:forParameter:" ), &interval, kNsOpenGlContextParameterSwapInterval );
            }
#endif
        }
    };
} // namespace sw
