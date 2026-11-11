#include "pch.h"

#include "Engine/Window/Linux/X11Window.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/WindowMonitor.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Foundation/X11Headers.h"

    #include <X11/XKBlib.h>
    #include <X11/cursorfont.h>
    #include <poll.h>

    #include "Core/Foundation/X11MacroUndef.h"

    #include <cstring>

namespace sw
{
    namespace
    {
        /** @brief 복원 위치의 "알아서" 값입니다. X11 에는 `CW_USEDEFAULT` 같은 약속이 없어 좌표를 하나 정해 둡니다. */
        constexpr int32 kDefaultRestoreX = 100;
        /** @brief 위와 같습니다(세로 좌표). */
        constexpr int32 kDefaultRestoreY = 100;
    } // namespace

    namespace
    {
        /** @brief 클립보드 주인에게 변환을 청한 뒤 답(`SelectionNotify`)을 기다리는 한 번의 길이(ms)입니다. */
        constexpr int32 kClipboardPollMs = 10;
        /** @brief 위 기다림의 최대 횟수입니다. 붙여넣기 한 번이 메인 스레드를 최대 200 ms 멈춘다(GLFW · SDL 도 같은 동기 대기다). */
        constexpr int32 kClipboardPollCount = 20;
        /** @brief 클립보드 속성을 한 번에 읽는 최대 길이(32 비트 단위)입니다. 64 MB 다. */
        constexpr int64 kClipboardMaxLongs = int64{ 1 } << 24;
        /** @brief INCR 로 보내는 중 상대가 다음 조각을 청하지 않고 기다려 주는 시간(ms)입니다. 넘으면 상대가 사라진 것으로 보고 건을 버린다. */
        constexpr int64 kClipboardIncrIdleMilliseconds = 10 * constant::time::kMillisecondsPerSecond;

        struct X11ClipboardWait
        {
            /** @brief `XChangeProperty` 한 번에 보낼 수 있는 바이트입니다(요청 최대 크기는 4 바이트 단위이고, 요청 머리 몫을 뺀다). */
            static size_t getMaxSingleTransferBytes( Display* pDisplay )
            {
                constexpr int64 kRequestHeaderBytes = 64;
                const int64     extended            = XExtendedMaxRequestSize( pDisplay );
                const int64     units               = extended != 0 ? extended : XMaxRequestSize( pDisplay );
                return static_cast<size_t>( units * 4 - kRequestHeaderBytes );
            }

            /**
             * @brief 창 @p win 에 온 @p eventType 이벤트 중 @p accept 가 받는 것 하나를 최대 `kClipboardPollMs` × `kClipboardPollCount` 동안 기다려 꺼냅니다.
             * @details 다른 종류의 이벤트는 큐에 남아 `processMessages` 가 받는다. 같은 종류인데 @p accept 가 거절한 것(다른 속성의 PropertyNotify)은 버린다 — 창은 그것을 쓰지 않는다.
             */
            template <typename Accept>
            static bool waitForEvent( Display* pDisplay, Window win, int32 eventType, XEvent& outEvent, Accept&& accept )
            {
                for ( int32 attempt = 0; attempt < kClipboardPollCount; ++attempt )
                {
                    while ( XCheckTypedWindowEvent( pDisplay, win, eventType, &outEvent ) != 0 )
                    {
                        if ( accept( outEvent ) )
                            return true;
                    }
                    pollfd connection{ ConnectionNumber( pDisplay ), POLLIN, 0 };
                    (void)poll( &connection, 1, kClipboardPollMs );
                }
                return false;
            }

            /**
             * @brief 창 @p win 의 큐에 이미 든 PropertyNotify 를 모두 버립니다.
             * @details 주인이 INCR 표지를 쓰면 PropertyNotify(새 값)가 `SelectionNotify` 보다 먼저 온다. 버리지 않으면 표지를 지운 뒤 첫 조각을 기다리는 루프가 이 낡은 알림을 첫 조각으로 받아
             *          아직 없는 속성을 길이 0 조각(끝 신호)으로 읽고 빈 글로 끝낸다. 낡은 알림은 모두 `SelectionNotify` 앞에 있어, 그것을 받은 뒤 표지를 지우기 전에 버리면 첫 조각의 알림은 남는다.
             */
            static void discardQueuedPropertyEvents( Display* pDisplay, Window win )
            {
                XEvent stale{};
                while ( XCheckTypedWindowEvent( pDisplay, win, PropertyNotify, &stale ) != 0 )
                {
                }
            }

            /** @brief 창 속성 @p property 를 읽고 지웁니다(INCR 은 지워야 다음 조각이 온다). 8 비트 형식이면 바이트를 @p outBytes 에 담습니다. 읽지 못하면 false 입니다. */
            static bool takeProperty( Display* pDisplay, Window win, Atom property, Atom& outType, string& outBytes )
            {
                int32       format     = 0;
                uint64      itemCount  = 0;
                uint64      bytesAfter = 0;
                uint8*      pData      = nullptr;
                const int32 result     = XGetWindowProperty( pDisplay, win, property, 0, kClipboardMaxLongs, 1, AnyPropertyType, &outType, &format, &itemCount, &bytesAfter, &pData );
                const bool  bBytes     = format == 8 && pData != nullptr;
                if ( result == 0 && bBytes )
                    outBytes.assign( reinterpret_cast<const utf8*>( pData ), static_cast<size_t>( itemCount ) );
                if ( pData != nullptr )
                    XFree( pData );
                if ( result != 0 )
                    SW_LOG_WARNING_ID( "X11Window.ClipboardPropertyCouldNot", "Clipboard property could not be read (X error %#) - nothing pasted", result );
                return result == 0;
            }
        };

        struct X11FrameExtentsInternal
        {
            /** @brief 창 관리자가 `_NET_REQUEST_FRAME_EXTENTS` 에 답하기를 기다리는 한 번의 길이(ms)입니다. */
            static constexpr int32 kPollMs = 10;
            /** @brief 위 기다림의 최대 횟수입니다. 최대 500 ms 다(GLFW `glfwGetWindowFrameSize` 의 기다림과 같다). */
            static constexpr int32 kPollCount = 50;

