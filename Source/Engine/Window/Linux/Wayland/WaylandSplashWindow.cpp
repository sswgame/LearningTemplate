#include "pch.h"

#include "Engine/Window/Linux/Wayland/WaylandSplashWindow.h"

#include "Core/String/StringUtil.h"

#if defined( SW_WITH_WAYLAND )
    #include "Engine/Window/Linux/Wayland/WaylandShell.h"
    #include "Engine/Window/Linux/Wayland/WaylandShmBuffer.h"
    #include "Engine/Window/Linux/Wayland/WaylandTextPainter.h"

    #include <wayland-client.h>

    // 구성 때 wayland-scanner 가 만든다(cmake/Toolchain/Platform/Linux.cmake 의 프로토콜 XML 목록)
    #include "xdg-shell-client-protocol.h"

    #include <cstring>

namespace sw
{
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
        bool                             _bConfigured{ SW_FALSE };
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

            static constexpr const utf8* kAppID          = "sw-engine-splash";
            static constexpr int         kDispatchFailed = -1; ///< `wl_display_dispatch_pending` 의 실패(연결이 끊겼다)

            static void onRegistryGlobal( void* pData, wl_registry* pRegistry, uint32 name, const utf8* pInterface, uint32 version )
            {
                (void)version;
                State& state = *static_cast<State*>( pData );
                if ( StringUtil::equals( pInterface, wl_compositor_interface.name ) )
                    state._pCompositor = static_cast<wl_compositor*>( wl_registry_bind( pRegistry, name, &wl_compositor_interface, kCompositorVersion ) );
                else if ( StringUtil::equals( pInterface, wl_shm_interface.name ) )
                    state._pShm = static_cast<wl_shm*>( wl_registry_bind( pRegistry, name, &wl_shm_interface, kShmVersion ) );
                else if ( StringUtil::equals( pInterface, xdg_wm_base_interface.name ) )
                    state._pWmBase = static_cast<xdg_wm_base*>( wl_registry_bind( pRegistry, name, &xdg_wm_base_interface, kWmBaseVersion ) );
            }

            static void onRegistryGlobalRemove( void* pData, wl_registry* pRegistry, uint32 name )
            {
                (void)pData;
                (void)pRegistry;
                (void)name;
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

            static xdg_toplevel_listener makeToplevelListener()
            {
                xdg_toplevel_listener listener{};
                listener.configure = &onToplevelConfigure;
                listener.close     = &onToplevelClose;
                return listener;
            }
        };

