/**
 * @file X11DevConsoleWindow.cpp
 * @brief 개발 콘솔 오버레이의 X11 창입니다 — 게임 창의 자식 창에 기본 글꼴로 줄을 그린다.
 */
#include "pch.h"

#include "Engine/Window/DevConsoleOverlay.h"

#if SW_DEV_COMMANDS_ENABLED && defined( SW_PLATFORM_LINUX )

    #include "Core/Log/Logger.h"

    #include "Engine/Window/IWindow.h"
    #include "Engine/Window/NativeWindowEvent.h"

    #include "Core/Common/PlatformOsHeaders.h"

namespace sw
{
    namespace
    {
        struct X11DevConsoleWindowInternal
        {
            static constexpr int32 kLineHeight = 16;
            static constexpr int32 kPadding    = 6;
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @class X11DevConsoleWindow
     * @brief 게임 창 위쪽을 덮는 자식 창입니다. 키보드는 게임 창이 받고(`decodeKey`), 이 창은 그리기만 합니다.
     */
    class X11DevConsoleWindow final : public IDevConsoleWindow
    {
    public:
        X11DevConsoleWindow( Display* pDisplay, ::Window ownerWindow )
            : _pDisplay{ pDisplay }
            , _ownerWindow{ ownerWindow }
            , _window{ 0 }
            , _gc{ nullptr }
            , _listLine{}
            , _listErrorFlag{}
        {
        }

        ~X11DevConsoleWindow() override
        {
            if ( _gc != nullptr )
                XFreeGC( _pDisplay, _gc );
            if ( _window != 0 )
                XDestroyWindow( _pDisplay, _window );
            XFlush( _pDisplay );
        }

        X11DevConsoleWindow( const X11DevConsoleWindow& )            = delete;
        X11DevConsoleWindow& operator=( const X11DevConsoleWindow& ) = delete;

        bool initialize()
        {
            const int32 screen = DefaultScreen( _pDisplay );
            _window            = XCreateSimpleWindow( _pDisplay, _ownerWindow, 0, 0, 1, 1, 0, WhitePixel( _pDisplay, screen ), BlackPixel( _pDisplay, screen ) );
            if ( _window == 0 )
                return false;
            XSelectInput( _pDisplay, _window, ExposureMask );
            _gc = XCreateGC( _pDisplay, _window, 0, nullptr );
            return _gc != nullptr;
        }

        bool decodeKey( const NativeWindowEvent& event, DevConsoleKey& outKey ) const override
        {
            outKey = DevConsoleKey{};
            if ( event._message != NativeWindowEvent::kMessageX11 || event._lParam == 0 )
                return false;
            XEvent* pEvent = reinterpret_cast<XEvent*>( event._lParam );
            if ( pEvent->type == Expose && pEvent->xexpose.window == _window )
            {
                const_cast<X11DevConsoleWindow*>( this )->redraw();
                return false;
            }
            if ( pEvent->type != KeyPress )
                return false;

            utf8        arrText[8]{};
            KeySym      keySym = 0;
            const int32 length = XLookupString( &pEvent->xkey, arrText, static_cast<int32>( sizeof( arrText ) ), &keySym, nullptr );
            switch ( keySym )
            {
                case XK_grave:
                case XK_asciitilde:
                {
                    outKey._kind = DevConsoleKey::Kind::Toggle;
                    return true;
                }
                case XK_Escape:
                {
                    outKey._kind = DevConsoleKey::Kind::Close;
                    return true;
                }
                case XK_BackSpace:
                {
                    outKey._kind = DevConsoleKey::Kind::Backspace;
                    return true;
                }
                case XK_Return:
                case XK_KP_Enter:
                {
                    outKey._kind = DevConsoleKey::Kind::Enter;
                    return true;
                }
                case XK_Tab:
                {
                    outKey._kind = DevConsoleKey::Kind::Tab;
                    return true;
                }
                case XK_Up:
                {
                    outKey._kind = DevConsoleKey::Kind::HistoryBack;
                    return true;
                }
                case XK_Down:
                {
                    outKey._kind = DevConsoleKey::Kind::HistoryForward;
                    return true;
                }
                default:
                {
                    break;
                }
            }
            if ( length == 1 && static_cast<uint8>( arrText[0] ) >= 0x20 && static_cast<uint8>( arrText[0] ) < 0x7F )
            {
                outKey._kind      = DevConsoleKey::Kind::Character;
                outKey._codepoint = static_cast<uint8>( arrText[0] );
                return true;
            }
            return false;
        }

        void setVisible( bool bVisible ) override
        {
            if ( bVisible )
            {
                updatePlacement();
                XMapRaised( _pDisplay, _window );
            }
            else
            {
                XUnmapWindow( _pDisplay, _window );
            }
            XFlush( _pDisplay );
        }

        void present( const vector<string>& listLine, const vector<uint8>& listErrorFlag ) override
        {
            _listLine      = listLine;
            _listErrorFlag = listErrorFlag;
            updatePlacement();
            redraw();
        }

    private:
        void updatePlacement()
        {
            XWindowAttributes attributes{};
            if ( XGetWindowAttributes( _pDisplay, _ownerWindow, &attributes ) == 0 )
                return;
            const int32 lineCount = static_cast<int32>( _listLine.empty() ? 1 : _listLine.size() );
            const int32 height    = lineCount * X11DevConsoleWindowInternal::kLineHeight + X11DevConsoleWindowInternal::kPadding * 2;
            XMoveResizeWindow( _pDisplay, _window, 0, 0, static_cast<uint32>( attributes.width > 1 ? attributes.width : 1 ), static_cast<uint32>( height ) );
        }

        void redraw()
        {
            XClearWindow( _pDisplay, _window );
            const int32 screen = DefaultScreen( _pDisplay );
            int32       lineY  = X11DevConsoleWindowInternal::kPadding + X11DevConsoleWindowInternal::kLineHeight - 4;
            for ( size_t index = 0; index < _listLine.size(); ++index )
            {
                XSetForeground( _pDisplay, _gc, WhitePixel( _pDisplay, screen ) );
                XDrawString( _pDisplay, _window, _gc, X11DevConsoleWindowInternal::kPadding, lineY, _listLine[index].c_str(), static_cast<int32>( _listLine[index].size() ) );
                lineY += X11DevConsoleWindowInternal::kLineHeight;
            }
            XFlush( _pDisplay );
        }

    private:
        Display*       _pDisplay;
        ::Window       _ownerWindow;
        ::Window       _window;
        GC             _gc;
        vector<string> _listLine;
        vector<uint8>  _listErrorFlag;
    };
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "DevConsoleOverlay" );

    unique_ptr<IDevConsoleWindow> IDevConsoleWindow::createPlatform( IWindow& owner )
    {
        Display*       pDisplay    = static_cast<Display*>( owner.getNativeDisplay() );
        const ::Window ownerWindow = static_cast<::Window>( reinterpret_cast<uintptr_t>( owner.getNativeHandle() ) );
        if ( pDisplay == nullptr || ownerWindow == 0 )
            return nullptr;
        unique_ptr<X11DevConsoleWindow> pWindow = make_unique<X11DevConsoleWindow>( pDisplay, ownerWindow );
        if ( pWindow->initialize() == false )
        {
            SW_LOG_WARNING( "Dev console overlay: could not create the overlay window" );
            return nullptr;
        }
        return pWindow;
    }
} // namespace sw

#endif
