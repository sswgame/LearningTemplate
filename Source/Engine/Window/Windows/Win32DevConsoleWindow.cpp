/**
 * @file Win32DevConsoleWindow.cpp
 * @brief 개발 콘솔 오버레이의 Win32 창입니다 — 게임 창이 소유한 반투명 GDI 팝업(포커스를 빼앗지 않는다).
 */
#include "pch.h"

#include "Engine/Window/DevConsoleOverlay.h"

#if SW_DEV_COMMANDS_ENABLED && defined( SW_PLATFORM_WINDOWS )

    #include "Core/Log/Logger.h"
    #include "Core/String/StringUtil.h"

    #include "Engine/Window/IWindow.h"
    #include "Engine/Window/NativeWindowEvent.h"

    #include "Engine/Common/EnginePlatformHeaders.h"

namespace sw
{
    namespace
    {
        struct Win32DevConsoleWindowInternal
        {
            static constexpr const utf16* kClassName  = L"SwDevConsoleOverlay";
            static constexpr int32        kFontHeight = 16;
            static constexpr int32        kPadding    = 6;
            static constexpr BYTE         kAlpha      = 225;
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @class Win32DevConsoleWindow
     * @brief 게임 창 클라이언트 영역의 위쪽에 겹쳐 그리는 팝업입니다. 키보드는 게임 창이 받고(`decodeKey`), 이 창은 그리기만 합니다.
     */
    class Win32DevConsoleWindow final : public IDevConsoleWindow
    {
    public:
        explicit Win32DevConsoleWindow( HWND hOwner )
            : _hOwner{ hOwner }
            , _hWnd{ nullptr }
            , _hFont{ nullptr }
            , _listLine{}
            , _listErrorFlag{}
        {
        }

        ~Win32DevConsoleWindow() override
        {
            if ( _hWnd != nullptr )
                DestroyWindow( _hWnd );
            if ( _hFont != nullptr )
                DeleteObject( _hFont );
        }

        Win32DevConsoleWindow( const Win32DevConsoleWindow& )            = delete;
        Win32DevConsoleWindow& operator=( const Win32DevConsoleWindow& ) = delete;

