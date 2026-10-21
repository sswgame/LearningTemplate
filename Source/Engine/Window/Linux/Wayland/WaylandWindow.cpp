#include "pch.h"

#include "Engine/Window/Linux/Wayland/WaylandWindow.h"

#include "Core/Container/vector.h"

#include "Engine/Window/Linux/Wayland/WaylandInputEvent.h"
#include "Engine/Window/NativeWindowEvent.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE ) && defined( SW_WITH_WAYLAND )
    #include <linux/input-event-codes.h>
    #include <poll.h>
    #include <sys/mman.h>
    #include <unistd.h>
    #include <wayland-client.h>
    #include <wayland-cursor.h>
    #include <xkbcommon/xkbcommon.h>

    #include "xdg-shell-client-protocol.h" // 구성 때 wayland-scanner 가 만든다(cmake/Modules/Platform/Linux.cmake)

    #include <cstring>
#endif

namespace sw
{
    SW_LOG_CALLER( "WaylandWindow" );
} // namespace sw

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE ) && defined( SW_WITH_WAYLAND )
namespace sw
{
    /** @brief 라이브러리 객체와 리스너가 쌓은 것입니다. 리스너는 C 콜백이라 이 값의 주소를 사용자 값으로 받는다. */
    struct WaylandWindow::State
    {
        wl_display*      _pDisplay{ nullptr };
        wl_registry*     _pRegistry{ nullptr };
        wl_compositor*   _pCompositor{ nullptr };
        wl_shm*          _pShm{ nullptr };
        xdg_wm_base*     _pWmBase{ nullptr };
        wl_seat*         _pSeat{ nullptr };
        wl_keyboard*     _pKeyboard{ nullptr };
        wl_pointer*      _pPointer{ nullptr };
        wl_surface*      _pSurface{ nullptr };
        xdg_surface*     _pXdgSurface{ nullptr };
        xdg_toplevel*    _pToplevel{ nullptr };
        xkb_context*     _pXkbContext{ nullptr };
        xkb_keymap*      _pKeymap{ nullptr };
        xkb_state*       _pXkbState{ nullptr };
        wl_cursor_theme* _pCursorTheme{ nullptr };
        wl_surface*      _pCursorSurface{ nullptr };

        vector<WaylandInputEvent> _listPendingInput;     ///< 리스너가 쌓고 `processMessages` 가 넘긴다
        uint32                    _pointerSerial{ 0 };   ///< 마지막 포인터 들어옴의 일련번호(커서를 바꿀 때 쓴다)
        int32                     _configuredWidth{ 0 }; ///< 컴포지터가 정한 크기(0 이면 클라이언트가 정한다)
        int32                     _configuredHeight{ 0 };
        bool                      _bConfigured{ false };
        bool                      _bCloseRequested{ false };
        bool                      _bCursorVisible{ true };

        void pushInput( const WaylandInputEvent& event ) { _listPendingInput.push_back( event ); }
    };
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 리스너 묶음입니다(C 콜백 — 사용자 값이 `WaylandWindow::State*`). */
        struct WaylandWindowInternal
        {
            using State = WaylandWindow::State;

            /** @brief 바인드할 판 상한입니다 — 리스너 구조체가 이 판까지의 이벤트만 채운다. */
            static constexpr uint32 kCompositorVersion = 4;
            static constexpr uint32 kSeatVersion       = 5;
            static constexpr uint32 kShmVersion        = 1;
            static constexpr uint32 kWmBaseVersion     = 1;
            /** @brief 휠 한 칸에 해당하는 축 값입니다(컴포지터 관례 — 마우스 휠 한 칸이 10). */
            static constexpr float32 kAxisUnitsPerNotch = 10.0f;
            /** @brief 기본 커서 테마 크기(픽셀)입니다. */
            static constexpr int32 kCursorSize = 24;

