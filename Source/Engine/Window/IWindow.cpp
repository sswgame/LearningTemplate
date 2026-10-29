#include "pch.h"

#include "Engine/Window/IWindow.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Engine/Window/Linux/LinuxWindowBackend.h"
#include "Engine/Window/Linux/Wayland/WaylandWindow.h"
#include "Engine/Window/Linux/X11Window.h"
#include "Engine/Window/Windows/Win32Window.h"

#include <cstdlib>
#include <cstring>

namespace sw
{
    SW_LOG_CALLER( "Window" );

    IWindow::IWindow()
        : _arrTitle{}
        , _customHandler{}
        , _accessibilityQuery{}
        , _onResize{}
        , _closeQuery{}
        , _width{ 1280 }
        , _height{ 720 }
        , _bShouldClose{ SW_FALSE }
        , _bRecreating{ SW_FALSE }
        , _bVisibleRequested{ SW_FALSE }
        , _bMinimized{ SW_FALSE }
        , _bFocused{ SW_FALSE }
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

    void* IWindow::tryGetWin32Handle() const
    {
        return getSurfaceKind() == NativeSurfaceKind::Win32 ? getNativeHandle() : nullptr;
    }

    bool IWindow::tryGetX11Surface( X11SurfaceHandles& outHandles ) const
    {
        outHandles = X11SurfaceHandles{};
        if ( getSurfaceKind() != NativeSurfaceKind::X11 )
            return false;
        X11SurfaceHandles handles;
        handles._pDisplay = getNativeDisplay();
        handles._window   = static_cast<uint64>( reinterpret_cast<uintptr_t>( getNativeHandle() ) );
        if ( handles._pDisplay == nullptr || handles._window == 0 )
            return false;
        outHandles = handles;
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
        if ( _closeQuery.isBound() && _closeQuery.get()() == false )
            return false;
        _bShouldClose = SW_TRUE;
        return true;
    }

    IWindow::~IWindow() = default;

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
        // 창 계층은 환경 변수 하나가 고른다(`SW_WINDOW_BACKEND` = x11 | wayland, 없으면 x11 — XWayland 데스크톱의 지금까지의 길). 모르는 값 · 짓지 않은 계층은 오류로 멈춘다.
        // 읽는 곳은 스플래시 창과 같다(`tryReadLinuxWindowBackend`).
        LinuxWindowBackend backend = LinuxWindowBackend::X11;
        if ( tryReadLinuxWindowBackend( backend ) == false )
            return nullptr;
        if ( backend == LinuxWindowBackend::X11 )
            return make_unique<X11Window>();
    #if defined( SW_WITH_WAYLAND )
        SW_LOG_INFO( "Window backend: Wayland (SW_WINDOW_BACKEND=wayland)" );
        return make_unique<WaylandWindow>();
    #else
        SW_LOG_ERROR( "SW_WINDOW_BACKEND=wayland but this build has no Wayland window layer (configure with -DSW_ENABLE_WAYLAND=ON)" );
        return nullptr;
    #endif
#else
        return nullptr;
#endif
    }
} // namespace sw
