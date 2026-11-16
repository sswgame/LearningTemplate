#include "pch.h"

#include "Engine/Window/Linux/Wayland/WaylandShell.h"

#if defined( SW_WITH_WAYLAND )
    #include <poll.h>
    #include <wayland-client.h>

    // 구성 때 wayland-scanner 가 만든다(cmake/Toolchain/Platform/Linux.cmake 의 프로토콜 XML 목록)
    #include "xdg-shell-client-protocol.h"

namespace sw
{
    namespace
    {
        struct WaylandShellInternal
        {
            static void onWmBasePing( void* pData, xdg_wm_base* pWmBase, uint32 serial )
            {
                (void)pData;
                xdg_wm_base_pong( pWmBase, serial );
            }

            static void onXdgSurfaceConfigure( void* pData, xdg_surface* pXdgSurface, uint32 serial )
            {
                xdg_surface_ack_configure( pXdgSurface, serial );
                *static_cast<bool*>( pData ) = true;
            }

            static xdg_wm_base_listener makeWmBaseListener()
            {
                xdg_wm_base_listener listener{};
                listener.ping = &onWmBasePing;
                return listener;
            }

            static xdg_surface_listener makeXdgSurfaceListener()
            {
                xdg_surface_listener listener{};
                listener.configure = &onXdgSurfaceConfigure;
                return listener;
            }
        };

        const xdg_wm_base_listener s_wmBaseListener     = WaylandShellInternal::makeWmBaseListener();
        const xdg_surface_listener s_xdgSurfaceListener = WaylandShellInternal::makeXdgSurfaceListener();
    } // namespace

    void WaylandShell::answerPings( xdg_wm_base* pWmBase )
    {
        xdg_wm_base_add_listener( pWmBase, &s_wmBaseListener, nullptr );
    }

    void WaylandShell::acknowledgeConfigures( xdg_surface* pXdgSurface, bool* pbConfigured )
    {
        xdg_surface_add_listener( pXdgSurface, &s_xdgSurfaceListener, pbConfigured );
    }

    bool WaylandShell::waitForFirstConfigure( wl_display* pDisplay, const bool& bConfigured )
    {
        int32 dispatchResult = 0;
        while ( bConfigured == false && dispatchResult != -1 )
        {
            dispatchResult = wl_display_dispatch( pDisplay );
        }
        return bConfigured;
    }

    bool WaylandShell::pollEvents( wl_display* pDisplay )
    {
        wl_display_dispatch_pending( pDisplay );
        while ( wl_display_prepare_read( pDisplay ) != 0 )
        {
            wl_display_dispatch_pending( pDisplay );
        }
        wl_display_flush( pDisplay );
        pollfd pollEntry{ wl_display_get_fd( pDisplay ), POLLIN, 0 };
        if ( poll( &pollEntry, 1, 0 ) > 0 )
            (void)wl_display_read_events( pDisplay ); // 실패는 다음 dispatch 가 -1 로 알린다
        else
            wl_display_cancel_read( pDisplay );
        return wl_display_dispatch_pending( pDisplay ) != -1;
    }
} // namespace sw
#endif
