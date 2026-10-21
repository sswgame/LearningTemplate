#include "pch.h"

#include "Engine/Window/PlatformClipboard.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    #include "Engine/Window/Linux/X11Window.h"

namespace sw
{
    SW_LOG_CALLER( "PlatformClipboard" );

    namespace
    {
        /** @brief CLIPBOARD 선택의 주인이 될 창(활성 창)입니다. 리눅스 클라이언트의 플랫폼 창은 `X11Window` 하나뿐이다(`IWindow::createPlatformWindow`). 없으면 nullptr 입니다. */
        X11Window* findOwnerWindow()
        {
            return static_cast<X11Window*>( IWindow::getActiveWindow() );
        }
    } // namespace

    // 선택 소유와 `SelectionRequest` 응답은 창이 한다(`X11Window::writeClipboardText` · `processMessages`). 주인인 동안의 글도 창이 든다.
    bool PlatformClipboard::isAvailable()
    {
        return true;
    }

    bool PlatformClipboard::readText( string& outText )
    {
        X11Window* pWindow = findOwnerWindow();
        return pWindow != nullptr && pWindow->readClipboardText( outText );
    }

    void PlatformClipboard::writeText( string_view text )
    {
        X11Window* pWindow = findOwnerWindow();
        if ( pWindow == nullptr )
        {
            SW_LOG_WARNING( "Clipboard write needs an active window to own the selection - not written" );
            return;
        }
        if ( pWindow->writeClipboardText( text ) == false )
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