            static void onRegistryGlobal( void* pData, wl_registry* pRegistry, uint32 name, const utf8* pInterface, uint32 version )
            {
                State& state = *static_cast<State*>( pData );
                if ( std::strcmp( pInterface, wl_compositor_interface.name ) == 0 )
                    state._pCompositor = static_cast<wl_compositor*>( wl_registry_bind( pRegistry, name, &wl_compositor_interface, version < kCompositorVersion ? version : kCompositorVersion ) );
                else if ( std::strcmp( pInterface, wl_shm_interface.name ) == 0 )
                    state._pShm = static_cast<wl_shm*>( wl_registry_bind( pRegistry, name, &wl_shm_interface, kShmVersion ) );
                else if ( std::strcmp( pInterface, xdg_wm_base_interface.name ) == 0 )
                    state._pWmBase = static_cast<xdg_wm_base*>( wl_registry_bind( pRegistry, name, &xdg_wm_base_interface, kWmBaseVersion ) );
                else if ( std::strcmp( pInterface, wl_seat_interface.name ) == 0 )
                    state._pSeat = static_cast<wl_seat*>( wl_registry_bind( pRegistry, name, &wl_seat_interface, version < kSeatVersion ? version : kSeatVersion ) );
            }

            static void onRegistryGlobalRemove( void* pData, wl_registry* pRegistry, uint32 name )
            {
                (void)pData;
                (void)pRegistry;
                (void)name; // 합성기 · 셸이 사라지는 일은 세션 끝이다 — 다음 요청이 실패로 알린다
            }

            static void onWmBasePing( void* pData, xdg_wm_base* pWmBase, uint32 serial )
            {
                (void)pData;
                xdg_wm_base_pong( pWmBase, serial );
            }

            static void onXdgSurfaceConfigure( void* pData, xdg_surface* pXdgSurface, uint32 serial )
            {
                State& state = *static_cast<State*>( pData );
                xdg_surface_ack_configure( pXdgSurface, serial );
                state._bConfigured = true;
            }

            static void onToplevelConfigure( void* pData, xdg_toplevel* pToplevel, int32 width, int32 height, wl_array* pStates )
            {
                (void)pToplevel;
                (void)pStates;
                State& state            = *static_cast<State*>( pData );
                state._configuredWidth  = width;
                state._configuredHeight = height;
            }

            static void onToplevelClose( void* pData, xdg_toplevel* pToplevel )
            {
                (void)pToplevel;
                static_cast<State*>( pData )->_bCloseRequested = true;
            }

            static void onKeyboardKeymap( void* pData, wl_keyboard* pKeyboard, uint32 format, int32 fd, uint32 size )
            {
                (void)pKeyboard;
                State& state = *static_cast<State*>( pData );
                if ( format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 )
                {
                    close( fd );
                    SW_LOG_ERROR( "Wayland keyboard sent a keymap that is not XKB v1 - keys are not read" );
                    return;
                }
                void* const pMapped = mmap( nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0 );
                close( fd );
                if ( pMapped == MAP_FAILED )
                {
                    SW_LOG_ERROR( "Wayland keymap could not be mapped - keys are not read" );
                    return;
                }
                xkb_keymap* const pKeymap = xkb_keymap_new_from_string( state._pXkbContext, static_cast<const utf8*>( pMapped ), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS );
                munmap( pMapped, size );
                if ( pKeymap == nullptr )
                {
                    SW_LOG_ERROR( "Wayland keymap could not be compiled - keys are not read" );
                    return;
                }
                xkb_state_unref( state._pXkbState );
                xkb_keymap_unref( state._pKeymap );
                state._pKeymap   = pKeymap;
                state._pXkbState = xkb_state_new( pKeymap );
            }

            static void onKeyboardEnter( void* pData, wl_keyboard* pKeyboard, uint32 serial, wl_surface* pSurface, wl_array* pKeys )
            {
                (void)pKeyboard;
                (void)serial;
                (void)pSurface;
                (void)pKeys;
                WaylandInputEvent event;
                event._kind = WaylandInputKind::FocusGained;
                static_cast<State*>( pData )->pushInput( event );
            }

