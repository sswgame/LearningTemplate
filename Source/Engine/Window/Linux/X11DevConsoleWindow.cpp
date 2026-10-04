/**
 * @file X11DevConsoleWindow.cpp
 * @brief 개발 콘솔의 X11 창입니다 — 게임 창의 자식 창에 기본 글꼴로 줄을 그린다.
 */
#include "pch.h"

#include "Engine/Window/DevConsoleWindow.h"

#if SW_DEV_COMMANDS_ENABLED && defined( SW_PLATFORM_LINUX )

    #include "Core/Log/Logger.h"

    #include "Engine/Window/IWindow.h"

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
     * @brief 게임 창 위쪽을 덮는 자식 창입니다. 키보드는 게임 창이 받아 `InputManager` 로 가고, 이 창은 그리기만 합니다. 열려 있는 동안 `present` 가 프레임마다 다시 그리므로 Expose 를 따로 받지 않습니다.
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
            _gc = XCreateGC( _pDisplay, _window, 0, nullptr );
            return _gc != nullptr;
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
    SW_LOG_CALLER( "DevConsole" );

    unique_ptr<IDevConsoleWindow> IDevConsoleWindow::createPlatform( IWindow& owner )
    {
        Display*       pDisplay    = static_cast<Display*>( owner.getNativeDisplay() );
        const ::Window ownerWindow = static_cast<::Window>( reinterpret_cast<uintptr_t>( owner.getNativeHandle() ) );
        if ( pDisplay == nullptr || ownerWindow == 0 )
            return nullptr;
        unique_ptr<X11DevConsoleWindow> pWindow = make_unique<X11DevConsoleWindow>( pDisplay, ownerWindow );
        if ( pWindow->initialize() == false )
        {
            SW_LOG_WARNING( "Dev console: could not create the console window" );
            return nullptr;
        }
        return pWindow;
    }
} // namespace sw

#endif
