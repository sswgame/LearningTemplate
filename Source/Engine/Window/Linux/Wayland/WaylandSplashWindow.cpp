#include "pch.h"

#include "Engine/Window/Linux/Wayland/WaylandSplashWindow.h"

#include "Core/Container/StringUtil.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE ) && defined( SW_WITH_WAYLAND )
    #include "Engine/Window/Linux/Wayland/WaylandShmBuffer.h"
    #include "Engine/Window/Linux/Wayland/WaylandTextPainter.h"

    #include <poll.h>
    #include <wayland-client.h>

    // 구성 때 wayland-scanner 가 만든다(cmake/Modules/Platform/Linux.cmake 의 프로토콜 XML 목록)
    #include "xdg-shell-client-protocol.h"

    #include <cstring>

namespace sw
{
    SW_LOG_CALLER( "WaylandSplash" );

    /** @brief 컴포지터 연결과 그리기 상태입니다. 리스너는 C 콜백이라 이 값의 주소를 사용자 값으로 받는다. */
    struct WaylandSplashWindow::State
    {
        wl_display*    _pDisplay{ nullptr };
        wl_registry*   _pRegistry{ nullptr };
        wl_compositor* _pCompositor{ nullptr };
        wl_shm*        _pShm{ nullptr };
        xdg_wm_base*   _pWmBase{ nullptr };
        wl_surface*    _pSurface{ nullptr };
        xdg_surface*   _pXdgSurface{ nullptr };
        xdg_toplevel*  _pToplevel{ nullptr };

        unique_ptr<WaylandShmBufferPool> _pPool;   ///< 그리기 버퍼(연결이 살아 있는 동안 지운다)
        WaylandTextPainter               _painter; ///< 상태 글을 그린다
        bool                             _bPainterReady{ false };
        bool                             _bConfigured{ false };
    };
} // namespace sw

namespace sw
{
    namespace
    {
        struct WaylandSplashWindowInternal
        {
            using State = WaylandSplashWindow::State;

            /** @brief 바인드할 판입니다 — 합성기 · 셸 · 공유 메모리는 이 창이 쓰는 요청이 모두 판 1 에 있다. */
            static constexpr uint32 kCompositorVersion = 1;
            static constexpr uint32 kShmVersion        = 1;
            static constexpr uint32 kWmBaseVersion     = 1;

            // 스플래시 팔레트(X11 판과 같은 값, 0x00RRGGBB).
            static constexpr uint32 kBackgroundColor = 0x181C24;
            static constexpr uint32 kTextColor       = 0xAEC3E6;
            static constexpr uint32 kBarTrackColor   = 0x202632;
            static constexpr uint32 kBarFillColor    = 0x4691FF;

            static constexpr uint32 kTextPixelSize  = 14;
            static constexpr int32  kMargin         = 32;
            static constexpr int32  kTextBaselineUp = 50; ///< 창 아래에서 상태 글 기준선까지
            static constexpr int32  kBarBottomUp    = 30; ///< 창 아래에서 막대 위쪽까지
            static constexpr int32  kBarHeight      = 4;

            static constexpr const utf8* kAppID = "sw-engine-splash";

            static void onRegistryGlobal( void* pData, wl_registry* pRegistry, uint32 name, const utf8* pInterface, uint32 version )
            {
                (void)version;
                State& state = *static_cast<State*>( pData );
                if ( std::strcmp( pInterface, wl_compositor_interface.name ) == 0 )
                    state._pCompositor = static_cast<wl_compositor*>( wl_registry_bind( pRegistry, name, &wl_compositor_interface, kCompositorVersion ) );
                else if ( std::strcmp( pInterface, wl_shm_interface.name ) == 0 )
                    state._pShm = static_cast<wl_shm*>( wl_registry_bind( pRegistry, name, &wl_shm_interface, kShmVersion ) );
                else if ( std::strcmp( pInterface, xdg_wm_base_interface.name ) == 0 )
                    state._pWmBase = static_cast<xdg_wm_base*>( wl_registry_bind( pRegistry, name, &xdg_wm_base_interface, kWmBaseVersion ) );
            }