            static void onKeyboardLeave( void* pData, wl_keyboard* pKeyboard, uint32 serial, wl_surface* pSurface )
            {
                (void)pKeyboard;
                (void)serial;
                (void)pSurface;
                WaylandInputEvent event;
                event._kind = WaylandInputKind::FocusLost;
                static_cast<State*>( pData )->pushInput( event );
            }

            static void onKeyboardKey( void* pData, wl_keyboard* pKeyboard, uint32 serial, uint32 time, uint32 key, uint32 keyState )
            {
                (void)pKeyboard;
                (void)serial;
                (void)time;
                State& state = *static_cast<State*>( pData );
                if ( state._pXkbState == nullptr )
                    return;
                // evdev 키 번호 + 8 이 XKB 키 코드다(xkbcommon 규약).
                const xkb_keycode_t keyCode = key + 8;
                const bool          bDown   = keyState == WL_KEYBOARD_KEY_STATE_PRESSED;
                WaylandInputEvent   event;
                event._kind   = bDown ? WaylandInputKind::KeyDown : WaylandInputKind::KeyUp;
                event._keySym = xkb_state_key_get_one_sym( state._pXkbState, keyCode );
                state.pushInput( event );
                if ( bDown == false )
                    return;
                WaylandInputEvent text;
                text._kind             = WaylandInputKind::Text;
                const int32 byteCount  = xkb_state_key_get_utf8( state._pXkbState, keyCode, text._arrText, WaylandInputEvent::kTextCapacity );
                const bool  bPrintable = byteCount > 0 && static_cast<uint8>( text._arrText[0] ) >= 0x20 && text._arrText[0] != 0x7F;
                if ( bPrintable )
                    state.pushInput( text );
            }

            static void onKeyboardModifiers( void* pData, wl_keyboard* pKeyboard, uint32 serial, uint32 depressed, uint32 latched, uint32 locked, uint32 group )
            {
                (void)pKeyboard;
                (void)serial;
                State& state = *static_cast<State*>( pData );
                if ( state._pXkbState != nullptr )
                    xkb_state_update_mask( state._pXkbState, depressed, latched, locked, 0, 0, group );
            }

            static void onKeyboardRepeatInfo( void* pData, wl_keyboard* pKeyboard, int32 rate, int32 delay )
            {
                (void)pData;
                (void)pKeyboard;
                (void)rate;
                (void)delay; // 자동 반복은 엔진이 하지 않는다(X11 의 감지 가능한 자동 반복과 같다 — 누름은 한 번)
            }

            static void onPointerEnter( void* pData, wl_pointer* pPointer, uint32 serial, wl_surface* pSurface, wl_fixed_t x, wl_fixed_t y )
            {
                (void)pPointer;
                (void)pSurface;
                State& state         = *static_cast<State*>( pData );
                state._pointerSerial = serial;
                WaylandInputEvent enter;
                enter._kind = WaylandInputKind::PointerEnter;
                state.pushInput( enter );
                WaylandInputEvent move;
                move._kind = WaylandInputKind::PointerMove;
                move._x    = wl_fixed_to_int( x );
                move._y    = wl_fixed_to_int( y );
                state.pushInput( move );
                if ( state._bCursorVisible == false )
                    wl_pointer_set_cursor( state._pPointer, serial, nullptr, 0, 0 );
            }

            static void onPointerLeave( void* pData, wl_pointer* pPointer, uint32 serial, wl_surface* pSurface )
            {
                (void)pPointer;
                (void)serial;
                (void)pSurface;
                WaylandInputEvent event;
                event._kind = WaylandInputKind::PointerLeave;
                static_cast<State*>( pData )->pushInput( event );
            }

