#include "pch.h"

#include "Engine/Graphics/RHI/GL/Platform/GlxPlatformContext.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Log/Logger.h"

    #include "Engine/Graphics/RHI/GL/OpenGLRHIDeviceInternal.h"
    #include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

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

    bool GlxPlatformContext::initialize( const RHISwapChainDesc& desc, OpenGLContextHandles& outHandles )
    {
        Display* pDpy = (Display*)desc._pWindowDisplay;
        Window   win  = (Window)(uintptr_t)desc._pWindowHandle;

        XWindowAttributes windowAttributes{};
        if ( XGetWindowAttributes( pDpy, win, &windowAttributes ) == 0 || windowAttributes.visual == nullptr )
        {
            SW_LOG_ERROR( "XGetWindowAttributes failed" );
            return false;
        }
        const VisualID windowVisualId = XVisualIDFromVisual( windowAttributes.visual );

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
            const bool bMatch = ( pVi->visualid == windowVisualId );
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

        GLXContext ctx{ nullptr };
        {
            OpenGLRHIDeviceInternal::GlxXErrorScope trap( pDpy );
            if ( glXCreateContextAttribsARB )
            {
                // Prefer 4.6, fall back for WSLg/Mesa (often ≤4.1 / 3.3).
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
                        0 };
                    ctx = glXCreateContextAttribsARB( pDpy, chosen, nullptr, 1, arrContextAttrib );
                    if ( ctx != nullptr && trap.failed() == false )
                    {
                        SW_LOG_TRACE( "GLX core context %#.%#", ver[0], ver[1] );
                        break;
                    }
                    if ( ctx != nullptr )
                    {
                        glXDestroyContext( pDpy, ctx );
                        ctx = nullptr;
                    }
                    trap.failed(); // clear
                }
            }
            if ( ctx == nullptr )
            {
                ctx = glXCreateNewContext( pDpy, chosen, GLX_RGBA_TYPE, nullptr, 1 );
                if ( ctx == nullptr || trap.failed() )
                {
                    if ( ctx != nullptr )
                    {
                        glXDestroyContext( pDpy, ctx );
                        ctx = nullptr;
                    }
                }
            }
        }
        XFree( pFbcAll );

        if ( ctx == nullptr )
        {
            SW_LOG_ERROR( "Failed to create GLX context (WSLg often lacks GL 4.x — use -vulkan)" );
            return false;
        }
        if ( glXMakeCurrent( pDpy, win, ctx ) == 0 )
        {
            SW_LOG_ERROR( "glXMakeCurrent failed" );
            glXDestroyContext( pDpy, ctx );
            return false;
        }
        _pDisplay       = pDpy;
        _windowHandle   = static_cast<uint64>( win );
        _pRenderContext = ctx;

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

        // 로그를 남기지 않는다 - 경합은 정상이고 알릴 책임은 호출부에 있다(WGL 과 같은 규약).
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
        // 예전 코드도 프레임 시작에 GLX 컨텍스트를 다시 바인딩하지 않았다. 동작을 바꾸지 않는다 —
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