        bool initialize()
        {
            const HINSTANCE hInstance = GetModuleHandleW( nullptr );
            WNDCLASSEXW     windowClass{};
            windowClass.cbSize        = sizeof( WNDCLASSEXW );
            windowClass.lpfnWndProc   = &Win32DevConsoleWindow::windowProc;
            windowClass.hInstance     = hInstance;
            windowClass.lpszClassName = Win32DevConsoleWindowInternal::kClassName;
            // 이미 등록돼 있으면(오버레이를 다시 만든다) 실패해도 된다.
            (void)RegisterClassExW( &windowClass );

            _hWnd = CreateWindowExW( WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, Win32DevConsoleWindowInternal::kClassName, L"Dev Console", WS_POPUP, 0, 0,
                                     1, 1, _hOwner, nullptr, hInstance, this );
            if ( _hWnd == nullptr )
                return false;
            SetLayeredWindowAttributes( _hWnd, 0, Win32DevConsoleWindowInternal::kAlpha, LWA_ALPHA );
            _hFont = CreateFontW( Win32DevConsoleWindowInternal::kFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas" );
            return true;
        }

        bool decodeKey( const NativeWindowEvent& event, DevConsoleKey& outKey ) const override
        {
            outKey = DevConsoleKey{};
            if ( event._message == WM_KEYDOWN || event._message == WM_SYSKEYDOWN )
            {
                switch ( event._wParam )
                {
                    case VK_OEM_3:
                    {
                        outKey._kind = DevConsoleKey::Kind::Toggle;
                        break;
                    }
                    case VK_ESCAPE:
                    {
                        outKey._kind = DevConsoleKey::Kind::Close;
                        break;
                    }
                    case VK_BACK:
                    {
                        outKey._kind = DevConsoleKey::Kind::Backspace;
                        break;
                    }
                    case VK_RETURN:
                    {
                        outKey._kind = DevConsoleKey::Kind::Enter;
                        break;
                    }
                    case VK_TAB:
                    {
                        outKey._kind = DevConsoleKey::Kind::Tab;
                        break;
                    }
                    case VK_UP:
                    {
                        outKey._kind = DevConsoleKey::Kind::HistoryBack;
                        break;
                    }
                    case VK_DOWN:
                    {
                        outKey._kind = DevConsoleKey::Kind::HistoryForward;
                        break;
                    }
                    default:
                    {
                        return false;
                    }
                }
                return true;
            }
            if ( event._message == WM_CHAR )
            {
                // 여는 키(` · ~)와 제어 문자(백스페이스 · 엔터 · 탭은 WM_KEYDOWN 이 이미 처리했다)는 글자가 아니다.
                const uint32 codepoint = static_cast<uint32>( event._wParam );
                if ( codepoint < 0x20 || codepoint == '`' || codepoint == '~' || codepoint == 0x7F || ( 0xD800 <= codepoint && codepoint <= 0xDFFF ) )
                    return false;
                outKey._kind      = DevConsoleKey::Kind::Character;
                outKey._codepoint = codepoint;
                return true;
            }
            return false;
        }

        void setVisible( bool bVisible ) override
        {
            if ( _hWnd == nullptr )
                return;
            if ( bVisible )
                updatePlacement();
            ShowWindow( _hWnd, bVisible ? SW_SHOWNOACTIVATE : SW_HIDE );
        }

        void present( const vector<string>& listLine, const vector<uint8>& listErrorFlag ) override
        {
            // 같은 내용을 프레임마다 다시 그리지 않는다 — 자리만 맞추고, 줄이 바뀌었을 때만 다시 그린다.
            const bool bChanged = listLine != _listLine || listErrorFlag != _listErrorFlag;
            _listLine           = listLine;
            _listErrorFlag      = listErrorFlag;
            updatePlacement();
            if ( bChanged && _hWnd != nullptr )
                InvalidateRect( _hWnd, nullptr, FALSE );
        }

    private:
        /** @brief 게임 창 클라이언트 영역의 위쪽, 줄 수만큼의 높이로 맞춥니다. */
        void updatePlacement()
        {
            RECT clientRect{};
            if ( _hWnd == nullptr || GetClientRect( _hOwner, &clientRect ) == FALSE )
                return;
            POINT origin{ 0, 0 };
            ClientToScreen( _hOwner, &origin );
            const int32 lineCount = static_cast<int32>( _listLine.empty() ? 1 : _listLine.size() );
            const int32 height    = lineCount * ( Win32DevConsoleWindowInternal::kFontHeight + 2 ) + Win32DevConsoleWindowInternal::kPadding * 2;
            const int32 width     = clientRect.right - clientRect.left;
            RECT        current{};
            GetWindowRect( _hWnd, &current );
            const bool bMoved = current.left != origin.x || current.top != origin.y || current.right - current.left != width || current.bottom - current.top != height;
            if ( bMoved )
                SetWindowPos( _hWnd, nullptr, origin.x, origin.y, width, height, SWP_NOACTIVATE | SWP_NOZORDER );
        }

        /** @brief 메모리 DC 에 다 그린 뒤 한 번에 옮깁니다 — 창에 바로 그리면 합성기가 반쯤 그린 화면(아래 줄이 빈)을 내보낸다. */
        void paint( HDC hWindowDC )
        {
            RECT clientRect{};
            GetClientRect( _hWnd, &clientRect );
            const int32   width          = clientRect.right - clientRect.left;
            const int32   height         = clientRect.bottom - clientRect.top;
            HDC           hDC            = CreateCompatibleDC( hWindowDC );
            HBITMAP       hBitmap        = CreateCompatibleBitmap( hWindowDC, width > 0 ? width : 1, height > 0 ? height : 1 );
            const HGDIOBJ hPreviousImage = SelectObject( hDC, hBitmap );

            HBRUSH hBackground = CreateSolidBrush( RGB( 18, 20, 26 ) );
            FillRect( hDC, &clientRect, hBackground );
            DeleteObject( hBackground );

            const HGDIOBJ hPreviousFont = _hFont != nullptr ? SelectObject( hDC, _hFont ) : nullptr;
            SetBkMode( hDC, TRANSPARENT );
            int32 lineY = Win32DevConsoleWindowInternal::kPadding;
            for ( size_t index = 0; index < _listLine.size(); ++index )
            {
                const bool bInput = index + 1 == _listLine.size();
                const bool bError = index < _listErrorFlag.size() && _listErrorFlag[index] != 0;
                SetTextColor( hDC, bInput ? RGB( 255, 220, 120 ) : ( bError ? RGB( 255, 120, 110 ) : RGB( 215, 220, 230 ) ) );
                const wstring text = StringUtil::utf8ToUtf16( _listLine[index].c_str() );
                TextOutW( hDC, Win32DevConsoleWindowInternal::kPadding, lineY, text.c_str(), static_cast<int32>( text.size() ) );
                lineY += Win32DevConsoleWindowInternal::kFontHeight + 2;
            }
            if ( hPreviousFont != nullptr )
                SelectObject( hDC, hPreviousFont );

            BitBlt( hWindowDC, 0, 0, width, height, hDC, 0, 0, SRCCOPY );
            SelectObject( hDC, hPreviousImage );
            DeleteObject( hBitmap );
            DeleteDC( hDC );
        }

        static LRESULT CALLBACK windowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
        {
            if ( message == WM_NCCREATE )
            {
                const CREATESTRUCTW* pCreate = reinterpret_cast<const CREATESTRUCTW*>( lParam );
                SetWindowLongPtrW( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pCreate->lpCreateParams ) );
            }
            Win32DevConsoleWindow* pSelf = reinterpret_cast<Win32DevConsoleWindow*>( GetWindowLongPtrW( hWnd, GWLP_USERDATA ) );
            switch ( message )
            {
                case WM_MOUSEACTIVATE:
                {
                    return MA_NOACTIVATE;
                }
                case WM_ERASEBKGND:
                {
                    return 1;
                }
                case WM_PAINT:
                {
                    PAINTSTRUCT paintStruct{};
                    HDC         hDC = BeginPaint( hWnd, &paintStruct );
                    if ( pSelf != nullptr )
                        pSelf->paint( hDC );
                    EndPaint( hWnd, &paintStruct );
                    return 0;
                }
                default:
                {
                    return DefWindowProcW( hWnd, message, wParam, lParam );
                }
            }
        }

    private:
        HWND           _hOwner;
        HWND           _hWnd;
        HFONT          _hFont;
        vector<string> _listLine;
        vector<uint8>  _listErrorFlag;
    };
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "DevConsoleOverlay" );

    unique_ptr<IDevConsoleWindow> IDevConsoleWindow::createPlatform( IWindow& owner )
    {
        HWND hOwner = static_cast<HWND>( owner.getNativeHandle() );
        if ( hOwner == nullptr )
            return nullptr;
        unique_ptr<Win32DevConsoleWindow> pWindow = make_unique<Win32DevConsoleWindow>( hOwner );
        if ( pWindow->initialize() == false )
        {
            SW_LOG_WARNING( "Dev console overlay: could not create the overlay window" );
            return nullptr;
        }
        return pWindow;
    }
} // namespace sw

#endif
