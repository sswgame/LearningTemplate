#include "pch.h"

#include "Engine/Graphics/RHI/GL/Platform/WGLPlatformContext.h"

#include "Core/Foundation/BuildInfo.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Log/Logger.h"

    #include "Engine/Graphics/RHI/GL/OpenGLRHIDeviceInternal.h"
    #include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

    WGLPlatformContext::WGLPlatformContext()
        : _pWindowHandle{ nullptr }
        , _pDeviceContext{ nullptr }
        , _pRenderContext{ nullptr }
    {
    }

    WGLPlatformContext::~WGLPlatformContext()
    {
        destroy();
    }

    // 길이 유지(긴 함수 정리 기준): 픽셀 포맷 · 임시 컨텍스트 · 실제 컨텍스트 생성의 순서가 곧 의미라 나누지 않는다.
    bool WGLPlatformContext::initialize( const RHISwapChainDesc& desc, OpenGLContextHandles& outHandles )
    {
        // WGL 은 Win32 창에만 문맥을 건다 — 핸들이 Win32 표면의 것이 아니면 HWND 로 읽지 않고 오류로 멈춘다.
        void* pWindowHandle{ nullptr };
        if ( desc.tryGetWin32Handle( pWindowHandle ) == false )
        {
            SW_LOG_ERROR( "OpenGL on Windows needs a Win32 window (WGL) - this surface is not Win32" );
            return false;
        }
        HWND hWnd = static_cast<HWND>( pWindowHandle );
        HDC  hDC  = GetDC( hWnd );

        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize                 = sizeof( PIXELFORMATDESCRIPTOR );
        pfd.nVersion              = 1;
        pfd.dwFlags               = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType            = PFD_TYPE_RGBA;
        pfd.cColorBits            = 32;
        pfd.cDepthBits            = 24;
        pfd.cStencilBits          = 8;

        // SetPixelFormat 은 HWND 마다 한 번뿐이다. 이미 설정돼 있으면 PFD_SUPPORT_OPENGL 인지 확인한다.
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
                // 디버그 컨텍스트는 진단을 켜는 빌드에서만 켠다(`build::kShipping` 가 아닌 구성). KHR_debug 메시지는 이 비트가 있어야 드라이버가 만들 의무가 있다
                // (OpenGLRHIDeviceInit 이 콜백을 등록한다). Shipping 은 검증 비용을 부담하지 않는다.
                int32 arrAttrib[] = {
                    WGL_CONTEXT_MAJOR_VERSION_ARB, ver[0],
                    WGL_CONTEXT_MINOR_VERSION_ARB, ver[1],
                    WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                    WGL_CONTEXT_FLAGS_ARB, build::kShipping == false ? WGL_CONTEXT_DEBUG_BIT_ARB : 0,
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

    void WGLPlatformContext::destroy()
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

    bool WGLPlatformContext::makeCurrent()
    {
        if ( _pDeviceContext == nullptr || _pRenderContext == nullptr )
            return false;

        // **로그를 남기지 않는다.** 다른 스레드가 컨텍스트를 가지고 있는 것(ERROR_BUSY)은 정상
        // 경합이고, 기다릴지 포기할지는 부르는 쪽이 정한다. 실패를 알려야 하는 쪽은
        // `OpenGLRHIDevice::bindGraphicsContext` 가, 기다리는 쪽은
        // `acquireGraphicsContextBlocking` 이 각자 로그를 남긴다.
        return wglMakeCurrent( static_cast<HDC>( _pDeviceContext ), static_cast<HGLRC>( _pRenderContext ) ) != FALSE;
    }

    bool WGLPlatformContext::isCurrent() const
    {
        if ( _pRenderContext == nullptr )
            return false;
        return wglGetCurrentContext() == static_cast<HGLRC>( _pRenderContext );
    }

    void WGLPlatformContext::clearCurrent()
    {
        wglMakeCurrent( nullptr, nullptr );
    }

    void WGLPlatformContext::reacquireForFrame()
    {
        // ImGui 멀티 뷰포트가 DC 를 바꿔 놓을 수 있어 매 프레임 다시 가져온다.
        if ( _pDeviceContext != nullptr && _pRenderContext != nullptr )
            wglMakeCurrent( static_cast<HDC>( _pDeviceContext ), static_cast<HGLRC>( _pRenderContext ) );
    }

    void WGLPlatformContext::present()
    {
        if ( _pDeviceContext == nullptr )
            return;

        // 멀티 뷰포트가 DC 를 바꿨을 수 있으니 스왑 직전에 한 번 더 다시 가져온다.
        reacquireForFrame();
        SwapBuffers( static_cast<HDC>( _pDeviceContext ) );
    }

    void WGLPlatformContext::setSwapInterval( bool bVSync )
    {
        using PFNWGLSWAPINTERVALEXTPROC                       = BOOL( WINAPI* )( int32 );
        static PFNWGLSWAPINTERVALEXTPROC s_wglSwapIntervalEXT = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>( wglGetProcAddress( "wglSwapIntervalEXT" ) );
        if ( s_wglSwapIntervalEXT != nullptr )
            s_wglSwapIntervalEXT( bVSync ? 1 : 0 );
    }
} // namespace sw
#endif
