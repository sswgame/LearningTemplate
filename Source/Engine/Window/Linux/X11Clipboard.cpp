#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Window/MainWindowSubsystem.h"
#include "Engine/Window/PlatformClipboard.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    #include "Engine/Window/IWindow.h"

namespace sw
{
    SW_LOG_CALLER( "PlatformClipboard" );

    namespace
    {
        /**
         * @brief 클립보드를 잇는 활성 창입니다. 창 종류를 가르지 않고 창의 능력 질의(`IWindow::supportsClipboard`)로만 고른다. 창이 없거나 잇지 못하면 nullptr 입니다.
         */
        IWindow* findClipboardWindow()
        {
            const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
            IWindow* const             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
            return pWindow != nullptr && pWindow->supportsClipboard() ? pWindow : nullptr;
        }
    } // namespace

    // X11: 선택 소유와 `SelectionRequest` 응답은 창이 한다(`X11Window::writeClipboardText` · `processMessages`). 주인인 동안의 글도 창이 든다.
    // Wayland: `wl_data_device` 선택을 창이 든다(`WaylandWindow::writeClipboardText` · `readClipboardText`). 호스트는 창을 만든 뒤에 부른다.
    bool PlatformClipboard::isAvailable()
    {
        return findClipboardWindow() != nullptr;
    }

    bool PlatformClipboard::readText( string& outText )
    {
        IWindow* const pWindow = findClipboardWindow();
        return pWindow != nullptr && pWindow->readClipboardText( outText );
    }

    bool PlatformClipboard::writeText( string_view text )
    {
        IWindow* const pWindow = findClipboardWindow();
        if ( pWindow == nullptr )
        {
            SW_LOG_WARNING( "Clipboard write needs an active window that supports the clipboard (X11, or Wayland with a data device) to own the selection (none) - not written" );
            return false;
        }
        const bool bWritten = pWindow->writeClipboardText( text );
        if ( bWritten == false )
            SW_LOG_WARNING( "Clipboard selection could not be set (X11: the server refused ownership; Wayland: no key/button input yet to take a serial from) - not written" );
        return bWritten;
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

    bool PlatformClipboard::writeText( [[maybe_unused]] string_view text )
    {
        return false;
    }
} // namespace sw
#endif