            /** @brief `XCheckIfEvent` 가 찾는 속성 알림(창 · 속성)입니다. */
            struct PropertyKey
            {
                Window _window;
                Atom   _property;
            };

            static Bool isPropertyNotify( Display* pDisplay, XEvent* pEvent, XPointer pArgument )
            {
                (void)pDisplay;
                const PropertyKey* pKey = reinterpret_cast<const PropertyKey*>( pArgument );
                return pEvent->type == PropertyNotify && pEvent->xproperty.window == pKey->_window && pEvent->xproperty.atom == pKey->_property ? 1 : 0;
            }

            /** @brief 뿌리 창에 `_NET_SUPPORTING_WM_CHECK`(EWMH 창 관리자가 적는 창 ID)가 있으면 true 입니다. */
            static bool hasEwmhWindowManager( Display* pDisplay, Window root )
            {
                Atom        type       = 0;
                int32       format     = 0;
                uint64      itemCount  = 0;
                uint64      bytesAfter = 0;
                uint8*      pData      = nullptr;
                const int32 result     = XGetWindowProperty( pDisplay, root, XInternAtom( pDisplay, "_NET_SUPPORTING_WM_CHECK", 0 ), 0, 1, 0, XA_WINDOW, &type, &format, &itemCount, &bytesAfter, &pData );
                if ( pData != nullptr )
                    XFree( pData );
                return result == 0 && type == XA_WINDOW && itemCount == 1;
            }

            /** @brief 창 관리자가 `_NET_SUPPORTED` 에 `_NET_REQUEST_FRAME_EXTENTS` 를 적었으면 true 입니다. 적지 않은 관리자(WSLg 의 Weston XWM 등)에게는 청해도 답이 없다. */
            static bool supportsFrameExtentsRequest( Display* pDisplay, Window root )
            {
                constexpr int64 kMaxAtomCount = 1024;
                Atom            type          = 0;
                int32           format        = 0;
                uint64          itemCount     = 0;
                uint64          bytesAfter    = 0;
                uint8*          pData         = nullptr;
                const Atom      wanted        = XInternAtom( pDisplay, "_NET_REQUEST_FRAME_EXTENTS", 0 );
                const int32     result        = XGetWindowProperty( pDisplay, root, XInternAtom( pDisplay, "_NET_SUPPORTED", 0 ), 0, kMaxAtomCount, 0, XA_ATOM, &type, &format, &itemCount, &bytesAfter, &pData );
                bool            bFound        = false;
                if ( result == 0 && type == XA_ATOM && pData != nullptr )
                {
                    const Atom* pAtom = reinterpret_cast<const Atom*>( pData );
                    for ( uint64 index = 0; index < itemCount && bFound == false; ++index )
                    {
                        bFound = pAtom[index] == wanted;
                    }
                }
                if ( pData != nullptr )
                    XFree( pData );
                return bFound;
            }

            /** @brief 창 @p win 의 `_NET_FRAME_EXTENTS`(CARDINAL[4] — 왼 · 오른 · 위 · 아래)를 읽습니다. 없으면 false 입니다. */
            static bool readFrameExtents( Display* pDisplay, Window win, Atom property, WindowFrameExtents& outExtents )
            {
                constexpr uint64 kExtentCount = 4;
                Atom             type         = 0;
                int32            format       = 0;
                uint64           itemCount    = 0;
                uint64           bytesAfter   = 0;
                uint8*           pData        = nullptr;
                const int32      result       = XGetWindowProperty( pDisplay, win, property, 0, kExtentCount, 0, XA_CARDINAL, &type, &format, &itemCount, &bytesAfter, &pData );
                const bool       bRead        = result == 0 && type == XA_CARDINAL && format == 32 && itemCount == kExtentCount && pData != nullptr;
                if ( bRead )
                {
                    // 32 비트 형식의 속성은 Xlib 이 `long` 배열로 돌려준다.
                    const long* pExtent = reinterpret_cast<const long*>( pData );
                    outExtents._left    = static_cast<int32>( pExtent[0] );
                    outExtents._right   = static_cast<int32>( pExtent[1] );
                    outExtents._top     = static_cast<int32>( pExtent[2] );
                    outExtents._bottom  = static_cast<int32>( pExtent[3] );
                }
                if ( pData != nullptr )
                    XFree( pData );
                return bRead;
            }
        };
    } // namespace

    X11Window::X11Window()
        : _clipboardText{}
        , _listClipboardIncr{}
        , _monitors{}
        , _inputMethod{}
        , _pX11Display{ nullptr }
        , _x11Window{ 0 }
        , _x11WmDelete{ 0 }
        , _invisibleCursor{ 0 }
        , _arrShapeCursor{}
        , _reservedX11{ 0 }
        , _padding{ 0 }
    {
        // 복원 위치는 기반(`IWindow`)이 가지고 절차도 기반이 맡는다. 플랫폼은 "알아서" 값만 정한다.
        clearRestorePosition();
    }

    X11Window::~X11Window()
    {
        X11Window::destroy();
    }

    bool X11Window::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        _width  = width;
        _height = height;
        if ( storeTitle( pTitle ) == false )
            return false; // 오류는 storeTitle 이 남겼다

        Display* pDisplay = XOpenDisplay( nullptr );
        if ( pDisplay == nullptr )
        {
            SW_LOG_ERROR_ID( "X11Window.FailedOpenX11Display", "Failed to open X11 Display!" );
            return false;
        }

        int32  screen = DefaultScreen( pDisplay );
        Window root   = RootWindow( pDisplay, screen );
        uint64 black  = BlackPixel( pDisplay, screen );
        uint64 white  = WhitePixel( pDisplay, screen );

        Window win = XCreateSimpleWindow(
            pDisplay, root,
            _restoreX, _restoreY, width, height,
            1, black, white );

        Atom wmDeleteMessage = XInternAtom( pDisplay, "WM_DELETE_WINDOW", 0 );
        XSetWMProtocols( pDisplay, win, &wmDeleteMessage, 1 );