            static void onRegistryGlobalRemove( void* pData, wl_registry* pRegistry, uint32 name )
            {
                (void)pData;
                (void)pRegistry;
                (void)name;
            }

            static void onWmBasePing( void* pData, xdg_wm_base* pWmBase, uint32 serial )
            {
                (void)pData;
                xdg_wm_base_pong( pWmBase, serial );
            }

            static void onXdgSurfaceConfigure( void* pData, xdg_surface* pXdgSurface, uint32 serial )
            {
                xdg_surface_ack_configure( pXdgSurface, serial );
                static_cast<State*>( pData )->_bConfigured = true;
            }

            static void onToplevelConfigure( void* pData, xdg_toplevel* pToplevel, int32 width, int32 height, wl_array* pStates )
            {
                (void)pData;
                (void)pToplevel;
                (void)width;
                (void)height;
                (void)pStates; // 크기는 최소 · 최대로 고정했다 — 컴포지터가 다른 크기를 권해도 따르지 않는다
            }

            static void onToplevelClose( void* pData, xdg_toplevel* pToplevel )
            {
                (void)pData;
                (void)pToplevel; // 스플래시는 사용자가 닫는 창이 아니다 — 앱이 `dismiss` 한다
            }

            static wl_registry_listener makeRegistryListener()
            {
                wl_registry_listener listener{};
                listener.global        = &onRegistryGlobal;
                listener.global_remove = &onRegistryGlobalRemove;
                return listener;
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

            static xdg_toplevel_listener makeToplevelListener()
            {
                xdg_toplevel_listener listener{};
                listener.configure = &onToplevelConfigure;
                listener.close     = &onToplevelClose;
                return listener;
            }

            /** @brief 쌓인 연결 이벤트(핑 · 버퍼 반납)를 막지 않고 처리합니다 — 스플래시에는 이벤트 루프가 없어 그릴 때마다 한 번 돈다(`WaylandWindow::processMessages` 와 같은 순서). */
            static void pumpEvents( wl_display* pDisplay )
            {
                wl_display_dispatch_pending( pDisplay );
                while ( wl_display_prepare_read( pDisplay ) != 0 )
                {
                    wl_display_dispatch_pending( pDisplay );
                }
                wl_display_flush( pDisplay );
                pollfd pollEntry{ wl_display_get_fd( pDisplay ), POLLIN, 0 };
                if ( poll( &pollEntry, 1, 0 ) > 0 )
                    (void)wl_display_read_events( pDisplay );
                else
                    wl_display_cancel_read( pDisplay );
                wl_display_dispatch_pending( pDisplay );
            }
        };

        const wl_registry_listener  s_registryListener   = WaylandSplashWindowInternal::makeRegistryListener();
        const xdg_wm_base_listener  s_wmBaseListener     = WaylandSplashWindowInternal::makeWmBaseListener();
        const xdg_surface_listener  s_xdgSurfaceListener = WaylandSplashWindowInternal::makeXdgSurfaceListener();
        const xdg_toplevel_listener s_toplevelListener   = WaylandSplashWindowInternal::makeToplevelListener();
    } // namespace

    WaylandSplashWindow::WaylandSplashWindow()
        : ISplashWindow{}
        , _pState{ make_unique<State>() }
    {
    }

    WaylandSplashWindow::~WaylandSplashWindow()
    {
        WaylandSplashWindow::dismiss();
    }