        const wl_registry_listener  s_registryListener = WaylandSplashWindowInternal::makeRegistryListener();
        const xdg_toplevel_listener s_toplevelListener = WaylandSplashWindowInternal::makeToplevelListener();
    } // namespace

    WaylandSplashWindow::WaylandSplashWindow()
        : SplashWindowBase{}
        , _pState{ make_unique<State>() }
        , _splashThread{}
    {
    }

    WaylandSplashWindow::~WaylandSplashWindow()
    {
        WaylandSplashWindow::dismiss();
    }

    bool WaylandSplashWindow::initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height )
    {
        _status = StringUtil::isNullOrEmpty( pInitialStatus ) ? "Initializing..." : pInitialStatus;
        _width  = width;
        _height = height;
        createScaledImage(); // 이미지는 부른 쪽이 먼저 읽어 두었다(`SplashWindow::initialize`) — 없으면 글과 진행 막대만 그린다

        const string                  title = StringUtil::isNullOrEmpty( pTitle ) ? string( "SW Engine" ) : string( pTitle );
        LinuxSplashThread::PaintState initialState;
        initialState._status   = _status;
        initialState._progress = _progress;
        const bool bOpened     = _splashThread.start( [this, title]( LinuxSplashThread& splashThread )
        { runEventLoop( splashThread, title ); }, initialState );
        _bOpen                 = bOpened ? SW_TRUE : SW_FALSE;
        return bOpened;
    }

    void WaylandSplashWindow::runEventLoop( LinuxSplashThread& splashThread, const string& title )
    {
        using Internal = WaylandSplashWindowInternal;
        State& state   = *_pState;

        // 연결은 이 스레드에서 열고 닫는다 — 모든 객체가 기본 큐 하나에서 이 스레드에만 dispatch 된다(libwayland 는 한 연결을 여러 스레드가 쓸 수 있지만
        // 그때는 스레드마다 큐와 `wl_display_prepare_read` 를 지켜야 한다 — https://wayland.freedesktop.org/docs/html/apb.html#Client-classwl__display).
        state._pDisplay = wl_display_connect( nullptr );
        if ( state._pDisplay == nullptr )
        {
            splashThread.markFailed();
            return;
        }
        state._pRegistry = wl_display_get_registry( state._pDisplay );
        wl_registry_add_listener( state._pRegistry, &s_registryListener, &state );
        wl_display_roundtrip( state._pDisplay ); // 전역 객체 목록

        const bool bMissingGlobals = state._pCompositor == nullptr || state._pShm == nullptr || state._pWmBase == nullptr;
        if ( bMissingGlobals )
        {
            SW_LOG_ERROR_ID( "WaylandSplash.WaylandSplashCompositorLacks", "Wayland splash: the compositor lacks wl_compositor, wl_shm or xdg_wm_base - no splash window" );
            releaseConnection();
            splashThread.markFailed();
            return;
        }
        WaylandShell::answerPings( state._pWmBase );

        state._pSurface    = wl_compositor_create_surface( state._pCompositor );
        state._pXdgSurface = xdg_wm_base_get_xdg_surface( state._pWmBase, state._pSurface );
        state._pToplevel   = xdg_surface_get_toplevel( state._pXdgSurface );
        WaylandShell::acknowledgeConfigures( state._pXdgSurface, &state._bConfigured );
        xdg_toplevel_add_listener( state._pToplevel, &s_toplevelListener, &state );
        xdg_toplevel_set_title( state._pToplevel, title.c_str() );
        xdg_toplevel_set_app_id( state._pToplevel, Internal::kAppID );
        // 최소 · 최대 크기를 같게 두어 창 크기를 고정한다(Wayland 클라이언트는 자리를 정하지 못한다 — 컴포지터가 가운데쯤에 놓는다).
        xdg_toplevel_set_min_size( state._pToplevel, static_cast<int32>( _width ), static_cast<int32>( _height ) );
        xdg_toplevel_set_max_size( state._pToplevel, static_cast<int32>( _width ), static_cast<int32>( _height ) );
        wl_surface_commit( state._pSurface ); // 버퍼 없는 첫 commit 이 첫 configure 를 부른다

        if ( WaylandShell::waitForFirstConfigure( state._pDisplay, state._bConfigured ) == false )
        {
            SW_LOG_ERROR_ID( "WaylandSplash.WaylandSplashCompositorClosed", "Wayland splash: the compositor closed the connection before configuring the window" );
            releaseConnection();
            splashThread.markFailed();
            return;
        }

        state._pPool         = make_unique<WaylandShmBufferPool>();
        state._bPainterReady = state._painter.initialize( Internal::kTextPixelSize ); // 글꼴이 없으면 이유를 남기고 글 없이 그린다
        splashThread.markReady();

        // 핑 응답 · 버퍼 반납을 늘 받는다 — 기동 스레드가 오래 막혀도 컴포지터가 창을 "응답 없음" 으로 보지 않는다.
        LinuxSplashThread::PaintState paintState;
        bool                          bPendingDraw = false; // 버퍼가 모두 컴포지터에 있으면 반납을 받은 뒤 다시 그린다
        for ( ;; )
        {
            bool bPrepared = true;
            while ( wl_display_prepare_read( state._pDisplay ) != 0 )
            {
                if ( wl_display_dispatch_pending( state._pDisplay ) == Internal::kDispatchFailed )
                {
                    bPrepared = false; // 연결이 끊겼다 — 읽을 준비 없이 끝낸다
                    break;
                }
            }
            if ( bPrepared == false )
                break;
            (void)wl_display_flush( state._pDisplay );
            LinuxSplashThread::PaintState requested;
            const uint8                   wakeMask = splashThread.waitForEvents( wl_display_get_fd( state._pDisplay ), requested );
            if ( ( wakeMask & static_cast<uint8>( LinuxSplashWakeReason::ConnectionReadable ) ) != 0 )
                (void)wl_display_read_events( state._pDisplay ); // 실패는 아래 dispatch 가 -1 로 알린다
            else
                wl_display_cancel_read( state._pDisplay );
            const bool bLost = wl_display_dispatch_pending( state._pDisplay ) == Internal::kDispatchFailed || ( wakeMask & static_cast<uint8>( LinuxSplashWakeReason::ConnectionLost ) ) != 0;
            if ( bLost || ( wakeMask & static_cast<uint8>( LinuxSplashWakeReason::Quit ) ) != 0 )
                break;
            if ( ( wakeMask & static_cast<uint8>( LinuxSplashWakeReason::Redraw ) ) != 0 )
            {
                paintState   = requested;
                bPendingDraw = true;
            }
            if ( bPendingDraw )
                bPendingDraw = drawStatus( paintState ) == false;
        }
        releaseConnection();
    }

    bool WaylandSplashWindow::drawStatus( const LinuxSplashThread::PaintState& paintState )
    {
        using Internal          = WaylandSplashWindowInternal;
        State&     state        = *_pState;
        const bool bNotDrawable = state._pDisplay == nullptr || state._pSurface == nullptr || state._pPool == nullptr;
        if ( bNotDrawable )
            return true; // 그릴 곳이 없다 — 다시 해도 같다

        WaylandShmBuffer* const pBuffer = state._pPool->acquire( state._pShm, _width, _height );
        if ( pBuffer == nullptr )
            return false; // 전부 컴포지터가 쥐고 있다 — 반납을 받은 뒤 다시 그린다

        uint32* const pPixels    = pBuffer->getPixels();
        const size_t  pixelCount = static_cast<size_t>( _width ) * static_cast<size_t>( _height );
        // 배경 → 이미지(창 크기로 줄여 둔 BGRA, 알파는 쓰지 않는다 — 시작 전에 정해져 바뀌지 않는다) → 상태 글 → 진행 막대
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
            (void)state._painter.drawText( pPixels, _width, _height, Internal::kMargin, height - Internal::kTextBaselineUp, paintState._status, Internal::kTextColor );

        const int32   totalBarWidth   = static_cast<int32>( _width ) - Internal::kMargin * 2;
        const float32 clampedProgress = ( paintState._progress < 0.0f ) ? 0.0f : ( ( paintState._progress > 1.0f ) ? 1.0f : paintState._progress );
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
        (void)wl_display_flush( state._pDisplay );
        return true;
    }

    void WaylandSplashWindow::updateStatus( const utf8* pStatus, float32 progress )
    {
        if ( _bOpen == SW_FALSE )
            return;

        if ( StringUtil::isNullOrEmpty( pStatus ) == false )
            _status = pStatus;
        if ( progress >= 0.0f )
            _progress = ( progress > 1.0f ) ? 1.0f : progress;

        LinuxSplashThread::PaintState paintState;
        paintState._status   = _status;
        paintState._progress = _progress;
        _splashThread.requestRedraw( paintState ); // 그리기는 스플래시 스레드가 한다
    }

    void WaylandSplashWindow::setProgress( float32 progress )
    {
        updateStatus( nullptr, progress );
    }

    void WaylandSplashWindow::dismiss()
    {
        _splashThread.stop(); // 스레드가 버퍼 · 표면 · 연결을 놓고 끝난다(`releaseConnection`)
        _bOpen = SW_FALSE;
    }

    void WaylandSplashWindow::releaseConnection()
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
            (void)wl_display_flush( state._pDisplay );
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
    }
} // namespace sw
#endif
