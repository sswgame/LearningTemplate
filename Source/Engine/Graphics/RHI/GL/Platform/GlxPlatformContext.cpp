#include "pch.h"

#include "Engine/Graphics/RHI/GL/Platform/GlxPlatformContext.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Log/Logger.h"

    #include "Engine/Graphics/RHI/GL/OpenGLRHIDeviceInternal.h"
    #include "Engine/Graphics/RHI/RHITypes.h"

    #include "Core/Common/X11Headers.h"

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

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

    namespace
    {
        /** @brief GLX 컨텍스트를 만드는 동안 X 오류를 가로채는 도우미입니다. */
        struct GlxPlatformContextInternal
        {
            static inline thread_local int32 t_glxXError{ 0 };

            static int32 glxXErrorHandler( Display*, XErrorEvent* )
            {
                t_glxXError = 1;
                return 0;
            }

            /** @brief 살아 있는 동안 X 오류 처리기를 교체하고, `failed()` 로 그 사이 오류가 있었는지 묻습니다. */
            struct GlxXErrorScope
            {
                Display*      _pDpy{ nullptr };
                XErrorHandler _prev{ nullptr };

                explicit GlxXErrorScope( Display* pDpy )
                    : _pDpy{ pDpy }
                    , _prev{ XSetErrorHandler( &GlxPlatformContextInternal::glxXErrorHandler ) }
                {
                    GlxPlatformContextInternal::t_glxXError = 0;
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
                    const bool bHadError                    = GlxPlatformContextInternal::t_glxXError != 0;
                    GlxPlatformContextInternal::t_glxXError = 0;
                    return bHadError;
                }
            };
        };
    } // namespace

    GlxPlatformContext::GlxPlatformContext()
        : _pDisplay{ nullptr }
        , _windowHandle{ 0 }
        , _pRenderContext{ nullptr }
    {
    }

    GlxPlatformContext::~GlxPlatformContext()
    {
        destroy();
    }

    // 길이 유지(긴 함수 정리 기준): 디스플레이 · 프레임버퍼 설정 · 컨텍스트 생성의 순서가 곧 의미라 나누지 않는다.
    bool GlxPlatformContext::initialize( const RHISwapChainDesc& desc, OpenGLContextHandles& outHandles )
    {
        // GLX 는 X11 창에만 문맥을 건다. Wayland 표면(wl_surface)에는 EGL 이 필요하고 그 길은 아직 없다 — X11 핸들로 읽지 않고 멈춘다.
        if ( desc._surfaceKind != NativeSurfaceKind::X11 )
        {
            SW_LOG_ERROR( "OpenGL on Linux needs an X11 window (GLX) - this window is not X11; use the Vulkan backend on Wayland" );
            return false;
        }
        Display* pDpy = (Display*)desc._pWindowDisplay;
        Window   win  = (Window)(uintptr_t)desc._pWindowHandle;

        XWindowAttributes windowAttributes{};
        if ( XGetWindowAttributes( pDpy, win, &windowAttributes ) == 0 || windowAttributes.visual == nullptr )
        {
            SW_LOG_ERROR( "XGetWindowAttributes failed" );
            return false;
        }
        const VisualID windowVisualID = XVisualIDFromVisual( windowAttributes.visual );

        int32        fbcount{ 0 };
        GLXFBConfig* pFbcAll = glXGetFBConfigs( pDpy, DefaultScreen( pDpy ), &fbcount );
        if ( pFbcAll == nullptr || fbcount <= 0 )
        {
            SW_LOG_ERROR( "glXGetFBConfigs failed" );
            return false;
        }

        GLXFBConfig chosen{ nullptr };
        for ( int32 configIndex = 0; configIndex < fbcount; ++configIndex )
        {
            int32 usable{ 0 };
            glXGetFBConfigAttrib( pDpy, pFbcAll[configIndex], GLX_DRAWABLE_TYPE, &usable );
            if ( ( usable & GLX_WINDOW_BIT ) == 0 )
                continue;
            glXGetFBConfigAttrib( pDpy, pFbcAll[configIndex], GLX_RENDER_TYPE, &usable );
            if ( ( usable & GLX_RGBA_BIT ) == 0 )
                continue;
            XVisualInfo* pVi = glXGetVisualFromFBConfig( pDpy, pFbcAll[configIndex] );
            if ( pVi == nullptr )
                continue;
            const bool bMatch = ( pVi->visualid == windowVisualID );
            XFree( pVi );
            if ( bMatch )
            {
                chosen = pFbcAll[configIndex];
                break;
            }
        }
        if ( chosen == nullptr )
            chosen = pFbcAll[0];

        PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribsARB =
            (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddressARB( (const GLubyte*)"glXCreateContextAttribsARB" );

        GLXContext context{ nullptr };
        {
            GlxPlatformContextInternal::GlxXErrorScope trap( pDpy );
            if ( glXCreateContextAttribsARB )
            {
                // 4.6 을 먼저 시도하고, WSLg 와 Mesa 를 위해 낮은 버전으로 물러난다(흔히 4.1 이하나 3.3).
                static const int32 kArrVersions[][2] = {
                    {4, 6},
                    {4, 5},
                    {4, 3},
                    {4, 2},
                    {4, 1},
                    {4, 0},
                    {3, 3}
                };
                for ( const int32( &ver )[2] : kArrVersions )
                {
                    int32 arrContextAttrib[] = {
                        GLX_CONTEXT_MAJOR_VERSION_ARB, ver[0],
                        GLX_CONTEXT_MINOR_VERSION_ARB, ver[1],
                        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
    #if !defined( SW_SHIPPING )
                        // 디버그 컨텍스트는 비-Shipping 에서만 켠다(WGL 과 같다). KHR_debug 메시지는 이 비트가 있어야
                        // 드라이버가 만들 의무가 있다(OpenGLRHIDeviceInit 이 콜백을 건다).
                        GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_DEBUG_BIT_ARB,
    #endif
                        0 };
                    context = glXCreateContextAttribsARB( pDpy, chosen, nullptr, 1, arrContextAttrib );
                    if ( context != nullptr && trap.failed() == false )
                    {
                        SW_LOG_TRACE( "GLX core context %#.%#", ver[0], ver[1] );
                        break;
                    }
                    if ( context != nullptr )
                    {
                        glXDestroyContext( pDpy, context );
                        context = nullptr;
                    }
                    trap.failed(); // 실패 표시를 비운다
                }
            }
            if ( context == nullptr )
            {
                context = glXCreateNewContext( pDpy, chosen, GLX_RGBA_TYPE, nullptr, 1 );
                if ( context == nullptr || trap.failed() )
                {
                    if ( context != nullptr )
                    {
                        glXDestroyContext( pDpy, context );
                        context = nullptr;
                    }
                }
            }
        }
        XFree( pFbcAll );

        if ( context == nullptr )
        {
            SW_LOG_ERROR( "Failed to create GLX context (WSLg often lacks GL 4.x — use -vk)" );
            return false;
        }
        if ( glXMakeCurrent( pDpy, win, context ) == 0 )
        {
            SW_LOG_ERROR( "glXMakeCurrent failed" );
            glXDestroyContext( pDpy, context );
            return false;
        }
        _pDisplay       = pDpy;
        _windowHandle   = static_cast<uint64>( win );
        _pRenderContext = context;

        outHandles._pDeviceContext = _pDisplay;
        outHandles._pRenderContext = _pRenderContext;
        return true;
    }

    void GlxPlatformContext::destroy()
    {
        if ( _pDisplay != nullptr && _pRenderContext != nullptr )
        {
            glXMakeCurrent( static_cast<Display*>( _pDisplay ), 0, nullptr );
            glXDestroyContext( static_cast<Display*>( _pDisplay ), static_cast<GLXContext>( _pRenderContext ) );
        }
        _pDisplay       = nullptr;
        _windowHandle   = 0;
        _pRenderContext = nullptr;
    }

    bool GlxPlatformContext::makeCurrent()
    {
        if ( _pDisplay == nullptr || _pRenderContext == nullptr )
            return false;

        // 로그를 남기지 않는다. 경합은 정상이고 알릴 책임은 부르는 쪽에 있다(WGL 과 같은 규약).
        return glXMakeCurrent( static_cast<Display*>( _pDisplay ),
                               static_cast<Window>( _windowHandle ),
                               static_cast<GLXContext>( _pRenderContext ) ) != 0;
    }

    bool GlxPlatformContext::isCurrent() const
    {
        if ( _pRenderContext == nullptr )
            return false;
        return glXGetCurrentContext() == static_cast<GLXContext>( _pRenderContext );
    }

    void GlxPlatformContext::clearCurrent()
    {
        if ( _pDisplay != nullptr )
            glXMakeCurrent( static_cast<Display*>( _pDisplay ), 0, nullptr );
    }

    void GlxPlatformContext::reacquireForFrame()
    {
        // GLX 는 프레임 시작에 컨텍스트를 다시 바인딩하지 않는다.
        // WGL 만 ImGui 멀티 뷰포트 때문에 되찾아야 한다.
    }

    void GlxPlatformContext::present()
    {
        if ( _pDisplay == nullptr )
            return;
        glXSwapBuffers( static_cast<Display*>( _pDisplay ), static_cast<Window>( _windowHandle ) );
    }

    void GlxPlatformContext::setSwapInterval( bool bVSync )
    {
        using PFNGLXSWAPINTERVALEXTPROC = void ( * )( Display*, GLXDrawable, int32 );
        static PFNGLXSWAPINTERVALEXTPROC s_glXSwapIntervalEXT =
            reinterpret_cast<PFNGLXSWAPINTERVALEXTPROC>( glXGetProcAddressARB( (const GLubyte*)"glXSwapIntervalEXT" ) );
        if ( s_glXSwapIntervalEXT != nullptr && _pDisplay != nullptr )
        {
            Display* pDpy = static_cast<Display*>( _pDisplay );
            s_glXSwapIntervalEXT( pDpy, glXGetCurrentDrawable(), bVSync ? 1 : 0 );
        }
    }
} // namespace sw
#endif