            static void onPointerMotion( void* pData, wl_pointer* pPointer, uint32 time, wl_fixed_t x, wl_fixed_t y )
            {
                (void)pPointer;
                (void)time;
                WaylandInputEvent event;
                event._kind = WaylandInputKind::PointerMove;
                event._x    = wl_fixed_to_int( x );
                event._y    = wl_fixed_to_int( y );
                static_cast<State*>( pData )->pushInput( event );
            }

            static void onPointerButton( void* pData, wl_pointer* pPointer, uint32 serial, uint32 time, uint32 button, uint32 buttonState )
            {
                (void)pPointer;
                (void)serial;
                (void)time;
                WaylandInputEvent event;
                event._kind   = buttonState == WL_POINTER_BUTTON_STATE_PRESSED ? WaylandInputKind::PointerDown : WaylandInputKind::PointerUp;
                event._button = button;
                static_cast<State*>( pData )->pushInput( event );
            }

            static void onPointerAxis( void* pData, wl_pointer* pPointer, uint32 time, uint32 axis, wl_fixed_t value )
            {
                (void)pPointer;
                (void)time;
                // 축 값은 아래(세로) · 오른쪽(가로)이 + 다. 엔진 휠은 위가 + 라 세로는 부호를 뒤집는다(X11 Button4 = +1 과 같은 뜻).
                const float32     notches = static_cast<float32>( wl_fixed_to_double( value ) ) / kAxisUnitsPerNotch;
                WaylandInputEvent event;
                event._kind = WaylandInputKind::PointerWheel;
                if ( axis == WL_POINTER_AXIS_VERTICAL_SCROLL )
                    event._wheel = -notches;
                else
                    event._horizontalWheel = notches;
                static_cast<State*>( pData )->pushInput( event );
            }

            static void onPointerFrame( void* pData, wl_pointer* pPointer )
            {
                (void)pData;
                (void)pPointer; // 이벤트를 하나씩 넘긴다 — 묶음 경계는 쓰지 않는다
            }

            static void onPointerAxisSource( void* pData, wl_pointer* pPointer, uint32 source )
            {
                (void)pData;
                (void)pPointer;
                (void)source;
            }

            static void onPointerAxisStop( void* pData, wl_pointer* pPointer, uint32 time, uint32 axis )
            {
                (void)pData;
                (void)pPointer;
                (void)time;
                (void)axis;
            }

            static void onPointerAxisDiscrete( void* pData, wl_pointer* pPointer, uint32 axis, int32 discrete )
            {
                (void)pData;
                (void)pPointer;
                (void)axis;
                (void)discrete; // 칸 수는 축 값(`onPointerAxis`)으로 이미 센다
            }

            static void onSeatCapabilities( void* pData, wl_seat* pSeat, uint32 capabilities );

            static void onSeatName( void* pData, wl_seat* pSeat, const utf8* pName )
            {
                (void)pData;
                (void)pSeat;
                (void)pName;
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
                // 판 1 의 두 이벤트만 채운다 — 판 1 로 바인드해 그 뒤 이벤트(configure_bounds · wm_capabilities)는 오지 않는다.
                xdg_toplevel_listener listener{};
                listener.configure = &onToplevelConfigure;
                listener.close     = &onToplevelClose;
                return listener;
            }

            static wl_keyboard_listener makeKeyboardListener()
            {
                wl_keyboard_listener listener{};
                listener.keymap      = &onKeyboardKeymap;
                listener.enter       = &onKeyboardEnter;
                listener.leave       = &onKeyboardLeave;
                listener.key         = &onKeyboardKey;
                listener.modifiers   = &onKeyboardModifiers;
                listener.repeat_info = &onKeyboardRepeatInfo;
                return listener;
            }

            static wl_pointer_listener makePointerListener()
            {
                // 자리 판 5 까지의 이벤트를 채운다(바인드도 5 까지 — `kSeatVersion`).
                wl_pointer_listener listener{};
                listener.enter         = &onPointerEnter;
                listener.leave         = &onPointerLeave;
                listener.motion        = &onPointerMotion;
                listener.button        = &onPointerButton;
                listener.axis          = &onPointerAxis;
                listener.frame         = &onPointerFrame;
                listener.axis_source   = &onPointerAxisSource;
                listener.axis_stop     = &onPointerAxisStop;
                listener.axis_discrete = &onPointerAxisDiscrete;
                return listener;
            }

