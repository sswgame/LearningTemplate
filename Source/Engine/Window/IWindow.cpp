#include "pch.h"

#include "Engine/Window/IWindow.h"

#include "Engine/Window/Linux/X11Window.h"
#include "Engine/Window/Mac/CocoaWindow.h"
#include "Engine/Window/Windows/Win32Window.h"

namespace sw
{
    namespace
    {

        IWindow* s_pActiveWindow{ nullptr };

    } // namespace

    IWindow::IWindow()
        : _title{}
        , _customHandler{}
        , _onResize{}
        , _closeQuery{}
        , _width{ 1280 }
        , _height{ 720 }
        , _bShouldClose{ SW_FALSE }
        , _reserved{ 0 }
        , _arrReserved{}
    {
    }

    void IWindow::requestClose()
    {
        _bShouldClose = SW_TRUE;
    }

    bool IWindow::tryBeginClose()
    {
        if ( _bShouldClose == SW_TRUE )
            return true;
        if ( _closeQuery.isBound() && _closeQuery() == false )
            return false;
        _bShouldClose = SW_TRUE;
        return true;
    }

    IWindow::~IWindow()
    {
        // **활성 창이 죽으면 전역 포인터도 같이 끊는다.** 그러지 않으면 뒤에
        // `getActiveWindow()` 를 부르는 쪽(RHI 초기화 · 에디터 명령 · 프레임 트랜지언트)이
        // 죽은 포인터를 받는다. `App::shutdown` 은 파괴 전에 손으로 끊고 있었지만 그것은
        // **한 경로의 규율**이었다 — `EngineLoop` 은 App 이 없는 임베드 시나리오에서 창을
        // 전역에 놓아둔 채 소유를 호출자에게 넘긴다(그 주석이 바로 위에 있다). 여기서
        // 끊으면 어느 경로로 죽어도 참이다.
        if ( s_pActiveWindow == this )
            s_pActiveWindow = nullptr;
    }

    bool IWindow::recreate()
    {
        if ( _title.empty() )
            return false;

        const uint32 width  = _width;
        const uint32 height = _height;
        destroy();
        _bShouldClose = SW_FALSE;

        const string title = StringUtil::utf16ToUtf8( _title.c_str() );
        return initializeWindow( title.c_str(), width, height );
    }

    unique_ptr<IWindow> IWindow::createPlatformWindow()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return make_unique<Win32Window>();
#elif defined( SW_PLATFORM_MACOS )
        return make_unique<CocoaWindow>();
#elif defined( SW_PLATFORM_LINUX )
        return make_unique<X11Window>();
#else
        return nullptr;
#endif
    }

    void IWindow::setActiveWindow( IWindow* pWindow )
    {
        s_pActiveWindow = pWindow;
    }

    IWindow* IWindow::getActiveWindow()
    {
        return s_pActiveWindow;
    }
} // namespace sw