        XSelectInput( pDisplay, win,
                      ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask |
                          ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask |
                          PropertyChangeMask ); // 클립보드의 큰 글(INCR) 조각 알림이다(`readClipboardText`)
        // 누르고 있으면 X 서버는 기본으로 Release/Press 쌍을 반복해서 보낸다 — 입력 쪽에 눌림과 뗌 엣지가 계속 생긴다. 감지 가능한 자동 반복을 켜면 반복은 Press 만 온다.
        Bool bAutoRepeatSupported = 0;
        XkbSetDetectableAutoRepeat( pDisplay, 1, &bAutoRepeatSupported ); // 1 == True. X11MacroUndef.h 가 True 매크로를 지운다.
        XFlush( pDisplay );

        _pX11Display  = pDisplay;
        _x11Window    = win;
        _x11WmDelete  = wmDeleteMessage;
        _bShouldClose = SW_FALSE;
        _monitors.initialize( pDisplay );
        _monitors.selectChangeEvents();
        notifyMonitorsChanged(); // 다시 만든 창은 새 연결이다 - 값을 든 쪽이 다시 읽는다
        _inputMethod.initialize( pDisplay, win, SW_DELEGATE_METHOD( X11InputMethodTextDelegate, &X11Window::onInputMethodText, this ) );
        _inputMethod.setWanted( isTextInputWanted() ); // 다시 만든 창도 받는 쪽의 뜻을 잇는다
        _inputMethod.setArea( _textInputArea );
        applyMinimumClientSize(); // 다시 만든 창(recreate)도 같은 하한을 가진다
        applyTitle();