    bool WaylandSplashWindow::initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height )
    {
        using Internal = WaylandSplashWindowInternal;
        State& state   = *_pState;

        _status = StringUtil::isNullOrEmpty( pInitialStatus ) ? "Initializing..." : pInitialStatus;
        _width  = width;
        _height = height;
        if ( loadSplashImage() == false )
            SW_LOG_INFO( "Wayland splash: no splash image - the status text and progress bar are drawn on the plain background" ); // 로드가 이유를 알렸다
        createScaledImage();

        state._pDisplay = wl_display_connect( nullptr );
        if ( state._pDisplay == nullptr )
        {
            _bOpen = SW_FALSE;
            return false;
        }
        state._pRegistry = wl_display_get_registry( state._pDisplay );
        wl_registry_add_listener( state._pRegistry, &s_registryListener, &state );
        wl_display_roundtrip( state._pDisplay ); // 전역 객체 목록

        const bool bMissingGlobals = state._pCompositor == nullptr || state._pShm == nullptr || state._pWmBase == nullptr;
        if ( bMissingGlobals )
        {
            SW_LOG_ERROR( "Wayland splash: the compositor lacks wl_compositor, wl_shm or xdg_wm_base - no splash window" );
            dismiss();
            return false;
        }
        xdg_wm_base_add_listener( state._pWmBase, &s_wmBaseListener, &state );

        state._pSurface    = wl_compositor_create_surface( state._pCompositor );
        state._pXdgSurface = xdg_wm_base_get_xdg_surface( state._pWmBase, state._pSurface );
        state._pToplevel   = xdg_surface_get_toplevel( state._pXdgSurface );
        xdg_surface_add_listener( state._pXdgSurface, &s_xdgSurfaceListener, &state );
        xdg_toplevel_add_listener( state._pToplevel, &s_toplevelListener, &state );
        xdg_toplevel_set_title( state._pToplevel, StringUtil::isNullOrEmpty( pTitle ) ? "SW Engine" : pTitle );
        xdg_toplevel_set_app_id( state._pToplevel, Internal::kAppID );
        // 최소 · 최대 크기를 같게 두어 창 크기를 고정한다(Wayland 클라이언트는 자리를 정하지 못한다 — 컴포지터가 가운데쯤에 놓는다).
        xdg_toplevel_set_min_size( state._pToplevel, static_cast<int32>( _width ), static_cast<int32>( _height ) );
        xdg_toplevel_set_max_size( state._pToplevel, static_cast<int32>( _width ), static_cast<int32>( _height ) );
        wl_surface_commit( state._pSurface ); // 버퍼 없는 첫 commit 이 첫 configure 를 부른다

        int32 dispatchResult = 0;
        while ( state._bConfigured == false && dispatchResult != -1 )
        {
            dispatchResult = wl_display_dispatch( state._pDisplay );
        }
        if ( state._bConfigured == false )
        {
            SW_LOG_ERROR( "Wayland splash: the compositor closed the connection before configuring the window" );
            dismiss();
            return false;
        }

        state._pPool         = make_unique<WaylandShmBufferPool>();
        state._bPainterReady = state._painter.initialize( Internal::kTextPixelSize ); // 글꼴이 없으면 이유를 남기고 글 없이 그린다
        _bOpen               = SW_TRUE;
        updateStatus( _status.c_str() );
        return true;
    }

    void WaylandSplashWindow::updateStatus( const utf8* pStatus, float32 progress )
    {
        using Internal = WaylandSplashWindowInternal;
        if ( _bOpen == SW_FALSE )
            return;

        if ( StringUtil::isNullOrEmpty( pStatus ) == false )
            _status = pStatus;
        if ( progress >= 0.0f )
            _progress = ( progress > 1.0f ) ? 1.0f : progress;

        State&     state        = *_pState;
        const bool bNotDrawable = state._pDisplay == nullptr || state._pSurface == nullptr || state._pPool == nullptr;
        if ( bNotDrawable )
            return;

        Internal::pumpEvents( state._pDisplay ); // 앞서 보낸 버퍼의 반납과 핑 응답
        WaylandShmBuffer* const pBuffer = state._pPool->acquire( state._pShm, _width, _height );
        if ( pBuffer == nullptr )
            return; // 전부 컴포지터가 쥐고 있다 — 다음 갱신이 그린다

        uint32* const pPixels    = pBuffer->getPixels();
        const size_t  pixelCount = static_cast<size_t>( _width ) * static_cast<size_t>( _height );
        // 배경 → 이미지(창 크기로 줄여 둔 BGRA, 알파는 쓰지 않는다) → 상태 글 → 진행 막대
        if ( _listScaledPixel.size() >= pixelCount * 4u )
        {
            for ( size_t index = 0; index < pixelCount; ++index )
            {
                uint32 pixel = 0;
                std::memcpy( &pixel, _listScaledPixel.data() + index * 4u, sizeof( pixel ) );
                pPixels[index] = pixel & 0x00FFFFFFu;
            }
        }
        else
        {
            for ( size_t index = 0; index < pixelCount; ++index )
            {
                pPixels[index] = Internal::kBackgroundColor;
            }
        }

        const int32 height = static_cast<int32>( _height );
        if ( state._bPainterReady )
            (void)state._painter.drawText( pPixels, _width, _height, Internal::kMargin, height - Internal::kTextBaselineUp, _status, Internal::kTextColor );

        const int32   totalBarWidth   = static_cast<int32>( _width ) - Internal::kMargin * 2;
        const float32 clampedProgress = ( _progress < 0.0f ) ? 0.0f : ( ( _progress > 1.0f ) ? 1.0f : _progress );
        const int32   fillWidth       = static_cast<int32>( static_cast<float32>( totalBarWidth ) * clampedProgress );
        const int32   barTop          = height - Internal::kBarBottomUp;
        for ( int32 row = 0; row < Internal::kBarHeight; ++row )
        {
            const int32 targetY = barTop + row;
            if ( targetY < 0 || targetY >= height )
                continue;
            uint32* const pRow = pPixels + static_cast<size_t>( targetY ) * static_cast<size_t>( _width );
            for ( int32 column = 0; column < totalBarWidth; ++column )
            {
                pRow[Internal::kMargin + column] = column < fillWidth ? Internal::kBarFillColor : Internal::kBarTrackColor;
            }
        }

        wl_surface_attach( state._pSurface, pBuffer->getBuffer(), 0, 0 );
        wl_surface_damage( state._pSurface, 0, 0, static_cast<int32>( _width ), height );
        pBuffer->markBusy();
        wl_surface_commit( state._pSurface );
        wl_display_flush( state._pDisplay );
    }

