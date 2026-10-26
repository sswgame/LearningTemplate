#include "pch.h"

#include "Engine/Window/Linux/X11Window.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Window/NativeWindowEvent.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    #include "Core/Common/X11Headers.h"

    #include <poll.h>
#endif

#include <cstring>

namespace sw
{
    SW_LOG_CALLER( "X11Window" );

    namespace
    {
        /** @brief 복원 위치의 "알아서" 값입니다. X11 에는 `CW_USEDEFAULT` 같은 약속이 없어 좌표를 하나 정해 둡니다. */
        constexpr int32 kDefaultRestoreX = 100;
        /** @brief 위와 같습니다(세로 좌표). */
        constexpr int32 kDefaultRestoreY = 100;
    } // namespace

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    namespace
    {
        /** @brief 클립보드 주인에게 변환을 청한 뒤 답(`SelectionNotify`)을 기다리는 한 번의 길이(ms)입니다. */
        constexpr int32 kClipboardPollMs = 10;
        /** @brief 위 기다림의 최대 횟수입니다. 붙여넣기 한 번이 메인 스레드를 최대 200 ms 멈춘다(GLFW · SDL 도 같은 동기 대기다). */
        constexpr int32 kClipboardPollCount = 20;
        /** @brief 클립보드 속성을 한 번에 읽는 최대 길이(32 비트 단위)입니다. 64 MB 다. */
        constexpr int64 kClipboardMaxLongs = int64{ 1 } << 24;
        /** @brief INCR 로 보내는 중 상대가 다음 조각을 청하지 않고 기다려 주는 시간(ms)입니다. 넘으면 상대가 사라진 것으로 보고 건을 버린다. */
        constexpr int64 kClipboardIncrIdleMilliseconds = 10 * constant::kMillisecondsPerSecond;

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
                    SW_LOG_WARNING( "Clipboard property could not be read (X error %#) - nothing pasted", result );
                return result == 0;
            }
        };
    } // namespace
#endif

    X11Window::X11Window()
        : _clipboardText{}
        , _listClipboardIncr{}
        , _pX11Display{ nullptr }
        , _x11Window{ 0 }
        , _x11WmDelete{ 0 }
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

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    bool X11Window::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        _width  = width;
        _height = height;
        if ( storeTitle( pTitle ) == false )
            return false; // 오류는 storeTitle 이 남겼다

        Display* pDisplay = XOpenDisplay( nullptr );
        if ( pDisplay == nullptr )
        {
            SW_LOG_ERROR( "Failed to open X11 Display!" );
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
        XFlush( pDisplay );

        _pX11Display  = pDisplay;
        _x11Window    = win;
        _x11WmDelete  = wmDeleteMessage;
        _bShouldClose = SW_FALSE;
        applyMinimumClientSize(); // 다시 만든 창(recreate)도 같은 하한을 가진다
        applyTitle();

        SW_LOG_INFO( "Native X11 Window created successfully! (%#×%#)", width, height );
        return true;
    }

    void X11Window::destroy()
    {
        if ( _pX11Display != nullptr )
        {
            Display* pDisplay = static_cast<Display*>( _pX11Display );
            if ( _x11Window != 0 )
            {
                XDestroyWindow( pDisplay, _x11Window );
                _x11Window = 0;
            }
            XCloseDisplay( pDisplay );
            _pX11Display = nullptr;
        }
        _clipboardText.clear(); // 창이 없어지면 서버가 선택 주인을 거둔다
        _listClipboardIncr.clear();
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
            XResizeWindow( pDisplay, static_cast<Window>( _x11Window ), width, height );
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
        const float32 dpi = static_cast<float32>( std::strtod( pKey + sizeof( "Xft.dpi:" ) - 1, nullptr ) );
        return dpi > 0.0f ? dpi / kReferenceDpi : 1.0f;
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

            // 선택 이벤트는 입력이 아니다 — 외부 처리기(ImGui)가 먹기 전에 답한다. 답하지 않으면 붙여넣는 프로그램이 시간 제한까지 기다린다.
            if ( handleSelectionEvent( &event ) || handleIncrPropertyEvent( &event ) )
                continue;

            if ( _customHandler.isBound() )
            {
                NativeWindowEvent windowEvent{};
                windowEvent.setNativeWindow( NativeSurfaceKind::X11, reinterpret_cast<void*>( static_cast<uintptr_t>( _x11Window ) ) );
                windowEvent._message = NativeWindowEvent::kMessageX11;
                windowEvent._wParam  = 0;
                windowEvent._lParam  = reinterpret_cast<intptr_t>( &event );
                if ( _customHandler.get()( windowEvent ) )
                    continue;
            }

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
            SW_LOG_WARNING( "Clipboard owner did not answer within %# ms - nothing pasted", kClipboardPollMs * kClipboardPollCount );
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
                SW_LOG_WARNING( "Clipboard owner stopped sending a large (INCR) text after %# bytes - nothing pasted", static_cast<uint64>( outText.size() ) );
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
            const size_t chunkBytes = std::min( transfer._text.size() - transfer._offset, X11ClipboardWait::getMaxSingleTransferBytes( pDisplay ) / 2 );
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
                SW_LOG_WARNING( "Clipboard pasting program stopped asking for the large (INCR) text after %# of %# bytes - dropped", static_cast<uint64>( transfer._offset ),
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
#else
    bool X11Window::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        std::ignore = pTitle;
        _width      = width;
        _height     = height;
        return true;
    }

    void X11Window::destroy()
    {
    }

    bool X11Window::setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
    {
        (void)width;
        (void)height;
        _displayMode = mode;
        return false;
    }

    void X11Window::captureRestorePosition()
    {
    }

    void X11Window::clearRestorePosition()
    {
        // 이 플랫폼에서는 창을 만들지 않지만, 생성자가 부르므로 값은 채워 둔다.
        _restoreX = kDefaultRestoreX;
        _restoreY = kDefaultRestoreY;
    }

    void X11Window::applyMinimumClientSize()
    {
    }

    void X11Window::applyTitle()
    {
    }

    bool X11Window::processMessages()
    {
        return _bShouldClose == SW_FALSE;
    }

    void X11Window::applyWindowVisibility( bool bShow )
    {
        (void)bShow;
    }

    bool X11Window::isVisible() const
    {
        return false;
    }

    float32 X11Window::getContentScale() const
    {
        return 1.0f;
    }
#endif
} // namespace sw
