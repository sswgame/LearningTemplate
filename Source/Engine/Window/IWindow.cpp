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
        , _bVisibleIntent{ SW_FALSE }
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
        // **한 경로의 규율**이었다. `EngineLoop` 은 App 이 없는 임베드 시나리오에서 창을
        // 전역에 놓아둔 채 소유를 부르는 쪽에 넘긴다(`EngineLoop::initialize` 의 `setActiveWindow`
        // 바로 위 주석). 여기서 끊으면 어느 경로로 죽어도 참이다.
        if ( s_pActiveWindow == this )
            s_pActiveWindow = nullptr;
    }

    bool IWindow::recreate()
    {
        if ( _title.empty() )
            return false;

        // 다시 만들어도 **보이던 창은 보여야 한다.** 예전 기반 구현이 이 한 줄을 빠뜨려서, 이 길로
        // 들어온 창은 백엔드 교체 뒤 사라졌다(플랫폼 재정의 둘만 제대로 하고 있었다).
        //
        // **묻는 것은 화면 상태가 아니라 의도다.** 한때 `isVisible()` 을 썼는데, X11 에서 그것은
        // "창 관리자가 이미 매핑했는가" 를 묻는다. 방금 `showWindow(true)` 한 창도 아직 아니고,
        // 최소화됐거나 다른 워크스페이스에 있어도 아니다. 그 상태로 다시 만들면 **창이 사라진다.**
        // (윈도우에서는 `IsWindowVisible` 이 WS_VISIBLE 스타일이라 둘이 우연히 같았고, 그래서
        // 리눅스에서만 깨졌다. WSL 에서 `WindowTest.RecreateKeepsVisibilityAndSize` 가 3회 모두 졌다.)
        const bool bWasVisible = isVisibleIntended();
        captureRestorePosition();

        const uint32 width  = _width;
        const uint32 height = _height;

        // 이 구간의 `destroy()` 는 앱 종료가 아니다. 플랫폼 메시지 처리기가 이 깃발을 보고 삼킨다.
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
