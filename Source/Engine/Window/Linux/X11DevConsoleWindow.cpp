/**
 * @file X11DevConsoleWindow.cpp
 * @brief 개발 콘솔의 X11 창입니다. 게임 창의 자식 창에 Xft(fontconfig)로 UTF-8 줄을 그립니다 — 기본 글꼴에 없는 글자(한글 · 일본어)는 fontconfig 가 고른 폴백 글꼴로 그립니다.
 * @details 코어 X 글꼴(iso10646-1)은 X 서버가 그 글자를 가진 만큼만 나와 한글 · 일본어가 비어 보였다. Xft 는 클라이언트 쪽 글꼴이라 서버 글꼴 설치와 무관하다.
 *          Xft 가 필요하다(`libxft-dev`, CMake `PkgConfig::SW_XFT`). 글꼴을 못 열면 콘솔을 세우지 않는다(깨진 글자로 그리지 않는다).
 */
#include "pch.h"

#include "Engine/Window/DevConsoleWindow.h"

#if SW_DEV_COMMANDS_ENABLED && defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )

    #include "Core/Container/StringUtil.h"
    #include "Core/Container/map.h"
    #include "Core/Log/Logger.h"

    #include "Engine/Window/IWindow.h"

    #include "Core/Common/PlatformOsHeaders.h"
    #include "Core/Common/X11Headers.h"

    #include <X11/Xft/Xft.h>
    #include <fontconfig/fontconfig.h>

    #include "Core/Common/X11MacroUndef.h" // Xft 헤더 뒤에 X11 매크로를 다시 지운다

namespace sw
{
    namespace
    {
        struct X11DevConsoleWindowInternal
        {
            static constexpr int32 kPadding = 6;
            /// 기본 글꼴입니다(fontconfig 이름). `monospace` 는 시스템이 정한 고정폭 글꼴로 풀린다 — 글자가 없으면 폴백 글꼴을 따로 찾는다(`findFontFor`).
            static constexpr const utf8* kFontName = "monospace-11";
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
            , _pDraw{ nullptr }
            , _pFont{ nullptr }
            , _lineHeight{ 16 }
            , _color{}
            , _bColorAllocated{ false }
            , _mapFallbackFont{}
            , _listUnit{}
            , _listLine{}
            , _listErrorFlag{}
        {
        }

        ~X11DevConsoleWindow() override
        {
            for ( auto& entry : _mapFallbackFont )
            {
                if ( entry.second != nullptr )
                    XftFontClose( _pDisplay, entry.second );
            }
            if ( _pFont != nullptr )
                XftFontClose( _pDisplay, _pFont );
            if ( _bColorAllocated )
                XftColorFree( _pDisplay, DefaultVisual( _pDisplay, DefaultScreen( _pDisplay ) ), DefaultColormap( _pDisplay, DefaultScreen( _pDisplay ) ), &_color );
            if ( _pDraw != nullptr )
                XftDrawDestroy( _pDraw );
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
            // 글꼴이 없으면 콘솔을 세우지 않는다(깨뜨려 그리지 않는다). 호출한 쪽이 경고를 남긴다.
            _pFont = XftFontOpenName( _pDisplay, screen, X11DevConsoleWindowInternal::kFontName );
            if ( _pFont == nullptr )
                return false;
            _lineHeight = _pFont->ascent + _pFont->descent + 2;
            _pDraw      = XftDrawCreate( _pDisplay, _window, DefaultVisual( _pDisplay, screen ), DefaultColormap( _pDisplay, screen ) );
            if ( _pDraw == nullptr )
                return false;
            _bColorAllocated = XftColorAllocName( _pDisplay, DefaultVisual( _pDisplay, screen ), DefaultColormap( _pDisplay, screen ), "white", &_color ) != 0;
            return _bColorAllocated;
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
            const int32 height    = lineCount * _lineHeight + X11DevConsoleWindowInternal::kPadding * 2;
            XMoveResizeWindow( _pDisplay, _window, 0, 0, static_cast<uint32>( attributes.width > 1 ? attributes.width : 1 ), static_cast<uint32>( height ) );
        }

