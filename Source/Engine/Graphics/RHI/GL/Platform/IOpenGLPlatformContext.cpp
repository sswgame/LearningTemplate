#include "pch.h"

#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"

#include "Core/Log/Logger.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Graphics/RHI/GL/Platform/WGLPlatformContext.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Engine/Graphics/RHI/GL/Platform/GLXPlatformContext.h"
    #if defined( SW_WITH_WAYLAND )
        #include "Engine/Graphics/RHI/GL/Platform/EGLPlatformContext.h"
    #endif
#else
    #error "Unsupported platform"
#endif

namespace sw
{
    void* IOpenGLPlatformContext::createWindowSurface( const RHISwapChainDesc& desc )
    {
        (void)desc;
        SW_LOG_ERROR_ID( "OpenGL.WindowSurfaceUnsupported", "This OpenGL platform context cannot draw a second window" );
        return nullptr;
    }

    bool IOpenGLPlatformContext::makeCurrentOnWindowSurface( void* pSurface )
    {
        return pSurface == nullptr && makeCurrent();
    }

    unique_ptr<IOpenGLPlatformContext> IOpenGLPlatformContext::create( NativeSurfaceKind surfaceKind )
    {
        // 플랫폼을 하나 더 지원하려면 **여기 한 줄과 파일 한 쌍**이면 된다.
#if defined( SW_PLATFORM_WINDOWS )
        (void)surfaceKind;
        return make_unique<WGLPlatformContext>();
#elif defined( SW_PLATFORM_LINUX )
        // 창 시스템이 문맥 API 를 정한다 — X11 은 GLX, Wayland 는 EGL. 다른 쪽으로 대신하지 않는다.
        if ( surfaceKind == NativeSurfaceKind::X11 )
            return make_unique<GLXPlatformContext>();
    #if defined( SW_WITH_WAYLAND )
        if ( surfaceKind == NativeSurfaceKind::Wayland )
            return make_unique<EGLPlatformContext>();
    #endif
        SW_LOG_ERROR_ID( "OpenGL.OpenGLLinuxNeedsX112", "OpenGL on Linux needs an X11 window (GLX) or, in a build with SW_ENABLE_WAYLAND, a Wayland window (EGL)" );
        return nullptr;
#else
    #error "Unsupported platform"
#endif
    }
} // namespace sw