            static wl_seat_listener makeSeatListener()
            {
                wl_seat_listener listener{};
                listener.capabilities = &onSeatCapabilities;
                listener.name         = &onSeatName;
                return listener;
            }

            // 리스너는 객체가 사는 동안 그 자리에 있어야 한다 — 정적 상수로 둔다(상태를 갖지 않는다).
            static const wl_registry_listener  s_registryListener;
            static const xdg_wm_base_listener  s_wmBaseListener;
            static const xdg_surface_listener  s_xdgSurfaceListener;
            static const xdg_toplevel_listener s_toplevelListener;
            static const wl_keyboard_listener  s_keyboardListener;
            static const wl_pointer_listener   s_pointerListener;
            static const wl_seat_listener      s_seatListener;
        };

        const wl_registry_listener  WaylandWindowInternal::s_registryListener   = WaylandWindowInternal::makeRegistryListener();
        const xdg_wm_base_listener  WaylandWindowInternal::s_wmBaseListener     = WaylandWindowInternal::makeWmBaseListener();
        const xdg_surface_listener  WaylandWindowInternal::s_xdgSurfaceListener = WaylandWindowInternal::makeXdgSurfaceListener();
        const xdg_toplevel_listener WaylandWindowInternal::s_toplevelListener   = WaylandWindowInternal::makeToplevelListener();
        const wl_keyboard_listener  WaylandWindowInternal::s_keyboardListener   = WaylandWindowInternal::makeKeyboardListener();
        const wl_pointer_listener   WaylandWindowInternal::s_pointerListener    = WaylandWindowInternal::makePointerListener();
        const wl_seat_listener      WaylandWindowInternal::s_seatListener       = WaylandWindowInternal::makeSeatListener();