        SW_LOG_INFO_ID( "X11Window.NativeX11WindowCreated", "Native X11 Window created successfully! (%#×%#)", width, height );
        return true;
    }

    void X11Window::destroy()
    {
        if ( _pX11Display != nullptr )
        {
            Display* pDisplay = static_cast<Display*>( _pX11Display );
            releaseInputResources(); // 커서 · XIC 는 이 창과 Display 의 것이다 — 창을 다시 만들면 옛 창을 가리키는 문맥이 남지 않는다
            if ( _x11Window != 0 )
            {
                XDestroyWindow( pDisplay, _x11Window );
                _x11Window = 0;
            }
            _monitors.shutdown();
            XCloseDisplay( pDisplay );
            _pX11Display = nullptr;
        }
        _clipboardText.clear(); // 창이 없어지면 서버가 선택 주인을 거둔다
        _listClipboardIncr.clear();
    }

    void X11Window::requestInputFocus()
    {
        // Win32Window 의 SetForegroundWindow · SetFocus 자리다. 보통은 창 관리자가 주지만, 주지 않는 곳(WSLg · Xvfb)에서는 키 · 마우스가 영영 오지 않는다.
        // 보이지 않는 창에 걸면 BadMatch 로 프로세스가 끝나므로 지금 보이는지 먼저 본다.
        Display*          pDisplay = static_cast<Display*>( _pX11Display );
        XWindowAttributes attributes{};
        if ( XGetWindowAttributes( pDisplay, _x11Window, &attributes ) != 0 && attributes.map_state == IsViewable )
            XSetInputFocus( pDisplay, _x11Window, RevertToParent, CurrentTime );
    }

    void X11Window::applyWindowVisibility( bool bShow )
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return;

        Display* pDisplay = static_cast<Display*>( _pX11Display );
        if ( bShow )
        {
            XMapWindow( pDisplay, _x11Window );
            XFlush( pDisplay );
        }
        else
        {
            XUnmapWindow( pDisplay, _x11Window );
            XFlush( pDisplay );
        }
    }

    bool X11Window::isVisible() const
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return false;

        Display*          pDisplay = static_cast<Display*>( _pX11Display );
        XWindowAttributes wa{};
        if ( XGetWindowAttributes( pDisplay, _x11Window, &wa ) != 0 )
            return wa.map_state == IsViewable;
        return false;
    }

    bool X11Window::canMinimize() const
    {
        // 아이콘화는 창 관리자에게 하는 요청이다. 없으면(Xvfb) 요청은 성공해도 아무 일도 없다 - EWMH 창 관리자는 루트에 이 속성을 둔다.
        if ( _pX11Display == nullptr )
            return false;
        Display*   pDisplay = static_cast<Display*>( _pX11Display );
        const Atom wmCheck  = XInternAtom( pDisplay, "_NET_SUPPORTING_WM_CHECK", 1 ); // only_if_exists
        if ( wmCheck == 0 )
            return false;
        Atom           actualType{ 0 };
        int32          actualFormat{ 0 };
        unsigned long  itemCount{ 0 };
        unsigned long  bytesAfter{ 0 };
        unsigned char* pData{ nullptr };
        const int32    result = XGetWindowProperty( pDisplay, DefaultRootWindow( pDisplay ), wmCheck, 0, 1, 0, XA_WINDOW, &actualType, &actualFormat, &itemCount, &bytesAfter, &pData );
        if ( pData != nullptr )
            XFree( pData );
        return result == 0 && itemCount > 0; // 0 = Success(X11Headers 가 매크로를 지운다)
    }

    bool X11Window::setMinimized( bool bMinimized )
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return false;
        Display* pDisplay = static_cast<Display*>( _pX11Display );
        // 아이콘화는 창 관리자에게 하는 요청이다 — 상태는 다음 `processMessages` 의 상태 통보가 바꾼다. 되돌리기는 다시 매핑이다(ICCCM).
        const bool bRequested = bMinimized ? XIconifyWindow( pDisplay, _x11Window, DefaultScreen( pDisplay ) ) != 0 : ( XMapRaised( pDisplay, _x11Window ), true );
        XFlush( pDisplay );
        return bRequested;
    }

    bool X11Window::sendFocusEvent( bool bFocused )
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return false;
        Display* pDisplay = static_cast<Display*>( _pX11Display );
        XEvent   event{};
        event.xfocus.type   = bFocused ? FocusIn : FocusOut;
        event.xfocus.window = static_cast<Window>( _x11Window );
        event.xfocus.mode   = NotifyNormal; // Grab · Ungrab 이면 `processMessages` 가 포커스 변화로 보지 않는다
        event.xfocus.detail = NotifyNonlinear;
        // 세 번째 0 == False(propagate, X11MacroUndef.h 가 False 매크로를 지운다). 마스크 0 은 창을 만든 연결, 곧 이 프로세스에만 보낸다 — 창 관리자에게 닿지 않는다.
        const bool bSent = XSendEvent( pDisplay, static_cast<Window>( _x11Window ), 0, 0, &event ) != 0;
        // 두 번째 0 == False(큐를 버리지 않는다). 왕복을 기다려 돌아올 때 이벤트가 큐에 있게 한다 — 시나리오의 프레임 수가 서버 응답 시간에 달리지 않는다.
        XSync( pDisplay, 0 );
        return bSent;
    }

    bool X11Window::setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return false;

        Display*   pDisplay    = static_cast<Display*>( _pX11Display );
        const bool bFullscreen = mode == WindowDisplayMode::BorderlessFullscreen;
        _displayMode           = mode;

        // 창 관리자에게 상태 변경을 청한다(EWMH). 1 = _NET_WM_STATE_ADD, 0 = _NET_WM_STATE_REMOVE, l[3] = 1 은 "일반 앱" 출처다.
        XEvent event{};
        event.xclient.type         = ClientMessage;
        event.xclient.window       = static_cast<Window>( _x11Window );
        event.xclient.message_type = XInternAtom( pDisplay, "_NET_WM_STATE", 0 );
        event.xclient.format       = 32;
        event.xclient.data.l[0]    = bFullscreen ? 1 : 0;
        event.xclient.data.l[1]    = static_cast<int64>( XInternAtom( pDisplay, "_NET_WM_STATE_FULLSCREEN", 0 ) );
        event.xclient.data.l[2]    = 0;
        event.xclient.data.l[3]    = 1;
        XSendEvent( pDisplay, DefaultRootWindow( pDisplay ), 0, SubstructureRedirectMask | SubstructureNotifyMask, &event );
        if ( bFullscreen == false )
        {
            clampToMinimumClientSize( width, height );
            XResizeWindow( pDisplay, static_cast<Window>( _x11Window ), width, height );
        }
        XFlush( pDisplay );
        return true;
    }

    bool X11Window::tryGetFrameExtents( WindowFrameExtents& outExtents ) const
    {
        using Internal = X11FrameExtentsInternal;
        outExtents     = WindowFrameExtents{};
        if ( _pX11Display == nullptr || _x11Window == 0 )
        {
            SW_LOG_ERROR_ID( "X11Window.FrameExtentsNoWindow", "Window frame extents asked before the X11 window exists" );
            return false;
        }
        Display*     pDisplay = static_cast<Display*>( _pX11Display );
        const Window win      = static_cast<Window>( _x11Window );
        const Window root     = DefaultRootWindow( pDisplay );
        if ( Internal::hasEwmhWindowManager( pDisplay, root ) == false )
            return true; // 창 관리자가 없으면 틀도 없다(Xvfb 등) - 두께는 0 이다
        const Atom frameExtents = XInternAtom( pDisplay, "_NET_FRAME_EXTENTS", 0 );
        if ( Internal::readFrameExtents( pDisplay, win, frameExtents, outExtents ) )
            return true;

        if ( Internal::supportsFrameExtentsRequest( pDisplay, root ) == false )
        {
            // 틀 두께를 알 길이 없는 창 관리자다 - 칸 배치에만 쓰는 값이라 창을 못 띄울 일이 아니다. 칸 사이가 틀만큼 겹칠 수 있다.
            SW_LOG_INFO_ID( "X11Window.FrameExtentsNotAdvertised", "The window manager does not offer _NET_REQUEST_FRAME_EXTENTS - tiling ignores the window frame" );
            return true;
        }

        // 띄우기 전이라 창 관리자가 아직 적지 않았다 — 적어 달라고 청하고 그 속성의 알림을 기다린다(EWMH `_NET_REQUEST_FRAME_EXTENTS`).
        XEvent request{};
        request.xclient.type         = ClientMessage;
        request.xclient.window       = win;
        request.xclient.message_type = XInternAtom( pDisplay, "_NET_REQUEST_FRAME_EXTENTS", 0 );
        request.xclient.format       = 32;
        XSendEvent( pDisplay, root, 0, SubstructureRedirectMask | SubstructureNotifyMask, &request );
        XFlush( pDisplay );
        Internal::PropertyKey key{ win, frameExtents };
        XEvent                notify{};
        for ( int32 attempt = 0; attempt < Internal::kPollCount; ++attempt )
        {
            if ( XCheckIfEvent( pDisplay, &notify, &Internal::isPropertyNotify, reinterpret_cast<XPointer>( &key ) ) != 0 )
                break;
            pollfd connection{ ConnectionNumber( pDisplay ), POLLIN, 0 };
            (void)poll( &connection, 1, Internal::kPollMs );
        }
        if ( Internal::readFrameExtents( pDisplay, win, frameExtents, outExtents ) )
            return true;
        SW_LOG_ERROR_ID( "X11Window.FrameExtentsNotReported", "Window frame extents are unknown - the window manager did not answer _NET_REQUEST_FRAME_EXTENTS within %# ms",
                         Internal::kPollMs * Internal::kPollCount );
        return false;
    }

    bool X11Window::setFramePosition( int32 x, int32 y )
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return false;
        Display*     pDisplay = static_cast<Display*>( _pX11Display );
        const Window win      = static_cast<Window>( _x11Window );
        // 처음 매핑할 때 창 관리자는 USPosition 이 켜진 자리만 따른다(아니면 스스로 놓는다). 최소 크기 같은 다른 힌트는 지킨다.
        XSizeHints sizeHints{};
        long       suppliedFlags = 0;
        (void)XGetWMNormalHints( pDisplay, win, &sizeHints, &suppliedFlags ); // 힌트가 아직 없으면 빈 힌트에서 시작한다
        sizeHints.flags |= USPosition;
        sizeHints.x = x;
        sizeHints.y = y;
        XSetWMNormalHints( pDisplay, win, &sizeHints );
        XMoveWindow( pDisplay, win, x, y );
        XFlush( pDisplay );
        return true;
    }

    void X11Window::captureRestorePosition()
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return;

        Display*          pDisplay = static_cast<Display*>( _pX11Display );
        XWindowAttributes wa{};
        if ( XGetWindowAttributes( pDisplay, _x11Window, &wa ) != 0 )
        {
            _restoreX = wa.x;
            _restoreY = wa.y;
        }
    }

    void X11Window::clearRestorePosition()
    {
        // X11 에는 `CW_USEDEFAULT` 같은 값이 없다. 창 관리자에게 맡기는 관례적 시작점을 쓴다.
        _restoreX = kDefaultRestoreX;
        _restoreY = kDefaultRestoreY;
    }

    void X11Window::applyMinimumClientSize()
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return;

        Display*   pDisplay = static_cast<Display*>( _pX11Display );
        XSizeHints sizeHints{};
        sizeHints.flags      = PMinSize;
        sizeHints.min_width  = static_cast<int32>( _minClientWidth );
        sizeHints.min_height = static_cast<int32>( _minClientHeight );
        XSetWMNormalHints( pDisplay, static_cast<Window>( _x11Window ), &sizeHints );
        XFlush( pDisplay );
    }

    void X11Window::applyTitle()
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return;

        // 제목은 UTF-8 이다. XStoreName 은 WM_NAME 을 Latin-1(STRING)으로 적어 비-ASCII 글자가 깨진다. 창 관리자는 _NET_WM_NAME(UTF8_STRING)을
        // 먼저 읽으므로 그것을 같이 적는다(XStoreName 은 _NET_WM_NAME 을 모르는 이전 창 관리자를 위한 것이다).
        Display*     pDisplay = static_cast<Display*>( _pX11Display );
        const Window win      = static_cast<Window>( _x11Window );
        XStoreName( pDisplay, win, _arrTitle );
        const Atom netWmName  = XInternAtom( pDisplay, "_NET_WM_NAME", 0 );
        const Atom utf8String = XInternAtom( pDisplay, "UTF8_STRING", 0 );
        XChangeProperty( pDisplay, win, netWmName, utf8String, 8, PropModeReplace, reinterpret_cast<const uint8*>( _arrTitle ),
                         static_cast<int32>( std::strlen( _arrTitle ) ) );
        XFlush( pDisplay );
    }

    float32 X11Window::getContentScale() const
    {
        if ( _pX11Display == nullptr )
            return 1.0f;
        // 데스크톱의 배율은 리소스 문자열의 "Xft.dpi:\t144" 한 줄이다(xrdb · GNOME · KDE). 없으면 96 DPI 로 본다.
        const utf8* const pResource = XResourceManagerString( static_cast<Display*>( _pX11Display ) );
        if ( pResource == nullptr )
            return 1.0f;
        const utf8* const pKey = std::strstr( pResource, "Xft.dpi:" );
        if ( pKey == nullptr )
            return 1.0f;
        const string_view rest{ pKey + sizeof( "Xft.dpi:" ) - 1 };
        float32           dpi{ 0.0f };
        if ( StringUtil::parseFloat( rest.substr( 0, rest.find( '\n' ) ), dpi ) == false || dpi <= 0.0f )
            return 1.0f; // 줄이 숫자가 아니면 키가 없는 것과 같이 본다(위 주석)
        return dpi / kReferenceDPI;
    }

    bool X11Window::tryGetMonitors( vector<WindowMonitor>& outListMonitor ) const
    {
        outListMonitor.clear();
        if ( _pX11Display == nullptr )
            return false;
        _monitors.read( getContentScale(), outListMonitor );
        return true;
    }

    bool X11Window::processMessages()
    {
        if ( _pX11Display == nullptr )
            return _bShouldClose == SW_FALSE;

        Display* pDisplay = static_cast<Display*>( _pX11Display );
        expireClipboardIncr();
        while ( XPending( pDisplay ) > 0 )
        {
            XEvent event;
            XNextEvent( pDisplay, &event );

            // 뿌리 창의 모니터 소식은 창 이벤트가 아니다 - 처리기(ImGui · 입력 관리자)에 넘기지 않는다.
            bool bMonitorsChanged = false;
            if ( _monitors.consumeEvent( &event, bMonitorsChanged ) )
            {
                if ( bMonitorsChanged )
                    notifyMonitorsChanged();
                continue;
            }

            // 입력기가 먼저 본다(GLFW · SDL 과 같다). 가져간 키는 조합이 되어 콜백으로 돌아온다.
            if ( _inputMethod.filterEvent( &event ) )
                continue;
            // 키 코드 0 은 입력기가 확정 글을 건네려고 만든 가짜 누름이다 - 키가 아니다.
            if ( event.type == KeyPress && event.xkey.keycode == 0 )
            {
                _inputMethod.emitKeyText( &event );
                continue;
            }

            // 최소화 · 포커스는 외부 처리기(ImGui)가 이벤트를 먹기 전에 적는다. 최소화(iconify)는 창 관리자가 창을 내리는(unmap) 것으로 온다.
            // 포커스의 Grab · Ungrab 은 키보드를 잠깐 쥔 것이라(메뉴 · 드래그) 포커스 변화로 보지 않는다.
            const bool bOwnWindowEvent = event.xany.window == static_cast<Window>( _x11Window );
            const bool bMapChange      = bOwnWindowEvent && ( event.type == UnmapNotify || event.type == MapNotify );
            const bool bFocusEvent     = bOwnWindowEvent && ( event.type == FocusIn || event.type == FocusOut );
            const bool bGrabFocus      = bFocusEvent && ( event.xfocus.mode == NotifyGrab || event.xfocus.mode == NotifyUngrab );
            if ( bMapChange )
            {
                SW_LOG_INFO_ID( "X11Window.MapChanged", "[Window] main window %#", event.type == UnmapNotify ? "unmapped" : "mapped" );
                // 우리가 숨긴 창(무인 실행의 showWindow( false ))은 최소화가 아니다 - Win32 의 SW_HIDE 와 같다. 보이라고 한 창이 내려갔을 때만 창 관리자의 아이콘화다.
                setMinimizedState( event.type == UnmapNotify && isVisibleRequested() );
                if ( event.type == MapNotify )
                    requestInputFocus();
            }
            else if ( bFocusEvent && bGrabFocus == false )
            {
                SW_LOG_INFO_ID( "X11Window.FocusChanged", "[Window] main window focus %# (mode %#, detail %#)", event.type == FocusIn ? "in" : "out", event.xfocus.mode, event.xfocus.detail );
                setFocusState( event.type == FocusIn );
            }

            // 입력기에는 우리 창 모두의 포커스를 알린다 - 떼어 낸 창의 글 칸도 조합을 받는다. Grab · Ungrab 은 위와 같이 포커스 변화가 아니다.
            const bool bAnyWindowFocusChange = ( event.type == FocusIn || event.type == FocusOut ) && event.xfocus.mode != NotifyGrab && event.xfocus.mode != NotifyUngrab;
            if ( bAnyWindowFocusChange )
                _inputMethod.onFocusChanged( static_cast<uint64>( event.xany.window ), event.type == FocusIn );

            // 선택 이벤트는 입력이 아니다 — 외부 처리기(ImGui)가 먹기 전에 답한다. 답하지 않으면 붙여넣는 프로그램이 시간 제한까지 기다린다.
            if ( handleSelectionEvent( &event ) || handleIncrPropertyEvent( &event ) )
                continue;

            bool bConsumed = false;
            if ( _customHandler.isBound() )
            {
                NativeWindowEvent windowEvent{};
                windowEvent.setNativeWindow( NativeSurfaceKind::X11, reinterpret_cast<void*>( static_cast<uintptr_t>( _x11Window ) ) );
                windowEvent._message = NativeWindowEvent::kMessageX11;
                windowEvent._wParam  = 0;
                windowEvent._lParam  = reinterpret_cast<intptr_t>( &event );
                bConsumed            = _customHandler.get()( windowEvent );
            }
            // 키 뒤에 그 글자가 온다(Win32 의 WM_KEYDOWN 뒤 WM_CHAR 와 같다). 글을 읽는 곳은 여기 하나다 - 머리말 `X11InputMethod.h`.
            if ( event.type == KeyPress )
                _inputMethod.emitKeyText( &event );
            if ( bConsumed )
                continue;

            if ( event.type == ClientMessage )
            {
                if ( event.xclient.window == static_cast<Window>( _x11Window ) &&
                     static_cast<Atom>( event.xclient.data.l[0] ) == static_cast<Atom>( _x11WmDelete ) )
                {
                    if ( _bRecreating == SW_FALSE )
                    {
                        if ( tryBeginClose() == false )
                            continue;
                        return false;
                    }
                }
            }
            else if ( event.type == ConfigureNotify )
            {
                if ( event.xconfigure.window != static_cast<Window>( _x11Window ) )
                    continue;

                uint32 newW = static_cast<uint32>( event.xconfigure.width );
                uint32 newH = static_cast<uint32>( event.xconfigure.height );
                if ( newW != _width || newH != _height )
                {
                    _width  = newW;
                    _height = newH;
                    if ( _onResize.isBound() )
                        _onResize.get()( _width, _height );
                }
            }
        }

        return _bShouldClose == SW_FALSE;
    }

    void X11Window::waitForMessages( uint32 timeoutMilliseconds )
    {
        if ( _pX11Display == nullptr )
            return;
        // XPending 이 보낼 요청을 내보내고 이미 읽힌 이벤트를 센다 — 큐에 있으면 소켓을 기다리지 않는다.
        Display* pDisplay = static_cast<Display*>( _pX11Display );
        if ( XPending( pDisplay ) > 0 )
            return;
        pollfd pollEntry{ ConnectionNumber( pDisplay ), POLLIN, 0 };
        (void)poll( &pollEntry, 1, static_cast<int32>( timeoutMilliseconds ) );
    }

    bool X11Window::writeClipboardText( string_view text )
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return false;
        Display*     pDisplay  = static_cast<Display*>( _pX11Display );
        const Window win       = static_cast<Window>( _x11Window );
        const Atom   clipboard = XInternAtom( pDisplay, "CLIPBOARD", 0 );
        _clipboardText         = string( text );
        XSetSelectionOwner( pDisplay, clipboard, win, CurrentTime );
        XFlush( pDisplay );
        if ( XGetSelectionOwner( pDisplay, clipboard ) == win )
            return true;
        _clipboardText.clear();
        return false;
    }

    bool X11Window::readClipboardText( string& outText )
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return false;
        Display*     pDisplay  = static_cast<Display*>( _pX11Display );
        const Window win       = static_cast<Window>( _x11Window );
        const Atom   clipboard = XInternAtom( pDisplay, "CLIPBOARD", 0 );
        const Window owner     = XGetSelectionOwner( pDisplay, clipboard );
        if ( owner == 0 )
            return false;
        if ( owner == win )
        {
            outText = _clipboardText;
            return true;
        }

        // 주인에게 UTF-8 로 바꿔 이 창의 속성에 적어 달라고 청하고, 답(`SelectionNotify`)만 골라 기다린다. 다른 이벤트는 큐에 남아 `processMessages` 가 받는다.
        const Atom utf8String = XInternAtom( pDisplay, "UTF8_STRING", 0 );
        const Atom property   = XInternAtom( pDisplay, "SW_CLIPBOARD", 0 );
        XConvertSelection( pDisplay, clipboard, utf8String, property, win, CurrentTime );
        XFlush( pDisplay );
        XEvent reply{};
        if ( X11ClipboardWait::waitForEvent( pDisplay, win, SelectionNotify, reply, []( const XEvent& )
        { return true; } ) == false )
        {
            SW_LOG_WARNING_ID( "X11Window.ClipboardOwnerDidNot", "Clipboard owner did not answer within %# ms - nothing pasted", kClipboardPollMs * kClipboardPollCount );
            return false;
        }
        if ( reply.xselection.property == 0 )
            return false; // 주인이 UTF-8 글을 주지 않았다(이미지 등)

        X11ClipboardWait::discardQueuedPropertyEvents( pDisplay, win ); // INCR 표지를 쓴 낡은 알림을 첫 조각으로 오인하지 않는다
        string bytes;
        Atom   type = 0;
        if ( X11ClipboardWait::takeProperty( pDisplay, win, property, type, bytes ) == false )
            return false;
        if ( type == utf8String )
        {
            outText = std::move( bytes );
            return true;
        }
        if ( type != XInternAtom( pDisplay, "INCR", 0 ) )
            return false;

        // INCR(ICCCM 2.7.2) — 큰 글은 조각으로 온다. INCR 속성을 지운 것(위 `takeProperty`)이 시작 신호이고, 주인이 조각을 쓸 때마다 PropertyNotify(새 값)가 온다.
        // 조각을 읽고 지우면 다음 조각이 오고, 길이 0 인 조각이 끝이다. 창은 PropertyChangeMask 를 늘 받는다(`initializeWindow`) — 시작 신호 전에 받고 있어야 첫 조각을 놓치지 않는다.
        outText.clear();
        for ( ;; )
        {
            XEvent     notify{};
            const auto isNewValue = [property]( const XEvent& event )
            {
                return event.xproperty.atom == property && event.xproperty.state == PropertyNewValue;
            };
            if ( X11ClipboardWait::waitForEvent( pDisplay, win, PropertyNotify, notify, isNewValue ) == false )
            {
                SW_LOG_WARNING_ID( "X11Window.ClipboardOwnerStoppedSending", "Clipboard owner stopped sending a large (INCR) text after %# bytes - nothing pasted", static_cast<uint64>( outText.size() ) );
                outText.clear();
                return false;
            }
            string chunk;
            Atom   chunkType = 0;
            if ( X11ClipboardWait::takeProperty( pDisplay, win, property, chunkType, chunk ) == false )
            {
                outText.clear();
                return false;
            }
            if ( chunkType == 0 )
                continue; // 속성이 없다 — 조각이 아니다. 끝 신호는 길이 0 인 속성이다
            if ( chunk.empty() )
                return true;
            outText += chunk;
        }
    }

    void X11Window::beginClipboardIncr( uint64 requestor, uint64 property )
    {
        Display* pDisplay = static_cast<Display*>( _pX11Display );
        // 같은 창 · 속성의 옛 건은 상대가 포기한 것이다.
        VectorUtil::removeIf( _listClipboardIncr, [requestor, property]( const X11ClipboardIncrTransfer& transfer )
        { return transfer._requestor == requestor && transfer._property == property; } );

        // 상대의 속성 삭제를 놓치지 않도록 표지를 적기 전에 그 창의 PropertyNotify 를 받기 시작한다(ICCCM 2.7.2).
        XSelectInput( pDisplay, static_cast<Window>( requestor ), PropertyChangeMask );
        const int64 lowerBoundBytes = static_cast<int64>( _clipboardText.size() ); // 형식 32 는 C `long` 배열이다
        XChangeProperty( pDisplay, static_cast<Window>( requestor ), static_cast<Atom>( property ), XInternAtom( pDisplay, "INCR", 0 ), 32, PropModeReplace,
                         reinterpret_cast<const uint8*>( &lowerBoundBytes ), 1 );
        _listClipboardIncr.emplace_back( requestor, property, _clipboardText, Deadline::afterMilliseconds( kClipboardIncrIdleMilliseconds ) );
    }

    bool X11Window::handleIncrPropertyEvent( const void* pEvent )
    {
        const XEvent& event = *static_cast<const XEvent*>( pEvent );
        if ( event.type != PropertyNotify || event.xproperty.state != PropertyDelete )
            return false;

        Display* pDisplay = static_cast<Display*>( _pX11Display );
        for ( size_t index = 0; index < _listClipboardIncr.size(); ++index )
        {
            X11ClipboardIncrTransfer& transfer = _listClipboardIncr[index];
            if ( transfer._requestor != event.xproperty.window || transfer._property != event.xproperty.atom )
                continue;

            const Window requestor = static_cast<Window>( transfer._requestor );
            if ( transfer._bFinished )
            {
                // 끝 신호를 상대가 지웠다. 건을 마치고 그 창의 알림을 끊는다.
                XSelectInput( pDisplay, requestor, NoEventMask );
                VectorUtil::removeAtSwap( _listClipboardIncr, index );
                return true;
            }
            const size_t chunkBytes = MathUtil::min( transfer._text.size() - transfer._offset, X11ClipboardWait::getMaxSingleTransferBytes( pDisplay ) / 2 );
            XChangeProperty( pDisplay, requestor, static_cast<Atom>( transfer._property ), XInternAtom( pDisplay, "UTF8_STRING", 0 ), 8, PropModeReplace,
                             reinterpret_cast<const uint8*>( transfer._text.c_str() + transfer._offset ), static_cast<int32>( chunkBytes ) );
            XFlush( pDisplay );
            transfer._offset += chunkBytes;
            transfer._bFinished = chunkBytes == 0; // 길이 0 조각이 끝 신호다
            transfer._deadline  = Deadline::afterMilliseconds( kClipboardIncrIdleMilliseconds );
            return true;
        }
        return false;
    }

    void X11Window::expireClipboardIncr()
    {
        if ( _listClipboardIncr.empty() )
            return;
        Display* pDisplay = static_cast<Display*>( _pX11Display );
        for ( const X11ClipboardIncrTransfer& transfer : _listClipboardIncr )
        {
            if ( transfer._deadline.isExpired() )
            {
                SW_LOG_WARNING_ID( "X11Window.ClipboardPastingProgramStopped", "Clipboard pasting program stopped asking for the large (INCR) text after %# of %# bytes - dropped", static_cast<uint64>( transfer._offset ),
                                   static_cast<uint64>( transfer._text.size() ) );
                XSelectInput( pDisplay, static_cast<Window>( transfer._requestor ), NoEventMask );
            }
        }
        VectorUtil::removeIf( _listClipboardIncr, []( const X11ClipboardIncrTransfer& transfer )
        { return transfer._deadline.isExpired(); } );
    }

    bool X11Window::handleSelectionEvent( const void* pEvent )
    {
        const XEvent& event    = *static_cast<const XEvent*>( pEvent );
        Display*      pDisplay = static_cast<Display*>( _pX11Display );
        if ( event.type == SelectionClear )
        {
            // 다른 프로그램이 클립보드를 가져갔다 — 들고 있던 글은 더 내줄 일이 없다.
            if ( event.xselectionclear.selection == XInternAtom( pDisplay, "CLIPBOARD", 0 ) )
                _clipboardText.clear();
            return true;
        }
        if ( event.type != SelectionRequest )
            return false;

        // 붙여넣는 프로그램에 답한다: TARGETS 면 줄 수 있는 형식, UTF8_STRING 이면 글. 그 밖의 형식은 속성 0 으로 거절한다.
        const XSelectionRequestEvent& request    = event.xselectionrequest;
        const Atom                    targets    = XInternAtom( pDisplay, "TARGETS", 0 );
        const Atom                    utf8String = XInternAtom( pDisplay, "UTF8_STRING", 0 );
        const Atom                    property   = request.property != 0 ? request.property : request.target; // 옛 클라이언트는 속성 없이 청한다(ICCCM)
        const bool                    bOwned     = request.selection == XInternAtom( pDisplay, "CLIPBOARD", 0 ) && request.owner == static_cast<Window>( _x11Window );
        XSelectionEvent               reply{};
        reply.type      = SelectionNotify;
        reply.display   = request.display;
        reply.requestor = request.requestor;
        reply.selection = request.selection;
        reply.target    = request.target;
        reply.time      = request.time;
        reply.property  = 0;
        if ( bOwned && request.target == targets )
        {
            const Atom arrTarget[] = { targets, utf8String };
            XChangeProperty( pDisplay, request.requestor, property, XA_ATOM, 32, PropModeReplace, reinterpret_cast<const uint8*>( arrTarget ), 2 );
            reply.property = property;
        }
        else if ( bOwned && request.target == utf8String )
        {
            // 요청 한 번보다 큰 글은 INCR 로 나눠 보낸다(표지만 지금 적고 조각은 `handleIncrPropertyEvent` 가 상대가 지울 때마다 적는다).
            if ( _clipboardText.size() > X11ClipboardWait::getMaxSingleTransferBytes( pDisplay ) )
            {
                beginClipboardIncr( request.requestor, property );
                reply.property = property;
            }
            else
            {
                XChangeProperty( pDisplay, request.requestor, property, utf8String, 8, PropModeReplace, reinterpret_cast<const uint8*>( _clipboardText.c_str() ),
                                 static_cast<int32>( _clipboardText.size() ) );
                reply.property = property;
            }
        }
        XEvent replyEvent{};
        replyEvent.xselection = reply;
        XSendEvent( pDisplay, request.requestor, 0, 0, &replyEvent );
        XFlush( pDisplay );
        return true;
    }

    bool X11Window::setCursorShape( CursorShape shape )
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return false;
        Display* pDisplay = static_cast<Display*>( _pX11Display );
        uint64&  cached   = _arrShapeCursor[static_cast<size_t>( shape )];
        if ( cached == 0 )
        {
            uint32 fontCursorID = XC_left_ptr;
            switch ( shape )
            {
                case CursorShape::Arrow:
                {
                    fontCursorID = XC_left_ptr;
                    break;
                }
                case CursorShape::Hand:
                {
                    fontCursorID = XC_hand2;
                    break;
                }
                case CursorShape::TextBeam:
                {
                    fontCursorID = XC_xterm;
                    break;
                }
                case CursorShape::ResizeHorizontal:
                {
                    fontCursorID = XC_sb_h_double_arrow;
                    break;
                }
                case CursorShape::ResizeVertical:
                {
                    fontCursorID = XC_sb_v_double_arrow;
                    break;
                }
                case CursorShape::NotAllowed:
                {
                    fontCursorID = XC_X_cursor;
                    break;
                }
            }
            cached = XCreateFontCursor( pDisplay, fontCursorID );
        }
        XDefineCursor( pDisplay, _x11Window, cached );
        XFlush( pDisplay );
        return true;
    }

    bool X11Window::setCursorVisible( bool bVisible )
    {
        if ( _pX11Display == nullptr || _x11Window == 0 )
            return false;
        Display* pDisplay = static_cast<Display*>( _pX11Display );
        if ( bVisible )
        {
            XUndefineCursor( pDisplay, _x11Window );
            XFlush( pDisplay );
            return true;
        }
        if ( _invisibleCursor == 0 )
        {
            XColor      dummyColor{};
            const uint8 arrPixelData[1] = { 0 };
            Pixmap      pixmap          = XCreateBitmapFromData( pDisplay, _x11Window, reinterpret_cast<const utf8*>( arrPixelData ), 1, 1 );
            _invisibleCursor            = XCreatePixmapCursor( pDisplay, pixmap, pixmap, &dummyColor, &dummyColor, 0, 0 );
            XFreePixmap( pDisplay, pixmap );
        }
        XDefineCursor( pDisplay, _x11Window, _invisibleCursor );
        XFlush( pDisplay );
        return true;
    }

    void X11Window::applyTextInputWanted( bool bWanted )
    {
        _inputMethod.setWanted( bWanted );
        if ( _pX11Display != nullptr )
            XFlush( static_cast<Display*>( _pX11Display ) );
    }

    void X11Window::applyTextInputArea( const WindowTextInputArea& area )
    {
        _inputMethod.setArea( area );
        if ( _pX11Display != nullptr )
            XFlush( static_cast<Display*>( _pX11Display ) );
    }

    void X11Window::releaseInputResources()
    {
        Display* pDisplay = static_cast<Display*>( _pX11Display );
        for ( uint64& cursor : _arrShapeCursor )
        {
            if ( cursor != 0 )
                XFreeCursor( pDisplay, cursor );
            cursor = 0;
        }
        if ( _invisibleCursor != 0 )
            XFreeCursor( pDisplay, _invisibleCursor );
        _invisibleCursor = 0;
        _inputMethod.shutdown();
    }
} // namespace sw
#endif
