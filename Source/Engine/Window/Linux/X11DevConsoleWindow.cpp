/**
 * @file X11DevConsoleWindow.cpp
 * @brief 개발 콘솔의 X11 창입니다. 게임 창의 자식 창에 iso10646-1 코어 글꼴로 UTF-8 줄을 그립니다(`XDrawString16`, 기본 다국어 평면).
 */
#include "pch.h"

#include "Engine/Window/DevConsoleWindow.h"

#if SW_DEV_COMMANDS_ENABLED && defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )

    #include "Core/Log/Logger.h"

    #include "Engine/Window/IWindow.h"

    #include "Core/Common/PlatformOsHeaders.h"
    #include "Core/Common/X11Headers.h"

namespace sw
{
    namespace
    {
        struct X11DevConsoleWindowInternal
        {
            static constexpr int32 kLineHeight = 16;
            static constexpr int32 kPadding    = 6;
            /// 코어 X 글꼴 후보입니다(앞에서부터 처음 열리는 것). 모두 UCS-2(iso10646-1) 글꼴이라 로캘(`setlocale` 을 아무도 부르지 않는다)과 무관하게 UTF-8 글을 그릴 수 있다.
            static constexpr const utf8* kArrFontPattern[] = { "-*-*-medium-r-normal--16-*-*-*-*-*-iso10646-1", "-*-*-medium-r-*--14-*-*-*-*-*-iso10646-1", "-*-*-*-*-*--*-*-*-*-*-*-iso10646-1" };

            /**
             * @brief UTF-8 글을 UCS-2(`XDrawString16` 의 글자)로 바꿉니다. 기본 다국어 평면 밖(이모지 등)과 잘못된 바이트는 U+FFFD 한 글자가 된다.
             * @details `XDrawString` 은 Latin-1 이라 한글 · 일본어가 깨진다. 글자 모양은 X 서버의 iso10646-1 글꼴이 그 글자를 가진 만큼만 나온다.
             */
            static void decodeUtf8( const string& text, vector<XChar2b>& outListChar )
            {
                outListChar.clear();
                const size_t size = text.size();
                size_t       at   = 0;
                while ( at < size )
                {
                    const uint32 lead   = static_cast<uint8>( text[at] );
                    uint32       length = 1;
                    uint32       point  = 0xFFFD;
                    if ( lead < 0x80 )
                        point = lead;
                    else if ( lead >= 0xC2 && lead < 0xE0 )
                    {
                        length = 2;
                        point  = lead & 0x1F;
                    }
                    else if ( lead >= 0xE0 && lead < 0xF0 )
                    {
                        length = 3;
                        point  = lead & 0x0F;
                    }
                    else if ( lead >= 0xF0 && lead < 0xF5 )
                    {
                        length = 4;
                        point  = lead & 0x07;
                    }
                    bool bValid = length == 1 ? lead < 0x80 : at + length <= size;
                    for ( uint32 index = 1; bValid && index < length; ++index )
                    {
                        const uint32 next = static_cast<uint8>( text[at + index] );
                        bValid            = ( next & 0xC0 ) == 0x80;
                        point             = ( point << 6 ) | ( next & 0x3F );
                    }
                    if ( bValid == false || point > 0xFFFF )
                        point = 0xFFFD;
                    XChar2b character{};
                    character.byte1 = static_cast<uint8>( point >> 8 );
                    character.byte2 = static_cast<uint8>( point & 0xFF );
                    outListChar.push_back( character );
                    at += bValid ? length : 1;
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @class X11DevConsoleWindow
     * @brief 게임 창 위쪽을 덮는 자식 창입니다. 키보드는 게임 창이 받아 `InputManager` 로 보내고, 이 창은 그리기만 합니다. 열려 있는 동안 `present` 가 프레임마다 다시 그리므로 Expose 를 따로 받지 않습니다.
     */
    class X11DevConsoleWindow final : public IDevConsoleWindow
    {
    public:
        X11DevConsoleWindow( Display* pDisplay, ::Window ownerWindow )
            : _pDisplay{ pDisplay }
            , _ownerWindow{ ownerWindow }
            , _window{ 0 }
            , _gc{ nullptr }
            , _pFont{ nullptr }
            , _listLine{}
            , _listErrorFlag{}
        {
        }

        ~X11DevConsoleWindow() override
        {
            if ( _pFont != nullptr )
                XFreeFont( _pDisplay, _pFont );
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
            if ( _gc == nullptr )
                return false;
            // 글꼴이 없으면 콘솔을 세우지 않는다(Latin-1 로 깨뜨려 그리지 않는다). 호출한 쪽이 경고를 남긴다.
            for ( const utf8* pPattern : X11DevConsoleWindowInternal::kArrFontPattern )
            {
                _pFont = XLoadQueryFont( _pDisplay, pPattern );
                if ( _pFont != nullptr )
                    break;
            }
            if ( _pFont == nullptr )
                return false;
            XSetFont( _pDisplay, _gc, _pFont->fid );
            return true;
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
                X11DevConsoleWindowInternal::decodeUtf8( _listLine[index], _listChar );
                XDrawString16( _pDisplay, _window, _gc, X11DevConsoleWindowInternal::kPadding, lineY, _listChar.data(), static_cast<int32>( _listChar.size() ) );
                lineY += X11DevConsoleWindowInternal::kLineHeight;
            }
            XFlush( _pDisplay );
        }

    private:
        Display*        _pDisplay;
        ::Window        _ownerWindow;
        ::Window        _window;
        GC              _gc;
        XFontStruct*    _pFont;
        vector<XChar2b> _listChar; ///< 한 줄을 UCS-2 로 바꾼 글자(그릴 때마다 다시 쓴다)
        vector<string>  _listLine;
        vector<uint8>   _listErrorFlag;
    };
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "DevConsole" );

    unique_ptr<IDevConsoleWindow> IDevConsoleWindow::createPlatform( IWindow& owner )
    {
        // `getNativeDisplay` 는 창 시스템마다 다른 연결(Wayland 는 wl_display)이라 X11 창이 아닌 것을 Display* 로 읽으면 DefaultScreen 에서 죽는다(CI Wayland 스모크 SIGSEGV).
        // 이 콘솔은 X11 자식 창이다 — Wayland 창에서는 만들지 않고, 콘솔은 그리지 않은 채 입력만 받는다.
        if ( owner.getSurfaceKind() != NativeSurfaceKind::X11 )
        {
            SW_LOG_INFO( "Dev console: the X11 console window is not created for a non-X11 window - input only" );
            return nullptr;
        }
        Display*       pDisplay    = static_cast<Display*>( owner.getNativeDisplay() );
        const ::Window ownerWindow = static_cast<::Window>( reinterpret_cast<uintptr_t>( owner.getNativeHandle() ) );
        if ( pDisplay == nullptr || ownerWindow == 0 )
            return nullptr;
        unique_ptr<X11DevConsoleWindow> pWindow = make_unique<X11DevConsoleWindow>( pDisplay, ownerWindow );
        if ( pWindow->initialize() == false )
        {
            SW_LOG_WARNING( "Dev console: could not create the console window (no X window, GC or iso10646-1 font)" );
            return nullptr;
        }
        return pWindow;
    }
} // namespace sw

#endif