    void WaylandSplashWindow::setProgress( float32 progress )
    {
        updateStatus( nullptr, progress );
    }

    void WaylandSplashWindow::dismiss()
    {
        State& state = *_pState;
        // 버퍼를 먼저 지운다(`wl_buffer` 는 연결이 살아 있을 때만 지울 수 있다). 객체는 만든 순서의 반대로 지운다(toplevel → xdg_surface → surface).
        state._pPool.reset();
        if ( state._pToplevel != nullptr )
            xdg_toplevel_destroy( state._pToplevel );
        if ( state._pXdgSurface != nullptr )
            xdg_surface_destroy( state._pXdgSurface );
        if ( state._pSurface != nullptr )
            wl_surface_destroy( state._pSurface );
        if ( state._pWmBase != nullptr )
            xdg_wm_base_destroy( state._pWmBase );
        if ( state._pShm != nullptr )
            wl_shm_destroy( state._pShm );
        if ( state._pCompositor != nullptr )
            wl_compositor_destroy( state._pCompositor );
        if ( state._pRegistry != nullptr )
            wl_registry_destroy( state._pRegistry );
        if ( state._pDisplay != nullptr )
        {
            wl_display_flush( state._pDisplay );
            wl_display_disconnect( state._pDisplay );
        }
        state._pToplevel   = nullptr;
        state._pXdgSurface = nullptr;
        state._pSurface    = nullptr;
        state._pWmBase     = nullptr;
        state._pShm        = nullptr;
        state._pCompositor = nullptr;
        state._pRegistry   = nullptr;
        state._pDisplay    = nullptr;
        _bOpen             = SW_FALSE;
    }
} // namespace sw

#else

namespace sw
{
    /** @brief Wayland 를 짓지 않은 구성의 빈 상태입니다 — 이 구성은 `createPlatformSplash` 가 이 창을 만들지 않는다. */
    struct WaylandSplashWindow::State
    {
    };

    WaylandSplashWindow::WaylandSplashWindow()
        : ISplashWindow{}
        , _pState{ make_unique<State>() }
    {
    }

    WaylandSplashWindow::~WaylandSplashWindow() = default;

    bool WaylandSplashWindow::initialize( const utf8*, const utf8*, uint32, uint32 )
    {
        return false;
    }

    void WaylandSplashWindow::updateStatus( const utf8*, float32 )
    {
    }

    void WaylandSplashWindow::setProgress( float32 )
    {
    }

    void WaylandSplashWindow::dismiss()
    {
    }
} // namespace sw

#endif