        void WaylandWindowInternal::onSeatCapabilities( void* pData, wl_seat* pSeat, uint32 capabilities )
        {
            State&     state     = *static_cast<State*>( pData );
            const bool bKeyboard = ( capabilities & WL_SEAT_CAPABILITY_KEYBOARD ) != 0;
            const bool bPointer  = ( capabilities & WL_SEAT_CAPABILITY_POINTER ) != 0;
            if ( bKeyboard && state._pKeyboard == nullptr )
            {
                state._pKeyboard = wl_seat_get_keyboard( pSeat );
                wl_keyboard_add_listener( state._pKeyboard, &s_keyboardListener, &state );
            }
            if ( bPointer && state._pPointer == nullptr )
            {
                state._pPointer = wl_seat_get_pointer( pSeat );
                wl_pointer_add_listener( state._pPointer, &s_pointerListener, &state );
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    WaylandWindow::WaylandWindow()
        : _pState{ make_unique<State>() }
    {
        clearRestorePosition();
    }

    WaylandWindow::~WaylandWindow()
    {
        WaylandWindow::destroy();
    }

    void WaylandWindow::clearRestorePosition()
    {
        _restoreX = 0;
        _restoreY = 0;
    }

    bool WaylandWindow::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        using Internal = WaylandWindowInternal;
        _width         = width;
        _height        = height;
        if ( storeTitle( pTitle ) == false )
            return false; // 오류는 storeTitle 이 남겼다

        State& state    = *_pState;
        state           = State{};
        state._pDisplay = wl_display_connect( nullptr );
        if ( state._pDisplay == nullptr )
        {
            SW_LOG_ERROR( "Wayland: no compositor to connect to (WAYLAND_DISPLAY) - the window is not created" );
            return false;
        }
        state._pXkbContext = xkb_context_new( XKB_CONTEXT_NO_FLAGS );
        state._pRegistry   = wl_display_get_registry( state._pDisplay );
        wl_registry_add_listener( state._pRegistry, &Internal::s_registryListener, &state );
        wl_display_roundtrip( state._pDisplay );
        const bool bMissingGlobals = state._pCompositor == nullptr || state._pWmBase == nullptr;
        if ( bMissingGlobals )
        {
            SW_LOG_ERROR( "Wayland: the compositor has no wl_compositor or xdg_wm_base - the window is not created" );
            destroy();
            return false;
        }
        xdg_wm_base_add_listener( state._pWmBase, &Internal::s_wmBaseListener, &state );
        if ( state._pSeat != nullptr )
            wl_seat_add_listener( state._pSeat, &Internal::s_seatListener, &state );

        state._pSurface    = wl_compositor_create_surface( state._pCompositor );
        state._pXdgSurface = xdg_wm_base_get_xdg_surface( state._pWmBase, state._pSurface );
        xdg_surface_add_listener( state._pXdgSurface, &Internal::s_xdgSurfaceListener, &state );
        state._pToplevel = xdg_surface_get_toplevel( state._pXdgSurface );
        xdg_toplevel_add_listener( state._pToplevel, &Internal::s_toplevelListener, &state );
        xdg_toplevel_set_app_id( state._pToplevel, "sw.engine" );
        applyTitle();
        applyMinimumClientSize();
        wl_surface_commit( state._pSurface );
        // 첫 configure 를 기다린다 — 그 전에는 그림을 붙이면 안 된다(xdg-shell 규약).
        int32 dispatchResult = 0;
        while ( state._bConfigured == false && dispatchResult != -1 )
        {
            dispatchResult = wl_display_dispatch( state._pDisplay );
        }
        if ( state._bConfigured == false )
        {
            SW_LOG_ERROR( "Wayland: the compositor closed the connection before configuring the window" );
            destroy();
            return false;
        }
        if ( state._configuredWidth > 0 && state._configuredHeight > 0 )
        {
            _width  = static_cast<uint32>( state._configuredWidth );
            _height = static_cast<uint32>( state._configuredHeight );
        }
        if ( state._pShm != nullptr )
        {
            state._pCursorTheme   = wl_cursor_theme_load( nullptr, Internal::kCursorSize, state._pShm );
            state._pCursorSurface = wl_compositor_create_surface( state._pCompositor );
        }
        _bShouldClose = SW_FALSE;
        SW_LOG_INFO( "Native Wayland Window created successfully! (%#×%#)", _width, _height );
        return true;
    }

    void WaylandWindow::destroy()
    {
        State& state = *_pState;
        if ( state._pCursorSurface != nullptr )
            wl_surface_destroy( state._pCursorSurface );
        if ( state._pCursorTheme != nullptr )
            wl_cursor_theme_destroy( state._pCursorTheme );
        if ( state._pToplevel != nullptr )
            xdg_toplevel_destroy( state._pToplevel );
        if ( state._pXdgSurface != nullptr )
            xdg_surface_destroy( state._pXdgSurface );
        if ( state._pSurface != nullptr )
            wl_surface_destroy( state._pSurface );
        if ( state._pPointer != nullptr )
            wl_pointer_destroy( state._pPointer );
        if ( state._pKeyboard != nullptr )
            wl_keyboard_destroy( state._pKeyboard );
        if ( state._pSeat != nullptr )
            wl_seat_destroy( state._pSeat );
        if ( state._pWmBase != nullptr )
            xdg_wm_base_destroy( state._pWmBase );
        if ( state._pShm != nullptr )
            wl_shm_destroy( state._pShm );
        if ( state._pCompositor != nullptr )
            wl_compositor_destroy( state._pCompositor );
        if ( state._pRegistry != nullptr )
            wl_registry_destroy( state._pRegistry );
        xkb_state_unref( state._pXkbState );
        xkb_keymap_unref( state._pKeymap );
        xkb_context_unref( state._pXkbContext );
        if ( state._pDisplay != nullptr )
            wl_display_disconnect( state._pDisplay );
        state = State{};
    }

    bool WaylandWindow::processMessages()
    {
        State& state = *_pState;
        if ( state._pDisplay == nullptr )
            return _bShouldClose == SW_FALSE;

        // 막지 않고 읽는다: 쌓인 것을 처리 → 읽을 준비 → 보낼 것을 보냄 → 읽을 것이 있으면 읽음(없으면 취소) → 다시 처리.
        wl_display_dispatch_pending( state._pDisplay );
        while ( wl_display_prepare_read( state._pDisplay ) != 0 )
        {
            wl_display_dispatch_pending( state._pDisplay );
        }
        wl_display_flush( state._pDisplay );
        pollfd pollEntry{ wl_display_get_fd( state._pDisplay ), POLLIN, 0 };
        if ( poll( &pollEntry, 1, 0 ) > 0 )
            (void)wl_display_read_events( state._pDisplay ); // 실패는 다음 dispatch 가 -1 로 알린다
        else
            wl_display_cancel_read( state._pDisplay );
        if ( wl_display_dispatch_pending( state._pDisplay ) == -1 )
        {
            SW_LOG_ERROR( "Wayland: the connection to the compositor broke - closing the window" );
            return false;
        }

        // 입력은 X11 과 같이 처리기(입력 관리자 · 에디터)에 하나씩 넘긴다.
        vector<WaylandInputEvent> listInput;
        listInput.swap( state._listPendingInput );
        for ( const WaylandInputEvent& input : listInput )
        {
            if ( _customHandler.isBound() == false )
                break;
            NativeWindowEvent windowEvent{};
            windowEvent._pNativeWindow = state._pSurface;
            windowEvent._message       = NativeWindowEvent::kMessageWayland;
            windowEvent._lParam        = reinterpret_cast<intptr_t>( &input );
            (void)_customHandler( windowEvent ); // 받은 쪽이 먹었는지는 Wayland 에서 할 일을 바꾸지 않는다
        }

        const bool bResized = state._configuredWidth > 0 && state._configuredHeight > 0 &&
                              ( static_cast<uint32>( state._configuredWidth ) != _width || static_cast<uint32>( state._configuredHeight ) != _height );
        if ( bResized )
        {
            _width  = static_cast<uint32>( state._configuredWidth );
            _height = static_cast<uint32>( state._configuredHeight );
            if ( _onResize.isBound() )
                _onResize( _width, _height );
        }
        if ( state._bCloseRequested && _bRecreating == SW_FALSE )
        {
            state._bCloseRequested = false;
            if ( tryBeginClose() )
                return false;
        }
        return _bShouldClose == SW_FALSE;
    }

    void WaylandWindow::applyWindowVisibility( bool bShow )
    {
        if ( bShow == false )
            SW_LOG_ERROR( "Wayland has no way for a client to hide its window - the window stays mapped" );
    }

    bool WaylandWindow::isVisible() const
    {
        return _pState->_bConfigured;
    }

    bool WaylandWindow::setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
    {
        State& state = *_pState;
        if ( state._pToplevel == nullptr )
            return false;
        _displayMode = mode;
        if ( mode == WindowDisplayMode::BorderlessFullscreen )
        {
            xdg_toplevel_set_fullscreen( state._pToplevel, nullptr ); // 크기는 다음 configure 가 준다
            wl_surface_commit( state._pSurface );
            return true;
        }
        xdg_toplevel_unset_fullscreen( state._pToplevel );
        wl_surface_commit( state._pSurface );
        // 창 모드의 크기는 클라이언트가 정한다 — 그 크기로 그린다고 알린다.
        _width  = width;
        _height = height;
        if ( _onResize.isBound() )
            _onResize( _width, _height );
        return true;
    }

    void* WaylandWindow::getNativeHandle() const
    {
        return _pState->_pSurface;
    }

    void* WaylandWindow::getNativeDisplay() const
    {
        return _pState->_pDisplay;
    }

    void WaylandWindow::setCursorVisible( bool bVisible )
    {
        State& state          = *_pState;
        state._bCursorVisible = bVisible;
        if ( state._pPointer == nullptr || state._pointerSerial == 0 )
            return; // 포인터가 들어올 때(`onPointerEnter`) 적용한다
        if ( bVisible == false )
        {
            wl_pointer_set_cursor( state._pPointer, state._pointerSerial, nullptr, 0, 0 );
            return;
        }
        wl_cursor* const pCursor        = state._pCursorTheme != nullptr ? wl_cursor_theme_get_cursor( state._pCursorTheme, "left_ptr" ) : nullptr;
        const bool       bNoCursorImage = pCursor == nullptr || pCursor->image_count == 0 || state._pCursorSurface == nullptr;
        if ( bNoCursorImage )
        {
            SW_LOG_ERROR( "Wayland: no cursor theme (left_ptr) to show the cursor with" );
            return;
        }
        wl_cursor_image* const pImage  = pCursor->images[0];
        wl_buffer* const       pBuffer = wl_cursor_image_get_buffer( pImage );
        wl_pointer_set_cursor( state._pPointer, state._pointerSerial, state._pCursorSurface, static_cast<int32>( pImage->hotspot_x ), static_cast<int32>( pImage->hotspot_y ) );
        wl_surface_attach( state._pCursorSurface, pBuffer, 0, 0 );
        wl_surface_damage( state._pCursorSurface, 0, 0, static_cast<int32>( pImage->width ), static_cast<int32>( pImage->height ) );
        wl_surface_commit( state._pCursorSurface );
    }

    void WaylandWindow::applyMinimumClientSize()
    {
        if ( _pState->_pToplevel != nullptr )
            xdg_toplevel_set_min_size( _pState->_pToplevel, static_cast<int32>( _minClientWidth ), static_cast<int32>( _minClientHeight ) );
    }

    void WaylandWindow::applyTitle()
    {
        if ( _pState->_pToplevel != nullptr )
            xdg_toplevel_set_title( _pState->_pToplevel, getTitle() );
    }
} // namespace sw
#else
namespace sw
{
    /** @brief Wayland 를 짓지 않은 구성의 빈 상태입니다 — 이 구성은 `createPlatformWindow` 가 이 창을 만들지 않는다. */
    struct WaylandWindow::State
    {
    };

