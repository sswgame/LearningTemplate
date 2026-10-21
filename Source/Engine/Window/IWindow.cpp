#include "pch.h"

#include "Engine/Window/IWindow.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Engine/Window/Linux/X11Window.h"
#include "Engine/Window/Windows/Win32Window.h"

#include <cstring>

namespace sw
{
    SW_LOG_CALLER( "Window" );

    namespace
    {

        IWindow* s_pActiveWindow{ nullptr };

    } // namespace

    IWindow::IWindow()
        : _arrTitle{}
        , _customHandler{}
        , _onResize{}
        , _closeQuery{}
        , _width{ 1280 }
        , _height{ 720 }
        , _bShouldClose{ SW_FALSE }
        , _bRecreating{ SW_FALSE }
        , _bVisibleRequested{ SW_FALSE }
        , _reserved{ 0 }
        , _displayMode{ WindowDisplayMode::Windowed }
        , _arrReserved{}
        , _restoreX{ 0 }
        , _restoreY{ 0 }
        , _minClientWidth{ 0 }
        , _minClientHeight{ 0 }
    {
    }

    void IWindow::setMinimumClientSize( uint32 width, uint32 height )
    {
        _minClientWidth  = width;
        _minClientHeight = height;
        applyMinimumClientSize();

        // 바닥은 사용자가 줄일 때만 막는다. 이미 바닥보다 작은 창은 여기서 키운다.
        const bool bSmaller = _width < width || _height < height;
        if ( _displayMode == WindowDisplayMode::Windowed && bSmaller )
            (void)setDisplayMode( WindowDisplayMode::Windowed, _width < width ? width : _width, _height < height ? height : _height );
    }

    bool IWindow::applyCommandLinePosition( const CommandLineManager& commandLine )
    {
        const bool bHasX = commandLine.isArgumentProvided( CommandLineArgument::WINDOW_X );
        const bool bHasY = commandLine.isArgumentProvided( CommandLineArgument::WINDOW_Y );
        if ( bHasX == false && bHasY == false )
            return true;
        int32      x{ 0 };
        int32      y{ 0 };
        const bool bComplete = bHasX && bHasY && commandLine.getArgument( CommandLineArgument::WINDOW_X, x ) && commandLine.getArgument( CommandLineArgument::WINDOW_Y, y );
        if ( bComplete == false )
        {
            SW_LOG_ERROR( "Window position needs both -WinX=<x> and -WinY=<y> as numbers - the window is not created" );
            return false;
        }
        setInitialPosition( x, y );
        return true;
    }

    void IWindow::setTitle( const utf8* pTitle )
    {
        if ( StringUtil::isNullOrEmpty( pTitle ) )
            return;
        if ( std::strcmp( pTitle, _arrTitle ) == 0 )
            return;
        if ( storeTitle( pTitle ) == false )
            return; // 오류는 storeTitle 이 남겼다 — 지금 제목이 그대로다
        applyTitle();
    }

    bool IWindow::storeTitle( const utf8* pTitle )
    {
        const utf8* const pSource = pTitle != nullptr ? pTitle : "";
        const size_t      length  = std::strlen( pSource );
        if ( length >= kTitleCapacity )
        {
            SW_LOG_ERROR( "Window title is %# bytes - it must be shorter than %# bytes (IWindow::kTitleCapacity)", length, kTitleCapacity );
            return false;
        }
        std::memcpy( _arrTitle, pSource, length + 1 );
        return true;
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

    uint32 IWindow::removeCodeWithin( const void* pBegin, const void* pEnd )
    {
        uint32 releasedCount{ 0 };
        if ( _customHandler.isCodeWithin( pBegin, pEnd ) )
        {
            _customHandler = {};
            ++releasedCount;
        }
        if ( _onResize.isCodeWithin( pBegin, pEnd ) )
        {
            _onResize = {};
            ++releasedCount;
        }
        if ( _closeQuery.isCodeWithin( pBegin, pEnd ) )
        {
            _closeQuery = {};
            ++releasedCount;
        }
        return releasedCount;
    }

    uint32 IWindow::onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped )
    {
        (void)outKeepImageMapped;
        return removeCodeWithin( pBegin, pEnd );
    }

    IWindow::~IWindow()
    {
        // **활성 창이 죽으면 전역 포인터도 같이 끊는다.** 그러지 않으면 뒤에
        // `getActiveWindow()` 를 부르는 쪽(RHI 초기화, 에디터 명령, 프레임 트랜지언트)이
        // 죽은 포인터를 받는다. 부르는 쪽이 파괴 전에 직접 끊는 것은 **한 경로의 규율**일 뿐이다.
        // `Engine` 은 App 이 없는 임베드 시나리오에서 창을
        // 전역에 놓아둔 채 소유권을 부르는 쪽에 넘긴다(`Engine::initialize` 의 `setActiveWindow`
        // 바로 위 주석). 여기서 끊으면 어느 경로로 죽어도 안전하다.
        if ( s_pActiveWindow == this )
            s_pActiveWindow = nullptr;
    }

    bool IWindow::recreate()
    {
        if ( _arrTitle[0] == '\0' )
            return false;

        // 다시 만들어도 **보이던 창은 보여야 한다**. 빠뜨리면 백엔드 교체 뒤 창이 사라진다.
        //
        // **묻는 것은 화면 상태가 아니라 의도다.** `isVisible()` 은 X11 에서 "창 관리자가 이미 매핑했는가" 를 묻는다.
        // 방금 `showWindow(true)` 한 창도 아직 아니고, 최소화됐거나 다른 워크스페이스에 있어도 아니다. 그 값으로 다시 만들면
        // **창이 사라진다**(윈도우에서는 `IsWindowVisible` 이 WS_VISIBLE 스타일이라 둘이 같아 리눅스에서만 드러난다.
        // `WindowTest.RecreateKeepsVisibilityAndSize` 참고).
        const bool bWasVisible = isVisibleRequested();
        captureRestorePosition();

        const uint32 width  = _width;
        const uint32 height = _height;

        // 이 구간의 `destroy()` 는 앱 종료가 아니다. 플랫폼 메시지 처리기가 이 깃발을 보고 삼킨다.
        _bRecreating = SW_TRUE;
        destroy();
        _bShouldClose = SW_FALSE;

        // 플랫폼의 `initializeWindow` 가 제목을 `_arrTitle` 에 다시 담으므로 사본으로 넘긴다(같은 버퍼를 읽으며 쓰지 않게).
        utf8 arrTitle[kTitleCapacity];
        std::memcpy( arrTitle, _arrTitle, kTitleCapacity );
        const bool bOk = initializeWindow( arrTitle, width, height );
        // 새 창은 늘 창 모드로 선다. 전체 화면이던 창은 보이기 전에 같은 방식으로 되돌린다(크기 통보는 `_bRecreating` 이 삼킨다).
        if ( bOk && _displayMode != WindowDisplayMode::Windowed )
            (void)setDisplayMode( _displayMode, width, height );
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
#elif defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
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
