#include "pch.h"

#include "Engine/Graphics/RHI/GL/Platform/WglPlatformContext.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Log/Logger.h"

    #include "Engine/Graphics/RHI/GL/OpenGLRHIDeviceInternal.h"
    #include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

    WglPlatformContext::WglPlatformContext()
        : _pWindowHandle{ nullptr }
        , _pDeviceContext{ nullptr }
        , _pRenderContext{ nullptr }
    {
    }

    WglPlatformContext::~WglPlatformContext()
    {
        destroy();
    }

    bool WglPlatformContext::initialize( const RHISwapChainDesc& desc, OpenGLContextHandles& outHandles )
    {
        HWND hWnd = static_cast<HWND>( desc._pWindowHandle );
        HDC  hDC  = GetDC( hWnd );

        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize                 = sizeof( PIXELFORMATDESCRIPTOR );
        pfd.nVersion              = 1;
        pfd.dwFlags               = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType            = PFD_TYPE_RGBA;
        pfd.cColorBits            = 32;
        pfd.cDepthBits            = 24;
        pfd.cStencilBits          = 8;

        // SetPixelFormat is once-per-HWND. Verify PFD_SUPPORT_OPENGL if already set.
        int32 pixelFormat = GetPixelFormat( hDC );
        bool  bFormatSet  = false;
        if ( pixelFormat != 0 )
        {
            PIXELFORMATDESCRIPTOR currentPfd{};
            DescribePixelFormat( hDC, pixelFormat, sizeof( currentPfd ), &currentPfd );
            if ( ( currentPfd.dwFlags & PFD_SUPPORT_OPENGL ) != 0 )
                bFormatSet = true;
        }

        if ( bFormatSet == false )
        {
            pixelFormat = ChoosePixelFormat( hDC, &pfd );
            if ( pixelFormat == 0 )
            {
                SW_LOG_ERROR( "ChoosePixelFormat failed (err=%#)", static_cast<uint32>( GetLastError() ) );
                ReleaseDC( hWnd, hDC );
                return false;
            }
            if ( SetPixelFormat( hDC, pixelFormat, &pfd ) == FALSE )
            {
                SW_LOG_ERROR( "SetPixelFormat failed (err=%#)", static_cast<uint32>( GetLastError() ) );
                ReleaseDC( hWnd, hDC );
                return false;
            }
        }

        HGLRC dummyContext = wglCreateContext( hDC );
        if ( dummyContext == nullptr || wglMakeCurrent( hDC, dummyContext ) == FALSE )
        {
            SW_LOG_ERROR( "wglCreateContext/MakeCurrent failed (err=%#)", static_cast<uint32>( GetLastError() ) );
            if ( dummyContext )
                wglDeleteContext( dummyContext );
            ReleaseDC( hWnd, hDC );
            return false;
        }

        PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>( wglGetProcAddress( "wglCreateContextAttribsARB" ) );
        HGLRC                             hRC{ nullptr };
        if ( wglCreateContextAttribsARB )
        {
            static const int32 kArrVersions[][2] = {
                {4, 6},
                {4, 5},
                {4, 3},
                {4, 1},
                {3, 3}
            };
            for ( const int32( &ver )[2] : kArrVersions )
            {
                int32 arrAttrib[] = {
                    WGL_CONTEXT_MAJOR_VERSION_ARB, ver[0],
                    WGL_CONTEXT_MINOR_VERSION_ARB, ver[1],
                    WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                    0 };
                hRC = wglCreateContextAttribsARB( hDC, nullptr, arrAttrib );
                if ( hRC != nullptr )
                {
                    SW_LOG_TRACE( "WGL core context %#.%# created", ver[0], ver[1] );
                    break;
                }
            }
            wglMakeCurrent( nullptr, nullptr );
            wglDeleteContext( dummyContext );
            if ( hRC != nullptr )
            {
                wglMakeCurrent( hDC, hRC );
                _pRenderContext = hRC;
            }
            else
            {
                SW_LOG_ERROR( "Failed to create WGL core context" );
                ReleaseDC( hWnd, hDC );
                return false;
            }
        }
        else
            _pRenderContext = dummyContext;

        _pWindowHandle  = hWnd;
        _pDeviceContext = hDC;

        outHandles._pDeviceContext = _pDeviceContext;
        outHandles._pRenderContext = _pRenderContext;
        return true;
    }

    void WglPlatformContext::destroy()
    {
        if ( _pDeviceContext != nullptr )
            wglMakeCurrent( nullptr, nullptr );
        if ( _pRenderContext != nullptr )
        {
            wglDeleteContext( static_cast<HGLRC>( _pRenderContext ) );
            _pRenderContext = nullptr;
        }
        if ( _pDeviceContext != nullptr && _pWindowHandle != nullptr )
        {
            ReleaseDC( static_cast<HWND>( _pWindowHandle ), static_cast<HDC>( _pDeviceContext ) );
            _pDeviceContext = nullptr;
        }
        _pWindowHandle = nullptr;
    }

    bool WglPlatformContext::makeCurrent()
    {
        if ( _pDeviceContext == nullptr || _pRenderContext == nullptr )
            return false;

        if ( wglMakeCurrent( static_cast<HDC>( _pDeviceContext ), static_cast<HGLRC>( _pRenderContext ) ) == FALSE )
        {
            SW_LOG_ERROR( "bindGraphicsContext wglMakeCurrent failed (err=%#)", static_cast<uint32>( GetLastError() ) );
            return false;
        }
        return true;
    }

    bool WglPlatformContext::isCurrent() const
    {
        if ( _pRenderContext == nullptr )
            return false;
        return wglGetCurrentContext() == static_cast<HGLRC>( _pRenderContext );
    }

    void WglPlatformContext::clearCurrent()
    {
        wglMakeCurrent( nullptr, nullptr );
    }

    void WglPlatformContext::reacquireForFrame()
    {
        // ImGui 멀티 뷰포트가 DC 를 바꿔 놓을 수 있어 매 프레임 되찾는다.
        if ( _pDeviceContext != nullptr && _pRenderContext != nullptr )
            wglMakeCurrent( static_cast<HDC>( _pDeviceContext ), static_cast<HGLRC>( _pRenderContext ) );
    }

    void WglPlatformContext::present()
    {
        if ( _pDeviceContext == nullptr )
            return;

        // 멀티 뷰포트가 DC 를 바꿨을 수 있으니 스왑 직전에 한 번 더 되찾는다(예전 코드와 같다).
        reacquireForFrame();
        SwapBuffers( static_cast<HDC>( _pDeviceContext ) );
    }

    void WglPlatformContext::setSwapInterval( bool bVSync )
    {
        using PFNWGLSWAPINTERVALEXTPROC                       = BOOL( WINAPI* )( int32 );
        static PFNWGLSWAPINTERVALEXTPROC s_wglSwapIntervalEXT = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>( wglGetProcAddress( "wglSwapIntervalEXT" ) );
        if ( s_wglSwapIntervalEXT != nullptr )
            s_wglSwapIntervalEXT( bVSync ? 1 : 0 );
    }
} // namespace sw
#endif