    WaylandWindow::WaylandWindow()
        : _pState{ make_unique<State>() }
    {
        clearRestorePosition();
    }

    WaylandWindow::~WaylandWindow() = default;

    void WaylandWindow::clearRestorePosition()
    {
        _restoreX = 0;
        _restoreY = 0;
    }

    bool WaylandWindow::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        (void)pTitle;
        (void)width;
        (void)height;
        SW_LOG_ERROR( "This build has no Wayland window layer (configure with -DSW_ENABLE_WAYLAND=ON on Linux)" );
        return false;
    }

    void WaylandWindow::destroy()
    {
    }

    bool WaylandWindow::processMessages()
    {
        return _bShouldClose == SW_FALSE;
    }

    void WaylandWindow::applyWindowVisibility( bool bShow )
    {
        (void)bShow;
    }

    bool WaylandWindow::isVisible() const
    {
        return false;
    }

    bool WaylandWindow::setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
    {
        (void)mode;
        (void)width;
        (void)height;
        return false;
    }

    void* WaylandWindow::getNativeHandle() const
    {
        return nullptr;
    }

    void* WaylandWindow::getNativeDisplay() const
    {
        return nullptr;
    }

    void WaylandWindow::setCursorVisible( bool bVisible )
    {
        (void)bVisible;
    }

    void WaylandWindow::applyMinimumClientSize()
    {
    }

    void WaylandWindow::applyTitle()
    {
    }
} // namespace sw
#endif
