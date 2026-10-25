#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Window/MainWindowSubsystem.h"
#include "Engine/Window/PlatformClipboard.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    #include "Engine/Window/Linux/Wayland/WaylandWindow.h"
    #include "Engine/Window/Linux/X11Window.h"

namespace sw
{
    SW_LOG_CALLER( "PlatformClipboard" );

    namespace
    {
        /**
         * @brief 클립보드를 이을 창(활성 창)입니다. 활성 창의 종류별 캐스트(`tryCast`)로만 읽는다 — 종류가 다른 창을 다른 클래스로 읽지 않는다.
         * @details 리눅스 클라이언트의 창은 X11 과 Wayland 두 가지다(`IWindow::createPlatformWindow`). 둘 다 아니면(창 없음) 둘 다 nullptr 이다.
         */
        struct ClipboardWindow
        {
            X11Window*     _pX11{ nullptr };
            WaylandWindow* _pWayland{ nullptr };
        };

        ClipboardWindow findOwnerWindow()
        {
            const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
            IWindow* const             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
            return ClipboardWindow{ X11Window::tryCast( pWindow ), WaylandWindow::tryCast( pWindow ) };
        }
    } // namespace

    // X11: 선택 소유와 `SelectionRequest` 응답은 창이 한다(`X11Window::writeClipboardText` · `processMessages`). 주인인 동안의 글도 창이 든다.
    // Wayland: `wl_data_device` 선택을 창이 든다(`WaylandWindow::writeClipboardText` · `readClipboardText`). 호스트는 창을 만든 뒤에 부른다.
    bool PlatformClipboard::isAvailable()
    {
        const ClipboardWindow window = findOwnerWindow();
        return window._pX11 != nullptr || window._pWayland != nullptr;
    }

    bool PlatformClipboard::readText( string& outText )
    {
        const ClipboardWindow window = findOwnerWindow();
        if ( window._pWayland != nullptr )
            return window._pWayland->readClipboardText( outText );
        return window._pX11 != nullptr && window._pX11->readClipboardText( outText );
    }

    void PlatformClipboard::writeText( string_view text )
    {
        const ClipboardWindow window = findOwnerWindow();
        if ( window._pWayland != nullptr )
        {
            if ( window._pWayland->writeClipboardText( text ) == false )
                SW_LOG_WARNING( "Wayland clipboard selection could not be set (no data device, or no key/button input yet to take a serial from) - not written" );
            return;
        }
        if ( window._pX11 == nullptr )
        {
            SW_LOG_WARNING( "Clipboard write needs an active X11 or Wayland window to own the selection (none) - not written" );
            return;
        }
        if ( window._pX11->writeClipboardText( text ) == false )
            SW_LOG_WARNING( "Clipboard selection could not be owned - not written" );
    }
} // namespace sw
#elif defined( SW_PLATFORM_LINUX )
namespace sw
{
    // 창이 없는 서버 빌드다. 클립보드를 이을 창이 없으므로 호스트는 `isAvailable` 을 보고 끼우지 않는다.
    bool PlatformClipboard::isAvailable()
    {
        return false;
    }

    bool PlatformClipboard::readText( [[maybe_unused]] string& outText )
    {
        return false;
    }

    void PlatformClipboard::writeText( [[maybe_unused]] string_view text )
    {
    }
} // namespace sw
#endif
