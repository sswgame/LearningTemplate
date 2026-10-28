#include "pch.h"

#include "Engine/Graphics/RHI/GL/Platform/EGLPlatformContext.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_WAYLAND )
    #include "Core/Log/Logger.h"

    #include "Engine/Graphics/RHI/RHITypes.h"

    // EGL 헤더가 X11 타입을 끌어오지 않게 한다(네이티브 타입은 void* 가 된다).
    #define EGL_NO_X11 1
    #include <EGL/egl.h>
    #include <EGL/eglext.h>
    #include <wayland-egl.h>

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

    EGLPlatformContext::EGLPlatformContext()
        : _pEGLDisplay{ nullptr }
        , _pEGLSurface{ nullptr }
        , _pEGLContext{ nullptr }
        , _pNativeWindow{ nullptr }
    {
    }

    EGLPlatformContext::~EGLPlatformContext()
    {
        destroy();
    }

    // 길이 유지(긴 함수 정리 기준): 디스플레이 · 설정 · 윈도 서피스 · 컨텍스트 생성의 순서가 곧 의미라 나누지 않는다. 실패마다 만든 것을 `destroy` 가 되돌린다.
    bool EGLPlatformContext::initialize( const RHISwapChainDesc& desc, OpenGLContextHandles& outHandles )
    {
        if ( desc._surfaceKind != NativeSurfaceKind::Wayland || desc._pWindowDisplay == nullptr || desc._pWindowHandle == nullptr )
        {
            SW_LOG_ERROR( "EGL needs a Wayland surface (wl_display and wl_surface)" );
            return false;
        }

        // 플랫폼 디스플레이(EGL 1.5)로 연다. `eglGetDisplay` 처럼 네이티브 타입을 추측하지 않는다.
        EGLDisplay display = eglGetPlatformDisplay( EGL_PLATFORM_WAYLAND_KHR, desc._pWindowDisplay, nullptr );
        if ( display == EGL_NO_DISPLAY )
        {
            SW_LOG_ERROR( "eglGetPlatformDisplay(wayland) failed (EGL error %#)", static_cast<int32>( eglGetError() ) );
            return false;
        }
        EGLint eglMajor{ 0 };
        EGLint eglMinor{ 0 };
        if ( eglInitialize( display, &eglMajor, &eglMinor ) == EGL_FALSE )
        {
            SW_LOG_ERROR( "eglInitialize failed (EGL error %#)", static_cast<int32>( eglGetError() ) );
            return false;
        }
        _pEGLDisplay = display;
        // 데스크톱 GL 을 쓴다(기본 API 는 GLES). 이 호출은 부른 스레드에만 걸린다.
        if ( eglBindAPI( EGL_OPENGL_API ) == EGL_FALSE )
        {
            SW_LOG_ERROR( "eglBindAPI(EGL_OPENGL_API) failed - the EGL driver has no desktop OpenGL" );
            destroy();
            return false;
        }

        // 깊이 · 스텐실은 GLX 가 고르는 창 비주얼처럼 24/8 이상을 요구한다. 알파는 요구하지 않는다(없으면 컴포지터가 불투명으로 합친다).
        const EGLint arrConfigAttrib[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE };
        EGLConfig    config{ nullptr };
        EGLint       configCount{ 0 };
        if ( eglChooseConfig( display, arrConfigAttrib, &config, 1, &configCount ) == EGL_FALSE || configCount <= 0 )
        {
            SW_LOG_ERROR( "eglChooseConfig found no window config for desktop OpenGL (EGL error %#)", static_cast<int32>( eglGetError() ) );
            destroy();
            return false;
        }

        // `wl_egl_window` 는 `wl_surface` 에 버퍼를 대는 어댑터다. 크기는 스왑체인 서술값(컴포지터가 준 창 크기)이고 이후에는 `resize` 가 따른다.
        wl_egl_window* pNativeWindow = wl_egl_window_create( static_cast<wl_surface*>( desc._pWindowHandle ), static_cast<int>( desc._width ), static_cast<int>( desc._height ) );
        if ( pNativeWindow == nullptr )
        {
            SW_LOG_ERROR( "wl_egl_window_create failed (%#x%#)", desc._width, desc._height );
            destroy();
            return false;
        }
        _pNativeWindow     = pNativeWindow;
        EGLSurface surface = eglCreatePlatformWindowSurface( display, config, pNativeWindow, nullptr );
        if ( surface == EGL_NO_SURFACE )
        {
            SW_LOG_ERROR( "eglCreatePlatformWindowSurface failed (EGL error %#)", static_cast<int32>( eglGetError() ) );
            destroy();
            return false;
        }
        _pEGLSurface = surface;

        // 4.6 을 먼저 시도하고 낮은 버전으로 물러난다(GLX 와 같은 목록).
        static const EGLint kArrVersions[][2] = {
            {4, 6},
            {4, 5},
            {4, 3},
            {4, 2},
            {4, 1},
            {4, 0},
            {3, 3}
        };
        EGLContext context{ EGL_NO_CONTEXT };
        for ( const EGLint( &ver )[2] : kArrVersions )
        {
            const EGLint arrContextAttrib[] = {
                EGL_CONTEXT_MAJOR_VERSION, ver[0],
                EGL_CONTEXT_MINOR_VERSION, ver[1],
                EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
    #if !defined( SW_SHIPPING )
                // 디버그 컨텍스트는 비-Shipping 에서만 켠다(WGL · GLX 와 같다). KHR_debug 메시지는 이 비트가 있어야 드라이버가 만들 의무가 있다.
                EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE,
    #endif
                EGL_NONE };
            context = eglCreateContext( display, config, EGL_NO_CONTEXT, arrContextAttrib );
            if ( context != EGL_NO_CONTEXT )
            {
                SW_LOG_TRACE( "EGL core context %#.%#", static_cast<int32>( ver[0] ), static_cast<int32>( ver[1] ) );
                break;
            }
        }
        if ( context == EGL_NO_CONTEXT )
        {
            SW_LOG_ERROR( "Failed to create an EGL core OpenGL context 3.3 or newer (EGL error %#) - use the Vulkan backend", static_cast<int32>( eglGetError() ) );
            destroy();
            return false;
        }
        _pEGLContext = context;

        if ( eglMakeCurrent( display, surface, surface, context ) == EGL_FALSE )
        {
            SW_LOG_ERROR( "eglMakeCurrent failed (EGL error %#)", static_cast<int32>( eglGetError() ) );
            destroy();
            return false;
        }

        outHandles._pDeviceContext = _pEGLDisplay;
        outHandles._pRenderContext = _pEGLContext;
        return true;
    }

    void EGLPlatformContext::destroy()
    {
        EGLDisplay display = static_cast<EGLDisplay>( _pEGLDisplay );
        if ( display != nullptr )
        {
            eglMakeCurrent( display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
            if ( _pEGLContext != nullptr )
                eglDestroyContext( display, static_cast<EGLContext>( _pEGLContext ) );
            if ( _pEGLSurface != nullptr )
                eglDestroySurface( display, static_cast<EGLSurface>( _pEGLSurface ) );
        }
        // 윈도 서피스를 없앤 뒤에 네이티브 창을 없앤다(순서가 반대면 서피스가 해제된 창을 가리킨다).
        if ( _pNativeWindow != nullptr )
            wl_egl_window_destroy( static_cast<wl_egl_window*>( _pNativeWindow ) );
        if ( display != nullptr )
            eglTerminate( display );
        _pEGLDisplay   = nullptr;
        _pEGLSurface   = nullptr;
        _pEGLContext   = nullptr;
        _pNativeWindow = nullptr;
    }

    bool EGLPlatformContext::makeCurrent()
    {
        if ( _pEGLDisplay == nullptr || _pEGLContext == nullptr )
            return false;

        // 로그를 남기지 않는다. 경합은 정상이고(다른 스레드가 쥐면 EGL_BAD_ACCESS) 알릴 책임은 부르는 쪽에 있다(WGL · GLX 와 같은 규약).
        return eglMakeCurrent( static_cast<EGLDisplay>( _pEGLDisplay ), static_cast<EGLSurface>( _pEGLSurface ), static_cast<EGLSurface>( _pEGLSurface ), static_cast<EGLContext>( _pEGLContext ) ) != EGL_FALSE;
    }

    bool EGLPlatformContext::isCurrent() const
    {
        if ( _pEGLContext == nullptr )
            return false;
        return eglGetCurrentContext() == static_cast<EGLContext>( _pEGLContext );
    }

    void EGLPlatformContext::clearCurrent()
    {
        if ( _pEGLDisplay != nullptr )
            eglMakeCurrent( static_cast<EGLDisplay>( _pEGLDisplay ), EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
    }

    void EGLPlatformContext::reacquireForFrame()
    {
        // EGL 은 프레임 시작에 컨텍스트를 다시 바인딩하지 않는다(GLX 와 같다). Wayland 에디터는 멀티 뷰포트를 끄므로 DC 를 바꿔 놓는 쪽이 없다.
    }

    void EGLPlatformContext::resize( uint32 width, uint32 height )
    {
        // 컴포지터가 준 새 크기를 렌더링 표면에 알린다. 이것을 하지 않으면 버퍼가 창 크기를 따르지 않는다. 다음 `eglSwapBuffers` 에서 새 버퍼가 잡힌다.
        if ( _pNativeWindow != nullptr )
            wl_egl_window_resize( static_cast<wl_egl_window*>( _pNativeWindow ), static_cast<int>( width ), static_cast<int>( height ), 0, 0 );
    }

    void EGLPlatformContext::present()
    {
        if ( _pEGLDisplay == nullptr || _pEGLSurface == nullptr )
            return;
        eglSwapBuffers( static_cast<EGLDisplay>( _pEGLDisplay ), static_cast<EGLSurface>( _pEGLSurface ) );
    }

    void EGLPlatformContext::setSwapInterval( bool bVSync )
    {
        // Wayland 의 EGL 은 기본 간격이 1 이라 `eglSwapBuffers` 가 프레임 콜백을 기다린다. VSync 를 끄려면 0 을 명시해야 한다. 컨텍스트가 current 인 스레드에서 불린다.
        if ( _pEGLDisplay != nullptr )
            eglSwapInterval( static_cast<EGLDisplay>( _pEGLDisplay ), bVSync ? 1 : 0 );
    }
} // namespace sw
#endif
