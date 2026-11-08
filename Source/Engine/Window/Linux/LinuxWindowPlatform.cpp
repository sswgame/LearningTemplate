#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Process/ProcessEnvironment.h"

#include "Engine/Window/Linux/LinuxWindowBackend.h"
#include "Engine/Window/Linux/Wayland/WaylandSplashWindow.h"
#include "Engine/Window/Linux/Wayland/WaylandWindow.h"
#include "Engine/Window/Linux/X11SplashWindow.h"
#include "Engine/Window/Linux/X11Window.h"
#include "Engine/Window/WindowPlatform.h"

#if defined( SW_WITH_CLIENT_CODE )

namespace sw
{
    namespace
    {
        struct LinuxWindowPlatformInternal
        {
            static unique_ptr<IWindow> createWindow()
            {
                // 창 계층은 환경 변수 하나가 고른다(`SW_WINDOW_BACKEND` = x11 | wayland, 없으면 x11 — XWayland 데스크톱의 지금까지의 길). 모르는 값 · 짓지 않은 계층은 오류로 멈춘다.
                // 읽는 곳은 스플래시 창과 같다(`tryReadLinuxWindowBackend`).
                LinuxWindowBackend backend = LinuxWindowBackend::X11;
                if ( tryReadLinuxWindowBackend( backend ) == false )
                    return nullptr;
                if ( backend == LinuxWindowBackend::X11 )
                    return make_unique<X11Window>();
    #if defined( SW_WITH_WAYLAND )
                SW_LOG_INFO_ID( "Window.WindowBackendWaylandSW", "Window backend: Wayland (SW_WINDOW_BACKEND=wayland)" );
                return make_unique<WaylandWindow>();
    #else
                SW_LOG_ERROR_ID( "Window.SWWINDOWBACKENDWayland", "SW_WINDOW_BACKEND=wayland but this build has no Wayland window layer (configure with -DSW_ENABLE_WAYLAND=ON)" );
                return nullptr;
    #endif
            }

            static unique_ptr<ISplashWindow> createSplash()
            {
                // 메인 창과 같은 계층을 고른다(`SW_WINDOW_BACKEND`). 모르는 값이면 읽는 쪽이 오류를 남겼고 `isSplashSupported` 가 false 라 여기 오지 않는다.
                LinuxWindowBackend backend = LinuxWindowBackend::X11;
                if ( tryReadLinuxWindowBackend( backend ) == false )
                    return nullptr;
                if ( backend == LinuxWindowBackend::X11 )
                    return make_unique<X11SplashWindow>();
    #if defined( SW_WITH_WAYLAND )
                return make_unique<WaylandSplashWindow>();
    #else
                return nullptr;
    #endif
            }

            static bool isSplashSupported()
            {
                LinuxWindowBackend backend = LinuxWindowBackend::X11;
                if ( tryReadLinuxWindowBackend( backend ) == false )
                    return false;
                string display;
                if ( backend == LinuxWindowBackend::Wayland )
                {
    #if defined( SW_WITH_WAYLAND )
                    // Wayland 스플래시는 `wl_display_connect( nullptr )` 로 연결한다 — `WAYLAND_DISPLAY` 가 없으면 기본 소켓 이름을 찾지만, 메인 창과 같은 조건(변수 있음)으로 맞춘다.
                    return ProcessEnvironment::tryReadNonEmpty( "WAYLAND_DISPLAY", display );
    #else
                    return false; // 이 빌드에 Wayland 창 계층이 없다(메인 창 생성이 오류로 알린다)
    #endif
                }
                // X11 스플래시는 `XOpenDisplay( nullptr )` 로 연결한다 — `DISPLAY` 가 없으면 열 수 없다(순수 Wayland 세션에서 X11 을 골랐을 때).
                return ProcessEnvironment::tryReadNonEmpty( "DISPLAY", display );
            }
        };

        const WindowPlatformFactory     s_linuxWindowFactory{ &LinuxWindowPlatformInternal::createWindow, &LinuxWindowPlatformInternal::createSplash, &LinuxWindowPlatformInternal::isSplashSupported };
        const WindowPlatform::Registrar s_linuxWindowRegistrar{ s_linuxWindowFactory };
    } // namespace
} // namespace sw

#endif