        /**
         * @brief 글자 @p unit 을 그릴 글꼴입니다. 기본 글꼴에 있으면 그것이고, 없으면 fontconfig 에게 그 글자를 가진 글꼴을 물어 연 것(없으면 nullptr — 그 글자는 건너뛴다)입니다.
         * @details 한 번 물은 결과(없음 포함)는 글자별로 기억해 프레임마다 fontconfig 를 다시 부르지 않는다. 글꼴 개체는 소멸자가 닫는다.
         */
        XftFont* findFontFor( uint16 unit )
        {
            if ( XftCharExists( _pDisplay, _pFont, unit ) != 0 )
                return _pFont;
            const auto found = _mapFallbackFont.find( unit );
            if ( found != _mapFallbackFont.end() )
                return found->second;
            XftFont*   pFallback = nullptr;
            FcCharSet* pCharSet  = FcCharSetCreate();
            FcPattern* pPattern  = FcPatternDuplicate( _pFont->pattern );
            if ( pCharSet != nullptr && pPattern != nullptr )
            {
                FcCharSetAddChar( pCharSet, unit );
                FcPatternAddCharSet( pPattern, FC_CHARSET, pCharSet );
                FcDefaultSubstitute( pPattern );
                FcResult   result = FcResultNoMatch;
                FcPattern* pMatch = FcFontMatch( nullptr, pPattern, &result );
                if ( pMatch != nullptr )
                {
                    pFallback = XftFontOpenPattern( _pDisplay, pMatch ); // 성공하면 XftFont 가 pMatch 를 소유한다
                    if ( pFallback == nullptr )
                        FcPatternDestroy( pMatch );
                    else if ( XftCharExists( _pDisplay, pFallback, unit ) == 0 )
                    {
                        XftFontClose( _pDisplay, pFallback ); // 시스템에 그 글자를 가진 글꼴이 없다
                        pFallback = nullptr;
                    }
                }
            }
            if ( pPattern != nullptr )
                FcPatternDestroy( pPattern );
            if ( pCharSet != nullptr )
                FcCharSetDestroy( pCharSet );
            _mapFallbackFont[unit] = pFallback;
            return pFallback;
        }

        void redraw()
        {
            XClearWindow( _pDisplay, _window );
            int32 lineY = X11DevConsoleWindowInternal::kPadding + _pFont->ascent;
            for ( size_t index = 0; index < _listLine.size(); ++index )
            {
                // UTF-8 → UCS-2(기본 다국어 평면 밖과 잘못된 바이트는 U+FFFD 한 글자). 글자마다 글꼴을 골라 이어 그린다.
                StringUtil::utf8ToUcs2( _listLine[index], _listUnit );
                int32 penX = X11DevConsoleWindowInternal::kPadding;
                for ( const uint16 unit : _listUnit )
                {
                    XftFont* pFont = findFontFor( unit );
                    if ( pFont == nullptr )
                        continue;
                    const FcChar16 character = unit;
                    XftDrawString16( _pDraw, &_color, pFont, penX, lineY, &character, 1 );
                    XGlyphInfo extents{};
                    XftTextExtents16( _pDisplay, pFont, &character, 1, &extents );
                    penX += extents.xOff;
                }
                lineY += _lineHeight;
            }
            XFlush( _pDisplay );
        }

    private:
        Display*              _pDisplay;
        ::Window              _ownerWindow;
        ::Window              _window;
        XftDraw*              _pDraw;
        XftFont*              _pFont;
        int32                 _lineHeight;
        XftColor              _color;
        bool                  _bColorAllocated;
        map<uint16, XftFont*> _mapFallbackFont; ///< 기본 글꼴에 없는 글자 → fontconfig 가 고른 폴백 글꼴(nullptr = 시스템에 없음)
        vector<uint16>        _listUnit;        ///< 한 줄을 UCS-2 로 바꾼 글자(그릴 때마다 다시 쓴다)
        vector<string>        _listLine;
        vector<uint8>         _listErrorFlag;
    };
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "DevConsole" );

    unique_ptr<IDevConsoleWindow> IDevConsoleWindow::createPlatform( IWindow& owner )
    {
        // 이 콘솔은 X11 자식 창이다 — Wayland 창에서는 만들지 않고, 콘솔은 그리지 않은 채 입력만 받는다(Wayland 연결을 Display* 로 읽으면 죽는다: `IWindow::tryGetX11Surface`).
        X11SurfaceHandles x11Surface;
        if ( owner.tryGetX11Surface( x11Surface ) == false )
        {
            SW_LOG_INFO( "Dev console: the X11 console window is not created without an X11 owner window - input only" );
            return nullptr;
        }
        Display*                        pDisplay    = static_cast<Display*>( x11Surface._pDisplay );
        const ::Window                  ownerWindow = static_cast<::Window>( x11Surface._window );
        unique_ptr<X11DevConsoleWindow> pWindow     = make_unique<X11DevConsoleWindow>( pDisplay, ownerWindow );
        if ( pWindow->initialize() == false )
        {
            SW_LOG_WARNING( "Dev console: could not create the console window (no X window, Xft font or color)" );
            return nullptr;
        }
        return pWindow;
    }
} // namespace sw

#endif
