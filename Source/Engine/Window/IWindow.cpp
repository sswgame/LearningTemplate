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
        , _bRecreating{ SW_FALSE }
        , _reserved{ 0 }
        , _arrReserved{}
        , _restoreX{ 0 }
        , _restoreY{ 0 }
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

        // 다시 만들어도 **보이던 창은 보여야 한다.** 예전 기반 구현이 이 한 줄을 빠뜨려서, 이 길로
        // 들어온 창은 백엔드 교체 뒤 사라졌다(플랫폼 재정의 둘만 제대로 하고 있었다).
        const bool bWasVisible = isVisible();
        captureRestorePosition();

        const uint32 width  = _width;
        const uint32 height = _height;

        // 이 구간의 `destroy()` 는 앱 종료가 아니다 — 플랫폼 메시지 처리기가 이 깃발을 보고 삼킨다.
        _bRecreating = SW_TRUE;
        destroy();
        _bShouldClose = SW_FALSE;

        const string title = StringUtil::utf16ToUtf8( _title.c_str() );
        const bool   bOk   = initializeWindow( title.c_str(), width, height );
        if ( bOk && bWasVisible )
            showWindow( true );

        _bRecreating = SW_FALSE;
        clearRestorePosition();
        return bOk;
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
